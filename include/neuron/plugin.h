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

#ifndef NEURON_PLUGIN_H
#define NEURON_PLUGIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "utils/utextend.h"
#include "utils/zlog.h"

#include "event/event.h"

#include "adapter.h"
#include "define.h"
#include "type.h"

#define NEURON_PLUGIN_VER_1_0 \
    NEU_VERSION(NEU_VERSION_MAJOR, NEU_VERSION_MINOR, NEU_VERSION_FIX)

#define NEU_PLUGIN_REGISTER_METRIC(plugin, name, init) \
    plugin->common.adapter_callbacks->register_metric( \
        plugin->common.adapter, name, name##_HELP, name##_TYPE, init)

#define NEU_PLUGIN_UPDATE_METRIC(plugin, name, val, grp)                    \
    plugin->common.adapter_callbacks->update_metric(plugin->common.adapter, \
                                                    name, val, grp)

extern int64_t global_timestamp;

/**
 * @brief 插件公共部分结构体，用于描述一个插件的基础信息和通用配置。
 */
typedef struct neu_plugin_common {
    /**
     * @brief 魔数。
     *
     * 用于验证数据结构的有效性，通常是一个固定的值，确保该结构体在
     * 初始化时被正确设置。
     */
    uint32_t                   magic;

    /**
     * @brief 指向neu_adapter_t类型的指针，表示与插件关联的适配器实例。
     *
     * 适配器用于处理插件与外部系统的交互逻辑。
     */
    neu_adapter_t             *adapter;

    /**
     * @brief 指向adapter_callbacks_t类型的常量指针，表示适配器的回调函数集合。
     *
     * 这些回调函数允许插件与适配器之间进行通信和协作。
     */
    const adapter_callbacks_t *adapter_callbacks;

    /**
     * @brief 插件名称。
     *
     * 一个固定长度的字符串数组，存储插件的名称，用于标识不同的插件实例。
     */
    char                       name[NEU_NODE_NAME_LEN];

    /**
     * @brief 连接状态。
     *
     * 表示插件当前的连接状态，例如连接正常、连接断开等。
     */
    neu_node_link_state_e      link_state;

    /**
     * @brief 日志级别。
     *
     * 用于控制日志输出的详细程度。
     */
    char                       log_level[NEU_LOG_LEVEL_LEN];

    /**
     * @brief 日志类别。
     *
     * 用于指定插件的日志记录类别，便于分类管理和查看日志信息。
     */
    zlog_category_t           *log;
} neu_plugin_common_t;

typedef struct neu_plugin neu_plugin_t;

typedef struct neu_plugin_group neu_plugin_group_t;
typedef void (*neu_plugin_group_free)(neu_plugin_group_t *pgp);

/**
 * @brief 插件组信息结构体。
 *
 * 该结构体主要用于管理与组相关的插件实现的具体细节，包括标签信息、上下文信息、用户数据等。
 * 它为插件提供了一种灵活的方式来处理与组相关的数据和操作，同时确保与其他组件的解耦和独立性。
 */
struct neu_plugin_group {
    /**
     * @brief 组名称。
     *
     * 该字段存储插件组的唯一标识符，通常用于引用或识别特定的数据标签组。
     */
    char *    group_name;

    /**
     * @brief 数据标签数组。
     *
     * 满足插件对于标签信息的特殊需求。例如，某个插件可能只关注组内部分标签
     * 的特定属性，将这些标签筛选出来存储在 neu_plugin_group 的 tags 
     * 中，以便插件进行特定的处理或操作，而不会影响 neu_group_t 中原始标
     * 签信息的管理
     */
    UT_array *tags;

    /**
     * @brief 上下文信息。
     *
     * 提供给插件实现使用的上下文指针，可用于存储与该组相关的状态信息或其他资源。
     */
    void *                context;

    /**
     * @brief 用户数据。
     *
     * 允许插件使用者传递任意数据到插件内部，以便在处理该组时使用。
     */
    void *                user_data;

    /**
     * @brief 组释放函数。
     *
     * 当不再需要该组时调用的函数指针，用于释放组相关的资源。
     */
    neu_plugin_group_free group_free;

    /**
     * @brief 同步间隔（单位：毫秒）。
     *
     * 指定该组中所有数据标签同步的时间间隔，适用于定时同步操作。
     */
    uint32_t              interval;
};

typedef int (*neu_plugin_tag_validator_t)(const neu_datatag_t *tag);

typedef struct {
    neu_datatag_t *tag;
    neu_value_u    value;
} neu_plugin_tag_value_t;

/**
 * @brief 插件接口函数集合结构体，定义了一组用于管理和操作插件的核心函数。
 *
 * 此结构体包含了一系列函数指针，提供了主程序与插件之间交互的接口。这些函数
 * 指针分别对应插件生命周期中的不同阶段以及其功能实现。通过这种设计，可以提
 * 供一种统一的方式来加载、初始化、启动、停止和关闭插件，并执行各种操作如设
 * 置配置、请求处理等。
 * 
 * @note
 * 结构体起到了一个接口规范的作用，它定义了主程序与插件之间交互的契约。主程
 * 序通过这个结构体中的函数指针来调用插件提供的功能，而不需要关心插件具体是
 * 如何实现这些功能的。例如，主程序只需要知道可以调用 init 函数来初始化插件
 * 、调用 process_data 函数来处理数据，但具体的初始化步骤和数据处理逻辑是
 * 由插件开发者实现的。
 */
typedef struct neu_plugin_intf_funs {
    /**
     * @brief 打开插件并返回插件实例。
     *
     * @return 返回指向neu_plugin_t类型的指针，表示插件实例。
     */
    neu_plugin_t *(*open)(void);

    /**
     * @brief 关闭插件，释放资源。
     *
     * @param plugin 指向neu_plugin_t类型的指针，表示要关闭的插件实例。
     * @return 成功返回0，失败返回非零值。
     */
    int (*close)(neu_plugin_t *plugin);

    /**
     * @brief 初始化插件。
     *
     * @param plugin 指向neu_plugin_t类型的指针，表示要初始化的插件实例。
     * @param load 如果为true，则从数据库加载标签信息；否则不加载。
     * @return 成功返回0，失败返回非零值。
     */
    int (*init)(neu_plugin_t *plugin, bool load);

    /**
     * @brief 反初始化插件。
     *
     * @param plugin 指向neu_plugin_t类型的指针，表示要反初始化的插件实例。
     * @return 成功返回0，失败返回非零值。
     */
    int (*uninit)(neu_plugin_t *plugin);

    /**
     * @brief 启动插件。
     *
     * @param plugin 指向neu_plugin_t类型的指针，表示要启动的插件实例。
     * @return 成功返回0，失败返回非零值。
     */
    int (*start)(neu_plugin_t *plugin);

    /**
     * @brief 停止插件。
     *
     * @param plugin 指向neu_plugin_t类型的指针，表示要停止的插件实例。
     * @return 成功返回0，失败返回非零值。
     */
    int (*stop)(neu_plugin_t *plugin);

    /**
     * @brief 设置插件的配置。
     *
     * @param plugin 指向neu_plugin_t类型的指针，表示要设置配置的插件实例。
     * @param setting 配置字符串。
     * @return 成功返回0，失败返回非零值。
     */
    int (*setting)(neu_plugin_t *plugin, const char *setting);

    /**
     * @brief 处理请求。
     *
     * @param plugin 指向neu_plugin_t类型的指针，表示要处理请求的插件实例。
     * @param head 请求响应头部信息。
     * @param data 请求数据。
     * @return 成功返回0，失败返回非零值。
     */
    int (*request)(neu_plugin_t *plugin, neu_reqresp_head_t *head, void *data);

    union {
        struct {
            /**
             * @brief 验证数据标签是否有效。
             *
             * @param plugin 指向neu_plugin_t类型的指针，表示要验证标签的插件实例。
             * @param tag 要验证的数据标签。
             * @return 成功返回0，失败返回非零值。
             */
            int (*validate_tag)(neu_plugin_t *plugin, neu_datatag_t *tag);

            /**
             * @brief 对一组标签进行定时同步操作。
             *
             * @param plugin 指向neu_plugin_t类型的指针，表示要同步的插件实例。
             * @param group 要同步的标签组。
             * @return 成功返回0，失败返回非零值。
             */
            int (*group_timer)(neu_plugin_t *plugin, neu_plugin_group_t *group);

            /**
             * @brief 对一组标签进行同步操作。
             *
             * @param plugin 指向neu_plugin_t类型的指针，表示要同步的插件实例。
             * @param group 要同步的标签组。
             * @return 成功返回0，失败返回非零值。
             */
            int (*group_sync)(neu_plugin_t *plugin, neu_plugin_group_t *group);

            /**
             * @brief 写入单个数据标签。
             *
             * @param plugin 指向neu_plugin_t类型的指针，表示要写入数据的插件实例。
             * @param req 请求对象。
             * @param tag 要写入的数据标签。
             * @param value 要写入的值。
             * @return 成功返回0，失败返回非零值。
             */
            int (*write_tag)(neu_plugin_t *plugin, void *req,
                             neu_datatag_t *tag, neu_value_u value);

            /**
             * @brief 写入多个数据标签。
             *
             * @param plugin 指向neu_plugin_t类型的指针，表示要写入数据的插件实例。
             * @param req 请求对象。
             * @param tag_values 包含多个数据标签及其值的数组。
             * @return 成功返回0，失败返回非零值。
             */
            int (*write_tags)(
                neu_plugin_t *plugin, void *req,
                UT_array *tag_values); // UT_array {neu_datatag_t, neu_value_u}

            /**
             * @brief 标签验证器，用于特定于插件的标签验证逻辑。
             */
            neu_plugin_tag_validator_t tag_validator;

            /**
             * @brief 从数据库加载标签。
             *
             * @param plugin 指向neu_plugin_t类型的指针，表示要加载标签的插件实例。
             * @param group 标签所属的组名。
             * @param tags 要加载的标签数组。
             * @param n_tag 要加载的标签数量。
             * @return 成功返回0，失败返回非零值。
             */
            int (*load_tags)(
                neu_plugin_t *plugin, const char *group, neu_datatag_t *tags,
                int n_tag); // create tags using data from the database

            /**
             * @brief 添加新的标签。
             *
             * @param plugin 指向neu_plugin_t类型的指针，表示要添加标签的插件实例。
             * @param group 标签所属的组名。
             * @param tags 要添加的标签数组。
             * @param n_tag 要添加的标签数量。
             * @return 成功返回0，失败返回非零值。
             */
            int (*add_tags)(neu_plugin_t *plugin, const char *group,
                            neu_datatag_t *tags,
                            int            n_tag); // create tags by API

            /**
             * @brief 删除指定数量的标签。
             *
             * @param plugin 指向neu_plugin_t类型的指针，表示要删除标签的插件实例。
             * @param n_tag 要删除的标签数量。
             * @return 成功返回0，失败返回非零值。
             */
            int (*del_tags)(neu_plugin_t *plugin, int n_tag);

            /**
             * @brief 扫描标签。
             *
             * @param plugin 指向neu_plugin_t类型的指针，表示要扫描标签的插件实例。
             * @param req 请求对象。
             * @param id 标识符。
             * @param ctx 上下文信息。
             * @return 成功返回0，失败返回非零值。
             */
            int (*scan_tags)(neu_plugin_t *plugin, void *req, char *id,
                             char *ctx);

            /**
             * @brief 测试读取一个标签。
             *
             * @param plugin 指向neu_plugin_t类型的指针，表示要测试读取的插件实例。
             * @param req 请求对象。
             * @param tag 要测试读取的标签。
             * @return 成功返回0，失败返回非零值。
             */
            int (*test_read_tag)(neu_plugin_t *plugin, void *req,
                                 neu_datatag_t tag);
          
            /**
             * @brief 执行指定的动作。
             *
             * @param plugin 指向neu_plugin_t类型的指针，表示要执行动作的插件实例。
             * @param action 动作名称。
             * @return 成功返回0，失败返回非零值。
             */
            int (*action)(neu_plugin_t *plugin, const char *action);

            int (*directory)(neu_plugin_t *plugin, void *req, const char *path);
            int (*fup_open)(neu_plugin_t *plugin, void *req, const char *path);
            int (*fup_data)(neu_plugin_t *plugin, void *req, const char *path);
            int (*fdown_open)(neu_plugin_t *plugin, void *req, const char *node,
                              const char *src_path, const char *dst_path,
                              int64_t size);
            int (*fdown_data)(neu_plugin_t *plugin, void *req, uint8_t *bytes,
                              uint16_t n_bytes, bool more);
        } driver;
    };

} neu_plugin_intf_funs_t;

/**
 * @brief 插件模块信息结构体
 *
 * 全面描述了插件的各种属性和信息，方便主程序对插件进行管理、识别、调用和配置
 */
typedef struct neu_plugin_module {
    /**
     * @brief 插件版本号
     *
     * 用于标识插件的版本，以便主程序检查插件版本是否与主程序兼容。
     */
    const uint32_t version;

    /**
     * @brief 插件的 schema 字符串
     *
     * 定义了插件使用的数据格式或协议。这通常用于配置解析或数据交换。
     */
    const char *schema;

    /**
     * @brief 插件模块名称
     *
     * 插件的唯一标识符，用于在系统中区分不同的插件。
     */
    const char *module_name;

    /**
     * @brief 插件模块描述（英文）
     *
     * 对插件功能的简短描述，用于在用户界面或日志中显示。
     */
    const char *module_descr;

    /**
     * @brief 插件模块描述（中文）
     *
     * 对插件功能的简短描述（中文版本），用于在支持中文的用户界面或日志中显示。
     */
    const char *module_descr_zh;

    /**
     * @brief 插件接口函数指针表
     *
     * 指向一个包含插件提供的所有接口函数指针的结构体。这些函数由主程序调用以实现插件的功能。
     */
    const neu_plugin_intf_funs_t *intf_funs;

    /**
     * @brief 插件类型
     *
     * 定义了插件的类型：驱动适配器，应用程序适配器
     */
    neu_node_type_e type;

    /**
     * @brief 插件种类
     *
     * 定义了插件的种类： 静态，系统，自定义
     */
    neu_plugin_kind_e kind;

    /**
     * @brief 是否在用户界面上显示插件
     *
     * 如果为 true，则插件的相关信息将在用户界面上显示；否则，插件将处于隐藏状态。
     */
    bool display;

    /**
     * @brief 是否为单例插件
     *
     * 如果为 true，则系统中只允许存在一个该插件的实例；否则，可以创建多个实例。
     */
    bool single;

    /**
     * @brief 单例插件的名称
     *
     * 当插件为单例时，该字段定义了插件的唯一名称。这有助于主程序在系统中查找和管理单例插件。
     */
    const char *single_name;

    /**
     * @brief 插件的定时器类型
     *
     * 定义了插件使用的定时器类型。阻塞 非阻塞类型。这有助于主程序根据定时器类型对插件进行调度和管理。
     */
    neu_event_timer_type_e timer_type;

    /**
     * @brief 插件的缓存类型
     *
     * 定义了插件使用的缓存类型。这有助于主程序根据缓存类型对插件进行优化和管理。
     */
    neu_tag_cache_type_e cache_type;
} neu_plugin_module_t;

inline static neu_plugin_common_t *
neu_plugin_to_plugin_common(neu_plugin_t *plugin)
{
    return (neu_plugin_common_t *) plugin;
}

/**
 * @brief when creating a node, initialize the common parameter in
 *        the plugin.
 *
 * @param[in] common refers to common parameter in plugins.
 */
void neu_plugin_common_init(neu_plugin_common_t *common);

/**
 * @brief Check the common parameter in the plugin.
 *
 * @param[in] plugin represents plugin information.
 * @return  Returns true if the check is correct,false otherwise.
 */
bool neu_plugin_common_check(neu_plugin_t *plugin);

/**
 * @brief Encapsulate the request,convert the request into a message and send.
 *
 * @param[in] plugin
 * @param[in] head the request header function.
 * @param[in] data that different request headers correspond to different data.
 *
 * @return 0 for successful message processing, non-0 for message processing
 * failure.
 */
int neu_plugin_op(neu_plugin_t *plugin, neu_reqresp_head_t head, void *data);

#ifdef __cplusplus
}
#endif

#endif
