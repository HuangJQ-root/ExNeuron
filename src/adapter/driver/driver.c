/**
 * NEURON IIoT System for Industry 4.0
 * Copyright (C) 2020-2021 EMQ Technologies Co., Ltd All rights reserved.
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
#include <assert.h>
#include <float.h>
#include <math.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdlib.h>

#define EPSILON 1e-9

#include "event/event.h"
#include "utils/http.h"
#include "utils/log.h"
#include "utils/time.h"
#include "utils/utextend.h"

#include "adapter.h"
#include "adapter/adapter_internal.h"
#include "adapter/storage.h"
#include "base/group.h"
#include "cache.h"
#include "driver_internal.h"
#include "errcodes.h"
#include "tag.h"

#include "otel/otel_manager.h"

typedef struct to_be_write_tag {
    bool           single;
    neu_datatag_t *tag;
    neu_value_u    value;
    UT_array *     tvs;
    void *         req;
} to_be_write_tag_t;

typedef struct {
    char               app[NEU_NODE_NAME_LEN];
    struct sockaddr_un addr;
} sub_app_t;

/**
 * @brief 表示一个组的信息。
 *  
 * 主要用于管理与特定组相关的数据和操作。
 */
typedef struct group {
    /**
     * @brief 组的名字。
     *
     * 这个字段存储了组的名称，通常用于标识不同的组实例。
     */
    char *name;

    /**
     * @brief 时间戳。
     *
     * 记录了与该组相关的最新操作的时间戳（以毫秒为单位）。
     */
    int64_t         timestamp;

    /**
     * @brief 指向 neu_group_t 类型结构体的指针。
     *
     * 包含了与该组相关的详细信息和配置。：标签列表，周期
     */
    neu_group_t *   group;

    /**
     * @brief 插件组信息。
     * 
     * 侧重于管理与该组相关的插件实现的具体细节。
     * 插件通常是为了扩展组的功能而存在的
     */
    neu_plugin_group_t    grp;

    /**
     * @brief 写标签列表。
     *
     * 使用 UT_array 存储需要写入的标签信息。
     */
    UT_array *      wt_tags;

    /**
     * @brief 写标签列表的互斥锁。
     *
     * 用于保护 wt_tags 的并发访问。
     */
    pthread_mutex_t wt_mtx;

    /**
     * @brief 应用列表的互斥锁。
     *
     * 用于保护 apps 的并发访问。
     */
    pthread_mutex_t apps_mtx;


    /**
     * @brief 报告定时器。
     *
     * 用于定期上报组状态或数据。
     * start_group_timer中启动。
     */
    neu_event_timer_t *report;

    /**
     * @brief 读取定时器。
     *
     * 用于触发周期性的数据读取操作。
     * start_group_timer中启动。
     */
    neu_event_timer_t *read;

    /**
     * @brief 写入定时器。
     *
     * 用于触发周期性的数据写入操作。
     * start_group_timer中启动。
     */
    neu_event_timer_t *write;

    /**
     * @brief 应用列表。
     *
     * 存储与该组关联的应用实例数组（sub_app_t类型）。
     */
    UT_array *      apps; 

    /**
     * @brief 指向驱动适配器的指针。
     *
     * 表示当前组所属的驱动适配器。
     */
    neu_adapter_driver_t *driver;

    /**
     * @brief UTHash句柄。
     *
     * 支持将该结构体作为哈希表中的元素进行高效管理。
     */
    UT_hash_handle hh;
} group_t;

/**
 * @struct neu_adapter_driver
 * @brief 该结构体用于表示驱动适配器的信息。
 *
 * 包含一个基础适配器结构体、缓存、事件处理机制、标签计数以及组信息。
 * 主要用于管理与硬件设备交互的驱动程序。
 */
struct neu_adapter_driver {
    /**
     * @brief 基础适配器结构体。
     *
     * 这个字段包含了所有适配器共有的基本信息和功能，如名称、状态、回调函数等。
     * 
     * @warning
     * -neu_adapter_t类必须为第一个元素。因为会使用强转(neu_adapter_t *)类型
     * -指针类型的转换仅仅改变了编译器对指针所指向内存区域的解释方式，
     *  而不会改变指针实际存储的内存地址。
     */
    neu_adapter_t       adapter;

    /**
     * @brief 驱动适配器的缓存。
     *
     * 用于存储驱动适配器相关的数据缓存，便于快速访问和操作。
     */
    neu_driver_cache_t *cache;  

    /**
     * @brief 指向驱动适配器事件处理结构体的指针。
     *
     * 此字段包含驱动适配器的所有事件处理逻辑，支持异步事件通知和处理。
     */
    neu_events_t       *driver_events;

    /**
     * @brief 标签计数。
     *
     * 表示该驱动适配器管理的标签数量。
     */
    size_t              tag_cnt;

    /**
     * @brief 组信息。
     *
     * 存储了该驱动适配器管理的所有组的信息，每个组可能包含多个标签。
     */
    struct group       *groups;
};

static void report_to_app(neu_adapter_driver_t *driver, group_t *group,
                          struct sockaddr_un dst);
static int  report_callback(void *usr_data);
static int  read_callback(void *usr_data);
static int  write_callback(void *usr_data);
static void read_group(int64_t timestamp, int64_t timeout,
                       neu_tag_cache_type_e cache_type,
                       neu_driver_cache_t *cache, const char *group,
                       UT_array *tags, UT_array *tag_values);
static void read_group_paginate(int64_t timestamp, int64_t timeout,
                                neu_tag_cache_type_e cache_type,
                                neu_driver_cache_t *cache, const char *group,
                                UT_array *tags, UT_array *tag_values);
static void read_report_group(int64_t timestamp, int64_t timeout,
                              neu_tag_cache_type_e cache_type,
                              neu_driver_cache_t *cache, const char *group,
                              UT_array *tags, UT_array *tag_values);
static void update_with_trace(neu_adapter_t *adapter, const char *group,
                              const char *tag, neu_dvalue_t value,
                              neu_tag_meta_t *metas, int n_meta,
                              void *trace_ctx);
static void update(neu_adapter_t *adapter, const char *group, const char *tag,
                   neu_dvalue_t value);
static void update_im(neu_adapter_t *adapter, const char *group,
                      const char *tag, neu_dvalue_t value,
                      neu_tag_meta_t *metas, int n_meta);
static void update_with_meta(neu_adapter_t *adapter, const char *group,
                             const char *tag, neu_dvalue_t value,
                             neu_tag_meta_t *metas, int n_meta);
static void write_response(neu_adapter_t *adapter, void *r, neu_error error);
static void write_responses(neu_adapter_t *adapter, void *r,
                            neu_driver_write_responses_t *response,
                            int                           n_response);
static void directory_response(neu_adapter_t *adapter, void *req, int error,
                               neu_driver_file_info_t *infos, int n_info);
static void fup_open_response(neu_adapter_t *adapter, void *req, int error,
                              int64_t size);
static void fdown_open_response(neu_adapter_t *adapter, void *req, int error);
static void fup_data_response(neu_adapter_t *adapter, void *req, int error,
                              uint8_t *bytes, uint16_t n_bytes, bool more);

static group_t *   find_group(neu_adapter_driver_t *driver, const char *name);
static void        store_write_tag(group_t *group, to_be_write_tag_t *tag);
static inline void start_group_timer(neu_adapter_driver_t *driver,
                                     group_t *             grp);
static inline void stop_group_timer(neu_adapter_driver_t *driver, group_t *grp);

/**
 * @brief 对 neu_dvalue_t 类型的双精度浮点数进行格式化处理。
 *
 * 该函数的主要目的是对双精度浮点数的小数部分进行简化，去除末尾连续的 0 或 9，
 * 以减少小数位数。处理过程包括处理负数、分离整数和小数部分、对小数部分进行
 * 四舍五入和字符串处理，最后合并整数和小数部分并恢复符号。
 *
 * @param value 指向 neu_dvalue_t 类型的指针，包含要格式化的双精度浮点数。
 */
static void format_tag_value(neu_dvalue_t *value)
{
    // 定义缩放因子，用于将小数部分放大 10^5 倍
    double scale = pow(10, 5);

    // 记录符号，初始为正数
    int negative = 1;

    // 如果输入的双精度浮点数为负数，将其转换为正数，并记录符号为负数
    if (value->value.d64 < 0) {
        value->value.d64 *= -1;
        negative = -1;
    }

    int64_t integer_part = (int64_t) value->value.d64;
    double  decimal_part = value->value.d64 - integer_part;
    decimal_part *= scale;

    // 对小数部分进行四舍五入
    decimal_part = round(decimal_part);

    // 定义一个字符串，用于存储小数部分转换后的结果
    char str[6]  = { 0 };
    // 将四舍五入后的小数部分转换为字符串，格式为 5 位整数
    snprintf(str, sizeof(str), "%05" PRId64 "", (int64_t) decimal_part);
    int i = 0, flag = 0;
    for (; i < 4; i++) {
        if (str[i] == '0' && str[i + 1] == '0') {
            flag = 1;
            break;
        } else if (str[i] == '9' && str[i + 1] == '9') {
            flag = 2;
            break;
        }
    }
    if (flag != 0 && i != 0) {
        decimal_part     = round(decimal_part / pow(10, 5 - i));
        value->value.d64 = (double) integer_part + decimal_part / pow(10, i);
    } else {
        value->value.d64 = (double) integer_part + decimal_part / scale;
    }

    value->value.d64 *= negative;
}

static void write_responses(neu_adapter_t *adapter, void *r,
                            neu_driver_write_responses_t *response,
                            int                           n_response)
{
    neu_reqresp_head_t *  req   = (neu_reqresp_head_t *) r;
    neu_resp_write_tags_t nresp = { 0 };
    req->type                   = NEU_RESP_WRITE_TAGS;
    UT_icd    icd   = { sizeof(neu_resp_write_tags_ele_t), NULL, NULL, NULL };
    UT_array *array = NULL;

    utarray_new(array, &icd);

    for (int i = 0; i < n_response; i++) {
        neu_driver_write_responses_t *resp = &response[i];
        neu_resp_write_tags_ele_t     ele  = { 0 };

        strcpy(ele.group, resp->group);
        strcpy(ele.tag, resp->name);
        ele.error = resp->error;
        utarray_push_back(array, &ele);
    }

    nresp.tags = array;
    adapter->cb_funs.response(adapter, req, &nresp);
}

/**
 * @brief 处理并响应写入标签请求。
 *
 * 根据请求类型执行清理操作，并根据请求的结果设置适当的响应，
 * 最终通过适配器的回调函数发送响应。如果启用了OpenTelemetry，则会记录跟踪信息。
 *
 * @param adapter 指向neu_adapter_t结构体的指针，包含适配器的相关信息。
 * @param r 请求的头部指针，转换为neu_reqresp_head_t类型使用。
 * @param error 错误码，表示请求处理的结果。
 */
static void write_response(neu_adapter_t *adapter, void *r, neu_error error)
{
    // 将传入的void指针转换为neu_reqresp_head_t类型的指针
    neu_reqresp_head_t *req    = (neu_reqresp_head_t *) r;

    // 创建一个包含错误码的响应对象
    neu_resp_error_t    nerror = { .error = error };

    // OpenTelemetry相关变量声明
    neu_otel_trace_ctx trace = NULL;
    neu_otel_scope_ctx scope = NULL;

    // 如果OpenTelemetry已启动，则获取当前请求的跟踪上下文
    if (neu_otel_control_is_started()) {
        // 查找与请求关联的跟踪上下文
        trace = neu_otel_find_trace(req->ctx);
        if (trace) { //找到了跟踪上下文，
            // 为当前请求添加一个新的span，并将其作用域上下文存储在 scope 中
            scope                = neu_otel_add_span(trace);
            char new_span_id[36] = { 0 };
            neu_otel_new_span_id(new_span_id);

            //设置 Span ID
            neu_otel_scope_set_span_id(scope, new_span_id);
            uint8_t *p_sp_id = neu_otel_scope_get_pre_span_id(scope);
            if (p_sp_id) {
                neu_otel_scope_set_parent_span_id2(scope, p_sp_id, 8);
            }

            //添加 Span 属性，记录当前线程的 ID
            neu_otel_scope_add_span_attr_int(scope, "thread id",
                                             (int64_t) pthread_self());

            //设置 Span 开始时间
            neu_otel_scope_set_span_start_time(scope, neu_time_ns());
        }
    }

    // 根据请求类型执行相应的清理操作
    if (NEU_REQ_WRITE_TAG == req->type) {
        // 清理单个标签写入请求
        neu_req_write_tag_fini((neu_req_write_tag_t *) &req[1]);
        if (neu_otel_control_is_started() && trace) {
            neu_otel_scope_set_span_name(scope, "driver write tag response");
        }
    } else if (NEU_REQ_WRITE_TAGS == req->type) {
        // 清理多个标签写入请求
        neu_req_write_tags_fini((neu_req_write_tags_t *) &req[1]);
        if (neu_otel_control_is_started() && trace) {
            neu_otel_scope_set_span_name(scope, "driver write tags response");
        }
    } else if (NEU_REQ_WRITE_GTAGS == req->type) {
        // 清理全局标签写入请求
        neu_req_write_gtags_fini((neu_req_write_gtags_t *) &req[1]);
        if (neu_otel_control_is_started() && trace) {
            neu_otel_scope_set_span_name(scope, "driver write gtags response");
        }
    }

    // 设置响应类型为错误响应
    req->type = NEU_RESP_ERROR;

    nlog_notice("write tag response start <%p>", req->ctx);

    // 调用适配器的回调函数发送响应
    adapter->cb_funs.response(adapter, req, &nerror);

    // 如果OpenTelemetry已启用且有跟踪上下文
    if (neu_otel_control_is_started() && trace) {
        // 根据错误状态设置span状态码
        if (error == NEU_ERR_SUCCESS) {
            neu_otel_scope_set_status_code2(scope, NEU_OTEL_STATUS_OK, error);
        } else {
            neu_otel_scope_set_status_code2(scope, NEU_OTEL_STATUS_ERROR,
                                            error);
        }

        // 设置span结束时间
        neu_otel_scope_set_span_end_time(scope, neu_time_ns());
    }
}

static void fup_data_response(neu_adapter_t *adapter, void *r, int error,
                              uint8_t *bytes, uint16_t n_bytes, bool more)
{
    neu_reqresp_head_t *req = (neu_reqresp_head_t *) r;
    req->type               = NEU_RESP_FUP_DATA;

    neu_resp_fup_data_t resp = { 0 };

    resp.error = error;
    if (resp.error == 0) {
        resp.more = more;
        resp.len  = n_bytes;
        resp.data = calloc(1, n_bytes);
        memcpy(resp.data, bytes, n_bytes);
    }

    adapter->cb_funs.response(adapter, req, &resp);
}

static void fdown_open_response(neu_adapter_t *adapter, void *req, int error)
{
    neu_reqresp_head_t *r = (neu_reqresp_head_t *) req;
    r->type               = NEU_RESP_FDOWN_OPEN;

    neu_resp_fup_data_t resp = { 0 };
    resp.error               = error;

    adapter->cb_funs.response(adapter, r, &resp);
}

static void fup_open_response(neu_adapter_t *adapter, void *r, int error,
                              int64_t size)
{
    neu_reqresp_head_t *req = (neu_reqresp_head_t *) r;
    req->type               = NEU_RESP_FUP_OPEN;

    neu_resp_fup_open_t resp = { 0 };

    resp.error = error;
    resp.size  = size;

    adapter->cb_funs.response(adapter, req, &resp);
}

static void directory_response(neu_adapter_t *adapter, void *r, int error,
                               neu_driver_file_info_t *infos, int n_info)
{
    neu_reqresp_head_t *req = (neu_reqresp_head_t *) r;
    req->type               = NEU_RESP_DRIVER_DIRECTORY;

    neu_resp_driver_directory_t resp = { 0 };
    UT_icd directory_file_icd = { sizeof(neu_resp_driver_directory_file_t),
                                  NULL, NULL, NULL };

    resp.error = error;
    if (error == NEU_ERR_SUCCESS) {
        utarray_new(resp.files, &directory_file_icd);

        for (int i = 0; i < n_info; i++) {
            neu_driver_file_info_t *         info = &infos[i];
            neu_resp_driver_directory_file_t file = { 0 };

            strcpy(file.name, info->path);
            file.ftype     = info->ftype;
            file.size      = info->size;
            file.timestamp = info->mtime;

            utarray_push_back(resp.files, &file);
        }
    }

    adapter->cb_funs.response(adapter, req, &resp);
}

/**
 * @brief 更新带有元数据的值。
 *
 * 该函数用于更新指定组和标签的数据值，并根据值的类型（正常或错误）进行相应的处理。
 * 如果是错误类型，会更新错误码和时间戳等指标；如果是正常类型，则直接更新缓存并增加读取计数。
 *
 * @param adapter 指向适配器结构体的指针。
 * @param group 组名字符串，标识所属的组。
 * @param tag 标签名字符串，标识组内的具体标签。对于某些错误情况，此参数可能为NULL。
 * @param value 包含新值及类型的 neu_dvalue_t 结构体。
 * @param metas 元数据数组，包含与该值相关的元数据信息。
 * @param n_meta 元数据的数量。
 */
static void update_with_meta(neu_adapter_t *adapter, const char *group,
                             const char *tag, neu_dvalue_t value,
                             neu_tag_meta_t *metas, int n_meta)
{
    // 将适配器转换为驱动适配器类型
    neu_adapter_driver_t *         driver = (neu_adapter_driver_t *) adapter;

    // 获取更新指标的回调函数
    neu_adapter_update_metric_cb_t update_metric =
        driver->adapter.cb_funs.update_metric;

    // 如果值的类型是错误类型，更新错误码和时间戳指标
    if (value.type == NEU_TYPE_ERROR) {
        update_metric(&driver->adapter, NEU_METRIC_GROUP_LAST_ERROR_CODE,
                      value.value.i32, group);
        update_metric(&driver->adapter, NEU_METRIC_GROUP_LAST_ERROR_TS,
                      global_timestamp, group);
    }

    // 如果值的类型是错误且标签为空，则遍历组中的所有属性符合的标签进行错误处理
    if (value.type == NEU_TYPE_ERROR && tag == NULL) {
        group_t *g = find_group(driver, group);
        if (g != NULL) {
            // 获取组中所有需要可读或可订阅的标签
            UT_array *tags      = neu_group_get_read_tag(g->group);
            uint64_t  err_count = 0;

            // 遍历指定组中的所有可读取标签，将每个标签的值更新为错误值
            utarray_foreach(tags, neu_datatag_t *, t)
            {
                neu_driver_cache_update(driver->cache, group, t->name,
                                        global_timestamp, value, NULL, 0);
                ++err_count;
            }

            // 更新读取总数和错误总数指标
            update_metric(&driver->adapter, NEU_METRIC_TAG_READS_TOTAL,
                          err_count, NULL);
            update_metric(&driver->adapter, NEU_METRIC_TAG_READ_ERRORS_TOTAL,
                          err_count, NULL);
            
            utarray_free(tags);
        }
    } else {
        // 对于正常值或者有特定标签的错误值，更新缓存
        neu_driver_cache_update(driver->cache, group, tag, global_timestamp,
                                value, metas, n_meta);
        
        /**
         * @bug
         *  -此处如果 value.type ！= NEU_TYPE_ERROR && tag == NULL，
         *   则neu_driver_cache_update 不会修改任何数量，读取指标应该为0
         */

        // 更新读取总数指标
        update_metric(&driver->adapter, NEU_METRIC_TAG_READS_TOTAL, 1, NULL);

        // 如果值的类型是错误，更新错误总数指标
        update_metric(&driver->adapter, NEU_METRIC_TAG_READ_ERRORS_TOTAL,
                      NEU_TYPE_ERROR == value.type, NULL);
    }
    nlog_debug(
        "update driver: %s, group: %s, tag: %s, type: %s, timestamp: %" PRId64
        " n_meta: %d",
        driver->adapter.name, group, tag, neu_type_string(value.type),
        global_timestamp, n_meta);
}

/**
 * @brief 使用元数据和跟踪上下文更新适配器中的值。
 *
 * 此函数首先使用给定的元数据更新指定组和标签的值，然后如果提供了跟踪上下文，则进一步更新驱动缓存中的跟踪信息。
 *
 * @param adapter 指向适配器对象的指针。
 * @param group 组名称。
 * @param tag 标签名称。
 * @param value 要更新的数据值。
 * @param metas 元数据数组。
 * @param n_meta 元数据数组的大小。
 * @param trace_ctx 跟踪上下文，用于更新驱动缓存中的跟踪信息。
 */
static void update_with_trace(neu_adapter_t *adapter, const char *group,
                              const char *tag, neu_dvalue_t value,
                              neu_tag_meta_t *metas, int n_meta,
                              void *trace_ctx)
{
    // 使用元数据更新指定组和标签的值
    update_with_meta(adapter, group, tag, value, metas, n_meta);

    // 如果提供了跟踪上下文，则进一步更新驱动缓存中的跟踪信息
    if (trace_ctx) {
        // 将适配器转换为驱动适配器类型以访问其缓存
        neu_adapter_driver_t *driver = (neu_adapter_driver_t *) adapter;

        // 更新驱动缓存中的跟踪信息
        neu_driver_cache_update_trace(driver->cache, group, trace_ctx);
    }
}

/**
 * @brief 更新指定适配器的立即更新组中的数据。
 *
 * 此函数用于处理来自特定适配器的数据更新请求，并根据提供的参数更新缓存中的数据。
 * 它还会检查数据类型并记录日志。如果数据有效，则会生成相应的响应并发送给订阅的应用程序。
 *
 * @param adapter 适配器对象指针。
 * @param group 组名称。
 * @param tag 标签名称。
 * @param value 数据值。
 * @param metas 元数据数组。
 * @param n_meta 元数据的数量。
 */
static void update_im(neu_adapter_t *adapter, const char *group,
                      const char *tag, neu_dvalue_t value,
                      neu_tag_meta_t *metas, int n_meta)
{
    neu_adapter_driver_t *driver = (neu_adapter_driver_t *) adapter;

    if (tag == NULL) {
        nlog_warn("update_im tag is null");
        return;
    }

    // 更新驱动缓存中的更改
    neu_driver_cache_update_change(driver->cache, group, tag, global_timestamp,
                                   value, metas, n_meta, true);
    
    // 调用回调函数更新度量
    driver->adapter.cb_funs.update_metric(&driver->adapter,
                                          NEU_METRIC_TAG_READS_TOTAL, 1, NULL);
    
    // 如果数据值类型是错误类型，则直接返回
    if (value.type == NEU_TYPE_ERROR) {
        return;
    }

    // 获取与组和标签关联的所有标签
    UT_array *tags = neu_adapter_driver_get_ptag(driver, group, tag);
    if (tags == NULL) {
        return;
    }

    // 获取第一个标签
    neu_datatag_t *first = utarray_front(tags);

    // 如果没有找到任何标签，则记录调试信息并释放资源
    if (first == NULL) {
        utarray_free(tags);
        nlog_debug("update immediately, driver: %s, "
                   "group: %s, tag: %s, type: %s, "
                   "timestamp: %" PRId64,
                   driver->adapter.name, group, tag,
                   neu_type_string(value.type), global_timestamp);
        return;
    }

    nlog_debug("update and report immediately, driver: %s, "
               "group: %s, tag: %s, type: %s, "
               "timestamp: %" PRId64,
               driver->adapter.name, group, tag, neu_type_string(value.type),
               global_timestamp);

    // 初始化响应头和数据结构
    neu_reqresp_head_t header = {
        .type = NEU_REQRESP_TRANS_DATA,
    };
    neu_reqresp_trans_data_t *data =
        calloc(1, sizeof(neu_reqresp_trans_data_t));

    data->driver = strdup(driver->adapter.name);
    data->group  = strdup(group);
    utarray_new(data->tags, neu_resp_tag_value_meta_icd());

    // 读取并报告组数据
    read_report_group(global_timestamp, 0,
                      neu_adapter_get_tag_cache_type(&driver->adapter),
                      driver->cache, group, tags, data->tags);

    // 如果有有效的标签数据
    if (utarray_len(data->tags) > 0) {
        group_t *find = NULL;
        HASH_FIND_STR(driver->groups, group, find);
        if (find != NULL) {
            pthread_mutex_lock(&find->apps_mtx);

            if (utarray_len(find->apps) > 0) {
                data->ctx = calloc(1, sizeof(neu_reqresp_trans_data_ctx_t));
                data->ctx->index = utarray_len(find->apps);
                pthread_mutex_init(&data->ctx->mtx, NULL);

                // 遍历所有应用程序并发送响应
                utarray_foreach(find->apps, sub_app_t *, app)
                {
                    if (driver->adapter.cb_funs.responseto(
                            &driver->adapter, &header, data, app->addr) != 0) {
                        neu_trans_data_free(data);
                    }
                }
            } else {
                // 清理无效数据
                utarray_foreach(data->tags, neu_resp_tag_value_meta_t *,
                                tag_value)
                {
                    if (tag_value->value.type == NEU_TYPE_PTR) {
                        free(tag_value->value.value.ptr.ptr);
                    }
                }
                utarray_free(data->tags);
                free(data->group);
                free(data->driver);
            }

            pthread_mutex_unlock(&find->apps_mtx);
        } else {
            // 清理无效数据
            utarray_foreach(data->tags, neu_resp_tag_value_meta_t *, tag_value)
            {
                if (tag_value->value.type == NEU_TYPE_PTR) {
                    free(tag_value->value.value.ptr.ptr);
                }
            }
            utarray_free(data->tags);
            free(data->group);
            free(data->driver);
        }
    } else {
        // 清理无效数据
        utarray_free(data->tags);
        free(data->group);
        free(data->driver);
    }

    // 释放标签数组
    utarray_free(tags);

    // 释放数据结构
    free(data);
}

static void update(neu_adapter_t *adapter, const char *group, const char *tag,
                   neu_dvalue_t value)
{
    update_with_meta(adapter, group, tag, value, NULL, 0);
}

/**
 * @brief 处理扫描标签响应。
 *
 * 此函数用于处理从适配器收到的扫描标签的响应。它首先设置请求头部的类型为 `NEU_RESP_SCAN_TAGS`，
 * 然后记录一条通知日志，并调用适配器的回调函数来处理这个响应。
 *
 * @param adapter 指向适配器对象的指针。
 * @param r 指向请求响应头部的指针。
 * @param resp_scan 指向包含扫描标签响应数据的结构体的指针。
 */
static void scan_tags_response(neu_adapter_t *adapter, void *r,
                               neu_resp_scan_tags_t *resp_scan)
{
    neu_reqresp_head_t *req = (neu_reqresp_head_t *) r;  // 将传入的指针转换为请求响应头部类型的指针
    req->type               = NEU_RESP_SCAN_TAGS;        // 设置请求类型为扫描标签响应
    nlog_notice("scan tags response <%p>", req->ctx);
    adapter->cb_funs.response(adapter, req, resp_scan);  // 调用适配器的回调函数来处理这个响应
}

/**
 * @brief 处理测试读取标签响应。
 *
 * 此函数用于处理从适配器收到的测试读取标签的响应。它设置请求头部的类型，
 * 记录一条通知日志，并调用适配器的回调函数来处理这个响应。
 *
 * @param adapter 指向适配器对象的指针。
 * @param r 指向请求响应头部的指针。
 * @param t JSON类型。
 * @param type 数据类型。
 * @param value 值（联合体）。
 * @param error 错误码。
 */
static void test_read_tag_response(neu_adapter_t *adapter, void *r,
                                   neu_json_type_e t, neu_type_e type,
                                   neu_json_value_u value, int64_t error)
{
    neu_reqresp_head_t *     req    = (neu_reqresp_head_t *) r;

    // 构造响应数据结构
    neu_resp_test_read_tag_t dvalue = {
        .t     = t,
        .type  = type,
        .value = value,
        .error = error,
    };

    // 设置请求类型为测试读取标签响应
    req->type = NEU_RESP_TEST_READ_TAG;
    nlog_notice("test reading tag response <%p>", req->ctx);

    // 使用构造好的响应数据调用适配器的回调函数来处理这个响应
    adapter->cb_funs.response(adapter, req, &dvalue);
}

/**
 * @brief 创建一个新的驱动适配器实例。
 *
 * 该函数分配内存并初始化一个新的 `neu_adapter_driver_t` 实例，包括设置缓存、事件处理机制以及回调函数等。
 * 它主要用于创建驱动类型的适配器，这些适配器通常用于与硬件设备进行交互。
 *
 * @return 成功时返回指向新创建的驱动适配器的指针；失败时返回 `NULL`。
 */
neu_adapter_driver_t *neu_adapter_driver_create()
{
    neu_adapter_driver_t *driver = calloc(1, sizeof(neu_adapter_driver_t));

    // 初始化驱动适配器的缓存
    driver->cache                                      = neu_driver_cache_new();

    // 创建新的事件对象，用于驱动适配器的事件处理
    driver->driver_events                              = neu_event_new();

    // 设置驱动适配器的北向回调函数集
    driver->adapter.cb_funs.driver.update              = update;
    driver->adapter.cb_funs.driver.write_response      = write_response;
    driver->adapter.cb_funs.driver.write_responses     = write_responses;
    driver->adapter.cb_funs.driver.directory_response  = directory_response;
    driver->adapter.cb_funs.driver.fup_open_response   = fup_open_response;
    driver->adapter.cb_funs.driver.fdown_open_response = fdown_open_response;
    driver->adapter.cb_funs.driver.fup_data_response   = fup_data_response;
    driver->adapter.cb_funs.driver.update_im           = update_im;
    driver->adapter.cb_funs.driver.update_with_trace   = update_with_trace;
    driver->adapter.cb_funs.driver.update_with_meta    = update_with_meta;
    driver->adapter.cb_funs.driver.scan_tags_response  = scan_tags_response;
    driver->adapter.cb_funs.driver.test_read_tag_response =
        test_read_tag_response;

    return driver;
}

void neu_adapter_driver_destroy(neu_adapter_driver_t *driver)
{
    neu_event_close(driver->driver_events);
    neu_driver_cache_destroy(driver->cache);
}

/**
 * @brief 初始化适配器驱动。
 *
 * 此函数用于初始化给定的适配器驱动对象。当前实现是一个空实现，不执行任何实际操作，
 * 该函数总是返回 0 表示成功。
 *
 * @param driver 表示要初始化的适配器驱动对象。
 * @return 总是返回 0，表示成功。
 */
int neu_adapter_driver_init(neu_adapter_driver_t *driver)
{
    (void) driver;

    return 0;
}

int neu_adapter_driver_uninit(neu_adapter_driver_t *driver)
{
    group_t *el = NULL, *tmp = NULL;

    HASH_ITER(hh, driver->groups, el, tmp)
    {
        HASH_DEL(driver->groups, el);

        neu_adapter_driver_try_del_tag(driver, neu_group_tag_size(el->group));
        stop_group_timer(driver, el);
        if (el->grp.group_free != NULL) {
            el->grp.group_free(&el->grp);
        }
        free(el->grp.group_name);
        if (el->grp.context) {
            free(el->grp.context);
        }
        free(el->name);
        utarray_free(el->grp.tags);

        utarray_foreach(el->wt_tags, to_be_write_tag_t *, tag)
        {
            neu_tag_free(tag->tag);
        }

        utarray_free(el->wt_tags);
        utarray_free(el->apps);
        neu_group_destroy(el->group);
        free(el);
    }

    return 0;
}

/**
 * @brief 启动驱动适配器的组定时器。
 *
 *为指定的驱动适配器（neu_adapter_driver_t 类型）中的特定组（group_t 类型）
 *启动多个定时器，这些定时器分别用于读取数据、报告数据和写入数据的操作。
 *
 * @param adapter 指向 neu_adapter_driver_t 类型的驱动适配器指针，
 *        代表要启动组定时器的驱动适配器。
 *
 * @return neu_err_code_e 启动操作的结果，可能的返回值如下：
 *         - NEU_ERR_SUCCESS: 定时器成功启动。
 *         - 其他错误码: 表示启动过程中出现错误，具体错误码的含义可参考错误码定义。
 *
 * @note 该函数假设传入的 adapter 指针不为 NULL。如果传入 NULL 指针，可能会导致未定义行为。
 *       同时，需要确保定时器相关的资源（如内存、系统调用等）可用。
 */
static inline void start_group_timer(neu_adapter_driver_t *driver, group_t *grp)
{
    // 从组中获取数据采集的时间间隔，单位为毫秒
    uint32_t interval = neu_group_get_interval(grp->group);

    // 初始化一个定时器参数结构体
    neu_event_timer_param_t param = {
        .second      = interval / 1000,
        .millisecond = interval % 1000,
        .usr_data    = (void *) grp,
        .type        = NEU_EVENT_TIMER_NOBLOCK,
    };
    
    // 启动读取数据定时器
    param.type = driver->adapter.module->timer_type;
    param.cb   = read_callback;
    grp->read  = neu_event_add_timer(driver->driver_events, param);

    // 定义一个 20 毫秒的延迟时间：避免多个定时器同时启动产生冲突。
    struct timespec t1 = {
        .tv_sec  = 0,
        .tv_nsec = 1000 * 1000 * 20,
    };
    struct timespec t2 = { 0 };
    // 线程休眠 20 毫秒
    nanosleep(&t1, &t2);

    // 启动报告定时器，并保存写入句柄
    param.type  = NEU_EVENT_TIMER_NOBLOCK;
    param.cb    = report_callback;
    grp->report = neu_adapter_add_timer((neu_adapter_t *) driver, param);

    // 启动写入数据定时器
    param.second      = 0;
    param.millisecond = 3;
    param.cb          = write_callback;
    grp->write        = neu_event_add_timer(driver->driver_events, param);
}

/**
 * @brief 启动驱动适配器的组定时器。
 *
 * 该函数用于启动指定驱动适配器中组的定时器。定时器的作用通常是触发周期性的操作，
 * 例如数据采集、状态更新等。在启动定时器之前，函数可能会进行一些必要的初始化操作，
 * 如设置定时器的间隔时间、回调函数等。
 *
 * @param adapter 指向 neu_adapter_driver_t 类型的驱动适配器指针，
 *        代表要启动组定时器的驱动适配器。
 *
 * @return neu_err_code_e 启动操作的结果，可能的返回值如下：
 *         - NEU_ERR_SUCCESS: 定时器成功启动。
 *         - 其他错误码: 表示启动过程中出现错误，具体错误码的含义可参考错误码定义。
 *
 * @note 该函数假设传入的 adapter 指针不为 NULL。如果传入 NULL 指针，可能会导致未定义行为。
 *       同时，需要确保定时器相关的资源（如内存、系统调用等）可用。
 */
void neu_adapter_driver_start_group_timer(neu_adapter_driver_t *driver)
{
    group_t *el = NULL, *tmp = NULL;

    HASH_ITER(hh, driver->groups, el, tmp)
    {
        start_group_timer(driver, el);
        neu_adapter_update_group_metric(
            &driver->adapter, neu_group_get_name(el->group),
            NEU_METRIC_GROUP_TAGS_TOTAL, neu_group_tag_size(el->group));
    }

    driver->adapter.cb_funs.update_metric(
        &driver->adapter, NEU_METRIC_TAGS_TOTAL, driver->tag_cnt, NULL);
}

static inline void stop_group_timer(neu_adapter_driver_t *driver, group_t *grp)
{
    if (grp->report) {
        neu_adapter_del_timer((neu_adapter_t *) driver, grp->report);
        grp->report = NULL;
    }
    if (grp->read) {
        neu_event_del_timer(driver->driver_events, grp->read);
        grp->read = NULL;
    }
    if (grp->write) {
        neu_event_del_timer(driver->driver_events, grp->write);
        grp->write = NULL;
    }
}

void neu_adapter_driver_stop_group_timer(neu_adapter_driver_t *driver)
{
    group_t *el = NULL, *tmp = NULL;

    HASH_ITER(hh, driver->groups, el, tmp) { stop_group_timer(driver, el); }
}

void neu_adapter_driver_read_group(neu_adapter_driver_t *driver,
                                   neu_reqresp_head_t *  req)
{
    neu_req_read_group_t *cmd = (neu_req_read_group_t *) &req[1];
    group_t *             g   = find_group(driver, cmd->group);
    if (g == NULL) {
        neu_resp_error_t error = { .error = NEU_ERR_GROUP_NOT_EXIST };
        req->type              = NEU_RESP_ERROR;
        neu_req_read_group_fini(cmd);
        driver->adapter.cb_funs.response(&driver->adapter, req, &error);
        return;
    }

    neu_resp_read_group_t resp  = { 0 };
    neu_group_t *         group = g->group;
    UT_array *tags = neu_group_query_read_tag(group, cmd->name, cmd->desc,
                                              cmd->n_tag, cmd->tags);

    utarray_new(resp.tags, neu_resp_tag_value_meta_icd());

    if (driver->adapter.state != NEU_NODE_RUNNING_STATE_RUNNING) {
        utarray_foreach(tags, neu_datatag_t *, tag)
        {
            neu_resp_tag_value_meta_t tag_value = { 0 };
            strcpy(tag_value.tag, tag->name);
            tag_value.value.type      = NEU_TYPE_ERROR;
            tag_value.value.value.i32 = NEU_ERR_PLUGIN_NOT_RUNNING;

            utarray_push_back(resp.tags, &tag_value);
        }
    } else if (cmd->sync) {
        if (NULL == driver->adapter.module->intf_funs->driver.group_sync) {
            // plugin does not support sync read
            utarray_foreach(tags, neu_datatag_t *, tag)
            {
                neu_resp_tag_value_meta_t tag_value = { 0 };
                strcpy(tag_value.tag, tag->name);
                tag_value.value.type = NEU_TYPE_ERROR;
                tag_value.value.value.i32 =
                    NEU_ERR_PLUGIN_NOT_SUPPORT_READ_SYNC;

                utarray_push_back(resp.tags, &tag_value);
            }
        } else {
            // sync read to update cache
            stop_group_timer(driver, g);
            driver->adapter.module->intf_funs->driver.group_sync(
                driver->adapter.plugin, &g->grp);
            // fetch updated data from cache
            read_group(global_timestamp,
                       neu_group_get_interval(group) *
                           NEU_DRIVER_TAG_CACHE_EXPIRE_TIME,
                       neu_adapter_get_tag_cache_type(&driver->adapter),
                       driver->cache, cmd->group, tags, resp.tags);
            start_group_timer(driver, g);
        }
    } else {
        read_group(global_timestamp,
                   neu_group_get_interval(group) *
                       NEU_DRIVER_TAG_CACHE_EXPIRE_TIME,
                   neu_adapter_get_tag_cache_type(&driver->adapter),
                   driver->cache, cmd->group, tags, resp.tags);
    }

    resp.driver = cmd->driver;
    resp.group  = cmd->group;
    cmd->driver = NULL; // ownership moved
    cmd->group  = NULL; // ownership moved

    utarray_free(tags);
    neu_req_read_group_fini(cmd);

    req->type = NEU_RESP_READ_GROUP;
    driver->adapter.cb_funs.response(&driver->adapter, req, &resp);
}

void neu_adapter_driver_read_group_paginate(neu_adapter_driver_t *driver,
                                            neu_reqresp_head_t *  req)
{
    neu_req_read_group_paginate_t *cmd =
        (neu_req_read_group_paginate_t *) &req[1];
    group_t *g = find_group(driver, cmd->group);
    if (g == NULL) {
        neu_resp_error_t error = { .error = NEU_ERR_GROUP_NOT_EXIST };
        req->type              = NEU_RESP_ERROR;
        neu_req_read_group_paginate_fini(cmd);
        driver->adapter.cb_funs.response(&driver->adapter, req, &error);
        return;
    }

    neu_resp_read_group_paginate_t resp  = { 0 };
    neu_group_t *                  group = g->group;
    UT_array *                     tags;

    if (cmd->is_error != true && cmd->current_page > 0 && cmd->page_size > 0) {
        tags = neu_group_query_read_tag_paginate(
            group, cmd->name, cmd->desc, cmd->current_page, cmd->page_size,
            &resp.total_count);
    } else {
        tags = neu_group_query_read_tag(group, cmd->name, cmd->desc, 0, NULL);
        resp.total_count = utarray_len(tags);
    }

    utarray_new(resp.tags, neu_resp_tag_value_meta_paginate_icd());

    if (driver->adapter.state != NEU_NODE_RUNNING_STATE_RUNNING) {
        utarray_foreach(tags, neu_datatag_t *, tag)
        {
            neu_resp_tag_value_meta_paginate_t tag_value = { 0 };
            strcpy(tag_value.tag, tag->name);
            tag_value.value.type      = NEU_TYPE_ERROR;
            tag_value.value.value.i32 = NEU_ERR_PLUGIN_NOT_RUNNING;

            tag_value.datatag.name        = strdup(tag->name);
            tag_value.datatag.address     = strdup(tag->address);
            tag_value.datatag.attribute   = tag->attribute;
            tag_value.datatag.type        = tag->type;
            tag_value.datatag.precision   = tag->precision;
            tag_value.datatag.decimal     = tag->decimal;
            tag_value.datatag.bias        = tag->bias;
            tag_value.datatag.description = strdup(tag->description);
            tag_value.datatag.option      = tag->option;
            memcpy(tag_value.datatag.meta, tag->meta, NEU_TAG_META_LENGTH);

            utarray_push_back(resp.tags, &tag_value);
        }
    } else if (cmd->sync) {
        if (NULL == driver->adapter.module->intf_funs->driver.group_sync) {
            // plugin does not support sync read
            utarray_foreach(tags, neu_datatag_t *, tag)
            {
                neu_resp_tag_value_meta_paginate_t tag_value = { 0 };
                strcpy(tag_value.tag, tag->name);
                tag_value.value.type = NEU_TYPE_ERROR;
                tag_value.value.value.i32 =
                    NEU_ERR_PLUGIN_NOT_SUPPORT_READ_SYNC;

                tag_value.datatag.name        = strdup(tag->name);
                tag_value.datatag.address     = strdup(tag->address);
                tag_value.datatag.attribute   = tag->attribute;
                tag_value.datatag.type        = tag->type;
                tag_value.datatag.precision   = tag->precision;
                tag_value.datatag.decimal     = tag->decimal;
                tag_value.datatag.bias        = tag->bias;
                tag_value.datatag.description = strdup(tag->description);
                tag_value.datatag.option      = tag->option;
                memcpy(tag_value.datatag.meta, tag->meta, NEU_TAG_META_LENGTH);

                utarray_push_back(resp.tags, &tag_value);
            }
        } else {
            // sync read to update cache
            stop_group_timer(driver, g);
            driver->adapter.module->intf_funs->driver.group_sync(
                driver->adapter.plugin, &g->grp);
            // fetch updated data from cache
            read_group_paginate(
                global_timestamp,
                neu_group_get_interval(group) *
                    NEU_DRIVER_TAG_CACHE_EXPIRE_TIME,
                neu_adapter_get_tag_cache_type(&driver->adapter), driver->cache,
                cmd->group, tags, resp.tags);
            start_group_timer(driver, g);
        }
    } else {
        read_group_paginate(global_timestamp,
                            neu_group_get_interval(group) *
                                NEU_DRIVER_TAG_CACHE_EXPIRE_TIME,
                            neu_adapter_get_tag_cache_type(&driver->adapter),
                            driver->cache, cmd->group, tags, resp.tags);
    }

    resp.driver       = cmd->driver;
    resp.group        = cmd->group;
    resp.current_page = cmd->current_page;
    resp.page_size    = cmd->page_size;
    resp.is_error     = cmd->is_error;
    cmd->driver       = NULL; // ownership moved
    cmd->group        = NULL; // ownership moved

    utarray_free(tags);
    neu_req_read_group_paginate_fini(cmd);

    req->type = NEU_RESP_READ_GROUP_PAGINATE;
    driver->adapter.cb_funs.response(&driver->adapter, req, &resp);
}

static void fix_value(neu_datatag_t *tag, neu_type_e value_type,
                      neu_dvalue_t *value)
{
    switch (tag->type) {
    case NEU_TYPE_BOOL:
    case NEU_TYPE_STRING:
    case NEU_TYPE_TIME:
    case NEU_TYPE_DATA_AND_TIME:
    case NEU_TYPE_ARRAY_INT64:
    case NEU_TYPE_ARRAY_CHAR:
    case NEU_TYPE_ARRAY_BOOL:
    case NEU_TYPE_ARRAY_STRING:
    case NEU_TYPE_CUSTOM:
        break;
    case NEU_TYPE_BIT:
        value->type     = NEU_TYPE_BIT;
        value->value.u8 = (uint8_t) value->value.u64;
        break;
    case NEU_TYPE_UINT8:
    case NEU_TYPE_INT8:
        value->type     = NEU_TYPE_UINT8;
        value->value.u8 = (uint8_t) value->value.u64;
        break;
    case NEU_TYPE_INT16:
    case NEU_TYPE_UINT16:
    case NEU_TYPE_WORD:
        value->type      = NEU_TYPE_UINT16;
        value->value.u16 = (uint16_t) value->value.u64;
        if (tag->option.value16.endian == NEU_DATATAG_ENDIAN_B16) {
            value->value.u16 = htons(value->value.u16);
        }
        break;
    case NEU_TYPE_UINT32:
    case NEU_TYPE_INT32:
    case NEU_TYPE_DWORD:
        value->type      = NEU_TYPE_UINT32;
        value->value.u32 = (uint32_t) value->value.u64;
        switch (tag->option.value32.endian) {
        case NEU_DATATAG_ENDIAN_LB32:
            neu_ntohs_p((uint16_t *) value->value.bytes.bytes);
            neu_ntohs_p((uint16_t *) (value->value.bytes.bytes + 2));
            break;
        case NEU_DATATAG_ENDIAN_BB32:
            value->value.u32 = htonl(value->value.u32);
            break;
        case NEU_DATATAG_ENDIAN_BL32:
            value->value.u32 = htonl(value->value.u32);
            neu_ntohs_p((uint16_t *) value->value.bytes.bytes);
            neu_ntohs_p((uint16_t *) (value->value.bytes.bytes + 2));
            break;
        case NEU_DATATAG_ENDIAN_LL32:
        default:
            break;
        }
        break;
    case NEU_TYPE_FLOAT:
        if (value_type != NEU_TYPE_FLOAT) {
            value->type      = NEU_TYPE_FLOAT;
            value->value.f32 = (float) value->value.d64;
        }

        switch (tag->option.value32.endian) {
        case NEU_DATATAG_ENDIAN_LB32:
            neu_ntohs_p((uint16_t *) value->value.bytes.bytes);
            neu_ntohs_p((uint16_t *) (value->value.bytes.bytes + 2));
            break;
        case NEU_DATATAG_ENDIAN_BB32:
            value->value.u32 = htonl(value->value.u32);
            break;
        case NEU_DATATAG_ENDIAN_BL32:
            value->value.u32 = htonl(value->value.u32);
            neu_ntohs_p((uint16_t *) value->value.bytes.bytes);
            neu_ntohs_p((uint16_t *) (value->value.bytes.bytes + 2));
            break;
        case NEU_DATATAG_ENDIAN_LL32:
        default:
            break;
        }

        break;
    case NEU_TYPE_DOUBLE:
    case NEU_TYPE_UINT64:
    case NEU_TYPE_INT64:
    case NEU_TYPE_LWORD:
        switch (tag->option.value64.endian) {
        case NEU_DATATAG_ENDIAN_B64:
            value->value.u64 = neu_ntohll(value->value.u64);
            break;
        case NEU_DATATAG_ENDIAN_L64:
        default:
            break;
        }
        break;
    case NEU_TYPE_BYTES:
        if (value->type == NEU_TYPE_ARRAY_INT64) {
            for (int i = 0; i < value->value.i64s.length; i++) {
                value->value.bytes.bytes[i] =
                    (uint8_t) value->value.i64s.i64s[i];
            }
            for (int i = value->value.i64s.length; i < NEU_VALUE_SIZE; i++) {
                value->value.bytes.bytes[i] = 0;
            }
            value->value.bytes.length = value->value.i64s.length;
        }
        break;
    case NEU_TYPE_ARRAY_INT8:
        if (value->type == NEU_TYPE_ARRAY_INT64) {
            for (int i = 0; i < value->value.i64s.length; i++) {
                value->value.i8s.i8s[i] = (int8_t) value->value.i64s.i64s[i];
            }
            for (int i = value->value.i64s.length; i < NEU_VALUE_SIZE; i++) {
                value->value.i8s.i8s[i] = 0;
            }
            value->value.i8s.length = value->value.i64s.length;
        }
        break;
    case NEU_TYPE_ARRAY_UINT8:
        if (value->type == NEU_TYPE_ARRAY_INT64) {
            for (int i = 0; i < value->value.i64s.length; i++) {
                value->value.u8s.u8s[i] = (uint8_t) value->value.i64s.i64s[i];
            }
            for (int i = value->value.i64s.length; i < NEU_VALUE_SIZE; i++) {
                value->value.u8s.u8s[i] = 0;
            }
            value->value.u8s.length = value->value.i64s.length;
        }
        break;
    case NEU_TYPE_ARRAY_INT16:
        if (value->type == NEU_TYPE_ARRAY_INT64) {
            for (int i = 0; i < value->value.i64s.length; i++) {
                value->value.i16s.i16s[i] = (int16_t) value->value.i64s.i64s[i];
            }
            for (int i = value->value.i64s.length; i < NEU_VALUE_SIZE; i++) {
                value->value.i16s.i16s[i] = 0;
            }
            value->value.i16s.length = value->value.i64s.length;
        }
        break;
    case NEU_TYPE_ARRAY_UINT16:
        if (value->type == NEU_TYPE_ARRAY_INT64) {
            for (int i = 0; i < value->value.i64s.length; i++) {
                value->value.u16s.u16s[i] =
                    (uint16_t) value->value.i64s.i64s[i];
            }
            for (int i = value->value.i64s.length; i < NEU_VALUE_SIZE; i++) {
                value->value.u16s.u16s[i] = 0;
            }
            value->value.u16s.length = value->value.i64s.length;
        }
        break;
    case NEU_TYPE_ARRAY_INT32:
        if (value->type == NEU_TYPE_ARRAY_INT64) {
            for (int i = 0; i < value->value.i64s.length; i++) {
                value->value.i32s.i32s[i] = (int32_t) value->value.i64s.i64s[i];
            }
            for (int i = value->value.i64s.length; i < NEU_VALUE_SIZE; i++) {
                value->value.i32s.i32s[i] = 0;
            }
            value->value.i32s.length = value->value.i64s.length;
        }
        break;
    case NEU_TYPE_ARRAY_UINT32:
        if (value->type == NEU_TYPE_ARRAY_INT64) {
            for (int i = 0; i < value->value.i64s.length; i++) {
                value->value.u32s.u32s[i] =
                    (uint32_t) value->value.i64s.i64s[i];
            }
            for (int i = value->value.i64s.length; i < NEU_VALUE_SIZE; i++) {
                value->value.u32s.u32s[i] = 0;
            }
            value->value.u32s.length = value->value.i64s.length;
        }
        break;
    case NEU_TYPE_ARRAY_UINT64:
        if (value->type == NEU_TYPE_ARRAY_INT64) {
            for (int i = 0; i < value->value.i64s.length; i++) {
                value->value.u64s.u64s[i] =
                    (uint64_t) value->value.i64s.i64s[i];
            }
            for (int i = value->value.i64s.length; i < NEU_VALUE_SIZE; i++) {
                value->value.u64s.u64s[i] = 0;
            }
            value->value.u64s.length = value->value.i64s.length;
        }
        break;
    case NEU_TYPE_ARRAY_FLOAT:
        if (value->type == NEU_TYPE_ARRAY_INT64) {

            for (int i = 0; i < value->value.i64s.length; i++) {
                value->value.f32s.f32s[i] = (float) value->value.i64s.i64s[i];
            }
            for (int i = value->value.i64s.length; i < NEU_VALUE_SIZE; i++) {
                value->value.f32s.f32s[i] = 0;
            }
            value->value.f32s.length = value->value.i64s.length;
        }
        if (value->type == NEU_TYPE_ARRAY_DOUBLE) {

            for (int i = 0; i < value->value.f64s.length; i++) {
                value->value.f32s.f32s[i] = (float) value->value.f64s.f64s[i];
            }
            for (int i = value->value.f64s.length; i < NEU_VALUE_SIZE; i++) {
                value->value.f32s.f32s[i] = 0;
            }
            value->value.f32s.length = value->value.f64s.length;
        }
        break;
    case NEU_TYPE_ARRAY_DOUBLE:
        if (value->type == NEU_TYPE_ARRAY_INT64) {

            for (int i = 0; i < value->value.i64s.length; i++) {
                value->value.f64s.f64s[i] = (double) value->value.i64s.i64s[i];
            }
            for (int i = value->value.i64s.length; i < NEU_VALUE_SIZE; i++) {
                value->value.f64s.f64s[i] = 0;
            }
            value->value.f64s.length = value->value.i64s.length;
        }
        break;
    default:
        assert(false);
        break;
    }
}

static void cal_decimal(neu_type_e tag_type, neu_type_e value_type,
                        neu_value_u *value, double decimal)
{
    switch (tag_type) {
    case NEU_TYPE_INT8:
        if (value_type == NEU_TYPE_DOUBLE) {
            value->i8 = (int8_t) round(value->d64 / decimal);
        } else {
            value->i8 = (int8_t) round(value->i64 / decimal);
        }
        break;
    case NEU_TYPE_INT16:
        if (value_type == NEU_TYPE_DOUBLE) {
            value->i16 = (int16_t) round(value->d64 / decimal);
        } else {
            value->i16 = (int16_t) round(value->i64 / decimal);
        }
        break;
    case NEU_TYPE_INT32:
        if (value_type == NEU_TYPE_DOUBLE) {
            value->i32 = (int32_t) round(value->d64 / decimal);
        } else {
            value->i32 = (int32_t) round(value->i64 / decimal);
        }
        break;
    case NEU_TYPE_INT64:
        if (value_type == NEU_TYPE_DOUBLE) {
            value->i64 = (int64_t) round(value->d64 / decimal);
        } else {
            value->i64 = (int64_t) round(value->i64 / decimal);
        }
        break;
    case NEU_TYPE_UINT8:
        if (value_type == NEU_TYPE_DOUBLE) {
            value->u8 = (uint8_t) round(value->d64 / decimal);
        } else {
            value->u8 = (uint8_t) round(value->u64 / decimal);
        }
        break;
    case NEU_TYPE_UINT16:
    case NEU_TYPE_WORD:
        if (value_type == NEU_TYPE_DOUBLE) {
            value->u16 = (uint16_t) round(value->d64 / decimal);
        } else {
            value->u16 = (uint16_t) round(value->u64 / decimal);
        }
        break;
    case NEU_TYPE_UINT32:
    case NEU_TYPE_LWORD:
        if (value_type == NEU_TYPE_DOUBLE) {
            value->u32 = (uint32_t) round(value->d64 / decimal);
        } else {
            value->u32 = (uint32_t) round(value->u64 / decimal);
        }
        break;
    case NEU_TYPE_UINT64:
    case NEU_TYPE_DWORD:
        if (value_type == NEU_TYPE_DOUBLE) {
            value->u64 = (uint64_t) round(value->d64 / decimal);
        } else {
            value->u64 = (uint64_t) round(value->u64 / decimal);
        }
        break;
    case NEU_TYPE_FLOAT:
    case NEU_TYPE_DOUBLE:
        if (value_type == NEU_TYPE_INT64) {
            value->d64 = (double) (value->d64 / decimal);
        } else if (value_type == NEU_TYPE_DOUBLE) {
            value->d64 = value->d64 / decimal;
        } else if (value_type == NEU_TYPE_FLOAT) {
            value->f32 = value->f32 / decimal;
        }
        break;
    default:
        break;
    }
}

int almost_equal(double a, double b, double epsilon)
{
    return fabs(a - b) < epsilon;
}

int check_value_decimal(neu_type_e write_type, double result, int64_t value_min,
                        int64_t value_max)
{
    if (write_type != NEU_TYPE_INT64 && write_type != NEU_TYPE_DOUBLE) {
        return NEU_ERR_PLUGIN_TAG_TYPE_MISMATCH;
    } else if (result < value_min || result > value_max) {
        return NEU_ERR_PLUGIN_TAG_VALUE_OUT_OF_RANGE;
    } else if (almost_equal(ceil(result), result, EPSILON) ||
               almost_equal(floor(result), result, EPSILON)) {
        return NEU_ERR_SUCCESS;
    } else {
        return NEU_ERR_PLUGIN_TAG_TYPE_MISMATCH;
    }
}

int check_value(neu_type_e write_type, int64_t value, int64_t value_min,
                int64_t value_max)
{
    if (write_type != NEU_TYPE_INT64) {
        return NEU_ERR_PLUGIN_TAG_TYPE_MISMATCH;
    } else if (value >= value_min && value <= value_max) {
        return NEU_ERR_SUCCESS;
    } else {
        return NEU_ERR_PLUGIN_TAG_VALUE_OUT_OF_RANGE;
    }
}

int is_value_in_range(neu_type_e tag_type, int64_t value, double value_d,
                      neu_type_e write_type, double decimal)
{
    if (decimal != 0) {
        double result;

        if (write_type == NEU_TYPE_INT64) {
            result = value / decimal;
        } else {
            result = value_d / decimal;
        }

        switch (tag_type) {
        case NEU_TYPE_INT8:
            return check_value_decimal(write_type, result, INT8_MIN, INT8_MAX);
        case NEU_TYPE_UINT8:
            return check_value_decimal(write_type, result, 0, UINT8_MAX);
        case NEU_TYPE_INT16:
            return check_value_decimal(write_type, result, INT16_MIN,
                                       INT16_MAX);
        case NEU_TYPE_UINT16:
            return check_value_decimal(write_type, result, 0, UINT16_MAX);
        case NEU_TYPE_INT32:
            return check_value_decimal(write_type, result, INT32_MIN,
                                       INT32_MAX);
        case NEU_TYPE_UINT32:
            return check_value_decimal(write_type, result, 0, UINT32_MAX);
        case NEU_TYPE_INT64:
        case NEU_TYPE_UINT64:
            if (write_type != NEU_TYPE_INT64 && write_type != NEU_TYPE_DOUBLE) {
                return NEU_ERR_PLUGIN_TAG_TYPE_MISMATCH;
            } else if (almost_equal(ceil(result), result, EPSILON) ||
                       almost_equal(floor(result), result, EPSILON)) {
                return NEU_ERR_SUCCESS;
            } else {
                return NEU_ERR_PLUGIN_TAG_TYPE_MISMATCH;
            }
        case NEU_TYPE_FLOAT:
            if (write_type != NEU_TYPE_DOUBLE && write_type != NEU_TYPE_INT64) {
                return NEU_ERR_PLUGIN_TAG_TYPE_MISMATCH;
            }
            if (result < (int64_t) -FLT_MAX || result > (int64_t) FLT_MAX) {
                return NEU_ERR_PLUGIN_TAG_VALUE_OUT_OF_RANGE;
            } else {
                return NEU_ERR_SUCCESS;
            }
        case NEU_TYPE_DOUBLE:
            if (write_type != NEU_TYPE_DOUBLE && write_type != NEU_TYPE_INT64) {
                return NEU_ERR_PLUGIN_TAG_TYPE_MISMATCH;
            }
            return NEU_ERR_SUCCESS;
        case NEU_TYPE_STRING:
        case NEU_TYPE_BOOL:
        case NEU_TYPE_BIT:
            return NEU_ERR_TAG_DECIMAL_INVALID;
        default:
            return NEU_ERR_SUCCESS;
        }
    } else {
        switch (tag_type) {
        case NEU_TYPE_BOOL:
            if (write_type == NEU_TYPE_BOOL) {
                return NEU_ERR_SUCCESS;
            } else {
                return NEU_ERR_PLUGIN_TAG_TYPE_MISMATCH;
            }
        case NEU_TYPE_BIT:
            if (write_type != NEU_TYPE_INT64) {
                return NEU_ERR_PLUGIN_TAG_TYPE_MISMATCH;
            } else if (value != 1 && value != 0) {
                return NEU_ERR_PLUGIN_TAG_VALUE_OUT_OF_RANGE;
            } else {
                return NEU_ERR_SUCCESS;
            }
        case NEU_TYPE_INT8:
            return check_value(write_type, value, INT8_MIN, INT8_MAX);
        case NEU_TYPE_UINT8:
            return check_value(write_type, value, 0, UINT8_MAX);
        case NEU_TYPE_INT16:
            return check_value(write_type, value, INT16_MIN, INT16_MAX);
        case NEU_TYPE_UINT16:
            return check_value(write_type, value, 0, UINT16_MAX);
        case NEU_TYPE_INT32:
            return check_value(write_type, value, INT32_MIN, INT32_MAX);
        case NEU_TYPE_UINT32:
            return check_value(write_type, value, 0, UINT32_MAX);
        case NEU_TYPE_INT64:
        case NEU_TYPE_UINT64:
            if (write_type == NEU_TYPE_INT64) {
                return NEU_ERR_SUCCESS;
            } else {
                return NEU_ERR_PLUGIN_TAG_TYPE_MISMATCH;
            }
        case NEU_TYPE_FLOAT:
            if (write_type != NEU_TYPE_DOUBLE && write_type != NEU_TYPE_INT64) {
                return NEU_ERR_PLUGIN_TAG_TYPE_MISMATCH;
            }
            if (value < (int64_t) -FLT_MAX || value > (int64_t) FLT_MAX) {
                return NEU_ERR_PLUGIN_TAG_VALUE_OUT_OF_RANGE;
            } else {
                return NEU_ERR_SUCCESS;
            }
        case NEU_TYPE_DOUBLE:
            if (write_type != NEU_TYPE_DOUBLE && write_type != NEU_TYPE_INT64) {
                return NEU_ERR_PLUGIN_TAG_TYPE_MISMATCH;
            }
            return NEU_ERR_SUCCESS;
        case NEU_TYPE_STRING:
            if (write_type != NEU_TYPE_STRING) {
                return NEU_ERR_PLUGIN_TAG_TYPE_MISMATCH;
            } else {
                return NEU_ERR_SUCCESS;
            }
        case NEU_TYPE_CUSTOM:
            if (write_type != NEU_TYPE_CUSTOM) {
                return NEU_ERR_PLUGIN_TAG_TYPE_MISMATCH;
            } else {
                return NEU_ERR_SUCCESS;
            }
        default:
            return NEU_ERR_SUCCESS;
        }
    }
}

/**
 * @brief 处理向驱动适配器写入多个标签值的请求。
 *
 * 该函数接收一个驱动适配器指针和一个请求头指针，用于处理写入多个标签值的请求。
 * 它会检查适配器的状态、插件是否支持写入标签值、组是否存在以及标签值是否合法，
 * 若所有检查都通过，则将待写入的标签信息存储起来。
 *
 * @param driver 指向驱动适配器的指针，包含适配器的状态、模块信息等。
 * @param req 指向请求头的指针，包含请求的基本信息。
 *
 * @return int 返回操作结果的错误码。
 *         - NEU_ERR_SUCCESS: 操作成功。
 *         - NEU_ERR_PLUGIN_NOT_RUNNING: 适配器未处于运行状态。
 *         - NEU_ERR_PLUGIN_NOT_SUPPORT_WRITE_TAGS: 插件不支持写入标签值。
 *         - NEU_ERR_GROUP_NOT_EXIST: 请求的组不存在。
 *         - 其他错误码: 表示标签值检查失败等情况。
 */
int neu_adapter_driver_write_tags(neu_adapter_driver_t *driver,
                                  neu_reqresp_head_t *  req)
{   
    // 从请求头之后的位置获取写入标签的请求结构体指针
    neu_req_write_tags_t *cmd = (neu_req_write_tags_t *) &req[1];

    // 检查适配器的状态是否为运行状态
    if (driver->adapter.state != NEU_NODE_RUNNING_STATE_RUNNING) {
        driver->adapter.cb_funs.driver.write_response(
            &driver->adapter, req, NEU_ERR_PLUGIN_NOT_RUNNING);
        return NEU_ERR_PLUGIN_NOT_RUNNING;
    }

    // 检查插件的接口函数中是否支持写入多个标签值的操作
    if (driver->adapter.module->intf_funs->driver.write_tags == NULL) {
        driver->adapter.cb_funs.driver.write_response(
            &driver->adapter, req, NEU_ERR_PLUGIN_NOT_SUPPORT_WRITE_TAGS);
        return NEU_ERR_PLUGIN_NOT_SUPPORT_WRITE_TAGS;
    }

    // 在驱动适配器中查找请求的组
    group_t *g = find_group(driver, cmd->group);
    if (g == NULL) {
        driver->adapter.cb_funs.driver.write_response(&driver->adapter, req,
                                                      NEU_ERR_GROUP_NOT_EXIST);
        return NEU_ERR_GROUP_NOT_EXIST;
    }

    // 定义一个 UT_array 用于存储待写入的标签值信息
    UT_array *tags = NULL;
    UT_icd    icd  = { sizeof(neu_plugin_tag_value_t), NULL, NULL, NULL };
    utarray_new(tags, &icd);

    // 初始化值检查的错误码为成功
    int value_err = NEU_ERR_SUCCESS;
    // 用于存储每次值检查的结果
    int value_check;

    // 遍历请求中的每个标签
    for (int i = 0; i < cmd->n_tag; i++) {
        neu_datatag_t *tag = neu_group_find_tag(g->group, cmd->tags[i].tag);

        // 若标签存在，检查值是否在合法范围内
        if (tag != NULL) {
            value_check =
                is_value_in_range(tag->type, cmd->tags[i].value.value.i64,
                                  cmd->tags[i].value.value.d64,
                                  cmd->tags[i].value.type, tag->decimal);
        } else {
            value_check = NEU_ERR_TAG_NOT_EXIST;
        }

         // 若标签存在、标签可写且值检查通过
        if (tag != NULL && neu_tag_attribute_test(tag, NEU_ATTRIBUTE_WRITE) &&
            value_check == NEU_ERR_SUCCESS) {
            if (tag->type == NEU_TYPE_FLOAT || tag->type == NEU_TYPE_DOUBLE) {
                if (cmd->tags[i].value.type == NEU_TYPE_INT64) {
                    cmd->tags[i].value.value.d64 =
                        (double) cmd->tags[i].value.value.i64;
                }
            }

            // 若标签有小数位数要求，进行小数位数处理
            if (tag->decimal != 0) {
                cal_decimal(tag->type, cmd->tags[i].value.type,
                            &cmd->tags[i].value.value, tag->decimal);
            }
            // 修正标签值
            fix_value(tag, cmd->tags[i].value.type, &cmd->tags[i].value);

            // 创建一个新的标签值结构体
            neu_plugin_tag_value_t tv = {
                .tag   = neu_tag_dup(tag),
                .value = cmd->tags[i].value.value,
            };
            utarray_push_back(tags, &tv);
        } else {
            value_err = value_check;
        }
        if (tag != NULL) {
            neu_tag_free(tag);
        }
    }

    // 若值检查存在错误，调用回调函数通知写入响应，释放 UT_array 资源并返回错误码
    if (value_err != NEU_ERR_SUCCESS) {
        driver->adapter.cb_funs.driver.write_response(&driver->adapter, req,
                                                      value_err);
        utarray_foreach(tags, neu_plugin_tag_value_t *, tv)
        {
            neu_tag_free(tv->tag);
        }
        utarray_free(tags);
        return value_err;
    }

    // 若 UT_array 中存储的标签数量与请求的标签数量不一致，说明有标签不存在
    if (utarray_len(tags) != (unsigned int) cmd->n_tag) {
        driver->adapter.cb_funs.driver.write_response(&driver->adapter, req,
                                                      NEU_ERR_TAG_NOT_EXIST);
        utarray_foreach(tags, neu_plugin_tag_value_t *, tv)
        {
            neu_tag_free(tv->tag);
        }
        utarray_free(tags);
        return NEU_ERR_TAG_NOT_EXIST;
    }

    // 定义一个待写入标签的结构体
    to_be_write_tag_t wtag = { 0 };
    wtag.single            = false;
    wtag.req               = (void *) req;
    wtag.tvs               = tags;

    // 将待写入的标签信息存储到组中
    store_write_tag(g, &wtag);

    return NEU_ERR_SUCCESS;
}

int neu_adapter_driver_write_gtags(neu_adapter_driver_t *driver,
                                   neu_reqresp_head_t *  req)
{
    neu_req_write_gtags_t *cmd     = (neu_req_write_gtags_t *) &req[1];
    group_t *              first_g = NULL;

    if (driver->adapter.state != NEU_NODE_RUNNING_STATE_RUNNING) {
        driver->adapter.cb_funs.driver.write_response(
            &driver->adapter, req, NEU_ERR_PLUGIN_NOT_RUNNING);
        return NEU_ERR_PLUGIN_NOT_RUNNING;
    }

    if (driver->adapter.module->intf_funs->driver.write_tags == NULL) {
        driver->adapter.cb_funs.driver.write_response(
            &driver->adapter, req, NEU_ERR_PLUGIN_NOT_SUPPORT_WRITE_TAGS);
        return NEU_ERR_PLUGIN_NOT_SUPPORT_WRITE_TAGS;
    }

    for (int i = 0; i < cmd->n_group; i++) {
        group_t *g = find_group(driver, cmd->groups[i].group);

        if (g == NULL) {
            driver->adapter.cb_funs.driver.write_response(
                &driver->adapter, req, NEU_ERR_GROUP_NOT_EXIST);
            return NEU_ERR_GROUP_NOT_EXIST;
        }

        if (first_g == NULL) {
            first_g = g;
        }
    }

    UT_array *tags = NULL;
    UT_icd    icd  = { sizeof(neu_plugin_tag_value_t), NULL, NULL, NULL };
    utarray_new(tags, &icd);
    int value_err = NEU_ERR_SUCCESS;
    int value_check;

    for (int i = 0; i < cmd->n_group; i++) {
        group_t *g = find_group(driver, cmd->groups[i].group);

        for (int k = 0; k < cmd->groups[i].n_tag; k++) {
            neu_datatag_t *tag =
                neu_group_find_tag(g->group, cmd->groups[i].tags[k].tag);

            if (tag != NULL) {
                value_check = is_value_in_range(
                    tag->type, cmd->groups[i].tags[k].value.value.i64,
                    cmd->groups[i].tags[k].value.value.d64,
                    cmd->groups[i].tags[k].value.type, tag->decimal);
            } else {
                value_check = NEU_ERR_TAG_NOT_EXIST;
            }

            if (tag != NULL &&
                neu_tag_attribute_test(tag, NEU_ATTRIBUTE_WRITE) &&
                value_check == NEU_ERR_SUCCESS) {
                if (tag->type == NEU_TYPE_FLOAT ||
                    tag->type == NEU_TYPE_DOUBLE) {
                    if (cmd->groups[i].tags[k].value.type == NEU_TYPE_INT64) {
                        cmd->groups[i].tags[k].value.value.d64 =
                            (double) cmd->groups[i].tags[k].value.value.i64;
                    }
                }

                if (tag->decimal != 0) {
                    cal_decimal(tag->type, cmd->groups[i].tags[k].value.type,
                                &cmd->groups[i].tags[k].value.value,
                                tag->decimal);
                }
                fix_value(tag, cmd->groups[i].tags[k].value.type,
                          &cmd->groups[i].tags[k].value);

                neu_plugin_tag_value_t tv = {
                    .tag   = neu_tag_dup(tag),
                    .value = cmd->groups[i].tags[k].value.value,
                };
                utarray_push_back(tags, &tv);
            } else {
                value_err = value_check;
            }
            if (tag != NULL) {
                neu_tag_free(tag);
            }
        }
    }

    if (value_err != NEU_ERR_SUCCESS) {
        driver->adapter.cb_funs.driver.write_response(&driver->adapter, req,
                                                      value_err);
        utarray_foreach(tags, neu_plugin_tag_value_t *, tv)
        {
            neu_tag_free(tv->tag);
        }
        utarray_free(tags);
        return value_err;
    }

    uint32_t n_tag = 0;
    for (int i = 0; i < cmd->n_group; i++) {
        n_tag += cmd->groups[i].n_tag;
    }

    if (utarray_len(tags) != (unsigned int) n_tag) {
        driver->adapter.cb_funs.driver.write_response(&driver->adapter, req,
                                                      NEU_ERR_TAG_NOT_EXIST);
        utarray_foreach(tags, neu_plugin_tag_value_t *, tv)
        {
            neu_tag_free(tv->tag);
        }
        utarray_free(tags);
        return NEU_ERR_TAG_NOT_EXIST;
    }

    to_be_write_tag_t wtag = { 0 };
    wtag.single            = false;
    wtag.req               = (void *) req;
    wtag.tvs               = tags;

    store_write_tag(first_g, &wtag);

    return NEU_ERR_SUCCESS;
}

int neu_adapter_driver_write_tag(neu_adapter_driver_t *driver,
                                 neu_reqresp_head_t *  req)
{
    if (driver->adapter.state != NEU_NODE_RUNNING_STATE_RUNNING) {
        driver->adapter.cb_funs.driver.write_response(
            &driver->adapter, req, NEU_ERR_PLUGIN_NOT_RUNNING);
        return NEU_ERR_PLUGIN_NOT_RUNNING;
    }

    neu_req_write_tag_t *cmd = (neu_req_write_tag_t *) &req[1];
    group_t *            g   = find_group(driver, cmd->group);

    if (g == NULL) {
        driver->adapter.cb_funs.driver.write_response(&driver->adapter, req,
                                                      NEU_ERR_GROUP_NOT_EXIST);
        return NEU_ERR_GROUP_NOT_EXIST;
    }
    neu_datatag_t *tag = neu_group_find_tag(g->group, cmd->tag);

    if (tag == NULL) {
        driver->adapter.cb_funs.driver.write_response(&driver->adapter, req,
                                                      NEU_ERR_TAG_NOT_EXIST);
        return NEU_ERR_TAG_NOT_EXIST;
    } else {
        if ((tag->attribute & NEU_ATTRIBUTE_WRITE) != NEU_ATTRIBUTE_WRITE) {
            driver->adapter.cb_funs.driver.write_response(
                &driver->adapter, req, NEU_ERR_PLUGIN_TAG_NOT_ALLOW_WRITE);
            neu_tag_free(tag);
            return NEU_ERR_PLUGIN_TAG_NOT_ALLOW_WRITE;
        }

        int value_check = is_value_in_range(tag->type, cmd->value.value.i64,
                                            cmd->value.value.d64,
                                            cmd->value.type, tag->decimal);

        if (value_check != NEU_ERR_SUCCESS) {
            driver->adapter.cb_funs.driver.write_response(&driver->adapter, req,
                                                          value_check);
            neu_tag_free(tag);
            return value_check;
        }

        if (tag->type == NEU_TYPE_FLOAT || tag->type == NEU_TYPE_DOUBLE) {
            if (cmd->value.type == NEU_TYPE_INT64) {
                cmd->value.value.d64 = (double) cmd->value.value.i64;
            }
        }

        if (tag->decimal != 0) {
            cal_decimal(tag->type, cmd->value.type, &cmd->value.value,
                        tag->decimal);
        }

        fix_value(tag, cmd->value.type, &cmd->value);

        to_be_write_tag_t wtag = { 0 };
        wtag.single            = true;
        wtag.req               = (void *) req;
        wtag.value             = cmd->value.value;
        wtag.tag               = neu_tag_dup(tag);

        store_write_tag(g, &wtag);

        neu_tag_free(tag);
        return NEU_ERR_SUCCESS;
    }
}

#define REGISTER_GROUP_METRIC(adapter, group, name, init)                  \
    neu_adapter_register_group_metric((adapter), group, name, name##_HELP, \
                                      name##_TYPE, (init))

/**
 * @brief 向适配器驱动中添加一个新的组。
 *
 * 该函数会检查指定名称的组是否已经存在于适配器驱动中。如果组不存在，
 * 则会创建一个新的组，初始化组的相关信息（如标签数组、订阅应用数组、互斥锁等），
 * 启动组的定时器（如果适配器驱动正在运行），注册组的各项指标，并将新组添加到适配器驱动的组哈希表中。
 *
 * @param driver 指向适配器驱动实例的指针，用于操作适配器驱动相关的数据和功能。
 * @param name 指向组名称的字符串指针，用于唯一标识要添加的组。
 * @param interval 无符号 32 位整数，表示组的采集间隔。
 * @param context 
 *
 * @return 整数类型，表示操作结果：
 *         - NEU_ERR_SUCCESS：组添加成功。
 *         - NEU_ERR_GROUP_EXIST：指定名称的组已经存在，未进行添加操作。
 */
int neu_adapter_driver_add_group(neu_adapter_driver_t *driver, const char *name,
                                 uint32_t interval, void *context)
{
    UT_icd   icd     = { sizeof(to_be_write_tag_t), NULL, NULL, NULL };
    UT_icd   sub_icd = { sizeof(sub_app_t), NULL, NULL, NULL };
    group_t *find    = NULL;
    int      ret     = NEU_ERR_GROUP_EXIST;

    HASH_FIND_STR(driver->groups, name, find);
    if (find == NULL) {
        // 创建一个新的组对象,添加到 driver->groups 哈希表
        find = calloc(1, sizeof(group_t));

        pthread_mutex_init(&find->wt_mtx, NULL);
        pthread_mutex_init(&find->apps_mtx, NULL);

        utarray_new(find->wt_tags, &icd);
        utarray_new(find->apps, &sub_icd);

        // 初始化组结构体成员
        find->driver         = driver;
        find->name           = strdup(name);
        find->group          = neu_group_new(name, interval);
        find->grp.group_name = strdup(name);
        find->grp.interval   = interval;
        find->grp.context    = context;
        find->grp.tags       = neu_group_get_tag(find->group);

        if (NEU_NODE_RUNNING_STATE_RUNNING == driver->adapter.state) {
            // 启动组定时器
            start_group_timer(driver, find);
        }

        /// 注册组指标

        // 组的标签总数
        REGISTER_GROUP_METRIC(&driver->adapter, find->name,
                              NEU_METRIC_GROUP_TAGS_TOTAL,
                              neu_group_tag_size(find->group));
        // 最后发送消息数
        REGISTER_GROUP_METRIC(&driver->adapter, find->name,
                              NEU_METRIC_GROUP_LAST_SEND_MSGS, 0);
        // 最后定时器时间
        REGISTER_GROUP_METRIC(&driver->adapter, find->name,
                              NEU_METRIC_GROUP_LAST_TIMER_MS, 0);
        // 最后错误代码
        REGISTER_GROUP_METRIC(&driver->adapter, find->name,
                              NEU_METRIC_GROUP_LAST_ERROR_CODE, 0);
        // 最后错误时间戳
        REGISTER_GROUP_METRIC(&driver->adapter, find->name,
                              NEU_METRIC_GROUP_LAST_ERROR_TS, 0);

        HASH_ADD_STR(driver->groups, name, find);
        ret = NEU_ERR_SUCCESS;
    }

    return ret;
}

int neu_adapter_driver_update_group(neu_adapter_driver_t *driver,
                                    const char *name, const char *new_name,
                                    uint32_t interval)
{
    int      ret  = 0;
    group_t *find = NULL;

    HASH_FIND_STR(driver->groups, name, find);
    if (NULL == find) {
        return NEU_ERR_GROUP_NOT_EXIST;
    }

    if (NULL != new_name) {
        if (0 == strlen(new_name)) {
            return NEU_ERR_EINTERNAL;
        }
        if (0 != strcmp(name, new_name)) {
            group_t *other = NULL;
            HASH_FIND_STR(driver->groups, new_name, other);
            if (NULL != other) {
                return NEU_ERR_GROUP_EXIST;
            }
        }
    } else if (neu_group_get_interval(find->group) == interval) {
        return 0; // no change
    }

    // stop the timer first to avoid race condition
    if (NEU_NODE_RUNNING_STATE_RUNNING == driver->adapter.state) {
        stop_group_timer(driver, find);
    }

    // a diminutive value should keep the interval untouched
    if (interval < NEU_GROUP_INTERVAL_LIMIT) {
        interval = neu_group_get_interval(find->group);
    }

    // update group name if a different name is provided
    if (NULL != new_name && 0 != strcmp(name, new_name)) {
        char *new_name_cp1 = strdup(new_name);
        char *new_name_cp2 = strdup(new_name);
        if (new_name_cp1 && new_name_cp2 &&
            0 == neu_group_set_name(find->group, new_name)) {
            HASH_DEL(driver->groups, find);
            free(find->name);
            find->name = new_name_cp1;
            free(find->grp.group_name);
            find->grp.group_name = new_name_cp2;
            neu_adapter_metric_update_group_name((neu_adapter_t *) driver, name,
                                                 new_name);
            HASH_ADD_STR(driver->groups, name, find);
        } else {
            free(new_name_cp1);
            free(new_name_cp2);
            ret = NEU_ERR_EINTERNAL;
        }
    }

    find->timestamp    = global_timestamp; // trigger group_change
    find->grp.interval = interval;
    neu_group_set_interval(find->group, interval);

    // restore the timers
    if (NEU_NODE_RUNNING_STATE_RUNNING == driver->adapter.state) {
        start_group_timer(driver, find);
    }

    return ret;
}

int neu_adapter_driver_del_group(neu_adapter_driver_t *driver, const char *name)
{
    group_t *find = NULL;
    int      ret  = NEU_ERR_GROUP_NOT_EXIST;

    HASH_FIND_STR(driver->groups, name, find);
    if (find != NULL) {
        HASH_DEL(driver->groups, find);

        neu_adapter_driver_try_del_tag(driver, neu_group_tag_size(find->group));

        if (NEU_NODE_RUNNING_STATE_RUNNING == driver->adapter.state) {
            stop_group_timer(driver, find);
        }

        if (find->grp.group_free != NULL) {
            find->grp.group_free(&find->grp);
        }
        free(find->grp.group_name);
        free(find->name);
        if (find->grp.context) {
            free(find->grp.context);
        }

        utarray_foreach(find->grp.tags, neu_datatag_t *, tag)
        {
            neu_driver_cache_del(driver->cache, name, tag->name);
        }

        utarray_foreach(find->wt_tags, to_be_write_tag_t *, tag)
        {
            if (tag->single) {
                neu_tag_free(tag->tag);
            } else {
                utarray_foreach(tag->tvs, neu_plugin_tag_value_t *, tv)
                {
                    neu_tag_free(tv->tag);
                }
                utarray_free(tag->tvs);
            }
        }

        driver->tag_cnt -= neu_group_tag_size(find->group);
        driver->adapter.cb_funs.update_metric(
            &driver->adapter, NEU_METRIC_TAGS_TOTAL, driver->tag_cnt, NULL);

        utarray_free(find->grp.tags);
        utarray_free(find->wt_tags);
        utarray_free(find->apps);
        neu_group_destroy(find->group);
        pthread_mutex_destroy(&find->wt_mtx);
        pthread_mutex_destroy(&find->apps_mtx);
        free(find);

        neu_adapter_del_group_metrics(&driver->adapter, name);
        ret = NEU_ERR_SUCCESS;
    }

    return ret;
}

uint16_t neu_adapter_driver_group_count(neu_adapter_driver_t *driver)
{
    u_int16_t num_groups = 0;
    if (driver && driver->groups) {
        num_groups = HASH_COUNT(driver->groups);
    }
    return num_groups;
}

uint16_t neu_adapter_driver_new_group_count(neu_adapter_driver_t *driver,
                                            neu_req_add_gtag_t *  cmd)
{
    uint16_t new_groups_count = 0;
    for (int i = 0; i < cmd->n_group; i++) {
        group_t *group_in_driver;
        HASH_FIND_STR(driver->groups, cmd->groups[i].group, group_in_driver);
        if (!group_in_driver) {
            new_groups_count++;
        }
    }
    return new_groups_count;
}

/**
 * @brief 查找指定名称的组。
 *
 * 该函数用于在驱动适配器中查找具有指定名称的组。它使用 UTHash 库中的 HASH_FIND_STR 宏来高效地查找组。
 *
 * @param driver 指向 neu_adapter_driver_t 结构体的指针，表示驱动适配器。
 * @param name 组名字符串，标识需要查找的组。
 * @return 返回找到的组指针；如果未找到，则返回 NULL。
 */
static group_t *find_group(neu_adapter_driver_t *driver, const char *name)
{
    // 声明一个指向 group_t 结构体的指针，用于存储查找结果
    group_t *find = NULL;

    HASH_FIND_STR(driver->groups, name, find);

    return find;
}

int neu_adapter_driver_group_exist(neu_adapter_driver_t *driver,
                                   const char *          name)
{
    group_t *find = NULL;
    int      ret  = NEU_ERR_GROUP_NOT_EXIST;

    HASH_FIND_STR(driver->groups, name, find);
    if (find != NULL) {
        ret = NEU_ERR_SUCCESS;
    }

    return ret;
}

UT_array *neu_adapter_driver_get_group(neu_adapter_driver_t *driver)
{
    group_t * el = NULL, *tmp = NULL;
    UT_array *groups = NULL;
    UT_icd    icd    = { sizeof(neu_resp_group_info_t), NULL, NULL, NULL };

    utarray_new(groups, &icd);

    HASH_ITER(hh, driver->groups, el, tmp)
    {
        neu_resp_group_info_t info = { 0 };

        info.interval  = neu_group_get_interval(el->group);
        info.tag_count = neu_group_tag_size(el->group);
        strncpy(info.name, el->name, sizeof(info.name));

        utarray_push_back(groups, &info);
    }

    return groups;
}

int neu_adapter_driver_try_add_tag(neu_adapter_driver_t *driver,
                                   const char *group, neu_datatag_t *tags,
                                   int n_tag)
{
    int ret = 0;
    if (driver->adapter.module->intf_funs->driver.add_tags != NULL) {
        ret = driver->adapter.module->intf_funs->driver.add_tags(
            driver->adapter.plugin, group, tags, n_tag);
    }
    return ret;
}

int neu_adapter_driver_load_tag(neu_adapter_driver_t *driver, const char *group,
                                neu_datatag_t *tags, int n_tag)
{
    int ret = 0;
    if (driver->adapter.module->intf_funs->driver.load_tags != NULL) {
        ret = driver->adapter.module->intf_funs->driver.load_tags(
            driver->adapter.plugin, group, tags, n_tag);
    }
    return ret;
}

int neu_adapter_driver_try_del_tag(neu_adapter_driver_t *driver, int n_tag)
{
    int ret = 0;
    if (driver->adapter.module->intf_funs->driver.del_tags != NULL) {
        ret = driver->adapter.module->intf_funs->driver.del_tags(
            driver->adapter.plugin, n_tag);
    }
    return ret;
}

int neu_adapter_driver_validate_tag(neu_adapter_driver_t *driver,
                                    const char *group, neu_datatag_t *tag)
{
    (void) group;

    if (strlen(tag->name) >= NEU_TAG_NAME_LEN) {
        return NEU_ERR_TAG_NAME_TOO_LONG;
    }

    if (strlen(tag->address) >= NEU_TAG_ADDRESS_LEN) {
        return NEU_ERR_TAG_ADDRESS_TOO_LONG;
    }

    if (strlen(tag->description) >= NEU_TAG_DESCRIPTION_LEN) {
        return NEU_ERR_TAG_DESCRIPTION_TOO_LONG;
    }

    if (tag->precision > NEU_TAG_FLOAG_PRECISION_MAX) {
        return NEU_ERR_TAG_PRECISION_INVALID;
    }

    if (tag->bias != 0) {
        switch (tag->type) {
        case NEU_TYPE_INT8:
        case NEU_TYPE_UINT8:
        case NEU_TYPE_INT16:
        case NEU_TYPE_UINT16:
        case NEU_TYPE_INT32:
        case NEU_TYPE_UINT32:
        case NEU_TYPE_INT64:
        case NEU_TYPE_UINT64:
        case NEU_TYPE_FLOAT:
        case NEU_TYPE_DOUBLE:
            if (tag->bias < -1000 || 1000 < tag->bias ||
                neu_tag_attribute_test(tag, NEU_ATTRIBUTE_WRITE)) {
                return NEU_ERR_TAG_BIAS_INVALID;
            }
            break;
        default:
            return NEU_ERR_TAG_BIAS_INVALID;
        }
    }

    int ret = driver->adapter.module->intf_funs->driver.validate_tag(
        driver->adapter.plugin, tag);
    if (ret != NEU_ERR_SUCCESS) {
        return ret;
    }

    neu_datatag_parse_addr_option(tag, &tag->option);

    return NEU_ERR_SUCCESS;
}

/**
 * @brief 向适配器驱动中添加标签信息。
 *
 * 该函数用于将一个标签添加到指定组的适配器驱动中。在添加标签之前，会对标签地址选项进行解析，并验证标签的有效性。
 * 如果指定的组不存在，会先创建该组。添加成功后，会更新适配器驱动的标签计数以及相关的指标信息。
 *
 * @param driver 指向 neu_adapter_driver_t 类型的指针，代表要添加标签的适配器驱动实例。
 * @param group 指向常量字符的指针，指定要添加标签的组名称。
 * @param tag 指向 neu_datatag_t 类型的指针，代表要添加的标签信息。
 * @param interval 无符号 16 位整数，表示标签的采集间隔
 * @return 函数的返回值为整数类型，代表操作结果：
 *         - NEU_ERR_SUCCESS：表示标签添加成功。
 *         - 其他非零值：表示添加过程中出现错误，具体错误码含义可参考相关定义。
 */
int neu_adapter_driver_add_tag(neu_adapter_driver_t *driver, const char *group,
                               neu_datatag_t *tag, uint16_t interval)
{
    int      ret  = NEU_ERR_SUCCESS;
    group_t *find = NULL;

    // 解析标签的地址选项，并将结果存储在 tag->option 中
    neu_datatag_parse_addr_option(tag, &tag->option);

    // 调用插件的验证函数，验证标签的有效性
    driver->adapter.module->intf_funs->driver.validate_tag(
        driver->adapter.plugin, tag);

    // 在驱动的组哈希表中查找指定名称的组
    HASH_FIND_STR(driver->groups, group, find);
    if (find == NULL) {
        // 向适配器驱动中添加该组
        neu_adapter_driver_add_group(driver, group, interval, NULL);
        // 将该组信息存储到持久化存储中
        adapter_storage_add_group(driver->adapter.name, group, interval, NULL);
    }

    // 再次在驱动的组哈希表中查找指定名称的组
    HASH_FIND_STR(driver->groups, group, find);
    // 确保组已成功找到
    assert(find != NULL);
    // 向找到的组中添加标签
    ret = neu_group_add_tag(find->group, tag);

    if (ret == NEU_ERR_SUCCESS) {
        // 增加驱动的标签计数
        driver->tag_cnt += 1;
        
        // 调用适配器的回调函数，更新标签总数的指标信息
        driver->adapter.cb_funs.update_metric(
            &driver->adapter, NEU_METRIC_TAGS_TOTAL, driver->tag_cnt, NULL);
        
        // 更新指定组的标签总数指标信息
        neu_adapter_update_group_metric(&driver->adapter, group,
                                        NEU_METRIC_GROUP_TAGS_TOTAL,
                                        neu_group_tag_size(find->group));
    }

    return ret;
}

int neu_adapter_driver_del_tag(neu_adapter_driver_t *driver, const char *group,
                               const char *tag)
{
    int      ret  = NEU_ERR_SUCCESS;
    group_t *find = NULL;

    HASH_FIND_STR(driver->groups, group, find);
    if (find != NULL) {
        ret = neu_group_del_tag(find->group, tag);
    } else {
        ret = NEU_ERR_GROUP_NOT_EXIST;
    }

    if (ret == NEU_ERR_SUCCESS) {
        neu_adapter_driver_try_del_tag(driver, 1);
        driver->tag_cnt -= 1;
        driver->adapter.cb_funs.update_metric(
            &driver->adapter, NEU_METRIC_TAGS_TOTAL, driver->tag_cnt, NULL);
        neu_adapter_update_group_metric(&driver->adapter, group,
                                        NEU_METRIC_GROUP_TAGS_TOTAL,
                                        neu_group_tag_size(find->group));
    }

    return ret;
}

int neu_adapter_driver_update_tag(neu_adapter_driver_t *driver,
                                  const char *group, neu_datatag_t *tag)
{
    int      ret  = NEU_ERR_SUCCESS;
    group_t *find = NULL;

    if (strlen(tag->name) >= NEU_TAG_NAME_LEN) {
        return NEU_ERR_TAG_NAME_TOO_LONG;
    }

    if (strlen(tag->address) > NEU_TAG_ADDRESS_LEN) {
        return NEU_ERR_TAG_ADDRESS_TOO_LONG;
    }

    if (strlen(tag->description) > NEU_TAG_DESCRIPTION_LEN) {
        return NEU_ERR_TAG_DESCRIPTION_TOO_LONG;
    }

    if (tag->precision > NEU_TAG_FLOAG_PRECISION_MAX) {
        return NEU_ERR_TAG_PRECISION_INVALID;
    }

    ret = driver->adapter.module->intf_funs->driver.validate_tag(
        driver->adapter.plugin, tag);
    if (ret != NEU_ERR_SUCCESS) {
        return ret;
    }

    neu_datatag_parse_addr_option(tag, &tag->option);
    HASH_FIND_STR(driver->groups, group, find);
    if (find != NULL) {
        ret = neu_group_update_tag(find->group, tag);
    } else {
        ret = NEU_ERR_GROUP_NOT_EXIST;
    }

    return ret;
}

int neu_adapter_driver_get_tag(neu_adapter_driver_t *driver, const char *group,
                               UT_array **tags)
{
    int      ret  = NEU_ERR_SUCCESS;
    group_t *find = NULL;

    HASH_FIND_STR(driver->groups, group, find);
    if (find != NULL) {
        *tags = neu_group_get_tag(find->group);
    } else {
        ret = NEU_ERR_GROUP_NOT_EXIST;
    }

    return ret;
}

int neu_adapter_driver_query_tag(neu_adapter_driver_t *driver,
                                 const char *group, const char *name,
                                 UT_array **tags)
{
    int      ret  = NEU_ERR_SUCCESS;
    group_t *find = NULL;

    HASH_FIND_STR(driver->groups, group, find);
    if (find != NULL) {
        if (strlen(name) > 0) {
            *tags = neu_group_query_tag(find->group, name);
        } else {
            *tags = neu_group_get_tag(find->group);
        }
    } else {
        ret = NEU_ERR_GROUP_NOT_EXIST;
    }

    return ret;
}

void neu_adapter_driver_get_value_tag(neu_adapter_driver_t *driver,
                                      const char *group, UT_array **tags)
{
    int ret = neu_adapter_driver_get_tag(driver, group, tags);
    if (ret == NEU_ERR_SUCCESS) {
        utarray_foreach(*tags, neu_datatag_t *, tag)
        {
            if (tag->decimal != 0) {
                if (tag->type == NEU_TYPE_UINT8 || tag->type == NEU_TYPE_INT8 ||
                    tag->type == NEU_TYPE_INT16 ||
                    tag->type == NEU_TYPE_UINT16 ||
                    tag->type == NEU_TYPE_INT32 ||
                    tag->type == NEU_TYPE_UINT32 ||
                    tag->type == NEU_TYPE_INT64 ||
                    tag->type == NEU_TYPE_UINT64 ||
                    tag->type == NEU_TYPE_FLOAT) {
                    tag->type = NEU_TYPE_DOUBLE;
                }
            }
        }
    } else {
        utarray_new(*tags, neu_tag_get_icd());
    }
}

/**
 * @brief 获取可读标签集
 * 
 * @param driver 驱动适配器对象指针。
 * @param group 组名称。
 * @return 返回包含找到的数据标签的UT_array数组；如果没有找到匹配项，则返回NULL。
 */
UT_array *neu_adapter_driver_get_read_tag(neu_adapter_driver_t *driver,
                                          const char *          group)
{
    group_t * find = NULL;
    UT_array *tags = NULL;

    HASH_FIND_STR(driver->groups, group, find);
    if (find != NULL) {
        tags = neu_group_get_read_tag(find->group);
    }

    return tags;
}

/**
 * @brief 获取与特定组和标签关联的所有数据标签。
 *
 * 此函数用于查找并返回与指定适配器、组和标签相关联的数据标签数组。
 * 如果找到匹配的组和标签，则将相应的数据标签添加到UT_array数组中并返回。
 * 若未找到匹配项，则返回NULL。
 *
 * @param driver 驱动适配器对象指针。
 * @param group 组名称。
 * @param tag 标签名称。
 * @return 返回包含找到的数据标签的UT_array数组；如果没有找到匹配项，则返回NULL。
 */
UT_array *neu_adapter_driver_get_ptag(neu_adapter_driver_t *driver,
                                      const char *group, const char *tag)
{
    group_t * find = NULL;
    UT_array *tags = NULL;

    // 在驱动适配器的组哈希表中查找指定的组
    HASH_FIND_STR(driver->groups, group, find);
    if (find != NULL) {
        // 查找该组中的指定标签
        neu_datatag_t *t = neu_group_find_tag(find->group, tag);
        if (t != NULL) {
            // 创建一个新的UT_array数组，并设置其元素销毁函数
            utarray_new(tags, neu_tag_get_icd());

            // 将找到的标签添加到数组中
            utarray_push_back(tags, t);

            // 释放找到的单个标签（因为它的副本已经存储在数组中）
            neu_tag_free(t);
        }
    }

    return tags;
}

static void report_to_app(neu_adapter_driver_t *driver, group_t *group,
                          struct sockaddr_un dst)
{
    neu_reqresp_head_t header = {
        .type = NEU_REQRESP_TRANS_DATA,
    };

    UT_array *tags =
        neu_adapter_driver_get_read_tag(group->driver, group->name);

    neu_reqresp_trans_data_t *data =
        calloc(1, sizeof(neu_reqresp_trans_data_t));

    data->driver = strdup(group->driver->adapter.name);
    data->group  = strdup(group->name);
    utarray_new(data->tags, neu_resp_tag_value_meta_icd());

    read_group(global_timestamp,
               neu_group_get_interval(group->group) *
                   NEU_DRIVER_TAG_CACHE_EXPIRE_TIME,
               neu_adapter_get_tag_cache_type(&driver->adapter), driver->cache,
               group->name, tags, data->tags);

    nlog_info("report group: %s, all tags: %d, report tags: %d", group->name,
              utarray_len(tags), utarray_len(data->tags));
    if (utarray_len(data->tags) > 0) {
        pthread_mutex_lock(&group->apps_mtx);

        data->ctx        = calloc(1, sizeof(neu_reqresp_trans_data_ctx_t));
        data->ctx->index = 1;

        pthread_mutex_init(&data->ctx->mtx, NULL);

        if (driver->adapter.cb_funs.responseto(&driver->adapter, &header, data,
                                               dst) != 0) {
            neu_trans_data_free(data);
        }

        pthread_mutex_unlock(&group->apps_mtx);
    } else {
        utarray_free(data->tags);
        free(data->group);
        free(data->driver);
    }
    utarray_free(tags);
    free(data);
}

/**
 * @brief 定期报告指定组的标签数据的回调函数。
 *
 * 该函数会在指定的时间间隔内被调用，用于报告指定组的标签数据。
 * 它会检查驱动节点的运行状态，准备消息头和数据，获取标签数据，
 * 处理跟踪上下文，读取报告数据，并将数据发送给订阅该组的应用。
 * 最后释放相关的资源。
 *
 * @param usr_data 指向 group_t 结构体的指针，包含了要报告的组的信息。
 * @return 始终返回 0。
 */
static int report_callback(void *usr_data)
{
    group_t *                group = (group_t *) usr_data;
    neu_node_running_state_e state = group->driver->adapter.state;
    if (state != NEU_NODE_RUNNING_STATE_RUNNING) {
        return 0;
    }

    neu_reqresp_head_t header = {
        .type = NEU_REQRESP_TRANS_DATA,
    };

    // 从驱动中获取指定组的标签数据集（neu_datatag_t类型，含标签的信息）
    UT_array *tags =
        neu_adapter_driver_get_read_tag(group->driver, group->name);

    // 分配内存用于存储传输的数据
    neu_reqresp_trans_data_t *data = 
        calloc(1, sizeof(neu_reqresp_trans_data_t));

    data->driver = strdup(group->driver->adapter.name);
    data->group  = strdup(group->name);

    // 初始化标签数组
    utarray_new(data->tags, neu_resp_tag_value_meta_icd());

    // 从从本地驱动缓存中获取跟踪上下文信息
    void *trace_ctx =
        neu_driver_cache_get_trace(group->driver->cache, group->name);

    /**
     * @brief 初始化跟踪和跨度上下文
     * 
     * -trans_trace：是一个跟踪上下文对象，用于存储整个跟踪过程的相关信息，例如跟踪的起始时间、跟踪
     *  的 ID 等。一个跟踪可以包含多个跨度（span），这些跨度共同描述了一个请求在分布式系统中的完整执行路径。
     * -trans_scope：是一个跨度上下文对象，用于存储一个特定操作的相关信息，例如操作的名称、开始时间、
     *  结束时间、状态码等。每个跨度代表了一个独立的操作或任务，它可以嵌套在其他跨度中，形成一个树形结构。
     */
    neu_otel_trace_ctx trans_trace = NULL;
    neu_otel_scope_ctx trans_scope = NULL;

    // 如果存在跟踪上下文且 OpenTelemetry 数据收集已启动
    if (trace_ctx) {
        data->trace_ctx = trace_ctx;
        if (neu_otel_data_is_started() && data->trace_ctx) {
            // 将本地获取的跟踪上下文指针转换为 OpenTelemetry 跟踪对象
            trans_trace = neu_otel_find_trace(data->trace_ctx);
            
            if (trans_trace) {
                // 生成一个新的跨度 ID
                char new_span_id[36] = { 0 };
                neu_otel_new_span_id(new_span_id);

                // 在指定的跟踪上下文中添加一个新的跨度。
                trans_scope =
                    neu_otel_add_span2(trans_trace, "report cb", new_span_id);

                // 添加跨度属性，记录线程 ID
                neu_otel_scope_add_span_attr_int(trans_scope, "thread id",
                                                 (int64_t)(pthread_self()));

                // 设置跨度的开始时间
                neu_otel_scope_set_span_start_time(trans_scope, neu_time_ns());
            }
        }
    }

    // 读取报告数据
    read_report_group(global_timestamp,
                      neu_group_get_interval(group->group) *
                          NEU_DRIVER_TAG_CACHE_EXPIRE_TIME,
                      neu_adapter_get_tag_cache_type(&group->driver->adapter),
                      group->driver->cache, group->name, tags, data->tags);

    if (utarray_len(data->tags) > 0) {
        pthread_mutex_lock(&group->apps_mtx);

        if (utarray_len(group->apps) > 0) {
            int app_num      = 0;

            // 分配内存用于存储传输数据的上下文
            data->ctx        = calloc(1, sizeof(neu_reqresp_trans_data_ctx_t));
            data->ctx->index = utarray_len(group->apps);

            pthread_mutex_init(&data->ctx->mtx, NULL);

            // 复制标签数据，为每个应用准备一份
            for (uint16_t i = 0; i < utarray_len(group->apps) - 1; i++) {
                utarray_foreach(data->tags, neu_resp_tag_value_meta_t *,
                                tag_value)
                {
                    if (tag_value->value.type == NEU_TYPE_CUSTOM) {
                        // 增加 JSON 对象的引用计数
                        json_incref(tag_value->value.value.json);
                    }
                }
            }

            // 遍历订阅该组的应用列表
            utarray_foreach(group->apps, sub_app_t *, app)
            {
                // 发送数据给应用
                if (group->driver->adapter.cb_funs.responseto(
                        &group->driver->adapter, &header, data, app->addr) !=
                    0) {
                    // 发送失败，释放相关资源
                    utarray_foreach(data->tags, neu_resp_tag_value_meta_t *,
                                    tag_value)
                    {
                        if (tag_value->value.type == NEU_TYPE_CUSTOM) {
                            json_decref(tag_value->value.value.json);
                        }
                    }
                    // 释放传输数据
                    neu_trans_data_free(data);
                    if (trans_trace) {
                        neu_otel_scope_add_span_attr_int(trans_scope, app->app,
                                                         0);
                    }
                } else {
                    // 发送成功，增加成功发送的应用数量
                    app_num += 1;
                    if (trans_trace) {
                        neu_otel_scope_add_span_attr_int(trans_scope, app->app,
                                                         1);
                    }
                }
            }

            if (trans_trace) {
                neu_otel_scope_set_span_end_time(trans_scope, neu_time_ns());
                neu_otel_trace_set_expected_span_num(trans_trace, app_num);
                if (app_num == 0) {
                    neu_otel_trace_set_final(trans_trace);
                }
            }

        } else {
            // 如果没有订阅该组的应用，释放标签数据
            utarray_foreach(data->tags, neu_resp_tag_value_meta_t *, tag_value)
            {
                if (tag_value->value.type == NEU_TYPE_PTR) {
                    free(tag_value->value.value.ptr.ptr);
                } else if (tag_value->value.type == NEU_TYPE_CUSTOM) {
                    json_decref(tag_value->value.value.json);
                } else if (tag_value->value.type == NEU_TYPE_ARRAY_STRING) {
                    for (size_t i = 0; i < tag_value->value.value.strs.length;
                         ++i) {
                        free(tag_value->value.value.strs.strs[i]);
                    }
                }
            }
            utarray_free(data->tags);
            free(data->group);
            free(data->driver);

            if (trans_trace) {
                neu_otel_scope_add_span_attr_int(trans_scope, "no sub app", 1);
                neu_otel_scope_set_span_end_time(trans_scope, neu_time_ns());
                neu_otel_trace_set_final(trans_trace);
            }
        }

        pthread_mutex_unlock(&group->apps_mtx);
    } else {
        // 如果没有标签数据，释放相关资源
        utarray_free(data->tags);
        free(data->group);
        free(data->driver);
        if (trans_trace) {
            neu_otel_scope_add_span_attr_int(trans_scope, "no tags", 1);
            neu_otel_scope_set_span_end_time(trans_scope, neu_time_ns());
            neu_otel_trace_set_final(trans_trace);
        }
    }
    utarray_free(tags);
    free(data);
    return 0;
}

/**
 * @brief 处理组数据发生变化时的回调函数。
 *
 * 当组数据发生变化时，该函数会更新组的时间戳，清除旧的缓存数据，
 * 添加新的缓存数据，并更新组的配置信息。
 *
 * @param arg 指向组对象的指针，用于标识发生变化的组。
 * @param timestamp 组数据发生变化的时间戳，用于更新组的时间戳信息。
 * @param tags 指向新的标签数组的指针，包含了组中最新的标签信息。
 * @param interval 组的采集间隔，当前函数未使用该参数。
 *
 * @return 无返回值。
 */
static void group_change(void *arg, int64_t timestamp, UT_array *tags,
                         uint32_t interval)
{
    group_t *group   = (group_t *) arg;
    group->timestamp = timestamp;
    (void) interval;

    // 如果组的 group_free 函数指针不为空，则调用该函数释放旧的组资源
    if (group->grp.group_free != NULL)
        group->grp.group_free(&group->grp);
    
    // 遍历旧的标签数组，从驱动缓存中删除每个标签的缓存数据
    utarray_foreach(group->grp.tags, neu_datatag_t *, tag)
    {
        neu_driver_cache_del(group->driver->cache, group->name, tag->name);
    }

    // 为每个标签创建一个初始值为错误状态的 neu_dvalue_t 对象，并将其添加到驱动缓存中
    // 确保在新数据还未有效填充之前，缓存中的数据不会被错误使用
    utarray_foreach(tags, neu_datatag_t *, tag)
    {
        neu_dvalue_t value = { 0 };

        value.precision = tag->precision;
        value.type      = NEU_TYPE_ERROR;
        value.value.i32 = NEU_ERR_PLUGIN_TAG_NOT_READY;

        neu_driver_cache_add(group->driver->cache, group->name, tag->name,
                             value);
    }

    // 组数据变化可能伴随着标签的添加、删除或修改；和配置信息更改
    // 所以要创建一个新的 neu_plugin_group_t 对象，用于更新组的配置信息
    neu_plugin_group_t grp = {
        .group_name = strdup(group->name),
        .interval   = neu_group_get_interval(group->group),
        .tags       = NULL,
        .group_free = NULL,
        .user_data  = NULL,
        .context    = NULL,
    };
    
    // 将新的上下文信息和标签数组赋值给新的组对象
    grp.context = group->grp.context;
    grp.tags    = tags;

    // 释放旧的组名称内存
    free(group->grp.group_name);

    // 如果旧的标签数组不为空，则释放其内存
    if (group->grp.tags != NULL) {
        utarray_free(group->grp.tags);
    }

    // 更新组的配置信息
    group->grp = grp;

    nlog_notice("group: %s changed, timestamp: %" PRIi64, group->name,
                timestamp);
}

static int write_callback(void *usr_data)
{
    group_t *                group = (group_t *) usr_data;
    neu_node_running_state_e state = group->driver->adapter.state;
    if (state != NEU_NODE_RUNNING_STATE_RUNNING) {
        return 0;
    }

    pthread_mutex_lock(&group->wt_mtx);
    utarray_foreach(group->wt_tags, to_be_write_tag_t *, wtag)
    {

        int64_t s_time = 0;
        int64_t e_time = 0;

        neu_otel_trace_ctx trace = NULL;
        neu_otel_scope_ctx scope = NULL;
        if (neu_otel_control_is_started()) {
            trace =
                neu_otel_find_trace(((neu_reqresp_head_t *) wtag->req)->ctx);
            if (trace) {
                scope = neu_otel_add_span(trace);
                if (wtag->single) {
                    neu_otel_scope_set_span_name(scope,
                                                 "driver timer cb write tag");
                } else {
                    neu_otel_scope_set_span_name(scope,
                                                 "driver timer cb write tags");
                }
                char new_span_id[36] = { 0 };
                neu_otel_new_span_id(new_span_id);
                neu_otel_scope_set_span_id(scope, new_span_id);
                uint8_t *p_sp_id = neu_otel_scope_get_pre_span_id(scope);
                if (p_sp_id) {
                    neu_otel_scope_set_parent_span_id2(scope, p_sp_id, 8);
                }
                neu_otel_scope_add_span_attr_int(scope, "thread id",
                                                 (int64_t) pthread_self());
                neu_otel_scope_add_span_attr_string(
                    scope, "plugin name",
                    group->driver->adapter.module->module_name);

                char version[64] = { 0 };
                sprintf(version, "%d.%d.%d",
                        NEU_GET_VERSION_MAJOR(
                            group->driver->adapter.module->version),
                        NEU_GET_VERSION_MINOR(
                            group->driver->adapter.module->version),
                        NEU_GET_VERSION_FIX(
                            group->driver->adapter.module->version));

                neu_otel_scope_add_span_attr_string(scope, "plugin version",
                                                    version);
            }
        }

        s_time = neu_time_ns();

        if (wtag->single) {
            group->driver->adapter.module->intf_funs->driver.write_tag(
                group->driver->adapter.plugin, (void *) wtag->req, wtag->tag,
                wtag->value);
            e_time = neu_time_ns();
            neu_tag_free(wtag->tag);
        } else {
            group->driver->adapter.module->intf_funs->driver.write_tags(
                group->driver->adapter.plugin, (void *) wtag->req, wtag->tvs);
            e_time = neu_time_ms();
            utarray_foreach(wtag->tvs, neu_plugin_tag_value_t *, tv)
            {
                neu_tag_free(tv->tag);
            }
            utarray_free(wtag->tvs);
        }
        if (neu_otel_control_is_started() && trace) {
            neu_otel_scope_set_span_start_time(scope, s_time);
            neu_otel_scope_set_span_end_time(scope, e_time);
        }
    }
    utarray_clear(group->wt_tags);
    pthread_mutex_unlock(&group->wt_mtx);

    return 0;
}

/**
 * @brief 读取回调函数，在定时器到期时被调用，用于处理组数据的读取操作。
 *
 * 该函数会检查组所属的驱动适配器的运行状态，若状态为运行中，则进一步检查组数据是否发生变化，
 * 若有变化则进行相应测试。如果组中存在标签，则调用驱动适配器模块的 group_timer 函数执行组定时器操作，
 * 记录操作的时间消耗并更新相关的指标。
 *
 * @param usr_data 指向用户数据的指针，这里是一个 group_t 类型的结构体指针。
 * @return int 操作结果，通常返回 0 表示操作成功。
 */
static int read_callback(void *usr_data)
{
    // 将用户数据转换为 group_t 类型的结构体指针
    group_t *                group = (group_t *) usr_data;
    // 获取组所属的驱动适配器的运行状态
    neu_node_running_state_e state = group->driver->adapter.state;
    
    if (state != NEU_NODE_RUNNING_STATE_RUNNING) {
        return 0;
    }

    // 检查组的数据是否发生变化
    if (neu_group_is_change(group->group, group->timestamp)) {
        // 若组数据发生变化，则进行组变化测试
        neu_group_change_test(group->group, group->timestamp, (void *) group,
                              group_change);
    }

    // 检查组中是否存在标签
    if (group->grp.tags != NULL && utarray_len(group->grp.tags) > 0) {
        int64_t spend = global_timestamp;

        // 调用驱动适配器模块，执行组定时器操作
        group->driver->adapter.module->intf_funs->driver.group_timer(
            group->driver->adapter.plugin, &group->grp);

        spend = global_timestamp - spend;
        nlog_debug("%s-%s timer: %" PRId64, group->driver->adapter.name,
                   group->name, spend);

        // 更新组的最后一次定时器操作时间指标
        neu_adapter_update_group_metric(&group->driver->adapter, group->name,
                                        NEU_METRIC_GROUP_LAST_TIMER_MS, spend);
    }

    return 0;
}

/**
 * @brief 从缓存中读取标签值并根据特定条件进行报告。
 *
 * 此函数遍历一组标签，检查每个标签的值是否发生了变化或是否有效，并将结果存储在一个动态数组中。
 * 它处理不同类型的数据（如整数、浮点数等），进行字节序转换，并根据标签的精度和偏置调整值。
 *
 * @param timestamp 当前时间戳，用于判断缓存中的数据是否过期。
 * @param timeout 超时时间，超过此时间的数据将被视为过期。
 * @param cache_type 缓存类型（如 NEU_TAG_CACHE_TYPE_INTERVAL 或 NEU_TAG_CACHE_TYPE_NEVER）。
 * @param cache 指向缓存对象的指针，包含所有标签的缓存数据。
 * @param group 组名称，标识属于同一组的标签。
 * @param tags 包含需要处理的所有标签的动态数组。
 * @param tag_values 用于存储处理后的标签值及其元数据的动态数组。
 */
static void read_report_group(int64_t timestamp, int64_t timeout,
                              neu_tag_cache_type_e cache_type,
                              neu_driver_cache_t *cache, const char *group,
                              UT_array *tags, UT_array *tag_values)
{
    // 遍历所有的标签
    utarray_foreach(tags, neu_datatag_t *, tag)
    {
        neu_driver_cache_value_t  value     = { 0 }; // 初始化缓存值
        neu_resp_tag_value_meta_t tag_value = { 0 }; // 初始化标签值及元数据

        // 判断标签设置了订阅属性
        if (neu_tag_attribute_test(tag, NEU_ATTRIBUTE_SUBSCRIBE)) {   
            //只有当标签的值发生变化时，才会继续后续的处理
            if (neu_driver_cache_meta_get_changed(cache, group, tag->name,
                                                  &value, tag_value.metas,
                                                  NEU_TAG_META_SIZE) != 0) {
                nlog_debug("tag: %s not changed", tag->name);
                continue;
            }
        } else {
            //从缓存中获取指定组和标签的值，无论该值是否发生了变化
            if (neu_driver_cache_meta_get(cache, group, tag->name, &value,
                                          tag_value.metas,
                                          NEU_TAG_META_SIZE) != 0) {
                // 找不到标签的值，修改标签的元数据
                strcpy(tag_value.tag, tag->name);
                tag_value.value.type      = NEU_TYPE_ERROR;
                tag_value.value.value.i32 = NEU_ERR_PLUGIN_TAG_NOT_READY;

                utarray_push_back(tag_values, &tag_value);
                continue;
            }
        }
        strcpy(tag_value.tag, tag->name);

        tag_value.datatag.bias = tag->bias;

        //缓存数据损坏或者数据源出现问题，则跳过后续处理逻辑
        if (value.value.type == NEU_TYPE_ERROR) {
            tag_value.value = value.value;
            utarray_push_back(tag_values, &tag_value);
            continue;
        }

        //获取到的是 NaN（表示一个无效的或未定义的数值），跳过后续处理逻辑
        if ((tag->type == NEU_TYPE_FLOAT && isnan(value.value.value.f32)) ||
            (tag->type == NEU_TYPE_DOUBLE && isnan(value.value.value.d64))) {
            tag_value.value.type      = NEU_TYPE_ERROR;
            tag_value.value.value.i32 = NEU_ERR_PLUGIN_TAG_VALUE_EXPIRED;
            utarray_push_back(tag_values, &tag_value);
            continue;
        }

        switch (tag->type) {
        case NEU_TYPE_UINT16:
        case NEU_TYPE_INT16:
            switch (tag->option.value16.endian) {
            case NEU_DATATAG_ENDIAN_B16:
                //数据从主机字节序转换为网络字节序，以确保数据在不同系统之间的正确传输和处理
                value.value.value.u16 = htons(value.value.value.u16);
                break;
            case NEU_DATATAG_ENDIAN_L16:
            default:
                break;
            }
            break;
        case NEU_TYPE_FLOAT:
        case NEU_TYPE_UINT32:
        case NEU_TYPE_INT32:
            switch (tag->option.value32.endian) {
            case NEU_DATATAG_ENDIAN_LB32: {
                uint16_t *v1 = (uint16_t *) value.value.value.bytes.bytes;
                uint16_t *v2 = (uint16_t *) (value.value.value.bytes.bytes + 2);

                neu_htons_p(v1);
                neu_htons_p(v2);
                break;
            }
            case NEU_DATATAG_ENDIAN_BB32:
                value.value.value.u32 = htonl(value.value.value.u32);
                break;
            case NEU_DATATAG_ENDIAN_BL32:
                value.value.value.u32 = htonl(value.value.value.u32);
                uint16_t *v1 = (uint16_t *) value.value.value.bytes.bytes;
                uint16_t *v2 = (uint16_t *) (value.value.value.bytes.bytes + 2);

                neu_htons_p(v1);
                neu_htons_p(v2);
                break;
            case NEU_DATATAG_ENDIAN_LL32:
            default:
                break;
            }
            break;
        case NEU_TYPE_DOUBLE:
        case NEU_TYPE_INT64:
        case NEU_TYPE_UINT64:
            switch (tag->option.value64.endian) {
            case NEU_DATATAG_ENDIAN_B64:
                value.value.value.u64 = neu_htonll(value.value.value.u64);
                break;
            case NEU_DATATAG_ENDIAN_L64:
            default:
                break;
            }
            break;
        default:
            break;
        }

        ///< 对缓存数据的有效性检查和数据处理逻辑

        if (cache_type != NEU_TAG_CACHE_TYPE_NEVER &&
            (timestamp - value.timestamp) > timeout && timeout > 0) {           
            // 缓存数据过期处理
            if (value.value.type == NEU_TYPE_PTR) {
                free(value.value.value.ptr.ptr);
            } else if (value.value.type == NEU_TYPE_CUSTOM) {
                json_decref(value.value.value.json);
            } else if (value.value.type == NEU_TYPE_ARRAY_STRING) {
                for (size_t i = 0; i < value.value.value.strs.length; ++i) {
                    free(value.value.value.strs.strs[i]);
                }
            }
            tag_value.value.type      = NEU_TYPE_ERROR;
            tag_value.value.value.i32 = NEU_ERR_PLUGIN_TAG_VALUE_EXPIRED;
        } else {
            if (value.value.type == NEU_TYPE_PTR) {
                tag_value.value.type             = NEU_TYPE_PTR;
                tag_value.value.value.ptr.length = value.value.value.ptr.length;
                tag_value.value.value.ptr.type   = value.value.value.ptr.type;
                tag_value.value.value.ptr.ptr    = value.value.value.ptr.ptr;
            } else {
                tag_value.value = value.value;
            }

            /**
             * @brief
             * 
             * 当 tag 的 decimal（缩放因子）或 bias（偏移量）不为 0 时，
             * 需要对 tag_value 的值进行数学运算（乘以 decimal 并加上 bias）
             * 
             * @note
             * 前提：只有数值类型的value才会有 decimal（缩放因子）和 bias（偏移量）的概念。
             * 不同类型的数据（如整数、浮点数等）在进行这些运算时，为了保证计算的精度和一致性，
             * 将所有数据统一转换为 double 类型进行处理是一种常见且有效的做法
             */
            if (tag->decimal != 0 || tag->bias != 0) {
                double decimal = tag->decimal != 0 ? tag->decimal : 1;
                double bias    = tag->bias;

                tag_value.value.type = NEU_TYPE_DOUBLE;
                switch (tag->type) {
                case NEU_TYPE_INT8:
                    tag_value.value.value.d64 =
                        (double) tag_value.value.value.i8 * decimal + bias;
                    break;
                case NEU_TYPE_UINT8:
                    tag_value.value.value.d64 =
                        (double) tag_value.value.value.u8 * decimal + bias;
                    break;
                case NEU_TYPE_INT16:
                    tag_value.value.value.d64 =
                        (double) tag_value.value.value.i16 * decimal + bias;
                    break;
                case NEU_TYPE_UINT16:
                    tag_value.value.value.d64 =
                        (double) tag_value.value.value.u16 * decimal + bias;
                    break;
                case NEU_TYPE_INT32:
                    tag_value.value.value.d64 =
                        (double) tag_value.value.value.i32 * decimal + bias;
                    break;
                case NEU_TYPE_UINT32:
                    tag_value.value.value.d64 =
                        (double) tag_value.value.value.u32 * decimal + bias;
                    break;
                case NEU_TYPE_INT64:
                    tag_value.value.value.d64 =
                        (double) tag_value.value.value.i64 * decimal + bias;
                    break;
                case NEU_TYPE_UINT64:
                    tag_value.value.value.d64 =
                        (double) tag_value.value.value.u64 * decimal + bias;
                    break;
                case NEU_TYPE_FLOAT:
                    tag_value.value.value.d64 =
                        (double) tag_value.value.value.f32 * decimal + bias;
                    break;
                case NEU_TYPE_DOUBLE:
                    tag_value.value.value.d64 =
                        (double) tag_value.value.value.d64 * decimal + bias;
                    break;
                default:
                    tag_value.value.type = tag->type;
                    break;
                }
            }
            if (tag->precision == 0 && tag->bias == 0 &&
                tag->type == NEU_TYPE_DOUBLE) {
                format_tag_value(&tag_value.value);
            }
        }

        utarray_push_back(tag_values, &tag_value);
    }
}

static void read_group(int64_t timestamp, int64_t timeout,
                       neu_tag_cache_type_e cache_type,
                       neu_driver_cache_t *cache, const char *group,
                       UT_array *tags, UT_array *tag_values)
{
    utarray_foreach(tags, neu_datatag_t *, tag)
    {
        neu_resp_tag_value_meta_t tag_value = { 0 };
        neu_driver_cache_value_t  value     = { 0 };

        strcpy(tag_value.tag, tag->name);

        tag_value.datatag.bias = tag->bias;

        if (neu_driver_cache_meta_get(cache, group, tag->name, &value,
                                      tag_value.metas,
                                      NEU_TAG_META_SIZE) != 0) {
            tag_value.value.type      = NEU_TYPE_ERROR;
            tag_value.value.value.i32 = NEU_ERR_PLUGIN_TAG_NOT_READY;

            utarray_push_back(tag_values, &tag_value);
            continue;
        }

        if (value.value.type == NEU_TYPE_ERROR) {
            tag_value.value = value.value;
            utarray_push_back(tag_values, &tag_value);
            continue;
        }

        switch (tag->type) {
        case NEU_TYPE_UINT16:
        case NEU_TYPE_INT16:
            switch (tag->option.value16.endian) {
            case NEU_DATATAG_ENDIAN_B16:
                value.value.value.u16 = htons(value.value.value.u16);
                break;
            case NEU_DATATAG_ENDIAN_L16:
            default:
                break;
            }
            break;
        case NEU_TYPE_FLOAT:
        case NEU_TYPE_UINT32:
        case NEU_TYPE_INT32:
            switch (tag->option.value32.endian) {
            case NEU_DATATAG_ENDIAN_LB32: {
                uint16_t *v1 = (uint16_t *) value.value.value.bytes.bytes;
                uint16_t *v2 = (uint16_t *) (value.value.value.bytes.bytes + 2);

                neu_htons_p(v1);
                neu_htons_p(v2);
                break;
            }
            case NEU_DATATAG_ENDIAN_BB32:
                value.value.value.u32 = htonl(value.value.value.u32);
                break;
            case NEU_DATATAG_ENDIAN_BL32:
                value.value.value.u32 = htonl(value.value.value.u32);
                uint16_t *v1 = (uint16_t *) value.value.value.bytes.bytes;
                uint16_t *v2 = (uint16_t *) (value.value.value.bytes.bytes + 2);

                neu_htons_p(v1);
                neu_htons_p(v2);
                break;
            case NEU_DATATAG_ENDIAN_LL32:
            default:
                break;
            }
            break;
        case NEU_TYPE_DOUBLE:
        case NEU_TYPE_INT64:
        case NEU_TYPE_UINT64:
            switch (tag->option.value64.endian) {
            case NEU_DATATAG_ENDIAN_B64:
                value.value.value.u64 = neu_htonll(value.value.value.u64);
                break;
            case NEU_DATATAG_ENDIAN_L64:
            default:
                break;
            }
            break;
        default:
            break;
        }

        if (cache_type != NEU_TAG_CACHE_TYPE_NEVER &&
            (timestamp - value.timestamp) > timeout) {
            if (value.value.type == NEU_TYPE_PTR) {
                free(value.value.value.ptr.ptr);
            } else if (value.value.type == NEU_TYPE_CUSTOM) {
                json_decref(value.value.value.json);
            } else if (value.value.type == NEU_TYPE_ARRAY_STRING) {
                for (size_t i = 0; i < value.value.value.strs.length; ++i) {
                    free(value.value.value.strs.strs[i]);
                }
            }
            tag_value.value.type      = NEU_TYPE_ERROR;
            tag_value.value.value.i32 = NEU_ERR_PLUGIN_TAG_VALUE_EXPIRED;
        } else {
            if (value.value.type == NEU_TYPE_PTR) {
                tag_value.value.type             = NEU_TYPE_PTR;
                tag_value.value.value.ptr.length = value.value.value.ptr.length;
                tag_value.value.value.ptr.type   = value.value.value.ptr.type;
                tag_value.value.value.ptr.ptr    = value.value.value.ptr.ptr;
            } else {
                tag_value.value = value.value;
            }
            if (tag->decimal != 0 || tag->bias != 0) {
                tag_value.value.type = NEU_TYPE_DOUBLE;
                double decimal       = tag->decimal != 0 ? tag->decimal : 1;
                double bias          = tag->bias;
                switch (tag->type) {
                case NEU_TYPE_INT8:
                    tag_value.value.value.d64 =
                        (double) tag_value.value.value.i8 * decimal + bias;
                    break;
                case NEU_TYPE_UINT8:
                    tag_value.value.value.d64 =
                        (double) tag_value.value.value.u8 * decimal + bias;
                    break;
                case NEU_TYPE_INT16:
                    tag_value.value.value.d64 =
                        (double) tag_value.value.value.i16 * decimal + bias;
                    break;
                case NEU_TYPE_UINT16:
                    tag_value.value.value.d64 =
                        (double) tag_value.value.value.u16 * decimal + bias;
                    break;
                case NEU_TYPE_INT32:
                    tag_value.value.value.d64 =
                        (double) tag_value.value.value.i32 * decimal + bias;
                    break;
                case NEU_TYPE_UINT32:
                    tag_value.value.value.d64 =
                        (double) tag_value.value.value.u32 * decimal + bias;
                    break;
                case NEU_TYPE_INT64:
                    tag_value.value.value.d64 =
                        (double) tag_value.value.value.i64 * decimal + bias;
                    break;
                case NEU_TYPE_UINT64:
                    tag_value.value.value.d64 =
                        (double) tag_value.value.value.u64 * decimal + bias;
                    break;
                case NEU_TYPE_FLOAT:
                    tag_value.value.value.d64 =
                        (double) tag_value.value.value.f32 * decimal + bias;
                    break;
                case NEU_TYPE_DOUBLE:
                    tag_value.value.value.d64 =
                        (double) tag_value.value.value.d64 * decimal + bias;
                    break;
                default:
                    tag_value.value.type = tag->type;
                    break;
                }
            }

            if (tag->precision == 0 && tag->bias == 0 &&
                tag->type == NEU_TYPE_DOUBLE) {
                format_tag_value(&tag_value.value);
            }
        }
        utarray_push_back(tag_values, &tag_value);
    }
}

static void read_group_paginate(int64_t timestamp, int64_t timeout,
                                neu_tag_cache_type_e cache_type,
                                neu_driver_cache_t *cache, const char *group,
                                UT_array *tags, UT_array *tag_values)
{
    utarray_foreach(tags, neu_datatag_t *, tag)
    {
        neu_resp_tag_value_meta_paginate_t tag_value = { 0 };
        neu_driver_cache_value_t           value     = { 0 };

        strcpy(tag_value.tag, tag->name);

        tag_value.datatag.name        = strdup(tag->name);
        tag_value.datatag.address     = strdup(tag->address);
        tag_value.datatag.attribute   = tag->attribute;
        tag_value.datatag.type        = tag->type;
        tag_value.datatag.precision   = tag->precision;
        tag_value.datatag.decimal     = tag->decimal;
        tag_value.datatag.bias        = tag->bias;
        tag_value.datatag.description = strdup(tag->description);
        tag_value.datatag.option      = tag->option;
        memcpy(tag_value.datatag.meta, tag->meta, NEU_TAG_META_LENGTH);

        if (tag->attribute == NEU_ATTRIBUTE_WRITE) {
            tag_value.value.type         = NEU_TYPE_STRING;
            tag_value.value.value.str[0] = 0;
            utarray_push_back(tag_values, &tag_value);
            continue;
        }

        if (neu_driver_cache_meta_get(cache, group, tag->name, &value,
                                      tag_value.metas,
                                      NEU_TAG_META_SIZE) != 0) {
            tag_value.value.type      = NEU_TYPE_ERROR;
            tag_value.value.value.i32 = NEU_ERR_PLUGIN_TAG_NOT_READY;

            utarray_push_back(tag_values, &tag_value);
            continue;
        }

        if (value.value.type == NEU_TYPE_ERROR) {
            tag_value.value = value.value;
            utarray_push_back(tag_values, &tag_value);
            continue;
        }

        switch (tag->type) {
        case NEU_TYPE_UINT16:
        case NEU_TYPE_INT16:
            switch (tag->option.value16.endian) {
            case NEU_DATATAG_ENDIAN_B16:
                value.value.value.u16 = htons(value.value.value.u16);
                break;
            case NEU_DATATAG_ENDIAN_L16:
            default:
                break;
            }
            break;
        case NEU_TYPE_FLOAT:
        case NEU_TYPE_UINT32:
        case NEU_TYPE_INT32:
            switch (tag->option.value32.endian) {
            case NEU_DATATAG_ENDIAN_LB32: {
                uint16_t *v1 = (uint16_t *) value.value.value.bytes.bytes;
                uint16_t *v2 = (uint16_t *) (value.value.value.bytes.bytes + 2);

                neu_htons_p(v1);
                neu_htons_p(v2);
                break;
            }
            case NEU_DATATAG_ENDIAN_BB32:
                value.value.value.u32 = htonl(value.value.value.u32);
                break;
            case NEU_DATATAG_ENDIAN_BL32:
                value.value.value.u32 = htonl(value.value.value.u32);
                uint16_t *v1 = (uint16_t *) value.value.value.bytes.bytes;
                uint16_t *v2 = (uint16_t *) (value.value.value.bytes.bytes + 2);

                neu_htons_p(v1);
                neu_htons_p(v2);
                break;
            case NEU_DATATAG_ENDIAN_LL32:
            default:
                break;
            }
            break;
        case NEU_TYPE_DOUBLE:
        case NEU_TYPE_INT64:
        case NEU_TYPE_UINT64:
            switch (tag->option.value64.endian) {
            case NEU_DATATAG_ENDIAN_B64:
                value.value.value.u64 = neu_htonll(value.value.value.u64);
                break;
            case NEU_DATATAG_ENDIAN_L64:
            default:
                break;
            }
            break;
        default:
            break;
        }

        if (cache_type != NEU_TAG_CACHE_TYPE_NEVER &&
            (timestamp - value.timestamp) > timeout) {
            if (value.value.type == NEU_TYPE_PTR) {
                free(value.value.value.ptr.ptr);
            } else if (value.value.type == NEU_TYPE_CUSTOM) {
                json_decref(value.value.value.json);
            } else if (value.value.type == NEU_TYPE_ARRAY_STRING) {
                for (size_t i = 0; i < value.value.value.strs.length; ++i) {
                    free(value.value.value.strs.strs[i]);
                }
            }
            tag_value.value.type      = NEU_TYPE_ERROR;
            tag_value.value.value.i32 = NEU_ERR_PLUGIN_TAG_VALUE_EXPIRED;
        } else {
            if (value.value.type == NEU_TYPE_PTR) {
                tag_value.value.type             = NEU_TYPE_PTR;
                tag_value.value.value.ptr.length = value.value.value.ptr.length;
                tag_value.value.value.ptr.type   = value.value.value.ptr.type;
                tag_value.value.value.ptr.ptr    = value.value.value.ptr.ptr;
            } else {
                tag_value.value = value.value;
            }
            if (tag->decimal != 0 || tag->bias != 0) {
                tag_value.value.type = NEU_TYPE_DOUBLE;
                double decimal       = tag->decimal != 0 ? tag->decimal : 1;
                double bias          = tag->bias;
                switch (tag->type) {
                case NEU_TYPE_INT8:
                    tag_value.value.value.d64 =
                        (double) tag_value.value.value.i8 * decimal + bias;
                    break;
                case NEU_TYPE_UINT8:
                    tag_value.value.value.d64 =
                        (double) tag_value.value.value.u8 * decimal + bias;
                    break;
                case NEU_TYPE_INT16:
                    tag_value.value.value.d64 =
                        (double) tag_value.value.value.i16 * decimal + bias;
                    break;
                case NEU_TYPE_UINT16:
                    tag_value.value.value.d64 =
                        (double) tag_value.value.value.u16 * decimal + bias;
                    break;
                case NEU_TYPE_INT32:
                    tag_value.value.value.d64 =
                        (double) tag_value.value.value.i32 * decimal + bias;
                    break;
                case NEU_TYPE_UINT32:
                    tag_value.value.value.d64 =
                        (double) tag_value.value.value.u32 * decimal + bias;
                    break;
                case NEU_TYPE_INT64:
                    tag_value.value.value.d64 =
                        (double) tag_value.value.value.i64 * decimal + bias;
                    break;
                case NEU_TYPE_UINT64:
                    tag_value.value.value.d64 =
                        (double) tag_value.value.value.u64 * decimal + bias;
                    break;
                case NEU_TYPE_FLOAT:
                    tag_value.value.value.d64 =
                        (double) tag_value.value.value.f32 * decimal + bias;
                    break;
                case NEU_TYPE_DOUBLE:
                    tag_value.value.value.d64 =
                        (double) tag_value.value.value.d64 * decimal + bias;
                    break;
                default:
                    tag_value.value.type = tag->type;
                    break;
                }
            }

            if (tag->precision == 0 && tag->bias == 0 &&
                tag->type == NEU_TYPE_DOUBLE) {
                format_tag_value(&tag_value.value);
            }
        }
        utarray_push_back(tag_values, &tag_value);
    }
}

static void store_write_tag(group_t *group, to_be_write_tag_t *tag)
{
    pthread_mutex_lock(&group->wt_mtx);
    utarray_push_back(group->wt_tags, tag);
    pthread_mutex_unlock(&group->wt_mtx);
}

void neu_adapter_driver_subscribe(neu_adapter_driver_t *driver,
                                  neu_req_subscribe_t * req)
{
    sub_app_t sub_app = { 0 };
    group_t * find    = NULL;

    HASH_FIND_STR(driver->groups, req->group, find);
    if (find == NULL) {
        nlog_warn("%s sub group: %s not exist", driver->adapter.name,
                  req->group);
        return;
    }

    pthread_mutex_lock(&find->apps_mtx);
    utarray_foreach(find->apps, sub_app_t *, app)
    {
        if (strcmp(app->app, req->app) == 0) {
            nlog_warn("%s sub group: %s app: %s already exist",
                      driver->adapter.name, req->group, req->app);
            pthread_mutex_unlock(&find->apps_mtx);
            return;
        }
    }

    strcpy(sub_app.app, req->app);
    sub_app.addr.sun_family = AF_UNIX;
    snprintf(sub_app.addr.sun_path, sizeof(sub_app.addr.sun_path),
             "%cneuron-%" PRIu16, '\0', req->port);

    utarray_push_back(find->apps, &sub_app);
    pthread_mutex_unlock(&find->apps_mtx);

    report_to_app(driver, find, sub_app.addr);
}

void neu_adapter_driver_unsubscribe(neu_adapter_driver_t * driver,
                                    neu_req_unsubscribe_t *req)
{
    group_t *find = NULL;

    HASH_FIND_STR(driver->groups, req->group, find);
    if (find == NULL) {
        nlog_warn("%s unsub group: %s not exist", driver->adapter.name,
                  req->group);
        return;
    }

    pthread_mutex_lock(&find->apps_mtx);
    utarray_foreach(find->apps, sub_app_t *, app)
    {
        if (strcmp(app->app, req->app) == 0) {
            utarray_erase(find->apps, utarray_eltidx(find->apps, app), 1);
            pthread_mutex_unlock(&find->apps_mtx);
            return;
        }
    }
    pthread_mutex_unlock(&find->apps_mtx);
}

void neu_adapter_driver_scan_tags(neu_adapter_driver_t *driver,
                                  neu_reqresp_head_t *  req)
{
    if (driver->adapter.state != NEU_NODE_RUNNING_STATE_RUNNING) {
        neu_resp_scan_tags_t resp_scan = {
            .scan_tags = NULL,
            .error     = NEU_ERR_PLUGIN_NOT_RUNNING,
            .type      = NEU_TYPE_ERROR,
            .is_array  = false,
        };
        driver->adapter.cb_funs.driver.scan_tags_response(&driver->adapter, req,
                                                          &resp_scan);

        nlog_warn("%s not running", driver->adapter.name);
        return;
    }

    if (driver->adapter.module->intf_funs->driver.scan_tags == NULL) {
        neu_resp_scan_tags_t resp_scan = {
            .scan_tags = NULL,
            .error     = NEU_ERR_PLUGIN_NOT_SUPPORT_SCAN_TAGS,
            .type      = NEU_TYPE_ERROR,
            .is_array  = false,
        };
        driver->adapter.cb_funs.driver.scan_tags_response(&driver->adapter, req,
                                                          &resp_scan);

        nlog_warn("%s not support scan tags", driver->adapter.name);
        return;
    }

    neu_req_scan_tags_t *cmd = (neu_req_scan_tags_t *) &req[1];
    driver->adapter.module->intf_funs->driver.scan_tags(driver->adapter.plugin,
                                                        req, cmd->id, cmd->ctx);
}

void neu_adapter_driver_test_read_tag(neu_adapter_driver_t *driver,
                                      neu_reqresp_head_t *  req)
{
    neu_json_value_u error_value;
    error_value.val_int = 0;

    if (driver->adapter.state != NEU_NODE_RUNNING_STATE_RUNNING) {
        driver->adapter.cb_funs.driver.test_read_tag_response(
            &driver->adapter, req, NEU_JSON_INT, NEU_TYPE_ERROR, error_value,
            NEU_ERR_PLUGIN_NOT_RUNNING);

        nlog_warn("%s not running", driver->adapter.name);
        return;
    }

    if (driver->adapter.module->intf_funs->driver.test_read_tag == NULL) {
        driver->adapter.cb_funs.driver.test_read_tag_response(
            &driver->adapter, req, NEU_JSON_INT, NEU_TYPE_ERROR, error_value,
            NEU_ERR_PLUGIN_NOT_SUPPORT_TEST_READ_TAG);

        nlog_warn("%s not support testing reading", driver->adapter.name);
        return;
    }

    neu_req_test_read_tag_t *cmd = (neu_req_test_read_tag_t *) &req[1];
    neu_datatag_t            datatag;
    datatag.name      = cmd->tag;
    datatag.address   = cmd->address;
    datatag.attribute = cmd->attribute;
    datatag.type      = cmd->type;
    datatag.precision = cmd->precision;
    datatag.decimal   = cmd->decimal;
    datatag.bias      = cmd->bias;
    datatag.option    = cmd->option;

    group_t *g = find_group(driver, cmd->group);
    if (g != NULL && neu_group_tag_size(g->group) > 0) {
        stop_group_timer(driver, g);
    }

    driver->adapter.module->intf_funs->driver.test_read_tag(
        driver->adapter.plugin, req, datatag);

    if (g != NULL && neu_group_tag_size(g->group) > 0) {
        start_group_timer(driver, g);
    }
}

int neu_adapter_driver_cmd(neu_adapter_driver_t *driver, const char *cmd)
{
    if (driver->adapter.state != NEU_NODE_RUNNING_STATE_RUNNING) {
        return NEU_ERR_PLUGIN_NOT_RUNNING;
    }
    if (driver->adapter.module->intf_funs->driver.action == NULL) {
        return NEU_ERR_PLUGIN_NOT_SUPPORT_EXE_ACTION;
    }
    return driver->adapter.module->intf_funs->driver.action(
        driver->adapter.plugin, cmd);
}

int neu_adapter_driver_directory(neu_adapter_driver_t *      driver,
                                 neu_reqresp_head_t *        req,
                                 neu_req_driver_directory_t *cmd)
{
    if (driver->adapter.state != NEU_NODE_RUNNING_STATE_RUNNING) {
        return NEU_ERR_PLUGIN_NOT_RUNNING;
    }
    if (driver->adapter.module->intf_funs->driver.directory == NULL) {
        return NEU_ERR_PLUGIN_NOT_SUPPORT_DIRECTORY;
    }
    return driver->adapter.module->intf_funs->driver.directory(
        driver->adapter.plugin, (void *) req, cmd->path);
}

int neu_adapter_driver_fup_open(neu_adapter_driver_t *driver,
                                neu_reqresp_head_t *req, const char *path)
{
    if (driver->adapter.state != NEU_NODE_RUNNING_STATE_RUNNING) {
        return NEU_ERR_PLUGIN_NOT_RUNNING;
    }
    if (driver->adapter.module->intf_funs->driver.fup_open == NULL) {
        return NEU_ERR_PLUGIN_NOT_SUPPORT_FUP_OPEN;
    }
    return driver->adapter.module->intf_funs->driver.fup_open(
        driver->adapter.plugin, (void *) req, path);
}

int neu_adapter_driver_fdown_open(neu_adapter_driver_t *driver,
                                  neu_reqresp_head_t *req, const char *node,
                                  const char *src_path, const char *dst_path,
                                  int64_t size)
{
    if (driver->adapter.state != NEU_NODE_RUNNING_STATE_RUNNING) {
        return NEU_ERR_PLUGIN_NOT_RUNNING;
    }
    if (driver->adapter.module->intf_funs->driver.fdown_open == NULL) {
        return NEU_ERR_PLUGIN_NOT_SUPPORT_FDOWN_OPEN;
    }
    return driver->adapter.module->intf_funs->driver.fdown_open(
        driver->adapter.plugin, (void *) req, node, src_path, dst_path, size);
}

int neu_adapter_driver_fup_data(neu_adapter_driver_t *driver,
                                neu_reqresp_head_t *req, const char *path)
{
    if (driver->adapter.state != NEU_NODE_RUNNING_STATE_RUNNING) {
        return NEU_ERR_PLUGIN_NOT_RUNNING;
    }
    if (driver->adapter.module->intf_funs->driver.fup_data == NULL) {
        return NEU_ERR_PLUGIN_NOT_SUPPORT_FUP_DATA;
    }
    return driver->adapter.module->intf_funs->driver.fup_data(
        driver->adapter.plugin, (void *) req, path);
}

int neu_adapter_driver_fdown_data(neu_adapter_driver_t *driver,
                                  neu_reqresp_head_t *req, uint8_t *data,
                                  uint16_t len, bool more)
{
    if (driver->adapter.state != NEU_NODE_RUNNING_STATE_RUNNING) {
        return NEU_ERR_PLUGIN_NOT_RUNNING;
    }
    if (driver->adapter.module->intf_funs->driver.fdown_data == NULL) {
        return NEU_ERR_PLUGIN_NOT_SUPPORT_FDOWN_DATA;
    }
    return driver->adapter.module->intf_funs->driver.fdown_data(
        driver->adapter.plugin, (void *) req, data, len, more);
}