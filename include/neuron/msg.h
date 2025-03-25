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

#ifndef NEURON_MSG_H
#define NEURON_MSG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>
#include <stdint.h>

#include "json/neu_json_rw.h"

#include "define.h"
#include "errcodes.h"
#include "tag.h"
#include "type.h"
#include <math.h>

typedef struct {
    neu_node_running_state_e running;
    neu_node_link_state_e    link;
    int                      log_level;
} neu_node_state_t;

/**
 * @brief 请求/响应类型枚举，用于区分不同类型的消息。
 * 涵盖读取、写入、订阅、节点管理等多方面。
 */
typedef enum neu_reqresp_type {
    /** @brief 错误响应，含错误信息用于调试 */
    NEU_RESP_ERROR,

    /**
     * @name 数据读取相关
     * @{
     */
    /** @brief 读取组请求 */
    NEU_REQ_READ_GROUP,
    /** @brief 读取组响应 */
    NEU_RESP_READ_GROUP,
    /** @brief 分页读取组请求 */
    NEU_REQ_READ_GROUP_PAGINATE,
    /** @brief 分页读取组响应 */
    NEU_RESP_READ_GROUP_PAGINATE,
    /** @brief 测试读取标签请求 */
    NEU_REQ_TEST_READ_TAG,
    /** @brief 测试读取标签响应 */
    NEU_RESP_TEST_READ_TAG,
    /** @} */

    /**
     * @name 数据写入相关
     * @{
     */
    /** @brief 写入标签请求 */
    NEU_REQ_WRITE_TAG,
    /** @brief 批量写入标签请求 */
    NEU_REQ_WRITE_TAGS,
    /** @brief 写入组标签请求 */
    NEU_REQ_WRITE_GTAGS,
    /** @} */

    /**
     * @name 订阅管理相关
     * @{
     */
    /** @brief 订阅组请求 */
    NEU_REQ_SUBSCRIBE_GROUP,
    /** @brief 取消订阅组请求 */
    NEU_REQ_UNSUBSCRIBE_GROUP,
    /** @brief 更新订阅组请求 */
    NEU_REQ_UPDATE_SUBSCRIBE_GROUP,
    /** @brief 批量订阅组请求 */
    NEU_REQ_SUBSCRIBE_GROUPS,
    /** @brief 获取订阅组请求 */
    NEU_REQ_GET_SUBSCRIBE_GROUP,
    /** @brief 获取订阅组响应 */
    NEU_RESP_GET_SUBSCRIBE_GROUP,
    /** @brief 获取驱动器标签请求 */
    NEU_REQ_GET_SUB_DRIVER_TAGS,
    /** @brief 获取驱动器标签响应 */
    NEU_RESP_GET_SUB_DRIVER_TAGS,
    /** @} */

    /**
     * @name 节点管理相关
     * @{
     */
    /** @brief 节点初始化请求 */
    NEU_REQ_NODE_INIT,
    /** @brief 节点反初始化请求 */
    NEU_REQ_NODE_UNINIT,
    /** @brief 节点反初始化响应 */
    NEU_RESP_NODE_UNINIT,
    /** @brief 添加节点请求 */
    NEU_REQ_ADD_NODE,
    /** @brief 更新节点请求 */
    NEU_REQ_UPDATE_NODE,
    /** @brief 删除节点请求 */
    NEU_REQ_DEL_NODE,
    /** @brief 获取节点请求 */
    NEU_REQ_GET_NODE,
    /** @brief 获取节点响应 */
    NEU_RESP_GET_NODE,
    /** @brief 设置节点配置请求 */
    NEU_REQ_NODE_SETTING,
    /** @brief 获取节点配置请求 */
    NEU_REQ_GET_NODE_SETTING,
    /** @brief 获取节点配置响应 */
    NEU_RESP_GET_NODE_SETTING,
    /** @brief 获取节点状态请求 */
    NEU_REQ_GET_NODE_STATE,
    /** @brief 获取节点状态响应 */
    NEU_RESP_GET_NODE_STATE,
    /** @brief 获取所有节点状态请求 */
    NEU_REQ_GET_NODES_STATE,
    /** @brief 获取所有节点状态响应 */
    NEU_RESP_GET_NODES_STATE,
    /** @brief 控制节点请求 */
    NEU_REQ_NODE_CTL,
    /** @brief 重命名节点请求 */
    NEU_REQ_NODE_RENAME,
    /** @brief 重命名节点响应 */
    NEU_RESP_NODE_RENAME,
    /** @} */

    /**
     * @name 组管理相关
     * @{
     */
    /** @brief 添加组请求 */
    NEU_REQ_ADD_GROUP,
    /** @brief 删除组请求 */
    NEU_REQ_DEL_GROUP,
    /** @brief 更新组请求 */
    NEU_REQ_UPDATE_GROUP,
    /** @brief 更新驱动器组请求 */
    NEU_REQ_UPDATE_DRIVER_GROUP,
    /** @brief 更新驱动器组响应 */
    NEU_RESP_UPDATE_DRIVER_GROUP,
    /** @brief 获取组请求 */
    NEU_REQ_GET_GROUP,
    /** @brief 获取组响应 */
    NEU_RESP_GET_GROUP,
    /** @brief 获取驱动器组请求 */
    NEU_REQ_GET_DRIVER_GROUP,
    /** @brief 获取驱动器组响应 */
    NEU_RESP_GET_DRIVER_GROUP,
    /** @} */

    /**
     * @name 标签管理相关
     * @{
     */
    /** @brief 添加标签请求 */
    NEU_REQ_ADD_TAG,
    /** @brief 添加标签响应 */
    NEU_RESP_ADD_TAG,
    /** @brief 添加组标签请求 */
    NEU_REQ_ADD_GTAG,
    /** @brief 添加组标签响应 */
    NEU_RESP_ADD_GTAG,
    /** @brief 删除标签请求 */
    NEU_REQ_DEL_TAG,
    /** @brief 更新标签请求 */
    NEU_REQ_UPDATE_TAG,
    /** @brief 更新标签响应 */
    NEU_RESP_UPDATE_TAG,
    /** @brief 获取标签请求 */
    NEU_REQ_GET_TAG,
    /** @brief 获取标签响应 */
    NEU_RESP_GET_TAG,
    /** @} */

    /**
     * @name 插件管理相关
     * @{
     */
    /** @brief 添加插件请求 */
    NEU_REQ_ADD_PLUGIN,
    /** @brief 删除插件请求 */
    NEU_REQ_DEL_PLUGIN,
    /** @brief 更新插件请求 */
    NEU_REQ_UPDATE_PLUGIN,
    /** @brief 获取插件请求 */
    NEU_REQ_GET_PLUGIN,
    /** @brief 获取插件响应 */
    NEU_RESP_GET_PLUGIN,
    /** @} */

    /**
     * @name 其他请求/响应类型
     * @{
     */
    /** @brief 数据传输请求/响应 */
    NEU_REQRESP_TRANS_DATA,
    /** @brief 节点状态请求/响应 */
    NEU_REQRESP_NODES_STATE,
    /** @brief 节点删除通知 */
    NEU_REQRESP_NODE_DELETED,
    /** @brief 添加驱动器请求 */
    NEU_REQ_ADD_DRIVERS,
    /** @brief 更新日志级别请求 */
    NEU_REQ_UPDATE_LOG_LEVEL,
    /** @brief 程序文件上传请求 */
    NEU_REQ_PRGFILE_UPLOAD,
    /** @brief 程序文件处理请求 */
    NEU_REQ_PRGFILE_PROCESS,
    /** @brief 程序文件处理响应 */
    NEU_RESP_PRGFILE_PROCESS,
    /** @brief 扫描标签请求 */
    NEU_REQ_SCAN_TAGS,
    /** @brief 扫描标签响应 */
    NEU_RESP_SCAN_TAGS,
    /** @brief 检查模式请求 */
    NEU_REQ_CHECK_SCHEMA,
    /** @brief 检查模式响应 */
    NEU_RESP_CHECK_SCHEMA,
    /** @brief 驱动器动作请求 */
    NEU_REQ_DRIVER_ACTION,
    /** @brief 驱动器动作响应 */
    NEU_RESP_DRIVER_ACTION,
    /** @} */
} neu_reqresp_type_e;

static const char *neu_reqresp_type_string_t[] = {
    [NEU_RESP_ERROR] = "NEU_RESP_ERROR",

    [NEU_REQ_READ_GROUP]           = "NEU_REQ_READ_GROUP",
    [NEU_RESP_READ_GROUP]          = "NEU_RESP_READ_GROUP",
    [NEU_REQ_READ_GROUP_PAGINATE]  = "NEU_REQ_READ_GROUP_PAGINATE",
    [NEU_RESP_READ_GROUP_PAGINATE] = "NEU_RESP_READ_GROUP_PAGINATE",
    [NEU_REQ_TEST_READ_TAG]        = "NEU_REQ_TEST_READ_TAG",
    [NEU_RESP_TEST_READ_TAG]       = "NEU_RESP_TEST_READ_TAG",
    [NEU_REQ_WRITE_TAG]            = "NEU_REQ_WRITE_TAG",
    [NEU_REQ_WRITE_TAGS]           = "NEU_REQ_WRITE_TAGS",
    [NEU_REQ_WRITE_GTAGS]          = "NEU_REQ_WRITE_GTAGS",

    [NEU_REQ_SUBSCRIBE_GROUP]        = "NEU_REQ_SUBSCRIBE_GROUP",
    [NEU_REQ_UNSUBSCRIBE_GROUP]      = "NEU_REQ_UNSUBSCRIBE_GROUP",
    [NEU_REQ_UPDATE_SUBSCRIBE_GROUP] = "NEU_REQ_UPDATE_SUBSCRIBE_GROUP",
    [NEU_REQ_SUBSCRIBE_GROUPS]       = "NEU_REQ_SUBSCRIBE_GROUPS",
    [NEU_REQ_GET_SUBSCRIBE_GROUP]    = "NEU_REQ_GET_SUBSCRIBE_GROUP",
    [NEU_RESP_GET_SUBSCRIBE_GROUP]   = "NEU_RESP_GET_SUBSCRIBE_GROUP",
    [NEU_REQ_GET_SUB_DRIVER_TAGS]    = "NEU_REQ_GET_SUB_DRIVER_TAGS",
    [NEU_RESP_GET_SUB_DRIVER_TAGS]   = "NEU_RESP_GET_SUB_DRIVER_TAGS",

    [NEU_REQ_NODE_INIT]         = "NEU_REQ_NODE_INIT",
    [NEU_REQ_NODE_UNINIT]       = "NEU_REQ_NODE_UNINIT",
    [NEU_RESP_NODE_UNINIT]      = "NEU_RESP_NODE_UNINIT",
    [NEU_REQ_ADD_NODE]          = "NEU_REQ_ADD_NODE",
    [NEU_REQ_UPDATE_NODE]       = "NEU_REQ_UPDATE_NODE",
    [NEU_REQ_DEL_NODE]          = "NEU_REQ_DEL_NODE",
    [NEU_REQ_GET_NODE]          = "NEU_REQ_GET_NODE",
    [NEU_RESP_GET_NODE]         = "NEU_RESP_GET_NODE",
    [NEU_REQ_NODE_SETTING]      = "NEU_REQ_NODE_SETTING",
    [NEU_REQ_GET_NODE_SETTING]  = "NEU_REQ_GET_NODE_SETTING",
    [NEU_RESP_GET_NODE_SETTING] = "NEU_RESP_GET_NODE_SETTING",
    [NEU_REQ_GET_NODE_STATE]    = "NEU_REQ_GET_NODE_STATE",
    [NEU_RESP_GET_NODE_STATE]   = "NEU_RESP_GET_NODE_STATE",
    [NEU_REQ_GET_NODES_STATE]   = "NEU_REQ_GET_NODES_STATE",
    [NEU_RESP_GET_NODES_STATE]  = "NEU_RESP_GET_NODES_STATE",
    [NEU_REQ_NODE_CTL]          = "NEU_REQ_NODE_CTL",
    [NEU_REQ_NODE_RENAME]       = "NEU_REQ_NODE_RENAME",
    [NEU_RESP_NODE_RENAME]      = "NEU_RESP_NODE_RENAME",

    [NEU_REQ_ADD_GROUP]            = "NEU_REQ_ADD_GROUP",
    [NEU_REQ_DEL_GROUP]            = "NEU_REQ_DEL_GROUP",
    [NEU_REQ_UPDATE_GROUP]         = "NEU_REQ_UPDATE_GROUP",
    [NEU_REQ_UPDATE_DRIVER_GROUP]  = "NEU_REQ_UPDATE_DRIVER_GROUP",
    [NEU_RESP_UPDATE_DRIVER_GROUP] = "NEU_RESP_UPDATE_DRIVER_GROUP",
    [NEU_REQ_GET_GROUP]            = "NEU_REQ_GET_GROUP",
    [NEU_RESP_GET_GROUP]           = "NEU_RESP_GET_GROUP",
    [NEU_REQ_GET_DRIVER_GROUP]     = "NEU_REQ_GET_DRIVER_GROUP",
    [NEU_RESP_GET_DRIVER_GROUP]    = "NEU_RESP_GET_DRIVER_GROUP",

    [NEU_REQ_ADD_TAG]     = "NEU_REQ_ADD_TAG",
    [NEU_RESP_ADD_TAG]    = "NEU_RESP_ADD_TAG",
    [NEU_REQ_ADD_GTAG]    = "NEU_REQ_ADD_GTAG",
    [NEU_RESP_ADD_GTAG]   = "NEU_RESP_ADD_GTAG",
    [NEU_REQ_DEL_TAG]     = "NEU_REQ_DEL_TAG",
    [NEU_REQ_UPDATE_TAG]  = "NEU_REQ_UPDATE_TAG",
    [NEU_RESP_UPDATE_TAG] = "NEU_RESP_UPDATE_TAG",
    [NEU_REQ_GET_TAG]     = "NEU_REQ_GET_TAG",
    [NEU_RESP_GET_TAG]    = "NEU_RESP_GET_TAG",

    [NEU_REQ_ADD_PLUGIN]    = "NEU_REQ_ADD_PLUGIN",
    [NEU_REQ_DEL_PLUGIN]    = "NEU_REQ_DEL_PLUGIN",
    [NEU_REQ_UPDATE_PLUGIN] = "NEU_REQ_UPDATE_PLUGIN",
    [NEU_REQ_GET_PLUGIN]    = "NEU_REQ_GET_PLUGIN",
    [NEU_RESP_GET_PLUGIN]   = "NEU_RESP_GET_PLUGIN",

    [NEU_REQRESP_TRANS_DATA]   = "NEU_REQRESP_TRANS_DATA",
    [NEU_REQRESP_NODES_STATE]  = "NEU_REQRESP_NODES_STATE",
    [NEU_REQRESP_NODE_DELETED] = "NEU_REQRESP_NODE_DELETED",

    [NEU_REQ_ADD_DRIVERS] = "NEU_REQ_ADD_DRIVERS",

    [NEU_REQ_UPDATE_LOG_LEVEL] = "NEU_REQ_UPDATE_LOG_LEVEL",
    [NEU_REQ_PRGFILE_UPLOAD]   = "NEU_REQ_PRGFILE_UPLOAD",
    [NEU_REQ_PRGFILE_PROCESS]  = "NEU_REQ_PRGFILE_PROCESS",
    [NEU_RESP_PRGFILE_PROCESS] = "NEU_RESP_PRGFILE_PROCESS",

    [NEU_REQ_SCAN_TAGS]  = "NEU_REQ_SCAN_TAGS",
    [NEU_RESP_SCAN_TAGS] = "NEU_RESP_SCAN_TAGS",

    [NEU_REQ_CHECK_SCHEMA]  = "NEU_REQ_CHECK_SCHEMA",
    [NEU_RESP_CHECK_SCHEMA] = "NEU_RESP_CHECK_SCHEMA",

    [NEU_REQ_DRIVER_ACTION]  = "NEU_REQ_DRIVER_ACTION",
    [NEU_RESP_DRIVER_ACTION] = "NEU_RESP_DRIVER_ACTION",
};

inline static const char *neu_reqresp_type_string(neu_reqresp_type_e type)
{
    return neu_reqresp_type_string_t[type];
}

typedef enum {
    NEU_OTEL_TRACE_TYPE_UNSET = 0,
    NEU_OTEL_TRACE_TYPE_REST_COMM,
    NEU_OTEL_TRACE_TYPE_REST_SPEC,
    NEU_OTEL_TRACE_TYPE_MQTT,
    NEU_OTEL_TRACE_TYPE_EKUIPER,
} neu_otel_trace_type_e;

/**
 * @brief 请求/响应头部结构体，用于描述消息的基本信息。
 *
 * 此结构体包含了消息类型、上下文指针、发送者与接收者的名称、追踪类型以及消息长度等信息。
 * 它是构建请求和响应消息的基础，提供了必要的元数据以便于消息的处理和路由。
 */
typedef struct neu_reqresp_head {
    /**
     * @brief 消息类型。
     *
     * 使用枚举类型`neu_reqresp_type_e`表示消息的类型，如请求或响应。
     */
    neu_reqresp_type_e type;

    /**
     * @brief 上下文指针。
     *
     * 一个通用指针，用于携带额外的信息或上下文数据，具体用途取决于消息类型。
     */
    void *             ctx;

    /**
     * @brief 发送者名称。
     *
     * 固定长度的字符数组，存储发送者的名称，用于标识消息的来源节点。
     */
    char               sender[NEU_NODE_NAME_LEN];

    /**
     * @brief 接收者名称。
     *
     * 固定长度的字符数组，存储接收者的名称，用于标识消息的目标节点。
     */
    char               receiver[NEU_NODE_NAME_LEN];

    /**
     * @brief 追踪类型。
     *
     * 表示是否以及如何对消息进行分布式追踪，值为`uint8_t`类型。
     */
    uint8_t            otel_trace_type;

    /**
     * @brief 消息长度。
     *
     * 表示消息体的实际长度，单位为字节。
     */
    uint32_t           len;
} neu_reqresp_head_t;

typedef struct neu_resp_error {
    int error;
} neu_resp_error_t;

typedef struct neu_req_driver_action {
    char  driver[NEU_NODE_NAME_LEN];
    char *action;
} neu_req_driver_action_t;

typedef struct neu_resp_driver_action {
    int error;
} neu_resp_driver_action_t;

typedef struct neu_req_check_schema {
    char schema[NEU_PLUGIN_NAME_LEN];
} neu_req_check_schema_t;

typedef struct {
    bool exist;
    char schema[NEU_PLUGIN_NAME_LEN];
} neu_resp_check_schema_t;

typedef struct {
    char                     node[NEU_NODE_NAME_LEN];
    neu_node_running_state_e state;
} neu_req_node_init_t, neu_req_node_uninit_t, neu_resp_node_uninit_t;

typedef struct neu_req_add_plugin {
    char  library[NEU_PLUGIN_LIBRARY_LEN];
    char *schema_file;
    char *so_file;
} neu_req_add_plugin_t;

typedef neu_req_add_plugin_t neu_req_update_plugin_t;

typedef struct neu_req_del_plugin {
    char plugin[NEU_PLUGIN_NAME_LEN];
} neu_req_del_plugin_t;

typedef struct neu_req_get_plugin {
} neu_req_get_plugin_t;

/**
 * @brief 描述插件信息的结构体。
 *
 * 该结构体用于存储插件的详细信息，包括其模式文件名、名称、描述（包括中文描述）、库名称、
 * 插件种类、类型、版本号以及是否为单例插件等。
 */
typedef struct neu_resp_plugin_info {
    /**
     * @brief 插件的模式文件名。
     *
     * 存储插件使用的JSON模式文件名，用于验证配置文件的正确性。
     */
    char schema[NEU_PLUGIN_NAME_LEN];

    /**
     * @brief 插件的名称。
     *
     * 存储插件的唯一标识符名称。
     */
    char name[NEU_PLUGIN_NAME_LEN];

    /**
     * @brief 插件的描述信息。
     *
     * 存储插件的功能描述信息，便于用户了解插件的作用。
     */
    char description[NEU_PLUGIN_DESCRIPTION_LEN];

    /**
     * @brief 插件的中文描述信息。
     *
     * 存储插件的中文功能描述信息，适用于中文用户环境。
     */
    char description_zh[NEU_PLUGIN_DESCRIPTION_LEN];

    /**
     * @brief 插件的库名称。
     *
     * 存储插件对应的共享库或动态链接库的名称。
     */
    char library[NEU_PLUGIN_LIBRARY_LEN];

    /**
     * @brief 插件的种类。
     *
     * 使用 `neu_plugin_kind_e` 枚举类型定义插件的种类，例如系统插件或自定义插件。
     */
    neu_plugin_kind_e kind;

    /**
     * @brief 插件的类型。
     *
     * 使用 `neu_node_type_e` 枚举类型定义插件的类型，例如输入节点、输出节点等。
     */
    neu_node_type_e type;

    /**
     * @brief 插件的版本号。
     *
     * 存储插件的版本号，便于版本管理和更新。
     */
    uint32_t version;

    /**
     * @brief 是否为单例插件。
     *
     * 如果为 `true`，表示该插件是单例插件；否则为普通插件。
     */
    bool single;

    /**
     * @brief 是否显示插件。
     *
     * 如果为 `true`，表示该插件在用户界面中可见；否则不可见。
     */
    bool display;

    /**
     * @brief 单例插件的名称。
     *
     * 如果插件是单例插件，则存储其唯一的实例名称。
     */
    char single_name[NEU_NODE_NAME_LEN];
} neu_resp_plugin_info_t;

typedef struct neu_resp_get_plugin {
    UT_array *plugins; // array neu_resp_plugin_info_t
} neu_resp_get_plugin_t;

typedef struct neu_req_add_node {
    char  node[NEU_NODE_NAME_LEN];
    char  plugin[NEU_PLUGIN_NAME_LEN];
    char *setting;
} neu_req_add_node_t;

static inline void neu_req_add_node_fini(neu_req_add_node_t *req)
{
    free(req->setting);
}

typedef struct neu_req_update_node {
    char node[NEU_NODE_NAME_LEN];
    char new_name[NEU_NODE_NAME_LEN];
} neu_req_update_node_t;

typedef struct neu_req_del_node {
    char node[NEU_NODE_NAME_LEN];
} neu_req_del_node_t;

typedef struct neu_req_get_node {
    neu_node_type_e type;
    char            plugin[NEU_PLUGIN_NAME_LEN];
    char            node[NEU_NODE_NAME_LEN];
} neu_req_get_node_t;

typedef struct neu_resp_node_info {
    char node[NEU_NODE_NAME_LEN];
    char plugin[NEU_PLUGIN_NAME_LEN];
} neu_resp_node_info_t;

typedef struct neu_resp_get_node {
    UT_array *nodes; // array neu_resp_node_info_t
} neu_resp_get_node_t;

typedef struct {
    char     driver[NEU_NODE_NAME_LEN];
    char     group[NEU_GROUP_NAME_LEN];
    uint32_t interval;
} neu_req_add_group_t;

typedef struct {
    char     driver[NEU_NODE_NAME_LEN];
    char     group[NEU_GROUP_NAME_LEN];
    char     new_name[NEU_GROUP_NAME_LEN];
    uint32_t interval;
} neu_req_update_group_t;

typedef struct {
    char driver[NEU_NODE_NAME_LEN];
    char group[NEU_GROUP_NAME_LEN];
    char new_name[NEU_GROUP_NAME_LEN];
    struct {
        int      error;
        uint32_t unused; // padding for memory layout compatible with
                         // neu_req_update_group_t
    };
} neu_resp_update_group_t;

typedef struct neu_req_del_group {
    char driver[NEU_NODE_NAME_LEN];
    char group[NEU_GROUP_NAME_LEN];
} neu_req_del_group_t;

typedef struct neu_req_get_group {
    char driver[NEU_NODE_NAME_LEN];
} neu_req_get_group_t;

typedef struct neu_resp_group_info {
    char     name[NEU_GROUP_NAME_LEN];
    uint16_t tag_count;
    uint32_t interval;
} neu_resp_group_info_t;

typedef struct neu_resp_get_group {
    UT_array *groups; // array neu_resp_group_info_t
} neu_resp_get_group_t;

typedef struct {
    char           group[NEU_GROUP_NAME_LEN];
    int            interval;
    int            n_tag;
    neu_datatag_t *tags;
    void *         context;
} neu_gdatatag_t;

typedef struct {
    char            driver[NEU_NODE_NAME_LEN];
    uint16_t        n_group;
    neu_gdatatag_t *groups;
} neu_req_add_gtag_t;

typedef struct {
    char     driver[NEU_NODE_NAME_LEN];
    char     group[NEU_GROUP_NAME_LEN];
    uint16_t tag_count;
    uint32_t interval;
} neu_resp_driver_group_info_t;

typedef struct {
    UT_array *groups; // array neu_resp_driver_group_info_t
} neu_resp_get_driver_group_t;

typedef struct {
    char           driver[NEU_NODE_NAME_LEN];
    char           group[NEU_GROUP_NAME_LEN];
    uint16_t       n_tag;
    neu_datatag_t *tags;
} neu_req_add_tag_t, neu_req_update_tag_t;

static inline void neu_req_add_tag_fini(neu_req_add_tag_t *req)
{
    for (uint16_t i = 0; i < req->n_tag; i++) {
        neu_tag_fini(&req->tags[i]);
    }
    free(req->tags);
}

static inline int neu_req_add_tag_copy(neu_req_add_tag_t *dst,
                                       neu_req_add_tag_t *src)
{
    strcpy(dst->driver, src->driver);
    strcpy(dst->group, src->group);
    dst->tags = (neu_datatag_t *) calloc(src->n_tag, sizeof(src->tags[0]));
    if (NULL == dst->tags) {
        return -1;
    }
    dst->n_tag = src->n_tag;
    for (uint16_t i = 0; i < src->n_tag; i++) {
        neu_tag_copy(&dst->tags[i], &src->tags[i]);
    }
    return 0;
}

typedef struct {
    uint16_t index;
    int      error;
} neu_resp_add_tag_t, neu_resp_update_tag_t;

typedef struct neu_req_del_tag {
    char     driver[NEU_NODE_NAME_LEN];
    char     group[NEU_GROUP_NAME_LEN];
    uint16_t n_tag;
    char **  tags;
} neu_req_del_tag_t;

static inline void neu_req_del_tag_fini(neu_req_del_tag_t *req)
{
    for (uint16_t i = 0; i < req->n_tag; i++) {
        free(req->tags[i]);
    }
    free(req->tags);
}

static inline int neu_req_del_tag_copy(neu_req_del_tag_t *dst,
                                       neu_req_del_tag_t *src)
{
    strcpy(dst->driver, src->driver);
    strcpy(dst->group, src->group);
    dst->tags = (char **) calloc(src->n_tag, sizeof(src->tags[0]));
    if (NULL == dst->tags) {
        return -1;
    }
    dst->n_tag = src->n_tag;
    for (uint16_t i = 0; i < src->n_tag; ++i) {
        dst->tags[i] = strdup(src->tags[i]);
        if (NULL == dst->tags[i]) {
            while (i-- > 0) {
                free(dst->tags[i]);
            }
            free(dst->tags);
        }
    }
    return 0;
}

typedef struct neu_req_get_tag {
    char driver[NEU_NODE_NAME_LEN];
    char group[NEU_GROUP_NAME_LEN];
    char name[NEU_TAG_NAME_LEN];
} neu_req_get_tag_t;

typedef struct neu_resp_get_tag {
    UT_array *tags; // array neu_datatag_t
} neu_resp_get_tag_t;

typedef struct {
    char     app[NEU_NODE_NAME_LEN];
    char     driver[NEU_NODE_NAME_LEN];
    char     group[NEU_GROUP_NAME_LEN];
    uint16_t port;
    char *   params;
    char *   static_tags;
} neu_req_subscribe_t;

typedef struct {
    char app[NEU_NODE_NAME_LEN];
    char driver[NEU_NODE_NAME_LEN];
    char group[NEU_GROUP_NAME_LEN];
} neu_req_unsubscribe_t;

typedef struct {
    char *   driver;
    char *   group;
    uint16_t port;
    char *   params;
    char *   static_tags;
} neu_req_subscribe_group_info_t;

typedef struct {
    char *                          app;
    uint16_t                        n_group;
    neu_req_subscribe_group_info_t *groups;
} neu_req_subscribe_groups_t;

static inline void
neu_req_subscribe_groups_fini(neu_req_subscribe_groups_t *req)
{
    for (uint16_t i = 0; i < req->n_group; ++i) {
        free(req->groups[i].driver);
        free(req->groups[i].group);
        free(req->groups[i].params);
        free(req->groups[i].static_tags);
    }
    free(req->groups);
    free(req->app);
}

typedef struct neu_req_get_subscribe_group {
    char app[NEU_NODE_NAME_LEN];
    char driver[NEU_NODE_NAME_LEN];
    char group[NEU_GROUP_NAME_LEN];
} neu_req_get_subscribe_group_t;

typedef struct {
    char app[NEU_NODE_NAME_LEN];
} neu_req_get_sub_driver_tags_t;

typedef struct neu_resp_subscribe_info {
    char  app[NEU_NODE_NAME_LEN];
    char  driver[NEU_NODE_NAME_LEN];
    char  group[NEU_GROUP_NAME_LEN];
    char *params;
    char *static_tags;
} neu_resp_subscribe_info_t;

static inline void neu_resp_subscribe_info_fini(neu_resp_subscribe_info_t *info)
{
    free(info->params);
    free(info->static_tags);
}

typedef struct {
    char      driver[NEU_NODE_NAME_LEN];
    char      group[NEU_GROUP_NAME_LEN];
    UT_array *tags;
} neu_resp_get_sub_driver_tags_info_t;
inline static UT_icd *neu_resp_get_sub_driver_tags_info_icd()
{
    static UT_icd icd = { sizeof(neu_resp_get_sub_driver_tags_info_t), NULL,
                          NULL, NULL };
    return &icd;
}

typedef struct {
    UT_array *infos; // array neu_resp_get_sub_driver_tags_info_t
} neu_resp_get_sub_driver_tags_t;

typedef struct neu_resp_get_subscribe_group {
    UT_array *groups; // array neu_resp_subscribe_info_t
} neu_resp_get_subscribe_group_t;

typedef struct neu_req_node_setting {
    char  node[NEU_NODE_NAME_LEN];
    char *setting;
} neu_req_node_setting_t;

static inline void neu_req_node_setting_fini(neu_req_node_setting_t *req)
{
    free(req->setting);
}

static inline int neu_req_node_setting_copy(neu_req_node_setting_t *dst,
                                            neu_req_node_setting_t *src)
{
    strcpy(dst->node, src->node);
    dst->setting = strdup(src->setting);
    return dst->setting ? 0 : -1;
}

typedef struct neu_req_get_node_setting {
    char node[NEU_NODE_NAME_LEN];
} neu_req_get_node_setting_t;

typedef struct neu_resp_get_node_setting {
    char  node[NEU_NODE_NAME_LEN];
    char *setting;
} neu_resp_get_node_setting_t;

typedef enum neu_adapter_ctl {
    NEU_ADAPTER_CTL_START = 0,
    NEU_ADAPTER_CTL_STOP,
} neu_adapter_ctl_e;

typedef struct neu_req_node_ctl {
    char              node[NEU_NODE_NAME_LEN];
    neu_adapter_ctl_e ctl;
} neu_req_node_ctl_t;

typedef struct {
    char node[NEU_NODE_NAME_LEN];
    char new_name[NEU_NODE_NAME_LEN];
} neu_req_node_rename_t;

typedef struct {
    char node[NEU_NODE_NAME_LEN];
    char new_name[NEU_NODE_NAME_LEN];
    int  error;
} neu_resp_node_rename_t;

typedef struct neu_req_get_node_state {
    char node[NEU_NODE_NAME_LEN];
} neu_req_get_node_state_t;

typedef struct neu_resp_get_node_state {
    neu_node_state_t state;
    uint16_t         rtt; // round trip time in milliseconds
    int              core_level;
    uint16_t         sub_group_count;
    bool             is_driver;
} neu_resp_get_node_state_t;

typedef struct neu_req_get_nodes_state {
} neu_req_get_nodes_state_t;

typedef struct {
    char             node[NEU_NODE_NAME_LEN];
    neu_node_state_t state;
    uint16_t         rtt; // round trip time in milliseconds
    uint16_t         sub_group_count;
    bool             is_driver;
} neu_nodes_state_t;

inline static UT_icd neu_nodes_state_t_icd()
{
    UT_icd icd = { sizeof(neu_nodes_state_t), NULL, NULL, NULL };

    return icd;
}
typedef struct {
    UT_array *states; // array of neu_nodes_state_t
    int       core_level;
} neu_resp_get_nodes_state_t, neu_reqresp_nodes_state_t;

typedef struct neu_req_read_group {
    char *   driver;
    char *   group;
    char *   name;
    char *   desc;
    bool     sync;
    uint16_t n_tag;
    char **  tags;
} neu_req_read_group_t;

static inline void neu_req_read_group_fini(neu_req_read_group_t *req)
{
    free(req->driver);
    free(req->group);
    free(req->name);
    free(req->desc);
    if (req->n_tag > 0) {
        for (uint16_t i = 0; i < req->n_tag; i++) {
            free(req->tags[i]);
        }
        free(req->tags);
    }
}

typedef struct neu_req_read_group_paginate {
    char *driver;
    char *group;
    char *name;
    char *desc;
    bool  sync;
    int   current_page;
    int   page_size;
    bool  is_error;
} neu_req_read_group_paginate_t;

static inline void
neu_req_read_group_paginate_fini(neu_req_read_group_paginate_t *req)
{
    free(req->driver);
    free(req->group);
    free(req->name);
    free(req->desc);
}

typedef struct neu_req_write_tag {
    char *       driver;
    char *       group;
    char *       tag;
    neu_dvalue_t value;
} neu_req_write_tag_t;

static inline void neu_req_write_tag_fini(neu_req_write_tag_t *req)
{
    free(req->driver);
    free(req->group);
    free(req->tag);
}

typedef struct neu_resp_tag_value {
    char         tag[NEU_TAG_NAME_LEN];
    neu_dvalue_t value;
} neu_resp_tag_value_t;

typedef neu_resp_tag_value_t neu_tag_value_t;

/**
 * @brief 用于存储标签值及其元数据信息的结构体。
 *
 * 此结构体包含了与特定标签相关的所有必要信息，包括标签名称、标签值、
 * 元数据数组和数据标签的详细信息。它在数据响应场景中使用，以完整地
 * 描述一个数据点的所有相关信息。
 * 
 * @note
 * 
 * 在数据响应场景中完整地描述一个数据点的所有相关信息。它是一个综合
 * 性的数据结构，用于将标签名称、标签值、元数据以及数据标签的详细信
 * 息整合在一起，方便在不同模块之间传递和处理与特定标签相关的完整数据。
 */
typedef struct neu_resp_tag_value_meta {
    /**
     * @brief 标签名称。
     *
     * 这是一个固定大小的字符数组，用于存储标签的名称。
     */
    char           tag[NEU_TAG_NAME_LEN];

    /**
     * @brief 标签值。
     *
     * 包含标签的实际值。这个字段可以表示不同类型的数据值
     * （如整数、浮点数等）。
     */
    neu_dvalue_t   value;

    /**
     * @brief 元数据数组。
     *
     * 存储了一组与标签相关的元数据信息，
     */
    neu_tag_meta_t metas[NEU_TAG_META_SIZE];

    /**
     * @brief 数据标签的详细信息。
     *
     * 包含关于数据标签的更详细信息，例如属性、精度、地址等。
     */
    neu_datatag_t  datatag;
} neu_resp_tag_value_meta_t;

/**
 * @brief 获取用于管理 neu_resp_tag_value_meta_t 类型数据的 UT_icd 接口。
 *
 * 此函数返回一个静态定义的 UT_icd 结构体的地址，该结构体包含了处理 neu_resp_tag_value_meta_t 
 * 类型数据的创建(create)、拷贝(copy)、释放(dtor)和清除(clear)操作所需的信息。
 * 在这个例子中，仅定义了元素的大小，其他操作（初始化、拷贝、释放）均未定义。
 *
 * @return 返回指向静态定义的 icd 的指针，即 neu_resp_tag_value_meta_t 类型的 UT_icd 结构体。
 */
static inline UT_icd *neu_resp_tag_value_meta_icd()
{   
    // 静态定义的 UT_icd 结构体，包含元素大小和其他操作的函数指针（如果有的话）
    static UT_icd icd = { sizeof(neu_resp_tag_value_meta_t), NULL, NULL, NULL };
    
    // 返回静态定义的 icd 的地址
    return &icd;
}

typedef struct neu_req_write_tags {
    char *driver;
    char *group;

    int                   n_tag;
    neu_resp_tag_value_t *tags;
} neu_req_write_tags_t;

static inline void neu_req_write_tags_fini(neu_req_write_tags_t *req)
{
    free(req->driver);
    free(req->group);
    free(req->tags);
}

typedef struct {
    char *group;

    int                   n_tag;
    neu_resp_tag_value_t *tags;
} neu_req_gtag_group_t;

static inline void neu_req_gtag_group_fini(neu_req_gtag_group_t *req)
{
    free(req->group);
    free(req->tags);
}

typedef struct {
    char *driver;

    int                   n_group;
    neu_req_gtag_group_t *groups;
} neu_req_write_gtags_t;

static inline void neu_req_write_gtags_fini(neu_req_write_gtags_t *req)
{
    free(req->driver);
    for (int i = 0; i < req->n_group; ++i) {
        neu_req_gtag_group_fini(&req->groups[i]);
    }
    free(req->groups);
}

typedef struct {
    char *driver;
    char *group;

    UT_array *tags; // neu_resp_tag_value_meta_t
} neu_resp_read_group_t;

typedef struct neu_resp_tag_value_meta_paginate {
    char           tag[NEU_TAG_NAME_LEN];
    neu_dvalue_t   value;
    neu_tag_meta_t metas[NEU_TAG_META_SIZE];
    neu_datatag_t  datatag;
} neu_resp_tag_value_meta_paginate_t;

static inline UT_icd *neu_resp_tag_value_meta_paginate_icd()
{
    static UT_icd icd = { sizeof(neu_resp_tag_value_meta_paginate_t), NULL,
                          NULL, NULL };
    return &icd;
}

typedef struct {
    char *driver;
    char *group;
    int   current_page;
    int   page_size;
    bool  is_error;
    int   total_count; // tags count without pagination

    UT_array *tags; // neu_resp_tag_value_meta_paginate_t
} neu_resp_read_group_paginate_t;

static inline void neu_resp_read_free(neu_resp_read_group_t *resp)
{
    utarray_foreach(resp->tags, neu_resp_tag_value_meta_t *, tag_value)
    {
        if (tag_value->value.type == NEU_TYPE_PTR) {
            free(tag_value->value.value.ptr.ptr);
        }
    }
    free(resp->driver);
    free(resp->group);
    utarray_free(resp->tags);
}

static inline void
neu_resp_read_paginate_free(neu_resp_read_group_paginate_t *resp)
{
    utarray_foreach(resp->tags, neu_resp_tag_value_meta_t *, tag_value)
    {
        if (tag_value->value.type == NEU_TYPE_PTR) {
            free(tag_value->value.value.ptr.ptr);
        }
    }
    free(resp->driver);
    free(resp->group);
    utarray_free(resp->tags);
}

/**
 * @brief 用于存储传输数据请求和响应上下文信息的结构体。
 *
 * 此结构体包含了与数据传输相关的上下文信息，包括一个索引和一个互斥锁，
 * 主要用于在多线程环境中同步对共享资源的访问。
 */
typedef struct {
    /**
     * @brief 索引。
     *
     * 这个集合指的是与某个组（group_t 结构体中的 apps 数组）关联的应用实例集合
     *  具体来说，它的值等于 apps 数组的长度
     */
    uint16_t        index;

    /**
     * @brief 互斥锁。
     *
     * 用于确保在多线程环境中对共享资源的安全访问。通过使用互斥锁，
     * 可以避免多个线程同时修改同一资源导致的数据不一致问题。
     */
    pthread_mutex_t mtx;
} neu_reqresp_trans_data_ctx_t;

/**
 * @brief 用于存储传输数据请求和响应相关信息的结构体。
 *
 * 此结构体包含了一系列字段，用于描述从驱动程序到应用程序的数据传输请求或响应。
 * 它包含了目标驱动程序名称、组信息、跟踪上下文、上下文信息以及标签值元数据数组。
 */
typedef struct {
    /**
     * @brief 目标驱动程序的名称。
     *
     * 这个字段指定了数据传输的目标驱动程序名称，通常是字符串形式。
     */
    char *driver;

    /**
     * @brief 组名称。
     *
     * 这个字段指定了与数据传输相关的组名称，也是字符串形式。
     */
    char *group;

    /**
     * @brief 跟踪上下文。
     *
     * 用于分布式跟踪系统的上下文信息。可以为空，如果不需要跟踪的话。
     */
    void *trace_ctx;

    /**
     * @brief 上下文信息。
     *
     * 包含了额外的上下文信息
     */
    neu_reqresp_trans_data_ctx_t *ctx;

    /**
     * @brief 标签值元数据数组。
     *
     * 存储了一组标签值元数据（neu_resp_tag_value_meta_t类型），这些是实际要传输的数据。
     */
    UT_array *                    tags; 
} neu_reqresp_trans_data_t;

typedef struct {
    char *          node;
    char *          plugin;
    char *          setting;
    uint16_t        n_group;
    neu_gdatatag_t *groups;
} neu_req_driver_t;

static inline void neu_req_driver_fini(neu_req_driver_t *req)
{
    free(req->node);
    free(req->plugin);
    free(req->setting);
    for (uint16_t i = 0; i < req->n_group; i++) {
        for (int j = 0; j < req->groups[i].n_tag; j++) {
            neu_tag_fini(&req->groups[i].tags[j]);
        }
        free(req->groups[i].tags);
    }
    free(req->groups);
}

typedef struct {
    uint16_t          n_driver;
    neu_req_driver_t *drivers;
} neu_req_driver_array_t;

static inline void neu_req_driver_array_fini(neu_req_driver_array_t *req)
{
    for (uint16_t i = 0; i < req->n_driver; ++i) {
        neu_req_driver_fini(&req->drivers[i]);
    }
    free(req->drivers);
}

static inline void neu_trans_data_free(neu_reqresp_trans_data_t *data)
{
    pthread_mutex_lock(&data->ctx->mtx);
    if (data->ctx->index > 0) {
        data->ctx->index -= 1;
    }

    if (data->ctx->index == 0) {
        utarray_foreach(data->tags, neu_resp_tag_value_meta_t *, tag_value)
        {
            if (tag_value->value.type == NEU_TYPE_PTR) {
                free(tag_value->value.value.ptr.ptr);
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
        pthread_mutex_unlock(&data->ctx->mtx);
        pthread_mutex_destroy(&data->ctx->mtx);
        free(data->ctx);
    } else {
        pthread_mutex_unlock(&data->ctx->mtx);
    }
}

static inline void neu_tag_value_to_json(neu_resp_tag_value_meta_t *tag_value,
                                         neu_json_read_resp_tag_t * tag_json)
{
    tag_json->name  = tag_value->tag;
    tag_json->error = 0;

    for (int k = 0; k < NEU_TAG_META_SIZE; k++) {
        if (strlen(tag_value->metas[k].name) > 0) {
            tag_json->n_meta++;
        } else {
            break;
        }
    }
    if (tag_json->n_meta > 0) {
        tag_json->metas = (neu_json_tag_meta_t *) calloc(
            tag_json->n_meta, sizeof(neu_json_tag_meta_t));
    }
    neu_json_metas_to_json(tag_value->metas, NEU_TAG_META_SIZE, tag_json);

    tag_json->datatag.bias = tag_value->datatag.bias;

    switch (tag_value->value.type) {
    case NEU_TYPE_ERROR:
        tag_json->t             = NEU_JSON_INT;
        tag_json->value.val_int = tag_value->value.value.i32;
        tag_json->error         = tag_value->value.value.i32;
        break;
    case NEU_TYPE_UINT8:
        tag_json->t             = NEU_JSON_INT;
        tag_json->value.val_int = tag_value->value.value.u8;
        break;
    case NEU_TYPE_INT8:
        tag_json->t             = NEU_JSON_INT;
        tag_json->value.val_int = tag_value->value.value.i8;
        break;
    case NEU_TYPE_INT16:
        tag_json->t             = NEU_JSON_INT;
        tag_json->value.val_int = tag_value->value.value.i16;
        break;
    case NEU_TYPE_INT32:
        tag_json->t             = NEU_JSON_INT;
        tag_json->value.val_int = tag_value->value.value.i32;
        break;
    case NEU_TYPE_INT64:
        tag_json->t             = NEU_JSON_INT;
        tag_json->value.val_int = tag_value->value.value.i64;
        break;
    case NEU_TYPE_WORD:
    case NEU_TYPE_UINT16:
        tag_json->t             = NEU_JSON_INT;
        tag_json->value.val_int = tag_value->value.value.u16;
        break;
    case NEU_TYPE_DWORD:
    case NEU_TYPE_UINT32:
        tag_json->t             = NEU_JSON_INT;
        tag_json->value.val_int = tag_value->value.value.u32;
        break;
    case NEU_TYPE_LWORD:
    case NEU_TYPE_UINT64:
        tag_json->t             = NEU_JSON_INT;
        tag_json->value.val_int = tag_value->value.value.u64;
        break;
    case NEU_TYPE_FLOAT:
        if (isnan(tag_value->value.value.f32)) {
            tag_json->t               = NEU_JSON_FLOAT;
            tag_json->value.val_float = tag_value->value.value.f32;
            tag_json->error           = NEU_ERR_PLUGIN_TAG_VALUE_EXPIRED;
        } else {
            tag_json->t               = NEU_JSON_FLOAT;
            tag_json->value.val_float = tag_value->value.value.f32;
            tag_json->precision       = tag_value->value.precision;
        }
        break;
    case NEU_TYPE_DOUBLE:
        if (isnan(tag_value->value.value.d64)) {
            tag_json->t                = NEU_JSON_DOUBLE;
            tag_json->value.val_double = tag_value->value.value.d64;
            tag_json->error            = NEU_ERR_PLUGIN_TAG_VALUE_EXPIRED;
        } else {
            tag_json->t                = NEU_JSON_DOUBLE;
            tag_json->value.val_double = tag_value->value.value.d64;
            tag_json->precision        = tag_value->value.precision;
        }
        break;
    case NEU_TYPE_BOOL:
        tag_json->t              = NEU_JSON_BOOL;
        tag_json->value.val_bool = tag_value->value.value.boolean;
        break;
    case NEU_TYPE_BIT:
        tag_json->t             = NEU_JSON_BIT;
        tag_json->value.val_bit = tag_value->value.value.u8;
        break;
    case NEU_TYPE_STRING:
    case NEU_TYPE_TIME:
    case NEU_TYPE_DATA_AND_TIME:
    case NEU_TYPE_ARRAY_CHAR:
        tag_json->t             = NEU_JSON_STR;
        tag_json->value.val_str = tag_value->value.value.str;
        break;
    case NEU_TYPE_PTR:
        tag_json->t             = NEU_JSON_STR;
        tag_json->value.val_str = (char *) tag_value->value.value.ptr.ptr;
        break;
    case NEU_TYPE_BYTES:
        tag_json->t = NEU_JSON_ARRAY_UINT8;
        tag_json->value.val_array_uint8.length =
            tag_value->value.value.bytes.length;
        tag_json->value.val_array_uint8.u8s =
            tag_value->value.value.bytes.bytes;
        break;
    case NEU_TYPE_ARRAY_BOOL:
        tag_json->t = NEU_JSON_ARRAY_BOOL;
        tag_json->value.val_array_bool.length =
            tag_value->value.value.bools.length;
        tag_json->value.val_array_bool.bools =
            tag_value->value.value.bools.bools;
        break;
    case NEU_TYPE_ARRAY_INT8:
        tag_json->t = NEU_JSON_ARRAY_INT8;
        tag_json->value.val_array_int8.length =
            tag_value->value.value.i8s.length;
        tag_json->value.val_array_int8.i8s = tag_value->value.value.i8s.i8s;
        break;
    case NEU_TYPE_ARRAY_UINT8:
        tag_json->t = NEU_JSON_ARRAY_UINT8;
        tag_json->value.val_array_uint8.length =
            tag_value->value.value.u8s.length;
        tag_json->value.val_array_uint8.u8s = tag_value->value.value.u8s.u8s;
        break;
    case NEU_TYPE_ARRAY_INT16:
        tag_json->t = NEU_JSON_ARRAY_INT16;
        tag_json->value.val_array_int16.length =
            tag_value->value.value.i16s.length;
        tag_json->value.val_array_int16.i16s = tag_value->value.value.i16s.i16s;
        break;
    case NEU_TYPE_ARRAY_UINT16:
        tag_json->t = NEU_JSON_ARRAY_UINT16;
        tag_json->value.val_array_uint16.length =
            tag_value->value.value.u16s.length;
        tag_json->value.val_array_uint16.u16s =
            tag_value->value.value.u16s.u16s;
        break;
    case NEU_TYPE_ARRAY_INT32:
        tag_json->t = NEU_JSON_ARRAY_INT32;
        tag_json->value.val_array_int32.length =
            tag_value->value.value.i32s.length;
        tag_json->value.val_array_int32.i32s = tag_value->value.value.i32s.i32s;
        break;
    case NEU_TYPE_ARRAY_UINT32:
        tag_json->t = NEU_JSON_ARRAY_UINT32;
        tag_json->value.val_array_uint32.length =
            tag_value->value.value.u32s.length;
        tag_json->value.val_array_uint32.u32s =
            tag_value->value.value.u32s.u32s;
        break;
    case NEU_TYPE_ARRAY_INT64:
        tag_json->t = NEU_JSON_ARRAY_INT64;
        tag_json->value.val_array_int64.length =
            tag_value->value.value.i64s.length;
        tag_json->value.val_array_int64.i64s = tag_value->value.value.i64s.i64s;
        break;
    case NEU_TYPE_ARRAY_UINT64:
        tag_json->t = NEU_JSON_ARRAY_UINT64;
        tag_json->value.val_array_uint64.length =
            tag_value->value.value.u64s.length;
        tag_json->value.val_array_uint64.u64s =
            tag_value->value.value.u64s.u64s;
        break;
    case NEU_TYPE_ARRAY_FLOAT:
        tag_json->t = NEU_JSON_ARRAY_FLOAT;
        tag_json->value.val_array_float.length =
            tag_value->value.value.f32s.length;
        tag_json->value.val_array_float.f32s = tag_value->value.value.f32s.f32s;
        break;
    case NEU_TYPE_ARRAY_DOUBLE:
        tag_json->t = NEU_JSON_ARRAY_DOUBLE;
        tag_json->value.val_array_double.length =
            tag_value->value.value.f64s.length;
        tag_json->value.val_array_double.f64s =
            tag_value->value.value.f64s.f64s;
        break;
    case NEU_TYPE_ARRAY_STRING:
        tag_json->t = NEU_JSON_ARRAY_STR;
        tag_json->value.val_array_str.length =
            tag_value->value.value.strs.length;
        tag_json->value.val_array_str.p_strs = tag_value->value.value.strs.strs;
        break;
    case NEU_TYPE_CUSTOM:
        tag_json->t                = NEU_JSON_OBJECT;
        tag_json->value.val_object = tag_value->value.value.json;
        break;
    default:
        break;
    }
}

static inline void
neu_tag_value_to_json_paginate(neu_resp_tag_value_meta_paginate_t *tag_value,
                               neu_json_read_paginate_resp_tag_t * tag_json)
{
    tag_json->name  = tag_value->tag;
    tag_json->error = 0;

    tag_json->datatag.name        = strdup(tag_value->datatag.name);
    tag_json->datatag.address     = strdup(tag_value->datatag.address);
    tag_json->datatag.attribute   = tag_value->datatag.attribute;
    tag_json->datatag.type        = tag_value->datatag.type;
    tag_json->datatag.precision   = tag_value->datatag.precision;
    tag_json->datatag.decimal     = tag_value->datatag.decimal;
    tag_json->datatag.bias        = tag_value->datatag.bias;
    tag_json->datatag.description = strdup(tag_value->datatag.description);
    tag_json->datatag.option      = tag_value->datatag.option;
    memcpy(tag_json->datatag.meta, tag_value->datatag.meta,
           NEU_TAG_META_LENGTH);

    for (int k = 0; k < NEU_TAG_META_SIZE; k++) {
        if (strlen(tag_value->metas[k].name) > 0) {
            tag_json->n_meta++;
        } else {
            break;
        }
    }
    if (tag_json->n_meta > 0) {
        tag_json->metas = (neu_json_tag_meta_t *) calloc(
            tag_json->n_meta, sizeof(neu_json_tag_meta_t));
    }
    neu_json_metas_to_json_paginate(tag_value->metas, NEU_TAG_META_SIZE,
                                    tag_json);

    switch (tag_value->value.type) {
    case NEU_TYPE_ERROR:
        tag_json->t             = NEU_JSON_INT;
        tag_json->value.val_int = tag_value->value.value.i32;
        tag_json->error         = tag_value->value.value.i32;
        break;
    case NEU_TYPE_UINT8:
        tag_json->t             = NEU_JSON_INT;
        tag_json->value.val_int = tag_value->value.value.u8;
        break;
    case NEU_TYPE_INT8:
        tag_json->t             = NEU_JSON_INT;
        tag_json->value.val_int = tag_value->value.value.i8;
        break;
    case NEU_TYPE_INT16:
        tag_json->t             = NEU_JSON_INT;
        tag_json->value.val_int = tag_value->value.value.i16;
        break;
    case NEU_TYPE_INT32:
        tag_json->t             = NEU_JSON_INT;
        tag_json->value.val_int = tag_value->value.value.i32;
        break;
    case NEU_TYPE_INT64:
        tag_json->t             = NEU_JSON_INT;
        tag_json->value.val_int = tag_value->value.value.i64;
        break;
    case NEU_TYPE_WORD:
    case NEU_TYPE_UINT16:
        tag_json->t             = NEU_JSON_INT;
        tag_json->value.val_int = tag_value->value.value.u16;
        break;
    case NEU_TYPE_DWORD:
    case NEU_TYPE_UINT32:
        tag_json->t             = NEU_JSON_INT;
        tag_json->value.val_int = tag_value->value.value.u32;
        break;
    case NEU_TYPE_LWORD:
    case NEU_TYPE_UINT64:
        tag_json->t             = NEU_JSON_INT;
        tag_json->value.val_int = tag_value->value.value.u64;
        break;
    case NEU_TYPE_FLOAT:
        if (isnan(tag_value->value.value.f32)) {
            tag_json->t               = NEU_JSON_FLOAT;
            tag_json->value.val_float = tag_value->value.value.f32;
            tag_json->error           = NEU_ERR_PLUGIN_TAG_VALUE_EXPIRED;
        } else {
            tag_json->t               = NEU_JSON_FLOAT;
            tag_json->value.val_float = tag_value->value.value.f32;
            tag_json->precision       = tag_value->value.precision;
        }
        break;
    case NEU_TYPE_DOUBLE:
        if (isnan(tag_value->value.value.d64)) {
            tag_json->t                = NEU_JSON_DOUBLE;
            tag_json->value.val_double = tag_value->value.value.d64;
            tag_json->error            = NEU_ERR_PLUGIN_TAG_VALUE_EXPIRED;
        } else {
            tag_json->t                = NEU_JSON_DOUBLE;
            tag_json->value.val_double = tag_value->value.value.d64;
            tag_json->precision        = tag_value->value.precision;
        }
        break;
    case NEU_TYPE_BOOL:
        tag_json->t              = NEU_JSON_BOOL;
        tag_json->value.val_bool = tag_value->value.value.boolean;
        break;
    case NEU_TYPE_BIT:
        tag_json->t             = NEU_JSON_BIT;
        tag_json->value.val_bit = tag_value->value.value.u8;
        break;
    case NEU_TYPE_STRING:
    case NEU_TYPE_TIME:
    case NEU_TYPE_DATA_AND_TIME:
    case NEU_TYPE_ARRAY_CHAR:
        tag_json->t             = NEU_JSON_STR;
        tag_json->value.val_str = tag_value->value.value.str;
        break;
    case NEU_TYPE_PTR:
        tag_json->t             = NEU_JSON_STR;
        tag_json->value.val_str = (char *) tag_value->value.value.ptr.ptr;
        break;
    case NEU_TYPE_BYTES:
        tag_json->t = NEU_JSON_ARRAY_UINT8;
        tag_json->value.val_array_uint8.length =
            tag_value->value.value.bytes.length;
        tag_json->value.val_array_uint8.u8s =
            tag_value->value.value.bytes.bytes;
        break;
    case NEU_TYPE_ARRAY_BOOL:
        tag_json->t = NEU_JSON_ARRAY_BOOL;
        tag_json->value.val_array_bool.length =
            tag_value->value.value.bools.length;
        tag_json->value.val_array_bool.bools =
            tag_value->value.value.bools.bools;
        break;
    case NEU_TYPE_ARRAY_INT8:
        tag_json->t = NEU_JSON_ARRAY_INT8;
        tag_json->value.val_array_int8.length =
            tag_value->value.value.i8s.length;
        tag_json->value.val_array_int8.i8s = tag_value->value.value.i8s.i8s;
        break;
    case NEU_TYPE_ARRAY_UINT8:
        tag_json->t = NEU_JSON_ARRAY_UINT8;
        tag_json->value.val_array_uint8.length =
            tag_value->value.value.u8s.length;
        tag_json->value.val_array_uint8.u8s = tag_value->value.value.u8s.u8s;
        break;
    case NEU_TYPE_ARRAY_INT16:
        tag_json->t = NEU_JSON_ARRAY_INT16;
        tag_json->value.val_array_int16.length =
            tag_value->value.value.i16s.length;
        tag_json->value.val_array_int16.i16s = tag_value->value.value.i16s.i16s;
        break;
    case NEU_TYPE_ARRAY_UINT16:
        tag_json->t = NEU_JSON_ARRAY_UINT16;
        tag_json->value.val_array_uint16.length =
            tag_value->value.value.u16s.length;
        tag_json->value.val_array_uint16.u16s =
            tag_value->value.value.u16s.u16s;
        break;
    case NEU_TYPE_ARRAY_INT32:
        tag_json->t = NEU_JSON_ARRAY_INT32;
        tag_json->value.val_array_int32.length =
            tag_value->value.value.i32s.length;
        tag_json->value.val_array_int32.i32s = tag_value->value.value.i32s.i32s;
        break;
    case NEU_TYPE_ARRAY_UINT32:
        tag_json->t = NEU_JSON_ARRAY_UINT32;
        tag_json->value.val_array_uint32.length =
            tag_value->value.value.u32s.length;
        tag_json->value.val_array_uint32.u32s =
            tag_value->value.value.u32s.u32s;
        break;
    case NEU_TYPE_ARRAY_INT64:
        tag_json->t = NEU_JSON_ARRAY_INT64;
        tag_json->value.val_array_int64.length =
            tag_value->value.value.i64s.length;
        tag_json->value.val_array_int64.i64s = tag_value->value.value.i64s.i64s;
        break;
    case NEU_TYPE_ARRAY_UINT64:
        tag_json->t = NEU_JSON_ARRAY_UINT64;
        tag_json->value.val_array_uint64.length =
            tag_value->value.value.u64s.length;
        tag_json->value.val_array_uint64.u64s =
            tag_value->value.value.u64s.u64s;
        break;
    case NEU_TYPE_ARRAY_FLOAT:
        tag_json->t = NEU_JSON_ARRAY_FLOAT;
        tag_json->value.val_array_float.length =
            tag_value->value.value.f32s.length;
        tag_json->value.val_array_float.f32s = tag_value->value.value.f32s.f32s;
        break;
    case NEU_TYPE_ARRAY_DOUBLE:
        tag_json->t = NEU_JSON_ARRAY_DOUBLE;
        tag_json->value.val_array_double.length =
            tag_value->value.value.f64s.length;
        tag_json->value.val_array_double.f64s =
            tag_value->value.value.f64s.f64s;
        break;
    case NEU_TYPE_ARRAY_STRING:
        tag_json->t = NEU_JSON_ARRAY_STR;
        tag_json->value.val_array_str.length =
            tag_value->value.value.strs.length;
        tag_json->value.val_array_str.p_strs = tag_value->value.value.strs.strs;
        break;
    case NEU_TYPE_CUSTOM:
        tag_json->t                = NEU_JSON_OBJECT;
        tag_json->value.val_object = tag_value->value.value.json;
        break;
    default:
        break;
    }
}

typedef struct {
    char node[NEU_NODE_NAME_LEN];
} neu_reqresp_node_deleted_t;

typedef struct neu_req_update_log_level {
    char node[NEU_NODE_NAME_LEN];
    int  log_level;
    bool core;
} neu_req_update_log_level_t;

void neu_msg_gen(neu_reqresp_head_t *header, void *data);

/**
 * @brief 交换消息头中的发送者和接收者信息。
 *
 * 此函数用于交换 `neu_reqresp_head_t` 结构体中 `sender` 和 `receiver` 字段的内容。
 *
 * @param header 指向 `neu_reqresp_head_t` 结构体的指针，表示要交换发送者和接收者信息的消息头部。
 */
inline static void neu_msg_exchange(neu_reqresp_head_t *header)
{
    char tmp[NEU_NODE_NAME_LEN] = { 0 };

    // 将 sender 字段的内容复制到临时缓冲区
    strcpy(tmp, header->sender);

    // 清空 sender 字段并用 receiver 字段的内容填充
    memset(header->sender, 0, sizeof(header->sender));
    strcpy(header->sender, header->receiver);

    // 清空 receiver 字段并用临时缓冲区中的内容填充
    memset(header->receiver, 0, sizeof(header->receiver));
    strcpy(header->receiver, tmp);
}

typedef struct {
    char driver[NEU_NODE_NAME_LEN];
    char path[128];
    bool del_flag;
    char prg_str_param1[128];
    char prg_str_param2[128];
    char prg_str_param3[128];
    char prg_str_param4[128];
} neu_req_prgfile_upload_t;

typedef struct {
    char driver[NEU_NODE_NAME_LEN];
} neu_req_prgfile_process_t;

typedef struct {
    char path[128];
    int  state;
    char reason[256];
} neu_resp_prgfile_process_t;

typedef struct neu_req_scan_tags {
    char driver[NEU_NODE_NAME_LEN];
    char id[NEU_TAG_ADDRESS_LEN];
    char ctx[NEU_VALUE_SIZE];
} neu_req_scan_tags_t;

typedef struct {
    char       name[NEU_TAG_NAME_LEN];
    char       id[5 + 1 + NEU_TAG_ADDRESS_LEN]; // ns + ! + address
    uint8_t    tag;
    bool       is_last_layer;
    neu_type_e type;
} neu_scan_tag_t;

/**
 * @brief 扫描标签响应的数据结构。
 *
 * 此结构体用于存储从适配器收到的扫描标签的响应信息。
 * 它包括了扫描到的标签列表、错误码、数据类型、是否为数组以及上下文信息。
 */
typedef struct {
    /** @brief 标签数组，使用UT_array存储多个扫描到的标签。*/
    UT_array * scan_tags;

    /** @brief 错误码，0表示成功，其他值表示具体的错误。*/
    int32_t    error;

    /** @brief 数据类型，指示标签的数据类型。*/
    neu_type_e type;

    /** @brief 是否为数组，true表示该标签代表一个数组，false表示单个值。*/
    bool       is_array;

    /** @brief 上下文信息，通常用于携带额外的信息或状态。*/
    char       ctx[NEU_VALUE_SIZE];
} neu_resp_scan_tags_t;

typedef struct {
    char       name[NEU_TAG_NAME_LEN];
    char       id[5 + 1 + NEU_TAG_ADDRESS_LEN];
    neu_type_e type;
} neu_scan_tag_attribute_t;

typedef struct neu_req_test_rea_tag {
    char                      driver[NEU_NODE_NAME_LEN];
    char                      group[NEU_GROUP_NAME_LEN];
    char                      tag[NEU_TAG_NAME_LEN];
    char                      address[NEU_TAG_ADDRESS_LEN];
    neu_attribute_e           attribute;
    neu_type_e                type;
    uint8_t                   precision;
    double                    decimal;
    double                    bias;
    neu_datatag_addr_option_u option;
} neu_req_test_read_tag_t;

/**
 * @brief 用于表示测试读取标签响应的数据结构。
 *
 * 此结构体包含从适配器收到的测试读取标签响应的所有必要信息，包括数据类型、JSON类型、值和错误码。
 */
typedef struct {
    /** @brief 数据类型，指示标签的具体数据类型（例如整型、浮点型等）。 */
    neu_type_e       type;

    /** @brief JSON类型，指示值在JSON格式中的类型。 */
    neu_json_type_e  t;

    /** @brief 值（联合体），根据数据类型存储实际的值。 */
    neu_json_value_u value;

    /** @brief 错误码，0表示成功，其他值表示具体的错误。 */
    int64_t          error;
} neu_resp_test_read_tag_t;

#ifdef __cplusplus
}
#endif

#endif
