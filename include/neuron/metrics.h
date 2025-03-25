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

#ifndef NEU_METRICS_H
#define NEU_METRICS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include <pthread.h>

#include "define.h"
#include "type.h"
#include "utils/rolling_counter.h"
#include "utils/utextend.h"
#include "utils/uthash.h"

extern int64_t global_timestamp;

/**
 * @brief 指标类型枚举，用于定义不同的性能指标类型。
 *
 * 此枚举定义了系统中可能使用的各种性能指标类型，包括计数器、仪表等。
 * 每种类型适用于不同的监控需求和场景。
 */
typedef enum {
    /**
     * @brief 计数器类型。
     *
     * 用于记录单调递增的数值，如请求的数量或错误的发生次数。
     * 计数器通常用于跟踪事件发生的频率。
     */
    NEU_METRIC_TYPE_COUNTER,

    /**
     * @brief 仪表类型（拼写应为GAUGE）。
     *
     * 用于记录可以任意上下波动的数值，如当前内存使用量或温度读数。
     * 仪表通常用于表示瞬时状态或测量值。
     */
    NEU_METRIC_TYPE_GAUAGE,

    /**
     * @brief 集合计数器类型。
     *
     * 用于记录一组计数器的集合，适用于需要同时监控多个相关计数器的情况。
     */
    NEU_METRIC_TYPE_COUNTER_SET,

    /**
     * @brief 滚动计数器类型。
     *
     * 用于记录一段时间内的统计数据，并在超过设定的时间窗口后自动重置。
     * 滚动计数器通常用于计算每分钟、每小时等时间段内的平均值或总数。
     */
    NEU_METRIC_TYPE_ROLLING_COUNTER,

    /**
     * @brief 标志位 - 不重置。
     *
     * 这是一个标志位，当与其它类型组合使用时，指示该指标不应被自动重置。
     * 可以通过按位或操作与其他类型结合使用。
     */
    NEU_METRIC_TYPE_FLAG_NO_RESET = 0x80,
} neu_metric_type_e;

#define NEU_METRIC_TYPE_MASK 0x0F

// node running state
#define NEU_METRIC_RUNNING_STATE "running_state"
#define NEU_METRIC_RUNNING_STATE_TYPE \
    (NEU_METRIC_TYPE_GAUAGE | NEU_METRIC_TYPE_FLAG_NO_RESET)
#define NEU_METRIC_RUNNING_STATE_HELP "Node running state"

// node link state
#define NEU_METRIC_LINK_STATE "link_state"
#define NEU_METRIC_LINK_STATE_TYPE \
    (NEU_METRIC_TYPE_GAUAGE | NEU_METRIC_TYPE_FLAG_NO_RESET)
#define NEU_METRIC_LINK_STATE_HELP "Node link state"

// last round trip time in millisends
#define NEU_METRIC_LAST_RTT_MS "last_rtt_ms"
#define NEU_METRIC_LAST_RTT_MS_TYPE NEU_METRIC_TYPE_GAUAGE
#define NEU_METRIC_LAST_RTT_MS_HELP \
    "Last request round trip time in milliseconds"
#define NEU_METRIC_LAST_RTT_MS_MAX 9999

// number of bytes sent
#define NEU_METRIC_SEND_BYTES "send_bytes"
#define NEU_METRIC_SEND_BYTES_TYPE NEU_METRIC_TYPE_COUNTER_SET
#define NEU_METRIC_SEND_BYTES_HELP "Total number of bytes sent"

// number of bytes received
#define NEU_METRIC_RECV_BYTES "recv_bytes"
#define NEU_METRIC_RECV_BYTES_TYPE NEU_METRIC_TYPE_COUNTER_SET
#define NEU_METRIC_RECV_BYTES_HELP "Total number of bytes received"

// maintained by neuron core
// number of tag read including errors
#define NEU_METRIC_TAG_READS_TOTAL "tag_reads_total"
#define NEU_METRIC_TAG_READS_TOTAL_TYPE NEU_METRIC_TYPE_COUNTER
#define NEU_METRIC_TAG_READS_TOTAL_HELP \
    "Total number of tag reads including errors"

// maintained by neuron core
// number of tag read errors
#define NEU_METRIC_TAG_READ_ERRORS_TOTAL "tag_read_errors_total"
#define NEU_METRIC_TAG_READ_ERRORS_TOTAL_TYPE NEU_METRIC_TYPE_COUNTER
#define NEU_METRIC_TAG_READ_ERRORS_TOTAL_HELP "Total number of tag read errors"

// maintained by neuron core
// number of tags in group
#define NEU_METRIC_TAGS_TOTAL "tags_total"
#define NEU_METRIC_TAGS_TOTAL_TYPE \
    (NEU_METRIC_TYPE_GAUAGE | NEU_METRIC_TYPE_FLAG_NO_RESET)
#define NEU_METRIC_TAGS_TOTAL_HELP "Total number of tags in the node"

// maintained by neuron core
// number of tags in group
#define NEU_METRIC_GROUP_TAGS_TOTAL "group_tags_total"
#define NEU_METRIC_GROUP_TAGS_TOTAL_TYPE \
    (NEU_METRIC_TYPE_GAUAGE | NEU_METRIC_TYPE_FLAG_NO_RESET)
#define NEU_METRIC_GROUP_TAGS_TOTAL_HELP "Total number of tags in the group"

// number of messages sent in last group timer
#define NEU_METRIC_GROUP_LAST_SEND_MSGS "group_last_send_msgs"
#define NEU_METRIC_GROUP_LAST_SEND_MSGS_TYPE NEU_METRIC_TYPE_GAUAGE
#define NEU_METRIC_GROUP_LAST_SEND_MSGS_HELP \
    "Number of messages sent on last group timer invocation"

// maintained by neuron core
// milliseconds consumed in last group timer invocation
#define NEU_METRIC_GROUP_LAST_TIMER_MS "group_last_timer_ms"
#define NEU_METRIC_GROUP_LAST_TIMER_MS_TYPE NEU_METRIC_TYPE_GAUAGE
#define NEU_METRIC_GROUP_LAST_TIMER_MS_HELP \
    "Time in milliseconds consumed on last group timer invocation"

// maintained by neuron core
// group last error code
#define NEU_METRIC_GROUP_LAST_ERROR_CODE "group_last_error_code"
#define NEU_METRIC_GROUP_LAST_ERROR_CODE_TYPE NEU_METRIC_TYPE_GAUAGE
#define NEU_METRIC_GROUP_LAST_ERROR_CODE_HELP \
    "Last encountered error code in group data acquisition"

// maintained by neuron core
// group last error timestamp
#define NEU_METRIC_GROUP_LAST_ERROR_TS "group_last_error_timestamp_ms"
#define NEU_METRIC_GROUP_LAST_ERROR_TS_TYPE NEU_METRIC_TYPE_GAUAGE
#define NEU_METRIC_GROUP_LAST_ERROR_TS_HELP \
    "Timestamp (ms) of the last encountered error in group data acquisition"

// number of messages sent
#define NEU_METRIC_SEND_MSGS_TOTAL "send_msgs_total"
#define NEU_METRIC_SEND_MSGS_TOTAL_TYPE NEU_METRIC_TYPE_COUNTER
#define NEU_METRIC_SEND_MSGS_TOTAL_HELP "Total number of messages sent"

// number of errors encounter while sending messages
#define NEU_METRIC_SEND_MSG_ERRORS_TOTAL "send_msg_errors_total"
#define NEU_METRIC_SEND_MSG_ERRORS_TOTAL_TYPE NEU_METRIC_TYPE_COUNTER
#define NEU_METRIC_SEND_MSG_ERRORS_TOTAL_HELP \
    "Total number of errors sending messages"

// number of messages received
#define NEU_METRIC_RECV_MSGS_TOTAL "recv_msgs_total"
#define NEU_METRIC_RECV_MSGS_TOTAL_TYPE NEU_METRIC_TYPE_COUNTER
#define NEU_METRIC_RECV_MSGS_TOTAL_HELP "Total number of messages received"

// number of trans data message within the last 5 seconds
#define NEU_METRIC_TRANS_DATA_5S "last_5s_trans_data_msgs"
#define NEU_METRIC_TRANS_DATA_5S_TYPE NEU_METRIC_TYPE_ROLLING_COUNTER
#define NEU_METRIC_TRANS_DATA_5S_HELP \
    "Number of internal trans data message within the last 5 seconds"

// number of trans data message within the last 30 seconds
#define NEU_METRIC_TRANS_DATA_30S "last_30s_trans_data_msgs"
#define NEU_METRIC_TRANS_DATA_30S_TYPE NEU_METRIC_TYPE_ROLLING_COUNTER
#define NEU_METRIC_TRANS_DATA_30S_HELP \
    "Number of internal trans data message within the last 30 seconds"

// number of trans data message within the last 60 seconds
#define NEU_METRIC_TRANS_DATA_60S "last_60s_trans_data_msgs"
#define NEU_METRIC_TRANS_DATA_60S_TYPE NEU_METRIC_TYPE_ROLLING_COUNTER
#define NEU_METRIC_TRANS_DATA_60S_HELP \
    "Number of internal trans data message within the last 60 seconds"

// number of bytes sent within the last 5 seconds
#define NEU_METRIC_SEND_BYTES_5S "last_5s_send_bytes"
#define NEU_METRIC_SEND_BYTES_5S_TYPE NEU_METRIC_TYPE_ROLLING_COUNTER
#define NEU_METRIC_SEND_BYTES_5S_HELP \
    "Number of bytes sent within the last 5 seconds"

// number of bytes sent within the last 30 seconds
#define NEU_METRIC_SEND_BYTES_30S "last_30s_send_bytes"
#define NEU_METRIC_SEND_BYTES_30S_TYPE NEU_METRIC_TYPE_ROLLING_COUNTER
#define NEU_METRIC_SEND_BYTES_30S_HELP \
    "Number of bytes sent within the last 30 seconds"

// number of bytes sent within the last 60 seconds
#define NEU_METRIC_SEND_BYTES_60S "last_60s_send_bytes"
#define NEU_METRIC_SEND_BYTES_60S_TYPE NEU_METRIC_TYPE_ROLLING_COUNTER
#define NEU_METRIC_SEND_BYTES_60S_HELP \
    "Number of bytes sent within the last 60 seconds"

// number of bytes received within the last 5 seconds
#define NEU_METRIC_RECV_BYTES_5S "last_5s_recv_bytes"
#define NEU_METRIC_RECV_BYTES_5S_TYPE NEU_METRIC_TYPE_ROLLING_COUNTER
#define NEU_METRIC_RECV_BYTES_5S_HELP \
    "Number of bytes received within the last 5 seconds"

// number of bytes received within the last 30 seconds
#define NEU_METRIC_RECV_BYTES_30S "last_30s_recv_bytes"
#define NEU_METRIC_RECV_BYTES_30S_TYPE NEU_METRIC_TYPE_ROLLING_COUNTER
#define NEU_METRIC_RECV_BYTES_30S_HELP \
    "Number of bytes received within the last 30 seconds"

// number of bytes received within the last 60 seconds
#define NEU_METRIC_RECV_BYTES_60S "last_60s_recv_bytes"
#define NEU_METRIC_RECV_BYTES_60S_TYPE NEU_METRIC_TYPE_ROLLING_COUNTER
#define NEU_METRIC_RECV_BYTES_60S_HELP \
    "Number of bytes received within the last 60 seconds"

// number of messages received within the last 5 seconds
#define NEU_METRIC_RECV_MSGS_5S "last_5s_recv_msgs"
#define NEU_METRIC_RECV_MSGS_5S_TYPE NEU_METRIC_TYPE_ROLLING_COUNTER
#define NEU_METRIC_RECV_MSGS_5S_HELP \
    "Number of messages received within the last 5 seconds"

// number of messages received within the last 30 seconds
#define NEU_METRIC_RECV_MSGS_30S "last_30s_recv_msgs"
#define NEU_METRIC_RECV_MSGS_30S_TYPE NEU_METRIC_TYPE_ROLLING_COUNTER
#define NEU_METRIC_RECV_MSGS_30S_HELP \
    "Number of messages received within the last 30 seconds"

// number of messages received within the last 60 seconds
#define NEU_METRIC_RECV_MSGS_60S "last_60s_recv_msgs"
#define NEU_METRIC_RECV_MSGS_60S_TYPE NEU_METRIC_TYPE_ROLLING_COUNTER
#define NEU_METRIC_RECV_MSGS_60S_HELP \
    "Number of messages received within the last 60 seconds"

// number of disconnection within the last 60 seconds
#define NEU_METRIC_DISCONNECTION_60S "last_60s_disconnections"
#define NEU_METRIC_DISCONNECTION_60S_TYPE NEU_METRIC_TYPE_ROLLING_COUNTER
#define NEU_METRIC_DISCONNECTION_60S_HELP \
    "Number of disconnection within the last 60 seconds"

// number of disconnection within the last 600 seconds
#define NEU_METRIC_DISCONNECTION_600S "last_600s_disconnections"
#define NEU_METRIC_DISCONNECTION_600S_TYPE NEU_METRIC_TYPE_ROLLING_COUNTER
#define NEU_METRIC_DISCONNECTION_600S_HELP \
    "Number of disconnection within the last 600 seconds"

// number of disconnection within the last 1800 seconds
#define NEU_METRIC_DISCONNECTION_1800S "last_1800s_disconnections"
#define NEU_METRIC_DISCONNECTION_1800S_TYPE NEU_METRIC_TYPE_ROLLING_COUNTER
#define NEU_METRIC_DISCONNECTION_1800S_HELP \
    "Number of disconnection within the last 1800 seconds"

typedef enum {
    NEU_METRICS_CATEGORY_GLOBAL,
    NEU_METRICS_CATEGORY_DRIVER,
    NEU_METRICS_CATEGORY_APP,
    NEU_METRICS_CATEGORY_ALL,
} neu_metrics_category_e;

/**
 * @brief 度量项结构体。
 *
 * 此结构体用于表示一个度量项的信息，包括名称、帮助信息、类型、初始值、当前值、滚动计数器以及哈希表句柄。
 */
typedef struct {
    /**
     * @brief 度量项的名称，应指向字符串字面量。
     *
     * @warning 此字段应指向字符串字面量以确保其在程序生命周期内有效。
     */
    const char *           name;  

    /**
     * @brief 度量项的帮助信息或描述，应指向字符串字面量。
     *
     * @warning 此字段应指向字符串字面量以确保其在程序生命周期内有效。
     */
    const char *           help;  

    /**@brief 度量项的类型（如计数器、仪表等）。*/
    neu_metric_type_e      type;  

    /** @brief 度量项的初始值。*/
    uint64_t               init;  

    /** @brief 度量项的当前值。*/
    uint64_t               value; 

    /** @brief 滚动计数器，用于记录度量项的历史数据。*/
    neu_rolling_counter_t *rcnt;  

    /** @brief 哈希表句柄，用于在哈希表中按name有序存储度量项对象。*/
    UT_hash_handle         hh;    
} neu_metric_entry_t;

/**
 * @brief 组度量结构体。
 *
 * 此结构体用于表示一个组的度量信息，包括组名称、组度量项列表以及哈希表句柄。
 */
typedef struct {
    char *              name;    // group name
    neu_metric_entry_t *entries; // group metric entries
    UT_hash_handle      hh;      // ordered by name
} neu_group_metrics_t;

/**
 * @brief 节点度量结构体。
 *
 * 此结构体用于表示一个节点的度量信息，包括互斥锁、节点类型、节点名称、
 * 度量项列表、组度量信息、关联的适配器以及哈希表句柄。
 */
typedef struct {
    /** @brief 互斥锁，用于保护对节点度量数据的并发访问。*/
    pthread_mutex_t      lock;         

    /** @brief 节点类型，指示节点的具体类型（例如驱动、APP等）。*/
    neu_node_type_e      type;          

    /** @brief 节点名称，用于标识该节点的唯一名称。*/
    char *               name;         
    
    /**@brief 度量项列表，指向该节点的所有度量项的指针。*/
    neu_metric_entry_t * entries;      

    /** @brief 组度量信息，指向与该节点相关的组度量对象的指针。*/
    neu_group_metrics_t *group_metrics; 

    /** @brief 关联的适配器，指向与该节点关联的适配器对象的指针。*/
    neu_adapter_t *      adapter;   

    /** @brief 哈希表句柄，用于在哈希表中按name有序存储节点度量对象。*/    
    UT_hash_handle       hh;            
} neu_node_metrics_t;

/**
 * @brief 度量信息结构体。
 *
 * 此结构体用于存储系统和应用程序的各种度量信息，
 * 包括操作系统详情、硬件资源使用情况、许可证信息以及节点和度量项的状态等。
 */
typedef struct {
    char                distro[32];                  ///< 操作系统发行版名称
    char                kernel[32];                  ///< 内核版本
    char                machine[32];                 ///< 机器类型或架构
    char                clib[32];                    ///< C库名称
    char                clib_version[32];            ///< C库版本
    unsigned            cpu_percent;                 ///< CPU使用率百分比
    unsigned            cpu_cores;                   ///< CPU核心数
    size_t              mem_total_bytes;             ///< 总内存大小（字节）
    size_t              mem_used_bytes;              ///< 已使用内存大小（字节）
    size_t              mem_cache_bytes;             ///< 缓存内存大小（字节）
    size_t              disk_size_gibibytes;         ///< 磁盘总大小（GiB）
    size_t              disk_used_gibibytes;         ///< 已使用磁盘空间（GiB）
    size_t              disk_avail_gibibytes;        ///< 可用磁盘空间（GiB）
    bool                core_dumped;                 ///< 是否发生核心转储
    uint64_t            license_max_tags;            ///< 许可证允许的最大标签数
    uint64_t            license_used_tags;           ///< 已使用的标签数
    uint64_t            uptime_seconds;              ///< 系统运行时间（秒）
    size_t              north_nodes;                 ///< 北向节点总数
    size_t              north_running_nodes;         ///< 正在运行的北向节点数
    size_t              north_disconnected_nodes;    ///< 断开连接的北向节点数
    size_t              south_nodes;                 ///< 南向节点总数
    size_t              south_running_nodes;         ///< 正在运行的南向节点数
    size_t              south_disconnected_nodes;    ///< 断开连接的南向节点数
    neu_node_metrics_t *node_metrics;                ///< 节点度量信息
    neu_metric_entry_t *registered_metrics;          ///< 注册的度量项列表
} neu_metrics_t;

void neu_metrics_init();
void neu_metrics_add_node(const neu_adapter_t *adapter);
void neu_metrics_del_node(const neu_adapter_t *adapter);
int  neu_metrics_register_entry(const char *name, const char *help,
                                neu_metric_type_e type);
void neu_metrics_unregister_entry(const char *name);

typedef void (*neu_metrics_cb_t)(const neu_metrics_t *metrics, void *data);
void neu_metrics_visist(neu_metrics_cb_t cb, void *data);

/**
 * @brief 检查度量类型是否为计数器类型。
 *
 * 此函数用于检查给定的度量类型是否为计数器类型。它通过将传入的度量类型与 `NEU_METRIC_TYPE_MASK` 
 * 进行按位与操作，并判断结果是否等于 `NEU_METRIC_TYPE_COUNTER` 来确定度量类型是否为计数器类型。
 *
 * @param type 度量类型的枚举值。
 * @return 如果度量类型是计数器类型，则返回 `true`；否则返回 `false`。
 */
static inline bool neu_metric_type_is_counter(neu_metric_type_e type)
{
    return NEU_METRIC_TYPE_COUNTER == (type & NEU_METRIC_TYPE_MASK);
}

static inline bool neu_metric_type_is_rolling_counter(neu_metric_type_e type)
{
    return NEU_METRIC_TYPE_ROLLING_COUNTER == (type & NEU_METRIC_TYPE_MASK);
}

static inline bool neu_metric_type_no_reset(neu_metric_type_e type)
{
    return NEU_METRIC_TYPE_FLAG_NO_RESET & type;
}

static inline const char *neu_metric_type_str(neu_metric_type_e type)
{
    if (neu_metric_type_is_counter(type)) {
        return "counter";
    } else {
        return "gauge";
    }
}

int neu_metric_entries_add(neu_metric_entry_t **entries, const char *name,
                           const char *help, neu_metric_type_e type,
                           uint64_t init);

static inline void neu_metric_entry_free(neu_metric_entry_t *entry)
{
    if (neu_metric_type_is_rolling_counter(entry->type)) {
        neu_rolling_counter_free(entry->rcnt);
    }
    free(entry);
}

static inline void neu_group_metrics_free(neu_group_metrics_t *group_metrics)
{
    if (NULL == group_metrics) {
        return;
    }

    neu_metric_entry_t *e = NULL, *tmp = NULL;
    HASH_ITER(hh, group_metrics->entries, e, tmp)
    {
        HASH_DEL(group_metrics->entries, e);
        neu_metrics_unregister_entry(e->name);
        neu_metric_entry_free(e);
    }
    free(group_metrics->name);
    free(group_metrics);
}

/**
 * @brief 创建并初始化一个新的节点度量对象。
 *
 * 此函数用于创建并初始化一个新的节点度量对象，并为其分配内存。
 * 如果成功，则返回指向新创建的 `neu_node_metrics_t` 对象的指针；
 * 如果失败（例如内存分配失败），则返回 `NULL`。
 *
 * @param adapter 指向适配器对象的指针。
 * @param type 节点类型。
 * @param name 节点名称。
 * @return neu_node_metrics_t* 返回指向新创建的节点度量对象的指针，或在失败时返回 `NULL`。
 */
static inline neu_node_metrics_t *
neu_node_metrics_new(neu_adapter_t *adapter, neu_node_type_e type, char *name)
{
    // 分配内存并初始化节点度量对象
    neu_node_metrics_t *node_metrics =
        (neu_node_metrics_t *) calloc(1, sizeof(*node_metrics));

    if (NULL != node_metrics) {
        // 初始化互斥锁
        pthread_mutex_init(&node_metrics->lock, NULL);

        // 设置节点度量对象的属性
        node_metrics->type    = type;
        node_metrics->name    = name;
        node_metrics->adapter = adapter;
        // neu_metrics_add_node(adapter);
    }
    return node_metrics;
}

static inline void neu_node_metrics_free(neu_node_metrics_t *node_metrics)
{
    if (NULL == node_metrics) {
        return;
    }

    pthread_mutex_destroy(&node_metrics->lock);

    neu_metric_entry_t *e = NULL, *etmp = NULL;
    HASH_ITER(hh, node_metrics->entries, e, etmp)
    {
        HASH_DEL(node_metrics->entries, e);
        neu_metrics_unregister_entry(e->name);
        neu_metric_entry_free(e);
    }

    neu_group_metrics_t *g = NULL, *gtmp = NULL;
    HASH_ITER(hh, node_metrics->group_metrics, g, gtmp)
    {
        HASH_DEL(node_metrics->group_metrics, g);
        neu_group_metrics_free(g);
    }

    free(node_metrics);
}

/**
 * @brief 向节点度量中添加一个新的度量项。
 *
 * 此函数用于向指定的节点度量中添加一个新的度量项。如果提供了组名，则会首先查找该组是否存在，若不存在则创建新组，并将度量项添加到相应的组中。
 * 如果在操作过程中遇到任何错误（例如注册失败或内存分配失败），则会记录错误并返回相应的错误码。
 *
 * @param node_metrics 指向 `neu_node_metrics_t` 结构体的指针，表示要添加度量项的节点度量对象。
 * @param group_name 组名称，可以为 `NULL`。如果非 `NULL`，则会在对应的组中添加度量项；如果组不存在，则会创建新的组。
 * @param name 度量项的名称。
 * @param help 度量项的帮助信息或描述。
 * @param type 度量项的类型（如计数器、仪表等）。
 * @param init 度量项的初始值。
 * @return 成功时返回 0；如果发生错误（如注册失败或内存分配失败），则返回 -1。
 */
static inline int neu_node_metrics_add(neu_node_metrics_t *node_metrics,
                                       const char *group_name, const char *name,
                                       const char *help, neu_metric_type_e type,
                                       uint64_t init)
{
    int rv = 0;

    // 尝试注册度量项，如果失败则直接返回错误码
    if (0 > neu_metrics_register_entry(name, help, type)) {
        return -1;
    }

    pthread_mutex_lock(&node_metrics->lock);
    if (NULL == group_name) {
        // 如果没有提供组名，则直接将度量项添加到节点度量中
        rv = neu_metric_entries_add(&node_metrics->entries, name, help, type,
                                    init);
    } else {
        neu_group_metrics_t *group_metrics = NULL;

        // 查找是否存在指定名称的组
        HASH_FIND_STR(node_metrics->group_metrics, group_name, group_metrics);
        if (NULL != group_metrics) {
            // 如果组存在，则将度量项添加到该组中
            rv = neu_metric_entries_add(&group_metrics->entries, name, help,
                                        type, init);
        } else {
            // 如果组不存在，则创建新组并将度量项添加到新组中
            group_metrics =
                (neu_group_metrics_t *) calloc(1, sizeof(*group_metrics));
            if (NULL != group_metrics &&
                NULL != (group_metrics->name = strdup(group_name))) {
                HASH_ADD_STR(node_metrics->group_metrics, name, group_metrics);
                rv = neu_metric_entries_add(&group_metrics->entries, name, help,
                                            type, init);
            } else {
                free(group_metrics);
                rv = -1;
            }
        }
    }
    pthread_mutex_unlock(&node_metrics->lock);

    // 如果添加度量项失败，则取消注册度量项
    if (0 != rv) {
        neu_metrics_unregister_entry(name);
    }
    return rv;
}

/**
 * @brief 更新节点度量信息。
 *
 * 此函数用于更新指定节点的度量信息。它首先根据提供的组名和度量项名称查找对应的度量条目。
 * 如果找到相应的度量条目，则根据其类型（计数器、滚动计数器或其他）更新度量值。
 * 如果未找到度量条目或未提供有效的组名，则返回错误码 `-1`。该函数使用互斥锁保护对度量数据的访问，以确保线程安全。
 *
 * @param node_metrics 指向 `neu_node_metrics_t` 结构体的指针，表示当前节点的度量对象。
 * @param group        度量项所属的组名。可以为 `NULL`，此时直接在全局度量中查找度量项。
 * @param metric_name  要更新的度量项名称。
 * @param n            要更新的度量值。
 * @return 成功时返回 0；如果未找到度量条目或未提供有效的组名，则返回 `-1`。
 */
static inline int neu_node_metrics_update(neu_node_metrics_t *node_metrics,
                                          const char *        group,
                                          const char *metric_name, uint64_t n)
{
    neu_metric_entry_t *entry = NULL;

    pthread_mutex_lock(&node_metrics->lock);

    // 根据组名查找指标项
    if (NULL == group) {
        HASH_FIND_STR(node_metrics->entries, metric_name, entry); // 表中查找指标名称的项
    } else if (NULL != node_metrics->group_metrics) {
        neu_group_metrics_t *g = NULL;
        HASH_FIND_STR(node_metrics->group_metrics, group, g);     //查找组名为 group 的组
        if (NULL != g) {
            HASH_FIND_STR(g->entries, metric_name, entry);
        }
    }

    if (NULL == entry) {
        pthread_mutex_unlock(&node_metrics->lock);
        return -1;
    }

    //判断度量的类型
    if (neu_metric_type_is_counter(entry->type)) {
        entry->value += n;
    } else if (neu_metric_type_is_rolling_counter(entry->type)) {
        entry->value =
            neu_rolling_counter_inc(entry->rcnt, global_timestamp, n);
    } else {
        entry->value = n;
    }
    pthread_mutex_unlock(&node_metrics->lock);

    return 0;
}

static inline void neu_node_metrics_reset(neu_node_metrics_t *node_metrics)
{
    neu_metric_entry_t *entry = NULL;

    pthread_mutex_lock(&node_metrics->lock);
    HASH_LOOP(hh, node_metrics->entries, entry)
    {
        if (!neu_metric_type_no_reset(entry->type)) {
            entry->value = entry->init;
            if (neu_metric_type_is_rolling_counter(entry->type)) {
                neu_rolling_counter_reset(entry->rcnt);
            }
        }
    }

    neu_group_metrics_t *g = NULL;
    HASH_LOOP(hh, node_metrics->group_metrics, g)
    {
        HASH_LOOP(hh, g->entries, entry)
        {
            if (!neu_metric_type_no_reset(entry->type)) {
                entry->value = entry->init;
                if (neu_metric_type_is_rolling_counter(entry->type)) {
                    neu_rolling_counter_reset(entry->rcnt);
                }
            }
        }
    }
    pthread_mutex_unlock(&node_metrics->lock);
}

static inline int
neu_node_metrics_update_group(neu_node_metrics_t *node_metrics,
                              const char *        group_name,
                              const char *        new_group_name)
{
    int                  rv            = -1;
    neu_group_metrics_t *group_metrics = NULL;

    pthread_mutex_lock(&node_metrics->lock);
    HASH_FIND_STR(node_metrics->group_metrics, group_name, group_metrics);
    if (NULL != group_metrics) {
        char *name = strdup(new_group_name);
        if (NULL != name) {
            HASH_DEL(node_metrics->group_metrics, group_metrics);
            free(group_metrics->name);
            group_metrics->name = name;
            HASH_ADD_STR(node_metrics->group_metrics, name, group_metrics);
            rv = 0;
        }
    }
    pthread_mutex_unlock(&node_metrics->lock);

    return rv;
}

static inline void neu_node_metrics_del_group(neu_node_metrics_t *node_metrics,
                                              const char *        group_name)
{
    neu_group_metrics_t *gm = NULL;
    pthread_mutex_lock(&node_metrics->lock);
    HASH_FIND_STR(node_metrics->group_metrics, group_name, gm);
    if (NULL != gm) {
        HASH_DEL(node_metrics->group_metrics, gm);
        neu_group_metrics_free(gm);
    }
    pthread_mutex_unlock(&node_metrics->lock);
}

neu_metrics_t *neu_get_global_metrics();

#ifdef __cplusplus
}
#endif

#endif
