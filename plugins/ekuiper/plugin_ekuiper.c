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

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "errcodes.h"
#include "neuron.h"
#include "otel/otel_manager.h"
#include "utils/asprintf.h"

#include "plugin_ekuiper.h"
#include "read_write.h"

#define EKUIPER_PLUGIN_URL "tcp://127.0.0.1:7081"

const neu_plugin_module_t neu_plugin_module;

static neu_plugin_t *ekuiper_plugin_open(void)
{
    neu_plugin_t *plugin = calloc(1, sizeof(neu_plugin_t));

    neu_plugin_common_init(&plugin->common);

    zlog_notice(neuron, "success to create plugin: %s",
                neu_plugin_module.module_name);
    return plugin;
}

static int ekuiper_plugin_close(neu_plugin_t *plugin)
{
    int rv = 0;

    free(plugin);
    zlog_notice(neuron, "success to free plugin: %s",
                neu_plugin_module.module_name);
    return rv;
}

/**
 * @brief 管道添加事件的回调函数
 *
 * 当有新的管道添加到套接字（plugin->sock）时，pipe_add_cb 函数会被触发。
 * 在这个函数中，将插件的连接状态更新为 NEU_NODE_LINK_STATE_CONNECTED，
 * 这表明此时有新的连接建立，系统可以准备进行数据传输等操作。当有客户端使用 
 * nng_dial 函数连接到服务器监听的地址时，连接成功后就会触发函数
 *
 * @param p 发生事件的 NNG 管道，在本函数中未被使用，通过 (void) p; 标记。
 * @param ev 管道事件的类型，在本函数中未被使用，通过 (void) ev; 标记。
 * @param arg 传递给回调函数的参数，是指向 neu_plugin_t 结构体的指针，代表相关插件。
 */
static void pipe_add_cb(nng_pipe p, nng_pipe_ev ev, void *arg)
{
    (void) p;
    (void) ev;
    neu_plugin_t *plugin = arg;
    nng_mtx_lock(plugin->mtx);
    plugin->common.link_state = NEU_NODE_LINK_STATE_CONNECTED;
    nng_mtx_unlock(plugin->mtx);
}

/**
 * @brief 管道移除事件的回调函数
 *
 * 当 NNG 管道移除事件发生时，此函数会被调用。
 * 它的主要作用是更新插件的连接状态为已断开，通过互斥锁保护对连接状态的修改，
 * 并且更新与断开连接相关的指标，包括 60 秒、600 秒和 1800 秒内的断开连接次数。
 *
 * @param p 发生事件的 NNG 管道，在本函数中未被使用，通过 (void) p; 标记。
 * @param ev 管道事件的类型，在本函数中未被使用，通过 (void) ev; 标记。
 * @param arg 传递给回调函数的参数，是指向 neu_plugin_t 结构体的指针，代表相关插件。
 */
static void pipe_rm_cb(nng_pipe p, nng_pipe_ev ev, void *arg)
{
    (void) p;
    (void) ev;
    neu_plugin_t *plugin = arg;
    nng_mtx_lock(plugin->mtx);
    plugin->common.link_state = NEU_NODE_LINK_STATE_DISCONNECTED;
    nng_mtx_unlock(plugin->mtx);

    NEU_PLUGIN_UPDATE_METRIC(plugin, NEU_METRIC_DISCONNECTION_60S, 1, NULL);
    NEU_PLUGIN_UPDATE_METRIC(plugin, NEU_METRIC_DISCONNECTION_600S, 1, NULL);
    NEU_PLUGIN_UPDATE_METRIC(plugin, NEU_METRIC_DISCONNECTION_1800S, 1, NULL);
}

/**
 * @brief 初始化 eKuiper 插件。
 *
 * 该函数用于对 eKuiper 插件进行初始化操作，包括分配互斥锁、异步 I/O 对象，
 * 以及注册多个指标。如果在初始化过程中出现错误，函数会记录错误日志并返回相应的错误码。
 *
 * @param plugin 指向 neu_plugin_t 结构体的指针，代表要初始化的插件。
 * @param load 一个布尔值，指示是否加载插件。在本函数中该参数未被使用。
 * @return int 初始化成功返回 0，失败则返回相应的错误码。
 */
static int ekuiper_plugin_init(neu_plugin_t *plugin, bool load)
{
    (void) load;
    int      rv       = 0;
    nng_aio *recv_aio = NULL;

    // 初始化插件的互斥锁
    plugin->mtx = NULL;
    rv          = nng_mtx_alloc(&plugin->mtx);
    if (0 != rv) {
        plog_error(plugin, "cannot allocate nng_mtx");
        return rv;
    }

    // 分配异步 I/O 对象
    rv = nng_aio_alloc(&recv_aio, recv_data_callback, plugin);
    if (rv < 0) {
        plog_error(plugin, "cannot allocate recv_aio: %s", nng_strerror(rv));
        nng_mtx_free(plugin->mtx);
        plugin->mtx = NULL;
        return rv;
    }

    // 将分配的异步 I/O 对象赋值给插件
    plugin->recv_aio = recv_aio;

    // 注册多个指标，用于监控数据传输、字节数、消息数和断开连接情况
    NEU_PLUGIN_REGISTER_METRIC(plugin, NEU_METRIC_TRANS_DATA_5S, 5000);
    NEU_PLUGIN_REGISTER_METRIC(plugin, NEU_METRIC_TRANS_DATA_30S, 30000);
    NEU_PLUGIN_REGISTER_METRIC(plugin, NEU_METRIC_TRANS_DATA_60S, 60000);
    NEU_PLUGIN_REGISTER_METRIC(plugin, NEU_METRIC_SEND_BYTES_5S, 5000);
    NEU_PLUGIN_REGISTER_METRIC(plugin, NEU_METRIC_SEND_BYTES_30S, 30000);
    NEU_PLUGIN_REGISTER_METRIC(plugin, NEU_METRIC_SEND_BYTES_60S, 60000);
    NEU_PLUGIN_REGISTER_METRIC(plugin, NEU_METRIC_RECV_BYTES_5S, 5000);
    NEU_PLUGIN_REGISTER_METRIC(plugin, NEU_METRIC_RECV_BYTES_30S, 30000);
    NEU_PLUGIN_REGISTER_METRIC(plugin, NEU_METRIC_RECV_BYTES_60S, 60000);
    NEU_PLUGIN_REGISTER_METRIC(plugin, NEU_METRIC_RECV_MSGS_5S, 5000);
    NEU_PLUGIN_REGISTER_METRIC(plugin, NEU_METRIC_RECV_MSGS_30S, 30000);
    NEU_PLUGIN_REGISTER_METRIC(plugin, NEU_METRIC_RECV_MSGS_60S, 60000);
    NEU_PLUGIN_REGISTER_METRIC(plugin, NEU_METRIC_DISCONNECTION_60S, 60000);
    NEU_PLUGIN_REGISTER_METRIC(plugin, NEU_METRIC_DISCONNECTION_600S, 600000);
    NEU_PLUGIN_REGISTER_METRIC(plugin, NEU_METRIC_DISCONNECTION_1800S, 1800000);

    plog_notice(plugin, "plugin initialized");
    return rv;
}

static int ekuiper_plugin_uninit(neu_plugin_t *plugin)
{
    int rv = 0;

    nng_close(plugin->sock);
    nng_aio_free(plugin->recv_aio);
    nng_mtx_free(plugin->mtx);
    free(plugin->host);
    free(plugin->url);

    plog_notice(plugin, "plugin uninitialized");
    return rv;
}

/**
 * @brief 启动插件的网络监听功能
 *
 * 该函数用于初始化并启动插件的网络监听功能，主要步骤包括打开一个 NNG 的 pair0 套接字，
 * 设置管道事件通知回调函数，配置套接字的发送和接收缓冲区大小，绑定到指定的 URL 进行监听，
 * 并在确保当前没有正在进行的接收操作后，开始异步接收数据。
 *
 * @param plugin 指向 neu_plugin_t 结构体的指针，代表要启动的插件。
 * @param url 一个指向字符串的指针，指定了要绑定和监听的 URL 地址。
 * @return int 返回值表示操作的结果，NEU_ERR_SUCCESS 表示成功，其他值表示失败，
 *               具体的错误码会根据不同的错误情况进行设置。
 * 
 * @warning
 * 
 * 在当前 start 函数的实现里，如果没有设计超时情况，并且双方正常连接且没有连接断开，
 * 也没有数据发送过来，那么异步接收操作就会一直处于等待状态，回调函数确实不会被调用，
 * plugin->receiving 会一直保持为 true。导致ekuiper_plugin_config无法重新启动
 * 新配置的插件。需要加入超时时间限制：nng_aio_set_timeout(plugin->recv_aio, 5000);
 * 
 */
static inline int start(neu_plugin_t *plugin, const char *url)
{
    int rv = 0;

    // 打开一个 NNG 的 pair0 套接字
    rv = nng_pair0_open(&plugin->sock);
    if (rv != 0) {
        plog_error(plugin, "nng_pair0_open: %s", nng_strerror(rv));
        return NEU_ERR_EINTERNAL;
    }

    // 设置管道添加事件的通知回调函数
    nng_pipe_notify(plugin->sock, NNG_PIPE_EV_ADD_POST, pipe_add_cb, plugin);

    // 设置管道移除事件的通知回调函数
    nng_pipe_notify(plugin->sock, NNG_PIPE_EV_REM_POST, pipe_rm_cb, plugin);

    // 设置套接字的发送缓冲区大小为 2048 字节
    nng_socket_set_int(plugin->sock, NNG_OPT_SENDBUF, 2048);

    // 设置套接字的接收缓冲区大小为 2048 字节
    nng_socket_set_int(plugin->sock, NNG_OPT_RECVBUF, 2048);

    // 将套接字绑定到指定的 URL 进行监听
    if ((rv = nng_listen(plugin->sock, url, NULL, 0)) != 0) {
        // 若绑定失败，记录错误日志并关闭套接字
        plog_error(plugin, "nng_listen: %s", nng_strerror(rv));
        nng_close(plugin->sock);

        // 根据不同的错误类型设置相应的错误码
        if (NNG_EADDRINVAL == rv) {
            rv = NEU_ERR_IP_ADDRESS_INVALID;
        } else if (NNG_EADDRINUSE == rv) {
            rv = NEU_ERR_IP_ADDRESS_IN_USE;
        } else {
            rv = NEU_ERR_EINTERNAL;
        }
        return rv;
    }

    // 锁定互斥锁，确保当前没有正在进行的接收操作
    nng_mtx_lock(plugin->mtx);
    while (plugin->receiving) {
        nng_mtx_unlock(plugin->mtx);
        nng_msleep(10);
        nng_mtx_lock(plugin->mtx);
    }

    // 设置接收标志为正在接收
    plugin->receiving = true;

    // 开始异步接收数据
    nng_recv_aio(plugin->sock, plugin->recv_aio);
    nng_mtx_unlock(plugin->mtx);

    return NEU_ERR_SUCCESS;
}

/**
 * @brief 启动 eKuiper 插件
 *
 * 该函数用于启动 eKuiper 插件，主要步骤包括获取插件的 URL（若未设置则使用默认 URL），
 * 调用 `start` 函数启动插件的网络监听功能，并在成功启动后设置插件的启动标志为 `true`，
 * 记录启动成功的通知日志。
 *
 * @param plugin 指向 neu_plugin_t 结构体的指针，代表要启动的 eKuiper 插件。
 * @return int 返回值表示插件启动操作的结果，NEU_ERR_SUCCESS 表示成功启动，
 *             若启动过程中出现错误，则返回相应的错误码（由 `start` 函数返回的错误码决定）。
 */
static int ekuiper_plugin_start(neu_plugin_t *plugin)
{
    int   rv  = 0;
    char *url = plugin->url ? plugin->url : EKUIPER_PLUGIN_URL; // default url

    // 调用 start 函数启动插件的网络监听功能
    rv = start(plugin, url);
    if (rv != 0) {
        return rv;
    }

    // 设置插件的启动标志为 true，表示插件已成功启动
    plugin->started = true;
    plog_notice(plugin, "start successfully");

    return NEU_ERR_SUCCESS;
}

static inline void stop(neu_plugin_t *plugin)
{
    nng_close(plugin->sock);
}

static int ekuiper_plugin_stop(neu_plugin_t *plugin)
{
    stop(plugin);
    plugin->started = false;
    plog_notice(plugin, "stop successfully");
    return NEU_ERR_SUCCESS;
}

static int parse_config(neu_plugin_t *plugin, const char *setting,
                        char **host_p, uint16_t *port_p)
{
    char *          err_param = NULL;
    neu_json_elem_t host      = { .name = "host", .t = NEU_JSON_STR };
    neu_json_elem_t port      = { .name = "port", .t = NEU_JSON_INT };

    if (0 != neu_parse_param(setting, &err_param, 2, &host, &port)) {
        plog_error(plugin, "parsing setting fail, key: `%s`", err_param);
        goto error;
    }

    // host, required
    if (0 == strlen(host.v.val_str)) {
        plog_error(plugin, "setting invalid host: `%s`", host.v.val_str);
        goto error;
    }

    struct in_addr addr;
    if (0 == inet_aton(host.v.val_str, &addr)) {
        plog_error(plugin, "inet_aton fail: %s", host.v.val_str);
        goto error;
    }

    // port, required
    if (0 == port.v.val_int || port.v.val_int > 65535) {
        plog_error(plugin, "setting invalid port: %" PRIi64, port.v.val_int);
        goto error;
    }

    *host_p = host.v.val_str;
    *port_p = port.v.val_int;

    plog_notice(plugin, "config host:%s port:%" PRIu16, *host_p, *port_p);

    return 0;

error:
    free(err_param);
    free(host.v.val_str);
    return -1;
}

/**
 * @brief 配置 eKuiper 插件
 *
 * 该函数用于对 eKuiper 插件进行配置，主要操作包括解析配置信息、生成插件监听的 URL、
 * 根据新配置尝试启动插件、处理启动失败的情况并恢复旧配置、更新插件的配置参数等。
 *
 * @param plugin 指向 neu_plugin_t 结构体的指针，代表要配置的 eKuiper 插件。
 * @param setting 一个指向字符串的指针，包含了插件的配置信息，用于解析出主机名和端口号。
 * @return int 返回值表示配置操作的结果，0 表示配置成功，其他值表示配置失败，
 *               具体的错误码由函数内部的操作和错误情况决定。
 */
static int ekuiper_plugin_config(neu_plugin_t *plugin, const char *setting)
{
    int      rv   = 0;
    char *   url  = NULL;
    char *   host = NULL;
    uint16_t port = 0;

    // 解析配置信息，获取主机名和端口号
    if (0 != parse_config(plugin, setting, &host, &port)) {
        rv = NEU_ERR_NODE_SETTING_INVALID;
        goto error;
    }

    // 根据主机名和端口号生成 URL
    neu_asprintf(&url, "tcp://%s:%" PRIu16, host, port);
    if (NULL == url) {
        plog_error(plugin, "create url fail");
        rv = NEU_ERR_EINTERNAL;
        goto error;
    }

    // 如果插件已经启动，先停止插件
    if (plugin->started) {
        stop(plugin);
    }

    // 尝试使用新配置启动插件
    if (0 != (rv = start(plugin, url))) {
        // 若启动失败，尝试恢复旧配置并启动插件
        if (plugin->started && 0 != start(plugin, plugin->url)) {
            plog_warn(plugin, "restart host:%s port:%" PRIu16 " fail",
                      plugin->host, plugin->port);
        }
        goto error;
    }

    // 如果插件没有成功启动，停止插件
    if (!plugin->started) {
        stop(plugin);
    }

    plog_notice(plugin, "config success");
    
    // 释放旧的主机名和 URL 内存，并更新插件的配置参数
    free(plugin->host);
    free(plugin->url);
    plugin->host = host;
    plugin->port = port;
    plugin->url  = url;

    return rv;

error:
    // 释放生成的 URL 和解析出的主机名内存
    free(url);
    free(host);
    plog_error(plugin, "config failure");
    return rv;
}

/**
 * @brief 处理来自插件的请求
 *
 * 该函数用于处理来自插件的请求，根据请求头的类型执行不同的操作。
 * 同时，支持 OpenTelemetry 跟踪，用于记录和分析请求处理过程。
 *
 * @param plugin 指向插件结构体的指针，包含插件的相关信息和状态。
 * @param header 指向请求响应头结构体的指针，包含请求的类型和上下文信息。
 * @param data 指向请求数据的指针，根据请求类型的不同，数据的内容和格式也不同。
 *
 * @return 总是返回 0，表示请求处理完成。
 */
static int ekuiper_plugin_request(neu_plugin_t *      plugin,
                                  neu_reqresp_head_t *header, void *data)
{
    bool disconnected = false;

    plog_debug(plugin, "handling request type: %d", header->type);

    nng_mtx_lock(plugin->mtx);
    disconnected =
        NEU_NODE_LINK_STATE_DISCONNECTED == plugin->common.link_state;
    nng_mtx_unlock(plugin->mtx);

    // OpenTelemetry 跟踪上下文和范围上下文
    neu_otel_trace_ctx trace = NULL;
    neu_otel_scope_ctx scope = NULL;

    if (neu_otel_control_is_started() && header->ctx) {
        trace = neu_otel_find_trace(header->ctx);
        if (trace) {
            // 添加一个新的跨度
            scope = neu_otel_add_span(trace);

            // 设置跨度的名称
            neu_otel_scope_set_span_name(scope, "ekuiper response");

            // 生成新的跨度 ID
            char new_span_id[36] = { 0 };
            neu_otel_new_span_id(new_span_id);

            // 设置跨度的 ID
            neu_otel_scope_set_span_id(scope, new_span_id);

            // 获取前一个跨度的 ID
            uint8_t *p_sp_id = neu_otel_scope_get_pre_span_id(scope);
            if (p_sp_id) {
                // 设置父跨度的 ID
                neu_otel_scope_set_parent_span_id2(scope, p_sp_id, 8);
            }

            // 添加跨度属性，记录线程 ID
            neu_otel_scope_add_span_attr_int(scope, "thread id",
                                             (int64_t)(pthread_self()));
            
            // 设置跨度的开始时间
            neu_otel_scope_set_span_start_time(scope, neu_time_ns());
        }
    }

    // 根据请求头的类型进行不同的处理s
    switch (header->type) {
    case NEU_RESP_ERROR: {
        neu_resp_error_t *error = (neu_resp_error_t *) data;
        plog_debug(plugin, "receive resp errcode: %d", error->error);
        if (trace) {
            if (error->error != NEU_ERR_SUCCESS) {
                // 设置跨度的状态码为错误
                neu_otel_scope_set_status_code2(scope, NEU_OTEL_STATUS_ERROR,
                                                error->error);
            } else {
                // 设置跨度的状态码为成功
                neu_otel_scope_set_status_code2(scope, NEU_OTEL_STATUS_OK,
                                                error->error);
            }
            // 设置跨度的结束时间
            neu_otel_scope_set_span_end_time(scope, neu_time_ns());

            // 标记跟踪为最终状态
            neu_otel_trace_set_final(trace);
        }
        if (header->ctx) {
            free(header->ctx);
        }
        break;
    }
    case NEU_REQRESP_TRANS_DATA: {
        neu_reqresp_trans_data_t *trans_data = data;

        if (plugin->started) {
            // 更新不同时间窗口的传输数据指标
            NEU_PLUGIN_UPDATE_METRIC(plugin, NEU_METRIC_TRANS_DATA_5S, 1, NULL);
            NEU_PLUGIN_UPDATE_METRIC(plugin, NEU_METRIC_TRANS_DATA_30S, 1,
                                     NULL);
            NEU_PLUGIN_UPDATE_METRIC(plugin, NEU_METRIC_TRANS_DATA_60S, 1,
                                     NULL);
        }

        if (disconnected) {
            plog_debug(plugin, "not connected");

            neu_otel_trace_ctx trans_trace = NULL;
            neu_otel_scope_ctx trans_scope = NULL;
            if (neu_otel_data_is_started() && trans_data->trace_ctx) {
                trans_trace = neu_otel_find_trace(trans_data->trace_ctx);
                if (trans_trace) {
                    char new_span_id[36] = { 0 };
                    neu_otel_new_span_id(new_span_id);
                    trans_scope = neu_otel_add_span2(
                        trans_trace, "ekuiper send", new_span_id);
                    neu_otel_scope_add_span_attr_int(trans_scope, "thread id",
                                                     (int64_t)(pthread_self()));
                    neu_otel_scope_set_status_code2(
                        trans_scope, NEU_OTEL_STATUS_ERROR,
                        NEU_ERR_PLUGIN_DISCONNECTED);
                    neu_otel_scope_set_span_start_time(trans_scope,
                                                       neu_time_ns());
                    neu_otel_scope_set_span_end_time(trans_scope,
                                                     neu_time_ns());
                    neu_otel_trace_set_final(trans_trace);
                }
            }

        } else {
            send_data(plugin, trans_data);
        }
        break;
    }
    case NEU_REQRESP_NODE_DELETED: {
        break;
    }
    case NEU_REQ_UPDATE_NODE: {
        break;
    }
    case NEU_REQ_UPDATE_GROUP: {
        break;
    }
    case NEU_REQ_DEL_GROUP:
        break;
    case NEU_REQ_SUBSCRIBE_GROUP:
    case NEU_REQ_UPDATE_SUBSCRIBE_GROUP: {
        neu_req_subscribe_t *sub_info = data;
        free(sub_info->params);
        break;
    }
    case NEU_REQ_UNSUBSCRIBE_GROUP:
        break;
    default:
        plog_warn(plugin, "unsupported request type: %d", header->type);
        break;
    }

    return 0;
}

static const neu_plugin_intf_funs_t plugin_intf_funs = {
    .open    = ekuiper_plugin_open,
    .close   = ekuiper_plugin_close,
    .init    = ekuiper_plugin_init,
    .uninit  = ekuiper_plugin_uninit,
    .start   = ekuiper_plugin_start,
    .stop    = ekuiper_plugin_stop,
    .setting = ekuiper_plugin_config,
    .request = ekuiper_plugin_request,
};

/**
 * @brief 插件模块信息
 *
 * 该结构体定义了插件的基本信息，如版本号、模式名称、描述等。
 * 生成的 .so 库可通过该结构体定位并获取插件的接口函数。
 */
const neu_plugin_module_t neu_plugin_module = {
    .version         = NEURON_PLUGIN_VER_1_0,
    .schema          = "ekuiper",
    .module_name     = "eKuiper",
    .module_descr    = "LF Edge eKuiper integration plugin",
    .module_descr_zh = "LF Edge eKuiper 一体化插件",
    .intf_funs       = &plugin_intf_funs,
    .kind            = NEU_PLUGIN_KIND_SYSTEM,
    .type            = NEU_NA_TYPE_APP,
    .display         = true,
    .single          = false,
};
