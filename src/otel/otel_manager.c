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

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "define.h"
#include "event/event.h"
#include "metrics.h"
#include "parser/neu_json_otel.h"
#include "plugin.h"
#include "utils/http.h"
#include "utils/log.h"
#include "utils/time.h"
#include "utils/utarray.h"
#include "utils/uthash.h"

#include "otel/otel_manager.h"
#include "trace.pb-c.h"

#define SPAN_ID_LENGTH 16
#define ID_CHARSET "0123456789abcdef"
#define TRACE_TIME_OUT (3 * 60 * 1000)

bool   otel_flag               = false;
char   otel_collector_url[128] = { 0 };
bool   otel_control_flag       = false;
bool   otel_data_flag          = false;
double otel_data_sample_rate   = 0.0;
char   otel_service_name[128]  = { 0 };

/**
 * @brief 定义用于存储OpenTelemetry追踪上下文信息的结构体。
 *
 * trace_ctx_t 结构体包含了一系列字段来存储追踪相关的上下文信息，
 * 包括追踪数据、追踪ID、标志位、是否为最终状态、Span的数量、
 * 期望的Span数量、时间戳、作用域数组以及一个互斥锁。
 */
typedef struct {
    /**
     * @brief OpenTelemetry追踪数据。
     *
     * 使用了 Opentelemetry__Proto__Trace__V1__TracesData 类型来存储具体的追踪数据，
     * 该类型可能是通过 Protocol Buffers 或其他序列化工具生成的。
     */
    Opentelemetry__Proto__Trace__V1__TracesData trace_data;

    /**
     * @brief 追踪ID。
     *
     * 一个长度为64字节的数组，用于存储追踪ID。通常情况下，追踪ID是一个唯一的标识符，
     * 用于在整个追踪系统中识别特定的追踪。
     */
    uint8_t                                     trace_id[64];

    /**
     * @brief 标志位。
     *
     * 一个32位无符号整数，可能用于存储与追踪相关的各种标志或状态信息。
     */
    uint32_t                                    flags;

    /**
     * @brief 是否为最终状态。
     *
     * 布尔值，表示当前追踪上下文是否处于最终状态（即不再会有新的Span添加）。
     */
    bool                                        final;

    /**
     * @brief 当前Span的数量。
     *
     * 一个 size_t 类型的变量，用于记录当前追踪上下文中已经存在的Span数量。
     */
    size_t                                      span_num;

    /**
     * @brief 期望的Span数量。
     *
     * 一个32位有符号整数，用于指定期望在这个追踪上下文中创建的Span总数。
     */
    int32_t                                     expected_span_num;

    /**
     * @brief 时间戳。
     *
     * 一个64位有符号整数，用于记录追踪上下文的时间戳，通常以纳秒或毫秒为单位。
     */
    int64_t                                     ts;

    /**
     * @brief 作用域数组。
     *
     * 使用 UT_array 类型的指针，用于存储一系列的作用域（scopes）。
     */
    UT_array *                                  scopes;

    /**
     * @brief 互斥锁。
     *
     * 用于保护对追踪上下文的操作，确保在多线程环境下访问共享资源时的线程安全性。
     */
    pthread_mutex_t                             mutex;
} trace_ctx_t;

/**
 * @brief  一个包含跨度相关信息的上下文范围。
 * 
 * 该结构体用于封装与 OpenTelemetry 跨度相关的上下文信息，
 * 包括跨度在跟踪数据中的索引、跨度对象本身以及所属的跟踪上下文。
 */
typedef struct {
    int                                    span_index;
    Opentelemetry__Proto__Trace__V1__Span *span;
    trace_ctx_t *                          trace_ctx;
} trace_scope_t;

/**
 * @brief 定义用于存储追踪上下文表元素的结构体。
 *
 * trace_ctx_table_ele_t 结构体用于在哈希表中存储每个
 * 请求上下文（key）及其对应的追踪上下文（ctx）。
 * hh 是 uthash 库使用的特殊字段，用于管理哈希表中的元素。
 */
typedef struct {
    void *         key;  ///< 请求上下文指针，作为哈希表中的键值。
    trace_ctx_t *  ctx;  ///< 指向追踪上下文的指针，存储实际的追踪信息。
    UT_hash_handle hh;   ///< uthash 库使用的字段key 管理哈希表中的元素。
} trace_ctx_table_ele_t;

typedef struct {
    char key[128];
    char value[256];
} trace_kv_t;

trace_ctx_table_ele_t *traces_table = NULL;

pthread_mutex_t table_mutex = PTHREAD_MUTEX_INITIALIZER;

neu_events_t *     otel_event = NULL;
neu_event_timer_t *otel_timer = NULL;

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

/**
 * @brief 将十六进制字符串转换为二进制数组。
 *
 * 此函数用于将给定的十六进制字符串转换为相应的二进制数据，并存储在提供的二进制数组中。
 * 它会检查输入的有效性，如长度是否为偶数、是否为空等，并确保生成的二进制数据不会超过指定的最大长度。
 *
 * @param hex_string 输入的十六进制字符串。
 * @param binary_array 用于存储转换后的二进制数据的数组。
 * @param max_length binary_array的最大长度。
 * @return 返回转换后的二进制数据的实际长度；如果输入无效，则返回-1。
 */
static int hex_string_to_binary(const char *   hex_string,
                                unsigned char *binary_array, int max_length)
{
    if (hex_string == NULL) {
        return -1;
    }
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

static int parse_tracestate(const char *tracestate, trace_kv_t *kvs,
                            int kvs_len, int *count)
{
    if (!tracestate) {
        return -1;
    }
    const char *delim = ",";
    char *      token;
    char *      saveptr;
    char *      tracestate_copy = strdup(tracestate);
    if (!tracestate_copy) {
        return -1;
    }

    *count = 0;
    token  = strtok_r(tracestate_copy, delim, &saveptr);

    while (token != NULL && *count < kvs_len) {
        char *equal_sign = strchr(token, '=');
        if (equal_sign == NULL) {
            free(tracestate_copy);
            return -1;
        }

        *equal_sign = '\0';
        strncpy(kvs[*count].key, token, 127);
        kvs[*count].key[127] = '\0';

        strncpy(kvs[*count].value, equal_sign + 1, 255);
        kvs[*count].value[255] = '\0';

        (*count)++;
        token = strtok_r(NULL, delim, &saveptr);
    }

    free(tracestate_copy);
    return 1;
}

void neu_otel_free_span(Opentelemetry__Proto__Trace__V1__Span *span)
{
    for (size_t i = 0; i < span->n_attributes; i++) {
        if (span->attributes[i]->value->value_case ==
            OPENTELEMETRY__PROTO__COMMON__V1__ANY_VALUE__VALUE_STRING_VALUE) {
            free(span->attributes[i]->value->string_value);
        }
        free(span->attributes[i]->key);
        free(span->attributes[i]->value);
        free(span->attributes[i]);
    }
    if (span->status) {
        if (span->status->message) {
            free(span->status->message);
        }
        free(span->status);
    }
    free(span->attributes);
    free(span->name);
    free(span->trace_id.data);
    free(span->parent_span_id.data);
    free(span->span_id.data);
    free(span);
}

neu_otel_trace_ctx neu_otel_create_trace(const char *trace_id, void *req_ctx,
                                         uint32_t flags, const char *tracestate)
{
    trace_ctx_t *ctx = calloc(1, sizeof(trace_ctx_t));
    opentelemetry__proto__trace__v1__traces_data__init(&ctx->trace_data);
    strncpy((char *) ctx->trace_id, trace_id, 64);
    ctx->expected_span_num = 0;

    ctx->trace_data.n_resource_spans = 1;
    ctx->trace_data.resource_spans =
        calloc(1, sizeof(Opentelemetry__Proto__Trace__V1__ResourceSpans *));
    ctx->trace_data.resource_spans[0] =
        calloc(1, sizeof(Opentelemetry__Proto__Trace__V1__ResourceSpans));
    opentelemetry__proto__trace__v1__resource_spans__init(
        ctx->trace_data.resource_spans[0]);

    ctx->trace_data.resource_spans[0]->resource =
        calloc(1, sizeof(Opentelemetry__Proto__Resource__V1__Resource));
    opentelemetry__proto__resource__v1__resource__init(
        ctx->trace_data.resource_spans[0]->resource);

    trace_kv_t tracestate_kvs[64] = { 0 };
    int        count              = 0;
    if (parse_tracestate(tracestate, tracestate_kvs, 64, &count)) {
        ctx->trace_data.resource_spans[0]->resource->n_attributes = 8 + count;
        ctx->trace_data.resource_spans[0]->resource->attributes   = calloc(
            8 + count, sizeof(Opentelemetry__Proto__Common__V1__KeyValue *));
        for (int i = 8; i < count + 8; i++) {
            ctx->trace_data.resource_spans[0]->resource->attributes[i] =
                calloc(1, sizeof(Opentelemetry__Proto__Common__V1__KeyValue));

            opentelemetry__proto__common__v1__key_value__init(
                ctx->trace_data.resource_spans[0]->resource->attributes[i]);

            ctx->trace_data.resource_spans[0]->resource->attributes[i]->key =
                strdup(tracestate_kvs[i - 8].key);

            ctx->trace_data.resource_spans[0]->resource->attributes[i]->value =
                calloc(1, sizeof(Opentelemetry__Proto__Common__V1__AnyValue));
            opentelemetry__proto__common__v1__any_value__init(
                ctx->trace_data.resource_spans[0]
                    ->resource->attributes[i]
                    ->value);
            ctx->trace_data.resource_spans[0]
                ->resource->attributes[i]
                ->value->value_case =
                OPENTELEMETRY__PROTO__COMMON__V1__ANY_VALUE__VALUE_STRING_VALUE;
            ctx->trace_data.resource_spans[0]
                ->resource->attributes[i]
                ->value->string_value = strdup(tracestate_kvs[i - 8].value);
        }
    } else {
        ctx->trace_data.resource_spans[0]->resource->n_attributes = 8;
        ctx->trace_data.resource_spans[0]->resource->attributes =
            calloc(8, sizeof(Opentelemetry__Proto__Common__V1__KeyValue *));
    }

    // 0
    ctx->trace_data.resource_spans[0]->resource->attributes[0] =
        calloc(1, sizeof(Opentelemetry__Proto__Common__V1__KeyValue));

    opentelemetry__proto__common__v1__key_value__init(
        ctx->trace_data.resource_spans[0]->resource->attributes[0]);

    ctx->trace_data.resource_spans[0]->resource->attributes[0]->key =
        strdup("app.name");

    ctx->trace_data.resource_spans[0]->resource->attributes[0]->value =
        calloc(1, sizeof(Opentelemetry__Proto__Common__V1__AnyValue));
    opentelemetry__proto__common__v1__any_value__init(
        ctx->trace_data.resource_spans[0]->resource->attributes[0]->value);
    ctx->trace_data.resource_spans[0]
        ->resource->attributes[0]
        ->value->value_case =
        OPENTELEMETRY__PROTO__COMMON__V1__ANY_VALUE__VALUE_STRING_VALUE;
    ctx->trace_data.resource_spans[0]
        ->resource->attributes[0]
        ->value->string_value = strdup("neuron");

    // 1
    ctx->trace_data.resource_spans[0]->resource->attributes[1] =
        calloc(1, sizeof(Opentelemetry__Proto__Common__V1__KeyValue));

    opentelemetry__proto__common__v1__key_value__init(
        ctx->trace_data.resource_spans[0]->resource->attributes[1]);

    ctx->trace_data.resource_spans[0]->resource->attributes[1]->key =
        strdup("app.version");

    ctx->trace_data.resource_spans[0]->resource->attributes[1]->value =
        calloc(1, sizeof(Opentelemetry__Proto__Common__V1__AnyValue));
    opentelemetry__proto__common__v1__any_value__init(
        ctx->trace_data.resource_spans[0]->resource->attributes[1]->value);
    char *version = calloc(1, 24);
    sprintf(version, "%d.%d.%d", NEU_VERSION_MAJOR, NEU_VERSION_MINOR,
            NEU_VERSION_FIX);
    ctx->trace_data.resource_spans[0]
        ->resource->attributes[1]
        ->value->value_case =
        OPENTELEMETRY__PROTO__COMMON__V1__ANY_VALUE__VALUE_STRING_VALUE;
    ctx->trace_data.resource_spans[0]
        ->resource->attributes[1]
        ->value->string_value = version;

    // 2
    ctx->trace_data.resource_spans[0]->resource->attributes[2] =
        calloc(1, sizeof(Opentelemetry__Proto__Common__V1__KeyValue));

    opentelemetry__proto__common__v1__key_value__init(
        ctx->trace_data.resource_spans[0]->resource->attributes[2]);

    ctx->trace_data.resource_spans[0]->resource->attributes[2]->key =
        strdup("distro");

    ctx->trace_data.resource_spans[0]->resource->attributes[2]->value =
        calloc(1, sizeof(Opentelemetry__Proto__Common__V1__AnyValue));
    opentelemetry__proto__common__v1__any_value__init(
        ctx->trace_data.resource_spans[0]->resource->attributes[2]->value);
    ctx->trace_data.resource_spans[0]
        ->resource->attributes[2]
        ->value->value_case =
        OPENTELEMETRY__PROTO__COMMON__V1__ANY_VALUE__VALUE_STRING_VALUE;
    ctx->trace_data.resource_spans[0]
        ->resource->attributes[2]
        ->value->string_value = strdup(neu_get_global_metrics()->distro);

    // 3
    ctx->trace_data.resource_spans[0]->resource->attributes[3] =
        calloc(1, sizeof(Opentelemetry__Proto__Common__V1__KeyValue));

    opentelemetry__proto__common__v1__key_value__init(
        ctx->trace_data.resource_spans[0]->resource->attributes[3]);

    ctx->trace_data.resource_spans[0]->resource->attributes[3]->key =
        strdup("kernel");

    ctx->trace_data.resource_spans[0]->resource->attributes[3]->value =
        calloc(1, sizeof(Opentelemetry__Proto__Common__V1__AnyValue));
    opentelemetry__proto__common__v1__any_value__init(
        ctx->trace_data.resource_spans[0]->resource->attributes[3]->value);
    ctx->trace_data.resource_spans[0]
        ->resource->attributes[3]
        ->value->value_case =
        OPENTELEMETRY__PROTO__COMMON__V1__ANY_VALUE__VALUE_STRING_VALUE;
    ctx->trace_data.resource_spans[0]
        ->resource->attributes[3]
        ->value->string_value = strdup(neu_get_global_metrics()->kernel);

    // 4
    ctx->trace_data.resource_spans[0]->resource->attributes[4] =
        calloc(1, sizeof(Opentelemetry__Proto__Common__V1__KeyValue));

    opentelemetry__proto__common__v1__key_value__init(
        ctx->trace_data.resource_spans[0]->resource->attributes[4]);

    ctx->trace_data.resource_spans[0]->resource->attributes[4]->key =
        strdup("machine");

    ctx->trace_data.resource_spans[0]->resource->attributes[4]->value =
        calloc(1, sizeof(Opentelemetry__Proto__Common__V1__AnyValue));
    opentelemetry__proto__common__v1__any_value__init(
        ctx->trace_data.resource_spans[0]->resource->attributes[4]->value);
    ctx->trace_data.resource_spans[0]
        ->resource->attributes[4]
        ->value->value_case =
        OPENTELEMETRY__PROTO__COMMON__V1__ANY_VALUE__VALUE_STRING_VALUE;
    ctx->trace_data.resource_spans[0]
        ->resource->attributes[4]
        ->value->string_value = strdup(neu_get_global_metrics()->machine);

    // 5
    ctx->trace_data.resource_spans[0]->resource->attributes[5] =
        calloc(1, sizeof(Opentelemetry__Proto__Common__V1__KeyValue));

    opentelemetry__proto__common__v1__key_value__init(
        ctx->trace_data.resource_spans[0]->resource->attributes[5]);

    ctx->trace_data.resource_spans[0]->resource->attributes[5]->key =
        strdup("clib");

    ctx->trace_data.resource_spans[0]->resource->attributes[5]->value =
        calloc(1, sizeof(Opentelemetry__Proto__Common__V1__AnyValue));
    opentelemetry__proto__common__v1__any_value__init(
        ctx->trace_data.resource_spans[0]->resource->attributes[5]->value);
    ctx->trace_data.resource_spans[0]
        ->resource->attributes[5]
        ->value->value_case =
        OPENTELEMETRY__PROTO__COMMON__V1__ANY_VALUE__VALUE_STRING_VALUE;
    ctx->trace_data.resource_spans[0]
        ->resource->attributes[5]
        ->value->string_value = strdup(neu_get_global_metrics()->clib);

    // 6
    ctx->trace_data.resource_spans[0]->resource->attributes[6] =
        calloc(1, sizeof(Opentelemetry__Proto__Common__V1__KeyValue));

    opentelemetry__proto__common__v1__key_value__init(
        ctx->trace_data.resource_spans[0]->resource->attributes[6]);

    ctx->trace_data.resource_spans[0]->resource->attributes[6]->key =
        strdup("clib_version");

    ctx->trace_data.resource_spans[0]->resource->attributes[6]->value =
        calloc(1, sizeof(Opentelemetry__Proto__Common__V1__AnyValue));
    opentelemetry__proto__common__v1__any_value__init(
        ctx->trace_data.resource_spans[0]->resource->attributes[6]->value);
    ctx->trace_data.resource_spans[0]
        ->resource->attributes[6]
        ->value->value_case =
        OPENTELEMETRY__PROTO__COMMON__V1__ANY_VALUE__VALUE_STRING_VALUE;
    ctx->trace_data.resource_spans[0]
        ->resource->attributes[6]
        ->value->string_value = strdup(neu_get_global_metrics()->clib_version);

    // 7
    ctx->trace_data.resource_spans[0]->resource->attributes[7] =
        calloc(1, sizeof(Opentelemetry__Proto__Common__V1__KeyValue));

    opentelemetry__proto__common__v1__key_value__init(
        ctx->trace_data.resource_spans[0]->resource->attributes[7]);

    ctx->trace_data.resource_spans[0]->resource->attributes[7]->key =
        strdup("service.name");

    ctx->trace_data.resource_spans[0]->resource->attributes[7]->value =
        calloc(1, sizeof(Opentelemetry__Proto__Common__V1__AnyValue));
    opentelemetry__proto__common__v1__any_value__init(
        ctx->trace_data.resource_spans[0]->resource->attributes[7]->value);
    ctx->trace_data.resource_spans[0]
        ->resource->attributes[7]
        ->value->value_case =
        OPENTELEMETRY__PROTO__COMMON__V1__ANY_VALUE__VALUE_STRING_VALUE;
    ctx->trace_data.resource_spans[0]
        ->resource->attributes[7]
        ->value->string_value = strdup(neu_otel_service_name());

    //
    ctx->trace_data.resource_spans[0]->n_scope_spans = 1;
    ctx->trace_data.resource_spans[0]->scope_spans =
        calloc(1, sizeof(Opentelemetry__Proto__Trace__V1__ScopeSpans *));
    ctx->trace_data.resource_spans[0]->scope_spans[0] =
        calloc(1, sizeof(Opentelemetry__Proto__Trace__V1__ScopeSpans));
    opentelemetry__proto__trace__v1__scope_spans__init(
        ctx->trace_data.resource_spans[0]->scope_spans[0]);

    ctx->flags = flags;

    ctx->ts = neu_time_ms();

    trace_ctx_table_ele_t *ele = calloc(1, sizeof(trace_ctx_table_ele_t));
    ele->key                   = req_ctx;
    ele->ctx                   = ctx;

    utarray_new(ctx->scopes, &ut_ptr_icd);

    pthread_mutex_lock(&table_mutex);

    HASH_ADD(hh, traces_table, key, sizeof(req_ctx), ele);

    pthread_mutex_unlock(&table_mutex);

    return (neu_otel_trace_ctx) ctx;
}

/**
 * @brief 根据请求上下文查找对应的OpenTelemetry追踪上下文。
 *
 * 该函数在线程安全的方式下，在追踪上下文表中查找与给定请求上下文相关联的追踪上下文。
 * 使用哈希表进行快速查找，并通过互斥锁保护对共享资源的访问。
 *
 * @param req_ctx 请求上下文指针，用于标识特定请求。
 * @return neu_otel_trace_ctx 如果找到，则返回相应的追踪上下文；否则返回NULL。
 */
neu_otel_trace_ctx neu_otel_find_trace(void *req_ctx)
{
    // 用于存放查找结果的指针
    trace_ctx_table_ele_t *find = NULL;

    pthread_mutex_lock(&table_mutex);

    HASH_FIND(hh, traces_table, &req_ctx, sizeof(req_ctx), find);

    pthread_mutex_unlock(&table_mutex);

    if (find) {
        return (void *) find->ctx;
    } else {
        return NULL;
    }
}

void neu_otel_trace_set_final(neu_otel_trace_ctx ctx)
{
    trace_ctx_t *trace_ctx = (trace_ctx_t *) ctx;
    trace_ctx->final       = true;
}

void neu_otel_trace_set_expected_span_num(neu_otel_trace_ctx ctx, uint32_t num)
{
    trace_ctx_t *trace_ctx       = (trace_ctx_t *) ctx;
    trace_ctx->expected_span_num = num;
}

uint8_t *neu_otel_get_trace_id(neu_otel_trace_ctx ctx)
{
    trace_ctx_t *trace_ctx = (trace_ctx_t *) ctx;
    pthread_mutex_lock(&trace_ctx->mutex);
    uint8_t *id = trace_ctx->trace_data.resource_spans[0]
                      ->scope_spans[0]
                      ->spans[0]
                      ->trace_id.data;
    pthread_mutex_unlock(&trace_ctx->mutex);
    return id;
}

neu_otel_trace_ctx neu_otel_find_trace_by_id(const char *trace_id)
{
    trace_ctx_table_ele_t *el = NULL, *tmp = NULL;

    pthread_mutex_lock(&table_mutex);

    HASH_ITER(hh, traces_table, el, tmp)
    {
        if (strcmp((char *) el->ctx->trace_id, trace_id) == 0) {
            pthread_mutex_unlock(&table_mutex);
            return (void *) el->ctx;
        }
    }

    pthread_mutex_unlock(&table_mutex);

    return NULL;
}

void neu_otel_free_trace(neu_otel_trace_ctx ctx)
{
    trace_ctx_t *trace_ctx = (trace_ctx_t *) ctx;
    for (size_t i = 0;
         i < trace_ctx->trace_data.resource_spans[0]->scope_spans[0]->n_spans;
         i++) {
        neu_otel_free_span(
            trace_ctx->trace_data.resource_spans[0]->scope_spans[0]->spans[i]);
    }
    free(trace_ctx->trace_data.resource_spans[0]->scope_spans[0]->spans);
    free(trace_ctx->trace_data.resource_spans[0]->scope_spans[0]);
    free(trace_ctx->trace_data.resource_spans[0]->scope_spans);
    for (size_t i = 0;
         i < trace_ctx->trace_data.resource_spans[0]->resource->n_attributes;
         i++) {
        if (trace_ctx->trace_data.resource_spans[0]
                ->resource->attributes[i]
                ->value->value_case ==
            OPENTELEMETRY__PROTO__COMMON__V1__ANY_VALUE__VALUE_STRING_VALUE) {
            free(trace_ctx->trace_data.resource_spans[0]
                     ->resource->attributes[i]
                     ->value->string_value);
        }
        free(trace_ctx->trace_data.resource_spans[0]
                 ->resource->attributes[i]
                 ->value);
        free(trace_ctx->trace_data.resource_spans[0]
                 ->resource->attributes[i]
                 ->key);
        free(trace_ctx->trace_data.resource_spans[0]->resource->attributes[i]);
    }
    free(trace_ctx->trace_data.resource_spans[0]->resource->attributes);
    free(trace_ctx->trace_data.resource_spans[0]->resource);
    free(trace_ctx->trace_data.resource_spans[0]);
    free(trace_ctx->trace_data.resource_spans);
    utarray_foreach(trace_ctx->scopes, void **, e) { free(*e); }
    utarray_free(trace_ctx->scopes);
    free(ctx);
}

/**
 * @brief 添加一个新的Span到OpenTelemetry跟踪上下文中。
 *
 * 此函数用于在给定的跟踪上下文中添加一个新的Span，并初始化该Span的基本属性。
 * 它支持动态调整Spans数组大小以适应新Span的添加。
 *
 * @param ctx OpenTelemetry跟踪上下文。
 * @return 返回包含新Span的scope上下文。
 */
neu_otel_scope_ctx neu_otel_add_span(neu_otel_trace_ctx ctx)
{
    // 分配新的scope结构体内存
    trace_scope_t *scope     = calloc(1, sizeof(trace_scope_t));
    trace_ctx_t *  trace_ctx = (trace_ctx_t *) ctx;
    
    pthread_mutex_lock(&trace_ctx->mutex);
    
    scope->trace_ctx = ctx;

    // 如果当前没有Span，则初始化第一个Span
    if (trace_ctx->trace_data.resource_spans[0]->scope_spans[0]->n_spans == 0) {
        // 动态分配Spans数组
        trace_ctx->trace_data.resource_spans[0]->scope_spans[0]->spans =
            calloc(1, sizeof(Opentelemetry__Proto__Trace__V1__Span *));

        // 初始化第一个Span
        trace_ctx->trace_data.resource_spans[0]->scope_spans[0]->spans[0] =
            calloc(1, sizeof(Opentelemetry__Proto__Trace__V1__Span));

        trace_ctx->trace_data.resource_spans[0]->scope_spans[0]->n_spans = 1;
        scope->span =
            trace_ctx->trace_data.resource_spans[0]->scope_spans[0]->spans[0];
        scope->span_index = 0;
    } else {
        // 如果已有Span，调整Spans数组大小并复制原有Span数据
        Opentelemetry__Proto__Trace__V1__Span **t_spans =
            trace_ctx->trace_data.resource_spans[0]->scope_spans[0]->spans;

        // 动态调整Spans数组大小
        trace_ctx->trace_data.resource_spans[0]->scope_spans[0]->spans =
            calloc(1 +
                       trace_ctx->trace_data.resource_spans[0]
                           ->scope_spans[0]
                           ->n_spans,
                   sizeof(Opentelemetry__Proto__Trace__V1__Span *));

        // 复制原有Span数据
        for (size_t i = 0; i <
             trace_ctx->trace_data.resource_spans[0]->scope_spans[0]->n_spans;
             i++) {
            trace_ctx->trace_data.resource_spans[0]->scope_spans[0]->spans[i] =
                t_spans[i];
        }

        free(t_spans);

        // 分配并初始化新Span
        trace_ctx->trace_data.resource_spans[0]->scope_spans[0]->spans
            [trace_ctx->trace_data.resource_spans[0]->scope_spans[0]->n_spans] =
            calloc(1, sizeof(Opentelemetry__Proto__Trace__V1__Span));
        scope->span = trace_ctx->trace_data.resource_spans[0]
                          ->scope_spans[0]
                          ->spans[trace_ctx->trace_data.resource_spans[0]
                                      ->scope_spans[0]
                                      ->n_spans];
        trace_ctx->trace_data.resource_spans[0]->scope_spans[0]->n_spans += 1;

        scope->span_index =
            trace_ctx->trace_data.resource_spans[0]->scope_spans[0]->n_spans -
            1;
    }

    // 初始化新Span
    opentelemetry__proto__trace__v1__span__init(scope->span);
    scope->span->kind =
        OPENTELEMETRY__PROTO__TRACE__V1__SPAN__SPAN_KIND__SPAN_KIND_SERVER;

    // 设置Trace ID
    uint8_t *t_id = calloc(1, 16);
    hex_string_to_binary((char *) trace_ctx->trace_id, t_id, 16);
    scope->span->trace_id.data = t_id;
    scope->span->trace_id.len  = 16;
    scope->span->flags         = trace_ctx->flags;

    // 将新scope添加到scopes列表中
    utarray_push_back(trace_ctx->scopes, &scope);

    pthread_mutex_unlock(&trace_ctx->mutex);
    return scope;
}

/**
 * @brief 在给定的 OpenTelemetry 跟踪上下文中添加一个新的跨度（span）。
 *
 * 该函数会在指定的跟踪上下文中创建一个新的跨度，并将其添加到跟踪数据的跨度数组中。
 * 它会初始化跨度的基本属性，如跟踪 ID、跨度 ID、父跨度 ID 等，并将新的跨度上下
 * 文添加到跟踪上下文的范围数组中。
 *
 * @param ctx 指向 OpenTelemetry 跟踪上下文的指针，用于指定要添加跨度的跟踪。
 * @param span_name 新跨度的名称，用于标识该跨度所代表的操作或任务。
 * @param span_id 新跨度的 ID，用于唯一标识该跨度。
 * @return neu_otel_scope_ctx 指向新创建的跨度上下文的指针，如果创建失败则返回 NULL。
 * 
 * @note
 * 
 * 在 OpenTelemetry 的跟踪系统中，一个跟踪（trace）可以包含多个跨度（span），
 * 这些跨度会被存储在一个数组中。在这个系统中，span 是按照顺序添加到跟踪数据中的。
 * 也就是说，新的 span 总是在已有的 span 之后添加。因此，当一个新的 span 被创建时，
 * 它的父 span 通常是前一个添加的 span。
 */
neu_otel_scope_ctx neu_otel_add_span2(neu_otel_trace_ctx ctx,
                                      const char *       span_name,
                                      const char *       span_id)
{
    trace_scope_t *scope     = calloc(1, sizeof(trace_scope_t));
    trace_ctx_t *  trace_ctx = (trace_ctx_t *) ctx;
    
    pthread_mutex_lock(&trace_ctx->mutex);
    
    scope->trace_ctx = ctx;

    // 如果当前跟踪数据中的跨度数组为空
    if (trace_ctx->trace_data.resource_spans[0]->scope_spans[0]->n_spans == 0) {
        // 分配内存用于存储跨度数组
        trace_ctx->trace_data.resource_spans[0]->scope_spans[0]->spans =
            calloc(1, sizeof(Opentelemetry__Proto__Trace__V1__Span *));

        // 分配内存用于存储第一个跨度
        trace_ctx->trace_data.resource_spans[0]->scope_spans[0]->spans[0] =
            calloc(1, sizeof(Opentelemetry__Proto__Trace__V1__Span));

        // 设置跨度数组的长度为 1
        trace_ctx->trace_data.resource_spans[0]->scope_spans[0]->n_spans = 1;

        // 将新创建的跨度赋值给跨度上下文
        scope->span =
            trace_ctx->trace_data.resource_spans[0]->scope_spans[0]->spans[0];
        
        // 设置跨度的索引为 0
        scope->span_index = 0;
    } else {
        // 保存当前的跨度数组指针
        Opentelemetry__Proto__Trace__V1__Span **t_spans =
            trace_ctx->trace_data.resource_spans[0]->scope_spans[0]->spans;
        
        // 分配内存用于存储新的跨度数组，长度比原来多 1
        trace_ctx->trace_data.resource_spans[0]->scope_spans[0]->spans =
            calloc(1 +
                       trace_ctx->trace_data.resource_spans[0]
                           ->scope_spans[0]
                           ->n_spans,
                   sizeof(Opentelemetry__Proto__Trace__V1__Span *));
        
        // 将原来的跨度数组复制到新的数组中
        for (size_t i = 0; i <
             trace_ctx->trace_data.resource_spans[0]->scope_spans[0]->n_spans;
             i++) {
            trace_ctx->trace_data.resource_spans[0]->scope_spans[0]->spans[i] =
                t_spans[i];
        }

        // 释放原来的跨度数组内存
        free(t_spans);

         // 分配内存用于存储新的跨度
        trace_ctx->trace_data.resource_spans[0]->scope_spans[0]->spans
            [trace_ctx->trace_data.resource_spans[0]->scope_spans[0]->n_spans] =
            calloc(1, sizeof(Opentelemetry__Proto__Trace__V1__Span));
        
        // 将新创建的跨度赋值给跨度上下文
        scope->span = trace_ctx->trace_data.resource_spans[0]
                          ->scope_spans[0]
                          ->spans[trace_ctx->trace_data.resource_spans[0]
                                      ->scope_spans[0]
                                      ->n_spans];
        
        // 增加跨度数组的长度
        trace_ctx->trace_data.resource_spans[0]->scope_spans[0]->n_spans += 1;

        // 设置跨度的索引为新的长度减 1
        scope->span_index =
            trace_ctx->trace_data.resource_spans[0]->scope_spans[0]->n_spans -
            1;
    }
    
    // 初始化新跨度的基本属性
    opentelemetry__proto__trace__v1__span__init(scope->span);
    
    // 设置跨度的类型为服务器跨度
    scope->span->kind =
        OPENTELEMETRY__PROTO__TRACE__V1__SPAN__SPAN_KIND__SPAN_KIND_SERVER;
    uint8_t *t_id = calloc(1, 16);
    hex_string_to_binary((char *) trace_ctx->trace_id, t_id, 16);   
    // 设置跨度的跟踪 ID
    scope->span->trace_id.data = t_id;
    scope->span->trace_id.len  = 16;
    // 设置跨度的标志位
    scope->span->flags         = trace_ctx->flags;
    // 设置跨度的名称
    neu_otel_scope_set_span_name(scope, span_name);
    // 设置跨度的 ID
    neu_otel_scope_set_span_id(scope, span_id);
    // 如果不是第一个跨度，设置父跨度 ID
    if (scope->span_index != 0) {
        neu_otel_scope_set_parent_span_id2(
            scope,
            trace_ctx->trace_data.resource_spans[0]
                ->scope_spans[0]
                ->spans[scope->span_index - 1]
                ->span_id.data,
            trace_ctx->trace_data.resource_spans[0]
                ->scope_spans[0]
                ->spans[scope->span_index - 1]
                ->span_id.len);
    }
    // 将新的跨度上下文添加到跟踪上下文的范围数组中
    utarray_push_back(trace_ctx->scopes, &scope);
    pthread_mutex_unlock(&trace_ctx->mutex);
    return scope;
}

void neu_otel_scope_set_parent_span_id(neu_otel_scope_ctx ctx,
                                       const char *       parent_span_id)
{
    trace_scope_t *scope   = (trace_scope_t *) ctx;
    uint8_t *      p_sp_id = calloc(1, 8);
    if (hex_string_to_binary(parent_span_id, p_sp_id, 8) > 0) {
        scope->span->parent_span_id.len  = 8;
        scope->span->parent_span_id.data = p_sp_id;
    } else {
        scope->span->parent_span_id.len  = 0;
        scope->span->parent_span_id.data = NULL;
        free(p_sp_id);
    }
}

/**
 * @brief 设置给定跟踪范围上下文的父Span ID。
 *
 * 此函数用于设置指定跟踪范围上下文的父Span ID。它首先分配内存以存储父Span ID，
 * 然后使用`memcpy`将提供的父Span ID数据复制到新分配的内存中，并更新上下文中的父Span ID字段。
 *
 * @param ctx 跟踪范围上下文。
 * @param parent_span_id 父Span ID的二进制数据。
 * @param len 父Span ID的长度（字节数）。
 */
void neu_otel_scope_set_parent_span_id2(neu_otel_scope_ctx ctx,
                                        uint8_t *parent_span_id, int len)
{
    trace_scope_t *scope = (trace_scope_t *) ctx;

    uint8_t *p_sp_id = calloc(1, 8);
    memcpy(p_sp_id, parent_span_id, len);

    // 设置Span的parent_span_id字段为新分配并填充好的内存
    scope->span->parent_span_id.len  = len;
    scope->span->parent_span_id.data = p_sp_id;
}

void neu_otel_scope_set_span_name(neu_otel_scope_ctx ctx, const char *span_name)
{
    trace_scope_t *scope = (trace_scope_t *) ctx;
    scope->span->name    = strdup(span_name);
}

/**
 * @brief 设置给定跟踪范围上下文的Span ID。
 *
 * 此函数用于将提供的十六进制字符串形式的Span ID转换为二进制格式，并将其设置到指定的跟踪范围上下文中。
 * 它首先分配内存以存储二进制形式的Span ID，然后使用`hex_string_to_binary`函数进行转换，
 * 并更新上下文中的Span ID。
 *
 * @param ctx 跟踪范围上下文。
 * @param span_id 十六进制字符串形式的Span ID。
 */
void neu_otel_scope_set_span_id(neu_otel_scope_ctx ctx, const char *span_id)
{
    // 将上下文转换为trace_scope_t类型指针
    trace_scope_t *scope = (trace_scope_t *) ctx;

    // 分配8字节的内存用于存储二进制形式的Span ID
    uint8_t *      sp_id = calloc(1, 8);

    // 使用hex_string_to_binary函数将十六进制字符串转换为二进制数据
    hex_string_to_binary(span_id, sp_id, 8);

    // 设置Span的span_id字段为转换后的二进制数据
    scope->span->span_id.data = sp_id;

    // 设置Span ID的长度为8字节
    scope->span->span_id.len  = 8;
}

void neu_otel_scope_set_span_flags(neu_otel_scope_ctx ctx, uint32_t flags)
{
    trace_scope_t *scope = (trace_scope_t *) ctx;
    scope->span->flags   = flags;
}

/**
 * @brief 向给定跟踪范围上下文的Span中添加一个整数类型的属性。
 *
 * 此函数用于向指定的Span添加一个新的键值对属性，其中值是int64_t类型。它首先调整属性数组的大小，
 * 然后初始化新的键值对，并设置键和值，最后更新属性数量。
 *
 * @param ctx 跟踪范围上下文。
 * @param key 属性的键。
 * @param val 属性的值（int64_t类型）。
 */
void neu_otel_scope_add_span_attr_int(neu_otel_scope_ctx ctx, const char *key,
                                      int64_t val)
{
    trace_scope_t *scope = (trace_scope_t *) ctx;

    // 保存原属性数组
    Opentelemetry__Proto__Common__V1__KeyValue **t_kvs =
        scope->span->attributes;

    // 重新分配属性数组内存
    scope->span->attributes =
        calloc(scope->span->n_attributes + 1,
               sizeof(Opentelemetry__Proto__Common__V1__KeyValue *));

    // 复制原有属性
    for (size_t i = 0; i < scope->span->n_attributes; i++) {
        scope->span->attributes[i] = t_kvs[i];
    }

    ///< 创建新的键值对
    // 为新的键值对 kv 分配内存，并使用
    Opentelemetry__Proto__Common__V1__KeyValue *kv =
        calloc(1, sizeof(Opentelemetry__Proto__Common__V1__KeyValue));
    opentelemetry__proto__common__v1__key_value__init(kv);
    kv->key   = strdup(key);

    // 为 kv->value 分配内存，并使用
    kv->value = calloc(1, sizeof(Opentelemetry__Proto__Common__V1__AnyValue));
    opentelemetry__proto__common__v1__any_value__init(kv->value);
    kv->value->value_case =
        OPENTELEMETRY__PROTO__COMMON__V1__ANY_VALUE__VALUE_INT_VALUE;
    kv->value->int_value                               = val;

    // 添加新的键值对到属性数组
    scope->span->attributes[scope->span->n_attributes] = kv;

    // 更新属性数量
    scope->span->n_attributes += 1;
    free(t_kvs);
}

void neu_otel_scope_add_span_attr_double(neu_otel_scope_ctx ctx,
                                         const char *key, double val)
{
    trace_scope_t *scope = (trace_scope_t *) ctx;

    Opentelemetry__Proto__Common__V1__KeyValue **t_kvs =
        scope->span->attributes;

    scope->span->attributes =
        calloc(scope->span->n_attributes + 1,
               sizeof(Opentelemetry__Proto__Common__V1__KeyValue *));

    for (size_t i = 0; i < scope->span->n_attributes; i++) {
        scope->span->attributes[i] = t_kvs[i];
    }

    Opentelemetry__Proto__Common__V1__KeyValue *kv =
        calloc(1, sizeof(Opentelemetry__Proto__Common__V1__KeyValue));
    opentelemetry__proto__common__v1__key_value__init(kv);
    kv->key   = strdup(key);
    kv->value = calloc(1, sizeof(Opentelemetry__Proto__Common__V1__AnyValue));
    opentelemetry__proto__common__v1__any_value__init(kv->value);
    kv->value->value_case =
        OPENTELEMETRY__PROTO__COMMON__V1__ANY_VALUE__VALUE_DOUBLE_VALUE;
    kv->value->double_value                            = val;
    scope->span->attributes[scope->span->n_attributes] = kv;

    scope->span->n_attributes += 1;
    free(t_kvs);
}

void neu_otel_scope_add_span_attr_string(neu_otel_scope_ctx ctx,
                                         const char *key, const char *val)
{
    trace_scope_t *scope = (trace_scope_t *) ctx;

    Opentelemetry__Proto__Common__V1__KeyValue **t_kvs =
        scope->span->attributes;

    scope->span->attributes =
        calloc(scope->span->n_attributes + 1,
               sizeof(Opentelemetry__Proto__Common__V1__KeyValue *));

    for (size_t i = 0; i < scope->span->n_attributes; i++) {
        scope->span->attributes[i] = t_kvs[i];
    }

    Opentelemetry__Proto__Common__V1__KeyValue *kv =
        calloc(1, sizeof(Opentelemetry__Proto__Common__V1__KeyValue));
    opentelemetry__proto__common__v1__key_value__init(kv);
    kv->key   = strdup(key);
    kv->value = calloc(1, sizeof(Opentelemetry__Proto__Common__V1__AnyValue));
    opentelemetry__proto__common__v1__any_value__init(kv->value);
    kv->value->value_case =
        OPENTELEMETRY__PROTO__COMMON__V1__ANY_VALUE__VALUE_STRING_VALUE;
    kv->value->string_value                            = strdup(val);
    scope->span->attributes[scope->span->n_attributes] = kv;

    scope->span->n_attributes += 1;
    free(t_kvs);
}

void neu_otel_scope_add_span_attr_bool(neu_otel_scope_ctx ctx, const char *key,
                                       bool val)
{
    trace_scope_t *scope = (trace_scope_t *) ctx;

    Opentelemetry__Proto__Common__V1__KeyValue **t_kvs =
        scope->span->attributes;

    scope->span->attributes =
        calloc(scope->span->n_attributes + 1,
               sizeof(Opentelemetry__Proto__Common__V1__KeyValue *));

    for (size_t i = 0; i < scope->span->n_attributes; i++) {
        scope->span->attributes[i] = t_kvs[i];
    }

    Opentelemetry__Proto__Common__V1__KeyValue *kv =
        calloc(1, sizeof(Opentelemetry__Proto__Common__V1__KeyValue));
    opentelemetry__proto__common__v1__key_value__init(kv);
    kv->key   = strdup(key);
    kv->value = calloc(1, sizeof(Opentelemetry__Proto__Common__V1__AnyValue));
    opentelemetry__proto__common__v1__any_value__init(kv->value);
    kv->value->value_case =
        OPENTELEMETRY__PROTO__COMMON__V1__ANY_VALUE__VALUE_BOOL_VALUE;
    kv->value->bool_value                              = val;
    scope->span->attributes[scope->span->n_attributes] = kv;

    scope->span->n_attributes += 1;
    free(t_kvs);
}

/**
 * @brief 设置给定跟踪范围上下文的Span开始时间。
 *
 * 此函数用于设置指定Span的开始时间，时间单位是纳秒（ns）。它直接将提供的纳秒值赋给Span的
 * `start_time_unix_nano`字段。
 *
 * @param ctx 跟踪范围上下文。
 * @param ns Span开始时间，以纳秒为单位。
 */
void neu_otel_scope_set_span_start_time(neu_otel_scope_ctx ctx, int64_t ns)
{
    trace_scope_t *scope              = (trace_scope_t *) ctx;
    scope->span->start_time_unix_nano = ((uint64_t) ns);
}

void neu_otel_scope_set_span_end_time(neu_otel_scope_ctx ctx, int64_t ns)

{
    trace_scope_t *scope            = (trace_scope_t *) ctx;
    scope->span->end_time_unix_nano = ((uint64_t) ns);
    trace_ctx_t *trace_ctx          = (trace_ctx_t *) scope->trace_ctx;
    trace_ctx->span_num += 1;
    trace_ctx->expected_span_num -= 1;
}

void neu_otel_scope_set_status_code(neu_otel_scope_ctx     ctx,
                                    neu_otel_status_code_e code,
                                    const char *           desc)
{
    trace_scope_t *scope = (trace_scope_t *) ctx;
    scope->span->status =
        calloc(1, sizeof(Opentelemetry__Proto__Trace__V1__Status));
    opentelemetry__proto__trace__v1__status__init(scope->span->status);
    scope->span->status->code =
        (Opentelemetry__Proto__Trace__V1__Status__StatusCode) code;
    scope->span->status->message = strdup(desc);
}

void neu_otel_scope_set_status_code2(neu_otel_scope_ctx     ctx,
                                     neu_otel_status_code_e code, int errorno)
{
    trace_scope_t *scope = (trace_scope_t *) ctx;
    scope->span->status =
        calloc(1, sizeof(Opentelemetry__Proto__Trace__V1__Status));
    opentelemetry__proto__trace__v1__status__init(scope->span->status);
    char *error_buf = calloc(1, 16);
    sprintf(error_buf, "%d", errorno);
    scope->span->status->code =
        (Opentelemetry__Proto__Trace__V1__Status__StatusCode) code;
    scope->span->status->message = error_buf;
}

/**
 * @brief 获取当前跟踪范围上下文中的前一个Span ID。
 *
 * 此函数用于从给定的跟踪范围上下文中获取前一个Span的ID（如果存在）。
 * 它首先确保当前Span不是第一个Span（即有前一个Span存在），然后在加锁的情况下访问并返回前一个Span的ID。
 * 如果没有前一个Span，则解锁并返回NULL。
 *
 * @param ctx 跟踪范围上下文。
 * @return 返回前一个Span的ID；如果没有前一个Span，则返回NULL。
 */
uint8_t *neu_otel_scope_get_pre_span_id(neu_otel_scope_ctx ctx)
{
    trace_scope_t *scope     = (trace_scope_t *) ctx;
    trace_ctx_t *  trace_ctx = (trace_ctx_t *) scope->trace_ctx;
    pthread_mutex_lock(&trace_ctx->mutex);

    // 检查是否有前一个Span
    if (scope->span_index <= 0) {
        pthread_mutex_unlock(&trace_ctx->mutex);
        return NULL;
    }

    // 获取前一个Span的ID
    uint8_t *id = trace_ctx->trace_data.resource_spans[0]
                      ->scope_spans[0]
                      ->spans[scope->span_index - 1]
                      ->span_id.data;
    pthread_mutex_unlock(&trace_ctx->mutex);
    return id;
}

int neu_otel_trace_pack_size(neu_otel_trace_ctx ctx)
{
    trace_ctx_t *trace_ctx = (trace_ctx_t *) ctx;
    return opentelemetry__proto__trace__v1__traces_data__get_packed_size(
        &trace_ctx->trace_data);
}

int neu_otel_trace_pack(neu_otel_trace_ctx ctx, uint8_t *out)
{
    trace_ctx_t *trace_ctx = (trace_ctx_t *) ctx;
    return opentelemetry__proto__trace__v1__traces_data__pack(
        &trace_ctx->trace_data, out);
}

/**
 * @brief 生成一个新的Span ID。
 *
 * 此函数用于生成一个指定长度（SPAN_ID_LENGTH）的新Span ID，并将其存储在提供的字符数组中。
 * Span ID由一组从ID_CHARSET中随机选取的字符组成。
 *
 * @param id 用于存储新生成的Span ID的字符数组。该数组大小应至少为(SPAN_ID_LENGTH + 1)以容纳终止符。
 */
void neu_otel_new_span_id(char *id)
{
    for (int i = SPAN_ID_LENGTH - 1; i >= 0; i--) {
        id[i] = ID_CHARSET[rand() % 16];
    }
    id[SPAN_ID_LENGTH] = '\0';
}

void neu_otel_new_trace_id(char *id)
{
    neu_otel_new_span_id(id);
    neu_otel_new_span_id(id + SPAN_ID_LENGTH);
}

void neu_otel_split_traceparent(const char *in, char *trace_id, char *span_id,
                                uint32_t *flags)
{
    const char *delimiter = "-";
    char *      token;
    char *      saveptr;
    char *      copy        = strdup(in);
    char        t_flags[32] = { 0 };

    token = strtok_r(copy, delimiter, &saveptr);

    token = strtok_r(NULL, delimiter, &saveptr);
    if (token != NULL) {
        strcpy(trace_id, token);
    } else {
        strcpy(trace_id, "");
    }

    token = strtok_r(NULL, delimiter, &saveptr);
    if (token != NULL) {
        strcpy(span_id, token);
    } else {
        strcpy(span_id, "");
    }

    token = strtok_r(NULL, delimiter, &saveptr);
    if (token != NULL) {
        strcpy(t_flags, token);
        sscanf(t_flags, "%x", flags);
    } else {
        strcpy(t_flags, "");
        *flags = 0;
    }

    free(copy);
}

static int otel_timer_cb(void *data)
{
    (void) data;

    trace_ctx_table_ele_t *el = NULL, *tmp = NULL;

    pthread_mutex_lock(&table_mutex);

    HASH_ITER(hh, traces_table, el, tmp)
    {
        if (el->ctx->final &&
            el->ctx->span_num ==
                el->ctx->trace_data.resource_spans[0]
                    ->scope_spans[0]
                    ->n_spans &&
            el->ctx->expected_span_num <= 0) {
            int data_size = neu_otel_trace_pack_size(el->ctx);

            uint8_t *data_buf = calloc(1, data_size);
            neu_otel_trace_pack(el->ctx, data_buf);
            int status = neu_http_post_otel_trace(data_buf, data_size);
            free(data_buf);
            nlog_debug("send trace:%s status:%d", (char *) el->ctx->trace_id,
                       status);
            if (status == 200 || status == 400) {
                HASH_DEL(traces_table, el);
                neu_otel_free_trace(el->ctx);
                free(el);
            }
        } else if (neu_time_ms() - el->ctx->ts >= TRACE_TIME_OUT) {
            nlog_debug("trace:%s time out", (char *) el->ctx->trace_id);
            HASH_DEL(traces_table, el);
            neu_otel_free_trace(el->ctx);
            free(el);
        }
    }

    pthread_mutex_unlock(&table_mutex);

    return 0;
}

void neu_otel_start()
{
    if (otel_event == NULL) {
        otel_event = neu_event_new();
    }

    if (otel_timer == NULL) {
        neu_event_timer_param_t param = { 0 };

        param.usr_data = NULL;

        param.second      = 0;
        param.millisecond = 100;
        param.cb          = otel_timer_cb;
        param.type        = NEU_EVENT_TIMER_BLOCK;

        otel_timer = neu_event_add_timer(otel_event, param);
    }

    nlog_debug("otel_start");
}

void neu_otel_stop()
{
    if (otel_timer) {
        neu_event_del_timer(otel_event, otel_timer);
        otel_timer = NULL;
    }

    if (otel_event) {
        neu_event_close(otel_event);
        otel_event = NULL;
    }
    trace_ctx_table_ele_t *el = NULL, *tmp = NULL;

    pthread_mutex_lock(&table_mutex);

    HASH_ITER(hh, traces_table, el, tmp)
    {

        HASH_DEL(traces_table, el);
        neu_otel_free_trace(el->ctx);
        free(el);
    }

    pthread_mutex_unlock(&table_mutex);

    nlog_debug("otel_stop");
}

bool neu_otel_control_is_started()
{
    return otel_flag && otel_control_flag;
}
bool neu_otel_data_is_started()
{
    return otel_flag && otel_data_flag;
}
void neu_otel_set_config(void *config)
{
    neu_json_otel_conf_req_t *req = (neu_json_otel_conf_req_t *) config;
    otel_flag         = strcmp(req->action, "start") == 0 ? true : false;
    otel_control_flag = req->control_flag;
    otel_data_flag    = req->data_flag;
    if (req->collector_url) {
        strcpy(otel_collector_url, req->collector_url);
    }
    if (req->service_name) {
        strcpy(otel_service_name, req->service_name);
    }
    otel_data_sample_rate = req->data_sample_rate;

    nlog_debug("otel config: %s %s %s %.2f %d %d", req->action,
               req->collector_url, req->service_name, req->data_sample_rate,
               req->data_flag, req->control_flag);
}

void *neu_otel_get_config()
{
    neu_json_otel_conf_req_t *req = calloc(1, sizeof(neu_json_otel_conf_req_t));
    if (otel_flag) {
        req->action = strdup("start");
    } else {
        req->action = strdup("stop");
    }

    req->collector_url    = strdup(otel_collector_url);
    req->control_flag     = otel_control_flag;
    req->data_flag        = otel_data_flag;
    req->service_name     = strdup(otel_service_name);
    req->data_sample_rate = otel_data_sample_rate;
    return req;
}

double neu_otel_data_sample_rate()
{
    return otel_data_sample_rate;
}

const char *neu_otel_collector_url()
{
    return otel_collector_url;
}

const char *neu_otel_service_name()
{
    return otel_service_name;
}