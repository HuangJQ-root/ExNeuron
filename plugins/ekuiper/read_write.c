/**
 * NEURON IIoT System for Industry 4.0
 * Copyright (C) 2020-2022 EMQ Technologies Co., Ltd All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 **/

#include <nng/nng.h>

#include "neuron.h"
#include "otel/otel_manager.h"
#include "json/neu_json_fn.h"
#include "json/neu_json_rw.h"

#include "json_rw.h"
#include "read_write.h"

static int send_write_tag_req(neu_plugin_t *plugin, neu_json_write_req_t *req,
                              char *playload, bool trace_flag,
                              uint8_t *trace_id, uint8_t *span_id);
static int send_write_tags_req(neu_plugin_t *             plugin,
                               neu_json_write_tags_req_t *req, char *playload,
                               bool trace_flag, uint8_t *trace_id,
                               uint8_t *span_id);

static int hex_char_to_int(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static int hex_string_to_binary(const char *   hex_string,
                                unsigned char *binary_array, int max_length)
{
    int length = strlen(hex_string);
    if (length % 2 != 0 || length <= 0)
        return -1;

    int binary_length = length / 2;
    if (binary_length > max_length)
        return -1;

    for (int i = 0; i < binary_length; i++) {
        int high_nibble = hex_char_to_int(hex_string[2 * i]);
        int low_nibble  = hex_char_to_int(hex_string[2 * i + 1]);
        if (high_nibble == -1 || low_nibble == -1)
            return -1;

        binary_array[i] = (high_nibble << 4) | low_nibble;
    }

    return binary_length;
}

static void byte_to_hex(unsigned char byte, char *hex)
{
    static const char hex_chars[] = "0123456789ABCDEF";
    hex[0]                        = hex_chars[(byte >> 4) & 0x0F];
    hex[1]                        = hex_chars[byte & 0x0F];
}

static void binary_to_hex(const unsigned char *binary, size_t length, char *hex)
{
    for (size_t i = 0; i < length; i++) {
        byte_to_hex(binary[i], &hex[i * 2]);
    }
}

/**
 * @brief 发送数据到指定的插件
 *
 * 此函数用于将传输数据编码为 JSON 格式，并通过网络套接字发送出去。
 * 同时，它还支持 OpenTelemetry 跟踪，可记录数据发送操作的详细信息。
 *
 * @param plugin 指向 `neu_plugin_t` 结构体的指针，代表要发送数据的插件。
 * @param trans_data 指向 `neu_reqresp_trans_data_t` 结构体的指针，包含要发送的传输数据。
 */
void send_data(neu_plugin_t *plugin, neu_reqresp_trans_data_t *trans_data)
{
    int rv = 0;

    // 用于存储编码后的 JSON 字符串
    char *           json_str = NULL;

    // 用于 JSON 编码的响应结构体
    json_read_resp_t resp     = {
        .plugin     = plugin,
        .trans_data = trans_data,
    };

    // OpenTelemetry 跟踪相关变量
    neu_otel_trace_ctx trans_trace     = NULL;
    neu_otel_scope_ctx trans_scope     = NULL;
    uint8_t *          trace_id        = NULL;
    char               new_span_id[36] = { 0 };

    // 检查 OpenTelemetry 数据跟踪是否启用，并且传输数据包含跟踪上下文
    if (neu_otel_data_is_started() && trans_data->trace_ctx) {
        trans_trace = neu_otel_find_trace(trans_data->trace_ctx);
        if (trans_trace) {
            // 生成新的跨度 ID
            neu_otel_new_span_id(new_span_id);

            // 在跟踪中添加一个新的跨度，命名为 "ekuiper send"
            trans_scope =
                neu_otel_add_span2(trans_trace, "ekuiper send", new_span_id);

            // 为跨度添加线程 ID 属性
            neu_otel_scope_add_span_attr_int(trans_scope, "thread id",
                                             (int64_t)(pthread_self()));

            // 设置跨度的开始时间
            neu_otel_scope_set_span_start_time(trans_scope, neu_time_ns());
        }
    }

    // 执行数据发送操作的主循环，仅执行一次
    do {
        // 将传输数据编码为 JSON 字符串
        rv = neu_json_encode_by_fn(&resp, json_encode_read_resp, &json_str);
        if (0 != rv || json_str == NULL) {
            plog_error(plugin, "fail encode trans data to json");
            break;
        }

        // 存储要发送的消息
        nng_msg *msg              = NULL;

        // 获取 JSON 字符串的长度
        size_t   json_len         = strlen(json_str);

        // 跟踪头部的长度
        size_t   trace_header_len = 0;

        // 如果启用了 OpenTelemetry 数据跟踪且传输数据包含跟踪上下文
        if (neu_otel_data_is_started() && trans_data->trace_ctx) {
            trace_header_len = 26;
        }

        // 分配消息内存
        rv = nng_msg_alloc(&msg, json_len + trace_header_len);
        if (0 != rv) {
            plog_error(plugin, "nng cannot allocate msg");
            free(json_str);
            break;
        }

        if (trans_trace) {
            // 获取跟踪 ID
            trace_id           = neu_otel_get_trace_id(trans_trace);
            uint8_t span_id[8] = { 0 };
            hex_string_to_binary(new_span_id, span_id, 8);
            uint16_t tarce_header_magic = 0xCE0A;

            // 将跟踪头部的魔术值复制到消息体中
            memcpy(nng_msg_body(msg), &tarce_header_magic, 2);

            // 将跟踪 ID 复制到消息体中
            memcpy(nng_msg_body(msg) + 2, trace_id, 16);

            // 将跨度 ID 复制到消息体中
            memcpy(nng_msg_body(msg) + 2 + 16, span_id, 8);
        }

        // 将传输数据复制到消息体中
        memcpy(nng_msg_body(msg) + trace_header_len, json_str,
               json_len); // no null byte
        plog_debug(plugin, ">> %s", json_str);

        // 释放 JSON 字符串内存
        free(json_str);

        // 非阻塞方式发送消息
        rv = nng_sendmsg(plugin->sock, msg,
                         NNG_FLAG_NONBLOCK); // TODO: use aio to send message
        if (0 == rv) {
            // 更新发送消息总数的指标
            NEU_PLUGIN_UPDATE_METRIC(plugin, NEU_METRIC_SEND_MSGS_TOTAL, 1,
                                     NULL);

            // 更新 5 秒内发送字节数的指标
            NEU_PLUGIN_UPDATE_METRIC(plugin, NEU_METRIC_SEND_BYTES_5S, json_len,
                                     NULL);
            NEU_PLUGIN_UPDATE_METRIC(plugin, NEU_METRIC_SEND_BYTES_30S,
                                     json_len, NULL);
            NEU_PLUGIN_UPDATE_METRIC(plugin, NEU_METRIC_SEND_BYTES_60S,
                                     json_len, NULL);
        } else {
            // 记录消息发送失败的错误信息
            plog_error(plugin, "nng cannot send msg: %s", nng_strerror(rv));

            // 释放消息内存
            nng_msg_free(msg);

            // 更新发送消息错误总数的指标
            NEU_PLUGIN_UPDATE_METRIC(plugin, NEU_METRIC_SEND_MSG_ERRORS_TOTAL,
                                     1, NULL);
        }
    } while (0);

    if (trans_trace) {
        if (rv == 0) {
            // 设置跨度的状态码为成功
            neu_otel_scope_set_status_code2(trans_scope, NEU_OTEL_STATUS_OK, 0);
        } else {
            // 设置跨度的状态码为错误，并记录错误信息
            neu_otel_scope_set_status_code(trans_scope, NEU_OTEL_STATUS_ERROR,
                                           nng_strerror(rv));
        }
        // 设置跨度的结束时间
        neu_otel_scope_set_span_end_time(trans_scope, neu_time_ns());

        // 标记跟踪为最终状态
        neu_otel_trace_set_final(trans_trace);
    }
}

/**
 * @brief 接收数据的回调函数
 * 
 * 此回调函数在接收到数据时被调用，用于处理接收到的消息，包括错误检查、消息解析、
 * 指标更新、JSON 解码以及数据写入请求的发送等操作。
 * 
 * @param arg 传递给回调函数的参数，通常是指向 neu_plugin_t 结构体的指针
 * 
 * @note
 * 
 * 当使用 nng_aio_alloc 分配好异步 I/O 对象后，还需要启动一个异步操作，
 * 例如异步接收数据(nng_recv_aio),当异步接收操作完成时（成功接收到数据或者出
 * 现错误），NNG 库会自动调用之前通过 nng_aio_alloc 关联的回调函数 recv_data_callback
 */
void recv_data_callback(void *arg)
{
    int               rv         = 0;
    neu_plugin_t *    plugin     = arg;
    nng_msg *         msg        = NULL;
    size_t            body_len   = 0;
    size_t            json_len   = 0;
    char *            body_str   = NULL;
    char *            json_str   = NULL;
    neu_json_write_t *req        = NULL;
    bool              trace_flag = false;
    uint8_t *         trace_id   = NULL;
    uint8_t *         span_id    = NULL;

    // 获取异步操作的结果
    rv = nng_aio_result(plugin->recv_aio);
    if (0 != rv) {
        plog_error(plugin, "nng_recv error: %s", nng_strerror(rv));
        nng_mtx_lock(plugin->mtx);
        plugin->receiving = false;
        nng_mtx_unlock(plugin->mtx);
        return;
    }

    // 从异步 I/O 对象中获取消息
    msg      = nng_aio_get_msg(plugin->recv_aio);
    body_str = nng_msg_body(msg);
    body_len = nng_msg_len(msg);

    // 检查消息是否包含跟踪信息
    if (*(uint8_t *) body_str == 0x0A && *(uint8_t *) (body_str + 1) == 0xCE) {
        // 若包含跟踪信息，解析跟踪 ID 和跨度 ID
        trace_id   = (uint8_t *) (body_str + 2);
        span_id    = (uint8_t *) (body_str + 18);
        trace_flag = true;
        json_str   = body_str + 26;
        json_len   = body_len - 26;
    } else {
        // 若不包含跟踪信息，直接使用消息体作为 JSON 数据
        json_str = body_str;
        json_len = body_len;
    }

    plog_debug(plugin, "<< %.*s", (int) json_len, json_str);

    // 更新接收消息总数的指标
    NEU_PLUGIN_UPDATE_METRIC(plugin, NEU_METRIC_RECV_MSGS_TOTAL, 1, NULL);

    // 更新不同时间间隔内接收字节数的指标
    NEU_PLUGIN_UPDATE_METRIC(plugin, NEU_METRIC_RECV_BYTES_5S, json_len, NULL);
    NEU_PLUGIN_UPDATE_METRIC(plugin, NEU_METRIC_RECV_BYTES_30S, json_len, NULL);
    NEU_PLUGIN_UPDATE_METRIC(plugin, NEU_METRIC_RECV_BYTES_60S, json_len, NULL);

    // 更新不同时间间隔内接收消息数的指标
    NEU_PLUGIN_UPDATE_METRIC(plugin, NEU_METRIC_RECV_MSGS_5S, 1, NULL);
    NEU_PLUGIN_UPDATE_METRIC(plugin, NEU_METRIC_RECV_MSGS_30S, 1, NULL);
    NEU_PLUGIN_UPDATE_METRIC(plugin, NEU_METRIC_RECV_MSGS_60S, 1, NULL);

    // 解码 JSON 写入请求
    if (json_decode_write_req(json_str, json_len, &req) < 0) {
        plog_error(plugin, "fail decode write request json: %.*s",
                   (int) nng_msg_len(msg), json_str);
        goto recv_data_callback_end;
    }

    // 根据请求类型（单条或多条）发送写入标签请求
    if (req->singular) {
        rv = send_write_tag_req(plugin, &req->single, json_str, trace_flag,
                                trace_id, span_id);
    } else {
        rv = send_write_tags_req(plugin, &req->plural, json_str, trace_flag,
                                 trace_id, span_id);
    }

    if (0 != rv) {
        // 若写入请求发送失败，记录错误日志并跳转到结束处理部分
        plog_error(plugin, "failed to write data");
        goto recv_data_callback_end;
    }

recv_data_callback_end:
    // 释放消息内存
    nng_msg_free(msg);

    // 释放 JSON 解码结果的内存
    neu_json_decode_write_free(req);

    // 再次启动异步接收操作
    nng_recv_aio(plugin->sock, plugin->recv_aio);
}

static int json_value_to_tag_value(union neu_json_value *req,
                                   enum neu_json_type t, neu_dvalue_t *value)
{
    switch (t) {
    case NEU_JSON_INT:
        value->type      = NEU_TYPE_INT64;
        value->value.u64 = req->val_int;
        break;
    case NEU_JSON_STR:
        value->type = NEU_TYPE_STRING;
        strncpy(value->value.str, req->val_str, sizeof(value->value.str));
        break;
    case NEU_JSON_DOUBLE:
        value->type      = NEU_TYPE_DOUBLE;
        value->value.d64 = req->val_double;
        break;
    case NEU_JSON_BOOL:
        value->type          = NEU_TYPE_BOOL;
        value->value.boolean = req->val_bool;
        break;
    case NEU_JSON_ARRAY_BOOL:
        value->type               = NEU_TYPE_ARRAY_BOOL;
        value->value.bools.length = req->val_array_bool.length;
        for (int i = 0; i < req->val_array_bool.length; i++) {
            value->value.bools.bools[i] = req->val_array_bool.bools[i];
        }
        break;
    case NEU_JSON_ARRAY_INT64:
        value->type              = NEU_TYPE_ARRAY_INT64;
        value->value.i64s.length = req->val_array_int64.length;
        for (int i = 0; i < req->val_array_int64.length; i++) {
            value->value.i64s.i64s[i] = req->val_array_int64.i64s[i];
        }
        break;
    case NEU_JSON_ARRAY_DOUBLE:
        value->type              = NEU_TYPE_ARRAY_DOUBLE;
        value->value.f64s.length = req->val_array_double.length;
        for (int i = 0; i < req->val_array_double.length; i++) {
            value->value.f64s.f64s[i] = req->val_array_double.f64s[i];
        }
        break;
    default:
        return -1;
    }
    return 0;
}

static int send_write_tag_req(neu_plugin_t *plugin, neu_json_write_req_t *req,
                              char *playload, bool trace_flag,
                              uint8_t *trace_id, uint8_t *span_id)
{

    neu_reqresp_head_t header = {
        .type = NEU_REQ_WRITE_TAG,
    };

    if (trace_flag) {
        header.otel_trace_type = NEU_OTEL_TRACE_TYPE_EKUIPER;
        header.ctx             = calloc(1, 48 + strlen(playload) + 1);
        binary_to_hex(trace_id, 16, header.ctx);
        binary_to_hex(span_id, 8, header.ctx + 32);
        strcpy(header.ctx + 48, playload);
    }

    neu_req_write_tag_t cmd = {
        .driver = req->node,
        .group  = req->group,
        .tag    = req->tag,
    };

    if (0 != json_value_to_tag_value(&req->value, req->t, &cmd.value)) {
        plog_error(plugin, "invalid tag value type: %d", req->t);
        return -1;
    }

    if (0 != neu_plugin_op(plugin, header, &cmd)) {
        plog_error(plugin, "neu_plugin_op(NEU_REQ_WRITE_TAG) fail");
        return -1;
    }

    req->node  = NULL; // ownership moved
    req->group = NULL; // ownership moved
    req->tag   = NULL; // ownership moved
    return 0;
}

static int send_write_tags_req(neu_plugin_t *             plugin,
                               neu_json_write_tags_req_t *req, char *playload,
                               bool trace_flag, uint8_t *trace_id,
                               uint8_t *span_id)
{
    for (int i = 0; i < req->n_tag; i++) {
        if (req->tags[i].t == NEU_JSON_STR) {
            if (strlen(req->tags[i].value.val_str) >= NEU_VALUE_SIZE) {
                return -1;
            }
        }
    }

    neu_reqresp_head_t header = {
        .type = NEU_REQ_WRITE_TAGS,
    };

    if (trace_flag) {
        header.otel_trace_type = NEU_OTEL_TRACE_TYPE_EKUIPER;
        header.ctx             = calloc(1, 48 + strlen(playload) + 1);
        binary_to_hex(trace_id, 16, header.ctx);
        binary_to_hex(span_id, 8, header.ctx + 32);
        strcpy(header.ctx + 48, playload);
    }

    neu_req_write_tags_t cmd = { 0 };
    cmd.driver               = req->node;
    cmd.group                = req->group;
    cmd.n_tag                = req->n_tag;
    cmd.tags                 = calloc(cmd.n_tag, sizeof(neu_resp_tag_value_t));
    if (NULL == cmd.tags) {
        return -1;
    }

    for (int i = 0; i < cmd.n_tag; i++) {
        strcpy(cmd.tags[i].tag, req->tags[i].tag);
        if (0 !=
            json_value_to_tag_value(&req->tags[i].value, req->tags[i].t,
                                    &cmd.tags[i].value)) {
            plog_error(plugin, "invalid tag value type: %d", req->tags[i].t);
            free(cmd.tags);
            return -1;
        }
    }

    if (0 != neu_plugin_op(plugin, header, &cmd)) {
        plog_error(plugin, "neu_plugin_op(NEU_REQ_WRITE_TAGS) fail");
        free(cmd.tags);
        return -1;
    }

    req->node  = NULL; // ownership moved
    req->group = NULL; // ownership moved

    return 0;
}
