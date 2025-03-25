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

#ifndef ADAPTER_INTERNAL_H
#define ADAPTER_INTERNAL_H

#include "define.h"
#include "event/event.h"
#include "plugin.h"

#include "adapter_info.h"
#include "core/manager.h"
#include "msg_q.h"

/**
 * @brief 适配器结构体，用于描述一个适配器的基础信息及其相关资源。
 * 
 * 用于管理和协调与插件相关的各种操作和资源，它不仅包含了对插件实例的引用，
 * 还涉及到适配器的配置、运行状态管理、事件处理、消息队列等多个方面。
 * 
 * @note
 * - 主程序可以根据 module 的信息来选择合适的插件，然后创建 plugin 实例并运行它。
 * - 将 metrics 放在 neu_adapter 中，能够更精准地聚焦于适配器在运行过程中的性能
 *   监控和健康评估，符合其功能定位。
 */
struct neu_adapter {
    /**
     * @brief 适配器名称。
     *
     * 存储适配器的唯一标识符，通常用于引用或识别特定的适配器实例。
     */
    char                 *name;

    /**
     * @brief 设置信息。
     *
     * 包含适配器的配置信息，可以是JSON格式或其他形式的字符串。
     */
    char                *setting;

    /**
     * @brief 运行状态。
     *
     * 适配器当前的运行状态：行中、停止等。
     */
    neu_node_running_state_e state;

    /**
     * @brief 回调函数集合。
     *
     * 包含适配器与外部系统交互时所需的各种回调函数，如启动、停止、处理消息等。
     */
    adapter_callbacks_t  cb_funs;

    /**
     * @brief 句柄。
     *
     * 用于链接动态链接库：通过dlopen函数获取的动态库句柄
     */
    void *               handle;

    /**
     * @brief 表示适配器使用的插件模块。
     *
     * 插件的元信息和通用配置，它可以看作是插件的一个抽象定义，
     * 类似于插件的模板或者规范，不包含插件在运行时的具体状态和数据。。
     * 为插件的加载、识别和兼容性检查提供依据
     */
    neu_plugin_module_t *module;

    /**
     * @brief 表示适配器关联的插件实例。
     * 
     * 是插件在运行时的具体实例，包含了插件在运行过程中的状态和数据，
     * 以及与插件运行相关的具体资源和上下文。用于插件的实际运行和交互
     */
    neu_plugin_t *       plugin;

    /**
     * @brief 控制事件I/O。
     *
     * 表示一个具体的 I/O 事件，用于处理控制相关的事件I/O操作。
     * 负责定义和封装具体的I/O 事件信息
     */
    neu_event_io_t     *control_io;

    /**
     * @brief 数据传输事件I/O。
     *
     * 表示一个具体的 I/O 事件，用于处理数据传输相关的事件
     * I/O操作。负责定义和封装具体的 I/O 事件信息
     */
    neu_event_io_t     *trans_data_io;

    /**
     * @brief 控制文件描述符。
     *
     * 控制事件I/O使用的文件描述符。
     */
    int                 control_fd;

    /**
     * @brief 数据传输文件描述符。
     *
     * 数据传输事件I/O使用的文件描述符。
     */
    int                 trans_data_fd;

    /**
     * @brief 消息队列。
     *
     * 用于存储待处理的消息，支持异步通信。
     */
    adapter_msg_q_t    *msg_q;

    /**
     * @brief 消费者线程ID。
     *
     * 处理消息队列的消费者线程的ID。
     */
    pthread_t           consumer_tid;

    /**
     * @brief 数据传输端口。作为负责与外部系统进行交互的组件
     *
     * 用于数据传输的网络端口号。网络端口用于网络通信，允许
     * 节点与外部网络中的其他设备或服务进行数据传输。例如，
     * 适配器可能需要通过特定的端口与远程的服务器进行通信，
     * 获取数据或者发送控制指令。
     * 
     * @note
     * 
     * 不同的适配器可能需要使用不同的网络端口来与不同的外部系统进行通信，
     * 通过在 neu_adapter 中单独管理网络端口，可以根据适配器的具体需
     * 求进行灵活配置和调整。同时，当需要添加新的网络通信功能时，也可以
     * 方便地在 neu_adapter 中进行扩展。
     */
    uint16_t            trans_data_port;

    /**
     * @brief 事件管理。
     *
     * 管理各种事件（如I/O事件、定时器事件）的调度和执行。包括 
     * control_io 和 trans_data_io 所代表的 I/O 事件。
     * timer_lev 所代表的定时器事件。负责监听这些事件的发生，
     * 并在事件发生时调用相应的回调函数进行处理。
     */
    neu_events_t       *events;

    /**
     * @brief 定时器。
     *
     * 用于触发特定级别的事件或任务。
     */
    neu_event_timer_t  *timer_lev;

    /**
     * @brief 时间戳。
     *
     * 记录定时器上次触发的时间戳，单位为毫秒。
     */
    int64_t             timestamp_lev;

    /**
     * @brief 指标。
     *
     * 用于监控适配器的性能和健康状况。
     */
    neu_node_metrics_t *metrics;

    int                 log_level;
};

typedef void (*adapter_handler)(neu_adapter_t *     adapter,
                                neu_reqresp_head_t *header);
typedef struct adapter_msg_handler {
    neu_reqresp_type_e type;
    adapter_handler    handler;
} adapter_msg_handler_t;

int  neu_adapter_error();
void neu_adapter_set_error(int error);

uint16_t neu_adapter_trans_data_port(neu_adapter_t *adapter);

neu_adapter_t *neu_adapter_create(neu_adapter_info_t *info, bool load);
void neu_adapter_init(neu_adapter_t *adapter, neu_node_running_state_e state);

int neu_adapter_rename(neu_adapter_t *adapter, const char *new_name);

int neu_adapter_start(neu_adapter_t *adapter);
int neu_adapter_start_single(neu_adapter_t *adapter);
int neu_adapter_stop(neu_adapter_t *adapter);

neu_node_type_e      neu_adapter_get_type(neu_adapter_t *adapter);
neu_tag_cache_type_e neu_adapter_get_tag_cache_type(neu_adapter_t *adapter);

int  neu_adapter_uninit(neu_adapter_t *adapter);
void neu_adapter_destroy(neu_adapter_t *adapter);

neu_event_timer_t *neu_adapter_add_timer(neu_adapter_t *         adapter,
                                         neu_event_timer_param_t param);
void neu_adapter_del_timer(neu_adapter_t *adapter, neu_event_timer_t *timer);

int neu_adapter_set_setting(neu_adapter_t *adapter, const char *config);
int neu_adapter_get_setting(neu_adapter_t *adapter, char **config);
neu_node_state_t neu_adapter_get_state(neu_adapter_t *adapter);

static inline void neu_adapter_reset_metrics(neu_adapter_t *adapter)
{
    if (NULL != adapter->metrics) {
        neu_node_metrics_reset(adapter->metrics);
    }
}

int  neu_adapter_register_group_metric(neu_adapter_t *adapter,
                                       const char *group_name, const char *name,
                                       const char *help, neu_metric_type_e type,
                                       uint64_t init);
int  neu_adapter_update_group_metric(neu_adapter_t *adapter,
                                     const char *   group_name,
                                     const char *metric_name, uint64_t n);
int  neu_adapter_metric_update_group_name(neu_adapter_t *adapter,
                                          const char *   group_name,
                                          const char *   new_group_name);
void neu_adapter_del_group_metrics(neu_adapter_t *adapter,
                                   const char *   group_name);

int neu_adapter_validate_gtags(neu_adapter_t *adapter, neu_req_add_gtag_t *cmd,
                               neu_resp_add_tag_t *resp);
int neu_adapter_try_add_gtags(neu_adapter_t *adapter, neu_req_add_gtag_t *cmd,
                              neu_resp_add_tag_t *resp);
int neu_adapter_add_gtags(neu_adapter_t *adapter, neu_req_add_gtag_t *cmd,
                          neu_resp_add_tag_t *resp);

#endif
