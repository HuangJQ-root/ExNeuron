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

#ifndef _NEU_MANAGER_INTERNAL_H_
#define _NEU_MANAGER_INTERNAL_H_

#include "event/event.h"
#include "persist/persist.h"

#include "base/msg_internal.h"
#include "msg.h"
#include "node_manager.h"
#include "plugin_manager.h"
#include "subscribe.h"
#include "utils/log.h"

/**
 * @brief neu_manager 结构体用于管理和协调多个子系统，如插件管理器、节点管理器和事件循环。
 *
 * 该结构体包含一个 Unix 域套接字文件描述符 (`server_fd`)，用于进程间通信；
 * 事件循环 (`events`, `loop`)，用于处理异步事件；
 * 插件管理器 (`plugin_manager`)、节点管理器 (`node_manager`) 
 * 和订阅管理器 (`subscribe_manager`)，用于管理不同的功能模块；
 * 定时器 (`timer_timestamp`)，用于定期更新时间戳；
 * 时间戳级别管理器 (`timestamp_lev_manager`)，用于记录时间戳级别的信息；
 * 日志级别 (`log_level`)，用于设置日志输出的详细程度。
 */
typedef struct neu_manager {
    /**
     * @brief Unix 域套接字文件描述符，用于进程间通信。
     *
     * 通过这个文件描述符，neu_manager 可以接收来自其他进程的消息，并进行相应的处理。
     */
    int server_fd;

    /**
     * @brief 事件循环对象，用于管理和调度异步事件。
     * 
     * 核心的事件管理结构体，负责对系统中所有类型的事件进行统一管理和调度。
     * 它就像是一个事件的 “指挥中心”，维护着一个事件队列，记录着各种待处理
     * 的事件，并根据事件的优先级和触发条件进行调度。
     * 
     * @note
     *  - 事件注册：允许其他组件（如 loop 和 timer_timestamp）将自己的
     *            事件注册到 events 中。这样，events 就能知道系统中有哪
     *            些事件需要处理。
     *  - 事件调度：不断地检查事件队列，根据事件的触发条件（如时间、I/O 状
     *            态等）决定哪些事件需要被处理，并按照一定的顺序调用相应的
     *            事件处理函数 
     *  - 资源管理：负责管理与事件相关的资源，如文件描述符、定时器等，确保这
     *            些资源在事件处理过程中被正确使用和释放。
     */
    neu_events_t *  events;      

    /**
     * @brief
     * 
     * 处理 I/O 相关的事件，专注于监测和响应系统中各种 I/O 设备（如套接字、文件等）
     * 的状态变化。它是 I/O 事件的具体执行者，负责将 I/O 事件信息传递给 events 进行调度。
     * 
     * @note
     * 
     * - I/O 事件监测：持续监测指定 I/O 设备的状态，例如检查套接字是否有新的数据可读、可写
     *              ，或者文件是否发生了特定的变化。
     * - 事件触发：当监测到 I/O 设备的状态发生变化时，生成相应的 I/O 事件，并将其传递给 
     *           events 进行处理。
     * - 回调处理：为每个 I/O 事件关联一个回调函数，当事件被触发时，events 会调用这个回调
     *           函数来执行具体的处理逻辑，如读取数据、发送响应等。
     */
    neu_event_io_t *loop;        
    
    /**
     * @brief neu_manager的定时器对象，用于定期更新时间戳。
     *
     * 处理定时器事件，通过设置定时器来定期触发特定的操作。它为系统提供了时间驱动的事件机制，
     * 确保某些任务能够按照预定的时间间隔执行。
     * 
     * @note
     *  - 定时器设置：允许用户设置定时器的触发时间和周期，例如设置每隔一段时间更新一次时间戳。
     *  - 时间监测：不断监测时间的流逝，当定时器到达设定的触发时间时，生成定时器事件并将其传
     *            递给 events
     *  - 周期性任务执行：对于周期性的定时器事件，timer_timestamp 会在每次触发后重新设置
     *                 定时器，确保任务能够持续按照预定的周期执行。
     */
    neu_event_timer_t *timer_timestamp;

    /**
     * @brief 插件管理器，用于加载、管理和卸载插件。
     *
     * 插件管理器负责维护所有已加载插件的状态，并提供接口来动态管理这些插件。
     */
    neu_plugin_manager_t *plugin_manager;

    /**
     * @brief 节点管理器，用于管理节点的生命周期和状态。
     *
     * 节点管理器负责维护所有节点的状态，并提供接口来创建、删除和查询节点。
     */
    neu_node_manager_t *node_manager;

    /**
     * @brief 订阅管理器，用于管理订阅关系。
     *
     * 订阅管理器负责维护所有订阅者和发布者之间的关系，并确保消息能够正确地传递给订阅者。
     */
    neu_subscribe_mgr_t *subscribe_manager;

    /**
     * @brief 时间戳级别管理器，用于记录时间戳级别的信息。
     *
     * 这个字段通常用于内部逻辑，记录当前的时间戳级别或其他相关信息。
     */
    int64_t timestamp_lev_manager;

    /**
     * @brief 日志级别，用于控制日志输出的详细程度。
     *
     * 不同的日志级别（如 DEBUG, INFO, WARN, ERROR）对应不同的日志输出详细程度，
     * 开发者可以根据需要调整日志级别来获取更多的调试信息或减少不必要的日志输出。
     */
    int log_level;
} neu_manager_t;

int       neu_manager_add_plugin(neu_manager_t *manager, const char *library);
int       neu_manager_del_plugin(neu_manager_t *manager, const char *plugin);
UT_array *neu_manager_get_plugins(neu_manager_t *manager);

int       neu_manager_add_node(neu_manager_t *manager, const char *node_name,
                               const char *plugin_name, const char *setting,
                               neu_node_running_state_e state, bool load);
int       neu_manager_del_node(neu_manager_t *manager, const char *node_name);
UT_array *neu_manager_get_nodes(neu_manager_t *manager, int type,
                                const char *plugin, const char *node);
int       neu_manager_update_node_name(neu_manager_t *manager, const char *node,
                                       const char *new_name);
int neu_manager_update_group_name(neu_manager_t *manager, const char *driver,
                                  const char *group, const char *new_name);

UT_array *neu_manager_get_driver_group(neu_manager_t *manager);

int       neu_manager_subscribe(neu_manager_t *manager, const char *app,
                                const char *driver, const char *group,
                                const char *params, const char *static_tags,
                                uint16_t *app_port);
int       neu_manager_update_subscribe(neu_manager_t *manager, const char *app,
                                       const char *driver, const char *group,
                                       const char *params, const char *static_tags);
int       neu_manager_send_subscribe(neu_manager_t *manager, const char *app,
                                     const char *driver, const char *group,
                                     uint16_t app_port, const char *params,
                                     const char *static_tags);
int       neu_manager_unsubscribe(neu_manager_t *manager, const char *app,
                                  const char *driver, const char *group);
UT_array *neu_manager_get_sub_group(neu_manager_t *manager, const char *app);
UT_array *neu_manager_get_sub_group_deep_copy(neu_manager_t *manager,
                                              const char *   app,
                                              const char *   driver,
                                              const char *   group);

int neu_manager_get_node_info(neu_manager_t *manager, const char *name,
                              neu_persist_node_info_t *info);

int neu_manager_add_drivers(neu_manager_t *         manager,
                            neu_req_driver_array_t *req);

inline static void forward_msg(neu_manager_t *     manager,
                               neu_reqresp_head_t *header, const char *node)
{
    struct sockaddr_un addr =
        neu_node_manager_get_addr(manager->node_manager, node);

    neu_reqresp_type_e t                           = header->type;
    char               receiver[NEU_NODE_NAME_LEN] = { 0 };
    strncpy(receiver, header->receiver, sizeof(receiver));

    neu_msg_t *msg = (neu_msg_t *) header;
    int        ret = neu_send_msg_to(manager->server_fd, &addr, msg);
    if (0 == ret) {
        nlog_info("forward msg %s to %s", neu_reqresp_type_string(t), receiver);
    } else {
        nlog_warn("forward msg %s to node (%s)%s %s %s fail",
                  neu_reqresp_type_string(t), receiver, &addr.sun_path[1], node,
                  header->sender);
        neu_msg_free(msg);
    }
}

#endif
