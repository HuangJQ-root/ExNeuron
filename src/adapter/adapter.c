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

#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include "utils/http.h"
#include "utils/log.h"
#include "utils/time.h"

#include "otel/otel_manager.h"

#include "adapter.h"
#include "adapter_internal.h"
#include "base/msg_internal.h"
#include "driver/driver_internal.h"
#include "errcodes.h"
#include "persist/persist.h"
#include "plugin.h"
#include "storage.h"

static void *adapter_consumer(void *arg);
static int   adapter_trans_data(enum neu_event_io_type type, int fd,
                                void *usr_data);
static int   adapter_loop(enum neu_event_io_type type, int fd, void *usr_data);
static int   adapter_command(neu_adapter_t *adapter, neu_reqresp_head_t header,
                             void *data);
static int adapter_response(neu_adapter_t *adapter, neu_reqresp_head_t *header,
                            void *data);
static int adapter_responseto(neu_adapter_t *     adapter,
                              neu_reqresp_head_t *header, void *data,
                              struct sockaddr_un dst);
static int adapter_register_metric(neu_adapter_t *adapter, const char *name,
                                   const char *help, neu_metric_type_e type,
                                   uint64_t init);
static int adapter_update_metric(neu_adapter_t *adapter,
                                 const char *metric_name, uint64_t n,
                                 const char *group);
inline static void reply(neu_adapter_t *adapter, neu_reqresp_head_t *header,
                         void *data);
/**
 * @brief 定义适配器回调函数集合。
 *
 * 此静态常量结构体 `callback_funs` 包含了一组指向适配器相关操作函数的指针。
 * 通过这个结构体，可以方便地调用与特定适配器相关的各种命令处理、响应处理、
 * 注册度量和更新度量等功能。每个成员变量都是一个函数指针，分别指向不同的适配器操作函数。
 *
 */
static const adapter_callbacks_t callback_funs = {
    .command         = adapter_command,
    .response        = adapter_response,
    .responseto      = adapter_responseto,
    .register_metric = adapter_register_metric,
    .update_metric   = adapter_update_metric,
};

/**
 * @brief 定义了一个静态的线程局部存储整型变量
 * 
 * 每个线程都有自己独立的 create_adapter_error 副本，线程可以独立地修改和
 * 使用这个变量，而不会影响其他线程中的同名变量。这种设计在多线程编程中非常有
 * 用，比如在多线程环境下创建适配器时，每个线程都可以使用自己的 
 * create_adapter_error 变量来记录创建过程中是否发生错误，避免了多线程之间的干扰。
 */
static __thread int create_adapter_error = 0;

/**
 * @note
 *  ##是连接符，将name和_HELP连接起来
 *  如：adapter_register_metric(adapter, NEU_METRIC_LINK_STATE, NEU_METRIC_LINK_STATE_HELP,
 *                             NEU_METRIC_LINK_STATE_TYPE, NEU_NODE_LINK_STATE_DISCONNECTED);
 *  而NEU_METRIC_LINK_STATE_TYPE是由(NEU_METRIC_TYPE_GAUAGE | NEU_METRIC_TYPE_FLAG_NO_RESET)
 */
#define REGISTER_METRIC(adapter, name, init) \
    adapter_register_metric(adapter, name, name##_HELP, name##_TYPE, init);

//宏的参数传递，adapter 这个实参就会被传递给宏定义中的每一个 REGISTER_METRIC 调用。
#define REGISTER_DRIVER_METRICS(adapter)                     \
    REGISTER_METRIC(adapter, NEU_METRIC_LINK_STATE,          \
                    NEU_NODE_LINK_STATE_DISCONNECTED);       \
    REGISTER_METRIC(adapter, NEU_METRIC_RUNNING_STATE,       \
                    NEU_NODE_RUNNING_STATE_INIT);            \
    REGISTER_METRIC(adapter, NEU_METRIC_LAST_RTT_MS,         \
                    NEU_METRIC_LAST_RTT_MS_MAX);             \
    REGISTER_METRIC(adapter, NEU_METRIC_SEND_BYTES, 0);      \
    REGISTER_METRIC(adapter, NEU_METRIC_RECV_BYTES, 0);      \
    REGISTER_METRIC(adapter, NEU_METRIC_TAGS_TOTAL, 0);      \
    REGISTER_METRIC(adapter, NEU_METRIC_TAG_READS_TOTAL, 0); \
    REGISTER_METRIC(adapter, NEU_METRIC_TAG_READ_ERRORS_TOTAL, 0);

#define REGISTER_APP_METRICS(adapter)                              \
    REGISTER_METRIC(adapter, NEU_METRIC_LINK_STATE,                \
                    NEU_NODE_LINK_STATE_DISCONNECTED);             \
    REGISTER_METRIC(adapter, NEU_METRIC_RUNNING_STATE,             \
                    NEU_NODE_RUNNING_STATE_INIT);                  \
    REGISTER_METRIC(adapter, NEU_METRIC_SEND_MSGS_TOTAL, 0);       \
    REGISTER_METRIC(adapter, NEU_METRIC_SEND_MSG_ERRORS_TOTAL, 0); \
    REGISTER_METRIC(adapter, NEU_METRIC_RECV_MSGS_TOTAL, 0);

int neu_adapter_error()
{
    return create_adapter_error;
}

void neu_adapter_set_error(int error)
{
    create_adapter_error = error;
}

/**
 * @brief 消费者线程函数，处理适配器消息队列中的消息。
 *
 * 此函数作为消费者线程运行，持续从适配器的消息队列中弹出消息并进行处理。
 * 它首先从消息队列中取出一条消息，并解析消息头信息。调用适配器模块的
 * 请求处理函数来处理该消息，并释放消息相关的资源。此循环会一直运行直到线程被外部中断或终止。
 *
 * @param arg 传递给线程的参数，应为指向 `neu_adapter_t` 结构体的指针。
 * @return 该函数永远不会正常返回，始终返回 `NULL`。
 */
static void *adapter_consumer(void *arg)
{
    neu_adapter_t *adapter = (neu_adapter_t *) arg;

    while (1) {
        neu_msg_t *         msg    = NULL;
        uint32_t            n      = adapter_msg_q_pop(adapter->msg_q, &msg);
        neu_reqresp_head_t *header = neu_msg_get_header(msg);

        nlog_debug("adapter(%s) recv msg from: %s %p, type: %s, %u",
                   adapter->name, header->sender, header->ctx,
                   neu_reqresp_type_string(header->type), n);
        
        // 调用消息处理函数
        adapter->module->intf_funs->request(
            adapter->plugin, (neu_reqresp_head_t *) header, &header[1]);
        
        // 释放消息数据
        neu_trans_data_free((neu_reqresp_trans_data_t *) &header[1]);
        
        // 释放消息
        neu_msg_free(msg);
    }

    return NULL;
}

static inline zlog_category_t *get_log_category(const char *node)
{
    char name[NEU_NODE_NAME_LEN] = { 0 };
    // replace path separators
    for (int i = 0; node[i]; ++i) {
        name[i] = ('/' == node[i] || '\\' == node[i]) ? '_' : node[i];
    }
    return zlog_get_category(name);
}

/**
 * @brief 创建并初始化一个新的适配器实例。
 *
 * 根据提供的适配器信息创建一个新的适配器，并根据适配器类型（驱动或应用）进行相应的初始化。
 * 该函数还会设置适配器的套接字、事件处理机制以及回调函数等。
 *
 * @param info 适配器的基本信息指针，包含名称、句柄和模块信息。
 * @param load 是否加载配置标志。
 *
 * @return 成功时返回指向新创建的适配器的指针；失败时返回 `NULL`。
 */
neu_adapter_t *neu_adapter_create(neu_adapter_info_t *info, bool load)
{
    int                  rv      = 0;
    int                  init_rv = 0;
    neu_adapter_t *      adapter = NULL;
    neu_event_io_param_t param   = { 0 };

    // 根据适配器类型创建适配器实例
    switch (info->module->type) {
    case NEU_NA_TYPE_DRIVER: // 创建驱动类型的适配器
        adapter = (neu_adapter_t *) neu_adapter_driver_create();
        break;
    case NEU_NA_TYPE_APP:    // 为应用类型的适配器分配内存
        adapter = calloc(1, sizeof(neu_adapter_t));
        break;
    }

    // 创建控制套接字
    adapter->control_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (adapter->control_fd <= 0) {
        free(adapter);
        return NULL;
    }

    // 创建数据传输套接字
    adapter->trans_data_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (adapter->trans_data_fd <= 0) {
        close(adapter->control_fd);
        free(adapter);
        return NULL;
    }

    // 设置套接字超时选项：1s
    struct timeval sock_timeout = {
        .tv_sec  = 1,   /* 秒 */
        .tv_usec = 0,   /* 微秒 */
    };

    //为套接字设置发送和接收超时时间
    if (setsockopt(adapter->control_fd, SOL_SOCKET, SO_SNDTIMEO, &sock_timeout,
                   sizeof(sock_timeout)) < 0 ||
        setsockopt(adapter->control_fd, SOL_SOCKET, SO_RCVTIMEO, &sock_timeout,
                   sizeof(sock_timeout)) < 0 ||
        setsockopt(adapter->trans_data_fd, SOL_SOCKET, SO_SNDTIMEO,
                   &sock_timeout, sizeof(sock_timeout)) < 0 ||
        setsockopt(adapter->trans_data_fd, SOL_SOCKET, SO_RCVTIMEO,
                   &sock_timeout, sizeof(sock_timeout)) < 0) {
        nlog_error("fail to set sock timeout for adapter:%s", info->name);
        close(adapter->trans_data_fd);
        close(adapter->control_fd);
        free(adapter);
        return NULL;
    }

    // 初始化适配器结构体成员
    adapter->name                    = strdup(info->name);
    adapter->events                  = neu_event_new();
    adapter->state                   = NEU_NODE_RUNNING_STATE_INIT;
    adapter->handle                  = info->handle;
    adapter->cb_funs.command         = callback_funs.command;
    adapter->cb_funs.response        = callback_funs.response;
    adapter->cb_funs.responseto      = callback_funs.responseto;
    adapter->cb_funs.register_metric = callback_funs.register_metric;
    adapter->cb_funs.update_metric   = callback_funs.update_metric;
    adapter->module                  = info->module;
    adapter->timestamp_lev           = 0;
    adapter->trans_data_port         = 0;
    adapter->log_level               = ZLOG_LEVEL_NOTICE;

    //获取端口号
    uint16_t           port  = neu_manager_get_port();

    //定义并初始化本地套接字地址
    struct sockaddr_un local = {
        .sun_family = AF_UNIX,
    };
    
    /**
     * @brief
     *  构造一个符合抽象命名空间套接字要求的唯一套接字路径
     * 
     * @note
     *  与基于文件系统的 Unix 域套接字不同，抽象命名空间套接字不依赖于文件系统中的实际文件，
     *  以 '\0' 开头的路径仅存在于内核空间，避免了文件权限和文件存在性等问题，提高了安全性和灵活性。
     */
    snprintf(local.sun_path, sizeof(local.sun_path), "%cneuron-%" PRIu16, '\0',
             port);

    // 绑定本地套接字
    rv = bind(adapter->control_fd, (struct sockaddr *) &local,
              sizeof(struct sockaddr_un));
    assert(rv == 0);


    // 定义并初始化远程套接字地址: "\0neuron-manager"
    struct sockaddr_un remote = {
        .sun_family = AF_UNIX,
        .sun_path   = "#neuron-manager",
    };
    remote.sun_path[0] = '\0';

    // 连接到远程套接字
    rv = connect(adapter->control_fd, (struct sockaddr *) &remote,
                 sizeof(struct sockaddr_un));
    assert(rv == 0);

    // 根据适配器类型执行特定的初始化步骤
    switch (info->module->type) {
    case NEU_NA_TYPE_DRIVER:
        if (adapter->module->display) {
            // 注册驱动指标
            REGISTER_DRIVER_METRICS(adapter);
        }

        // 初始化驱动适配器
        neu_adapter_driver_init((neu_adapter_driver_t *) adapter);
        break;
    case NEU_NA_TYPE_APP: {
        // 创建消息队列
        adapter->msg_q = adapter_msg_q_new(adapter->name, 1024);

        // 启动消费者线程,消费adapter->msg_q中的数据
        pthread_create(&adapter->consumer_tid, NULL, adapter_consumer,
                       (void *) adapter);

        // 尝试绑定数据传输套接字直到成功
        while (true) {
            port = neu_manager_get_port();
            snprintf(local.sun_path, sizeof(local.sun_path),
                     "%cneuron-%" PRIu16, '\0', port);
            if (bind(adapter->trans_data_fd, (struct sockaddr *) &local,
                     sizeof(struct sockaddr_un)) == 0) {
                adapter->trans_data_port = port;
                break;
            }
        }

        param.usr_data = (void *) adapter;
        param.cb       = adapter_trans_data;
        param.fd       = adapter->trans_data_fd;

        // 添加处理数据传输事件到事件集,并存储IO事件句柄
        adapter->trans_data_io = neu_event_add_io(adapter->events, param);

        if (adapter->module->display) {
            // 注册应用指标
            REGISTER_APP_METRICS(adapter);
        }

        break;
    }
    }

    // 创建适配器的插件
    adapter->plugin = adapter->module->intf_funs->open();
    assert(adapter->plugin != NULL);
    assert(neu_plugin_common_check(adapter->plugin));

    /**
     * @brief  初始化公共插件接口
     * @warning common后面实际没有使用
     */
    neu_plugin_common_t *common = neu_plugin_to_plugin_common(adapter->plugin);
    common->adapter             = adapter;
    common->adapter_callbacks   = &adapter->cb_funs;
    common->link_state          = NEU_NODE_LINK_STATE_DISCONNECTED;
    common->log                 = get_log_category(adapter->name);
    strcpy(common->name, adapter->name);

    zlog_level_switch(common->log, default_log_level);

    // 初始化适配器的插件
    init_rv = adapter->module->intf_funs->init(adapter->plugin, load);

    // 加载适配器设置: 实际上dashboard的setting函数不加载任何配置
    if (adapter_load_setting(adapter->name, &adapter->setting) == 0) {
        if (adapter->module->intf_funs->setting(adapter->plugin,
                                                adapter->setting) == 0) {
            adapter->state = NEU_NODE_RUNNING_STATE_READY;
        } else {
            free(adapter->setting);
            adapter->setting = NULL;
        }
    }

    // 如果是驱动类型，则加载组和标签
    if (info->module->type == NEU_NA_TYPE_DRIVER) {
        adapter_load_group_and_tag((neu_adapter_driver_t *) adapter);
    }

    param.fd       = adapter->control_fd;
    param.usr_data = (void *) adapter;
    param.cb       = adapter_loop;

    // 添加控制事件
    adapter->control_io = neu_event_add_io(adapter->events, param);

    // 存储适配器状态
    adapter_storage_state(adapter->name, adapter->state);

    // 检查初始化结果
    if (init_rv != 0) {
        nlog_warn("Failed to init adapter: %s", adapter->name);
        neu_adapter_set_error(init_rv);

        if (adapter->module->type == NEU_NA_TYPE_DRIVER) {
            neu_adapter_driver_destroy((neu_adapter_driver_t *) adapter);
        } else {
            neu_event_del_io(adapter->events, adapter->trans_data_io);
        }
        neu_event_del_io(adapter->events, adapter->control_io);

        neu_adapter_destroy(adapter);
        return NULL;
    } else {
        // 将适配器创建过程中的错误状态码设置为 0，表示适配器创建成功
        neu_adapter_set_error(0);
        return adapter;
    }
}

uint16_t neu_adapter_trans_data_port(neu_adapter_t *adapter)
{
    return adapter->trans_data_port;
}

int neu_adapter_rename(neu_adapter_t *adapter, const char *new_name)
{
    char *name     = strdup(new_name);
    char *old_name = strdup(adapter->name);
    if (NULL == name) {
        return NEU_ERR_EINTERNAL;
    }

    zlog_category_t *log = get_log_category(name);
    if (NULL == log) {
        free(name);
        free(old_name);
        return NEU_ERR_EINTERNAL;
    }

    if (NEU_NA_TYPE_DRIVER == adapter->module->type) {
        neu_adapter_driver_stop_group_timer((neu_adapter_driver_t *) adapter);
    }

    // fix metrics
    if (adapter->metrics) {
        neu_metrics_del_node(adapter);
    }
    free(adapter->name);
    adapter->name = name;
    if (adapter->metrics) {
        adapter->metrics->name = name;
        neu_metrics_add_node(adapter);
    }

    // fix log
    neu_plugin_common_t *common = neu_plugin_to_plugin_common(adapter->plugin);
    common->log                 = log;
    strcpy(common->name, adapter->name);
    zlog_level_switch(common->log, default_log_level);

    if (NEU_NA_TYPE_DRIVER == adapter->module->type) {
        neu_adapter_driver_start_group_timer((neu_adapter_driver_t *) adapter);
    }

    remove_logs(old_name);
    free(old_name);

    return 0;
}

/**
 * @brief 向管理器发送适配器初始化消息。
 *
 * 此函数用于创建并发送一个适配器初始化请求消息给管理器，告知管理器适配器的初始化状态。
 * 消息中包含适配器的名称和初始化状态。
 *
 * @param adapter 指向 neu_adapter_t 类型的适配器指针，代表要初始化的适配器。
 * @param state 适配器的初始化状态，类型为 neu_node_running_state_e。
 *
 * @return 无返回值。如果消息分配失败，会记录错误日志；如果消息发送失败，也会记录错误日志。
 *
 * @note 该函数假设传入的 adapter 指针不为 NULL。如果传入 NULL 指针，可能会导致未定义行为。
 *       同时，需要确保 neu_msg_new 和 neu_send_msg 函数的正确实现和调用。
 */
void neu_adapter_init(neu_adapter_t *adapter, neu_node_running_state_e state)
{
    neu_req_node_init_t init = { 0 };
    init.state               = state;
    strcpy(init.node, adapter->name);

    neu_msg_t *msg = neu_msg_new(NEU_REQ_NODE_INIT, NULL, &init);
    if (NULL == msg) {
        nlog_error("failed alloc msg for %s", adapter->name);
        return;
    }
    neu_reqresp_head_t *header = neu_msg_get_header(msg);
    strcpy(header->sender, adapter->name);
    strcpy(header->receiver, "manager");

    int ret = neu_send_msg(adapter->control_fd, msg);
    if (0 != ret) {
        nlog_error("%s failed to send init msg to manager, ret: %d, errno: %d",
                   adapter->name, ret, errno);
        neu_msg_free(msg);
    }
}

neu_node_type_e neu_adapter_get_type(neu_adapter_t *adapter)
{
    return adapter->module->type;
}

/**
 * @brief 获取指定适配器的标签缓存类型。
 *
 * 此函数返回给定适配器的标签缓存类型，这决定了该适配器如何管理其标签数据的缓存。
 * 缓存类型可以是基于时间间隔更新缓存（NEU_TAG_CACHE_TYPE_INTERVAL）或从不更新缓存（NEU_TAG_CACHE_TYPE_NEVER）。
 *
 * @param adapter 指向适配器对象的指针。
 * @return 返回适配器的缓存类型（neu_tag_cache_type_e）。
 */
neu_tag_cache_type_e neu_adapter_get_tag_cache_type(neu_adapter_t *adapter)
{
    // 直接从适配器模块中获取缓存类型
    return adapter->module->cache_type;
}

/**
 * @brief 注册一个新的度量指标到适配器。
 *
 * 此函数用于在指定的适配器中注册一个新的度量指标。如果适配器的度量指标集合尚未初始化，则会创建一个新的度量指标集合。
 * 如果成功添加新的度量指标，则返回0；否则返回-1。
 *
 * @param adapter 指向适配器对象的指针。
 * @param name 度量指标的名称。
 * @param help 度量指标的帮助信息或描述。
 * @param type 度量指标的类型（如计数器、仪表等）。
 * @param init 度量指标的初始值。
 * @return int 返回0表示成功，返回-1表示失败。
 */
static int adapter_register_metric(neu_adapter_t *adapter, const char *name,
                                   const char *help, neu_metric_type_e type,
                                   uint64_t init)
{
    // 如果适配器的度量指标集合未初始化，则创建一个新的度量指标集合
    if (NULL == adapter->metrics) {
        adapter->metrics =
            neu_node_metrics_new(adapter, adapter->module->type, adapter->name);
        if (NULL == adapter->metrics) {
            return -1;
        }

        // 添加适配器度量到全局度量对象的节点度量信息中
        neu_metrics_add_node(adapter);
    }

    // 添加新的度量指标到适配器的度量指标集合中
    if (0 >
        neu_node_metrics_add(adapter->metrics, NULL, name, help, type, init)) {
        return -1;
    }

    return 0;
}

/**
 * @brief 更新适配器的度量信息。
 *
 * 此函数用于更新指定适配器的度量信息。它首先检查适配器是否已经设置了度量对象
 * （即 `adapter->metrics` 是否为 `NULL`），如果没有设置，则直接返回错误码 `-1`。
 * 如果度量对象存在，则调用 `neu_node_metrics_update` 函数来更新具体的度量项。
 *
 * @param adapter     指向 `neu_adapter_t` 结构体的指针，表示当前适配器对象。
 * @param metric_name 要更新的度量项名称。
 * @param n           要更新的度量值。
 * @param group       度量项所属的组名。
 * @return 成功时返回 `neu_node_metrics_update` 的返回值；如果适配器没有设置度量对象，则返回 `-1`。
 */
static int adapter_update_metric(neu_adapter_t *adapter,
                                 const char *metric_name, uint64_t n,
                                 const char *group)
{
    if (NULL == adapter->metrics) {
        return -1;
    }

    return neu_node_metrics_update(adapter->metrics, group, metric_name, n);
}

/**
 * @brief 处理并发送适配器命令。
 *
 * 此函数根据给定的命令类型处理请求，并将消息发送到相应的接收者（例如驱动程序或节点）。
 * 它首先创建一个新的消息，设置其头部信息，然后根据命令类型填充接收者名称，最后通过控
 * 制文件描述符发送消息。如果在操作过程中遇到任何错误（如内存分配失败或消息发送失败），
 * 则会记录错误日志并返回相应的错误码。
 *
 * @param adapter 指向 `neu_adapter_t` 结构体的指针，表示当前适配器对象。
 * @param header  请求响应头结构体，包含命令类型和上下文信息。
 * @param data    命令的具体数据，具体类型取决于命令类型。
 * @return 成功时返回 0；如果发生错误（如消息发送失败），则返回 -1 或特定的错误码。
 */
static int adapter_command(neu_adapter_t *adapter, neu_reqresp_head_t header,
                           void *data)
{
    int ret = 0;

    // 创建新消息
    neu_msg_t *msg = neu_msg_new(header.type, header.ctx, data);
    if (NULL == msg) {
        return NEU_ERR_EINTERNAL;
    }
    neu_reqresp_head_t *pheader = neu_msg_get_header(msg);

    // 设置发送者
    strcpy(pheader->sender, adapter->name);

    // 根据命令类型设置接收者
    switch (pheader->type) {
    case NEU_REQ_DRIVER_ACTION: {
        neu_req_driver_action_t *cmd = (neu_req_driver_action_t *) data;
        strcpy(pheader->receiver, cmd->driver);
        break;
    }
    case NEU_REQ_READ_GROUP: {
        neu_req_read_group_t *cmd = (neu_req_read_group_t *) data;
        strcpy(pheader->receiver, cmd->driver);
        break;
    }
    case NEU_REQ_READ_GROUP_PAGINATE: {
        neu_req_read_group_paginate_t *cmd =
            (neu_req_read_group_paginate_t *) data;
        strcpy(pheader->receiver, cmd->driver);
        break;
    }
    case NEU_REQ_TEST_READ_TAG: {
        neu_req_test_read_tag_t *cmd = (neu_req_test_read_tag_t *) data;
        strcpy(pheader->receiver, cmd->driver);
        break;
    }
    case NEU_REQ_WRITE_TAG: {
        neu_req_write_tag_t *cmd = (neu_req_write_tag_t *) data;
        strcpy(pheader->receiver, cmd->driver);
        break;
    }
    case NEU_REQ_WRITE_TAGS: {
        neu_req_write_tags_t *cmd = (neu_req_write_tags_t *) data;
        strcpy(pheader->receiver, cmd->driver);
        break;
    }
    case NEU_REQ_WRITE_GTAGS: {
        neu_req_write_gtags_t *cmd = (neu_req_write_gtags_t *) data;
        strcpy(pheader->receiver, cmd->driver);
        break;
    }
    case NEU_REQ_DEL_NODE: {
        neu_req_del_node_t *cmd = (neu_req_del_node_t *) data;
        strcpy(pheader->receiver, cmd->node);
        break;
    }
    case NEU_REQ_UPDATE_GROUP:
    case NEU_REQ_GET_GROUP:
    case NEU_REQ_DEL_GROUP:
    case NEU_REQ_ADD_GROUP: {
        neu_req_add_group_t *cmd = (neu_req_add_group_t *) data;
        strcpy(pheader->receiver, cmd->driver);
        break;
    }
    case NEU_REQ_GET_TAG:
    case NEU_REQ_UPDATE_TAG:
    case NEU_REQ_DEL_TAG:
    case NEU_REQ_ADD_TAG: {
        neu_req_add_tag_t *cmd = (neu_req_add_tag_t *) data;
        strcpy(pheader->receiver, cmd->driver);
        break;
    }
    case NEU_REQ_ADD_GTAG: {
        neu_req_add_gtag_t *cmd = (neu_req_add_gtag_t *) data;
        strcpy(pheader->receiver, cmd->driver);
        break;
    }
    case NEU_REQ_UPDATE_NODE:
    case NEU_REQ_NODE_CTL:
    case NEU_REQ_GET_NODE_STATE:
    case NEU_REQ_GET_NODE_SETTING:
    case NEU_REQ_NODE_SETTING: {
        neu_req_node_setting_t *cmd = (neu_req_node_setting_t *) data;
        strcpy(pheader->receiver, cmd->node);
        break;
    }
    case NEU_REQ_CHECK_SCHEMA:
    case NEU_REQ_GET_DRIVER_GROUP:
    case NEU_REQ_GET_SUB_DRIVER_TAGS: {
        strcpy(pheader->receiver, "manager");
        break;
    }
    case NEU_REQRESP_NODE_DELETED: {
        neu_reqresp_node_deleted_t *cmd = (neu_reqresp_node_deleted_t *) data;
        strcpy(pheader->receiver, cmd->node);
        break;
    }
    case NEU_REQ_UPDATE_LOG_LEVEL: {
        neu_req_update_log_level_t *cmd = (neu_req_update_log_level_t *) data;
        strcpy(pheader->receiver, cmd->node);
        break;
    }
    case NEU_REQ_PRGFILE_UPLOAD: {
        neu_req_prgfile_upload_t *cmd = (neu_req_prgfile_upload_t *) data;
        strcpy(pheader->receiver, cmd->driver);
        break;
    }
    case NEU_REQ_PRGFILE_PROCESS: {
        neu_req_prgfile_process_t *cmd = (neu_req_prgfile_process_t *) data;
        strcpy(pheader->receiver, cmd->driver);
        break;
    }
    case NEU_RESP_PRGFILE_PROCESS: {
        strcpy(pheader->receiver, header.sender);
        break;
    }
    case NEU_REQ_SCAN_TAGS: {
        neu_req_scan_tags_t *cmd = (neu_req_scan_tags_t *) data;
        strcpy(pheader->receiver, cmd->driver);
        break;
    }
    default:
        break;
    }
    
    // 发送消息
    ret = neu_send_msg(adapter->control_fd, msg);
    if (0 != ret) {
        nlog_error(
            "adapter: %s send %d command %s failed, ret: %d-%d, errno: %s(%d)",
            adapter->name, adapter->control_fd,
            neu_reqresp_type_string(pheader->type), ret, pheader->len,
            strerror(errno), errno);
        neu_msg_free(msg);
        return -1;
    } else {
        return 0;
    }
}

/**
 * @brief 处理并发送适配器响应。
 *
 * 此函数处理接收到的请求/响应头部，并生成相应的消息以响应请求。它首先检查请求类型是否不是
 *  `NEU_REQRESP_TRANS_DATA`，然后调用 `neu_msg_exchange` 交换消息头信息，接着
 * 生成消息体。最终通过控制文件描述符发送该消息。如果发送过程中出现错误，则记录错误日志并释放消息资源。
 *
 * @param adapter 指向 `neu_adapter_t` 结构体的指针，表示当前适配器对象。
 * @param header  请求/响应头部结构体指针，包含命令类型和上下文信息等。
 * @param data    消息体数据，具体格式取决于请求/响应类型。
 * @return 成功时返回 0；如果发生错误（如消息发送失败），则返回错误码。
 */
static int adapter_response(neu_adapter_t *adapter, neu_reqresp_head_t *header,
                            void *data)
{
    assert(header->type != NEU_REQRESP_TRANS_DATA);
    neu_msg_exchange(header);

    neu_msg_gen(header, data);

    /**
     * @note
     * 
     * 强制类型转换只是改变了编译器对指针的解释方式，而不会改变指针所指向的实际内存地址。
     * 编译器会将 header 指针从原本的 neu_reqresp_head_t * 类型，视为 neu_msg_t * 类型。
     * 由于 header 指向的内存地址是 neu_msg_t 结构体的起始地址，所以这种转换在逻辑上是合理的。
     * 如果不是则会有内存布局不一致的问题，导致强制转换后访问成员出错
     */
    neu_msg_t *msg = (neu_msg_t *) header;
    int        ret = neu_send_msg(adapter->control_fd, msg);
    if (0 != ret) {
        nlog_error("adapter: %s send response %s failed, ret: %d, errno: %d",
                   adapter->name, neu_reqresp_type_string(header->type), ret,
                   errno);
        neu_msg_free(msg);
    }

    return ret;
}

/**
 * @brief 发送适配器响应到指定的目标地址。
 *
 * 此函数用于发送适配器响应到指定的目标地址。它首先检查请求类型是否为 `NEU_REQRESP_TRANS_DATA`，
 * 然后创建一个新的消息对象，并设置消息头部信息。接着将响应发送到指定的目标地址。
 * 如果发送过程中出现错误，则记录错误日志并释放消息资源。
 *
 * @param adapter 指向 `neu_adapter_t` 结构体的指针，表示当前适配器对象。
 * @param header  请求/响应头部结构体指针，包含命令类型和上下文信息等。
 * @param data    消息体数据，具体格式取决于请求/响应类型。
 * @param dst     目标地址，使用 `sockaddr_un` 结构体表示 Unix 域套接字地址。
 * @return 成功时返回 0；如果发生错误（如消息发送失败），则返回错误码。
 */
static int adapter_responseto(neu_adapter_t *     adapter,
                              neu_reqresp_head_t *header, void *data,
                              struct sockaddr_un dst)
{
    //只处理 NEU_REQRESP_TRANS_DATA 类型的请求
    assert(header->type == NEU_REQRESP_TRANS_DATA);

    neu_msg_t *msg = neu_msg_new(header->type, header->ctx, data);
    if (NULL == msg) {
        return NEU_ERR_EINTERNAL;
    }
    neu_reqresp_head_t *pheader = neu_msg_get_header(msg);
    strcpy(pheader->sender, adapter->name);

    int ret = neu_send_msg_to(adapter->control_fd, &dst, msg);
    if (0 != ret) {
        nlog_error("adapter: %s send responseto %s failed, ret: %d, errno: %d",
                   adapter->name, neu_reqresp_type_string(header->type), ret,
                   errno);
        neu_msg_free(msg);
    }

    return ret;
}

/**
 * @brief 处理传输数据事件。
 *
 * 在适配器从套接字接收到消息后，根据消息类型将数据压入适配器的消息队列msg_q
 * 或者直接处理请求
 *
 * @param type     事件类型，必须是 `NEU_EVENT_IO_READ` 
 * @param fd       文件描述符，用于接收数据。
 * @param usr_data 用户数据，应为指向 `neu_adapter_t` 结构体的指针。
 * @return 返回 0 表示成功处理事件；非零值表示处理失败。
 */
static int adapter_trans_data(enum neu_event_io_type type, int fd,
                              void *usr_data)
{
    neu_adapter_t *adapter = (neu_adapter_t *) usr_data;

    // 检查事件类型： 只处理 NEU_EVENT_IO_READ
    if (type != NEU_EVENT_IO_READ) {
        nlog_warn("adapter: %s recv close, exit loop, fd: %d", adapter->name,
                  fd);
        return 0;
    }

    // 从适配器的传输数据套接字接收消息
    neu_msg_t *msg = NULL;
    int        rv  = neu_recv_msg(adapter->trans_data_fd, &msg);
    if (0 != rv) {
        nlog_warn("adapter: %s recv trans data failed, ret: %d, errno: %s(%d)",
                  adapter->name, rv, strerror(errno), errno);
        return 0;
    }

    // 获取消息头部
    neu_reqresp_head_t *header = neu_msg_get_header(msg);

    nlog_debug("adapter(%s) recv msg from: %s %p, type: %s", adapter->name,
               header->sender, header->ctx,
               neu_reqresp_type_string(header->type));
    
    // 检查消息类型：对非 NEU_REQRESP_TRANS_DATA，NEU_RESP_ERROR 类型记录告警
    if (header->type != NEU_REQRESP_TRANS_DATA &&
        header->type != NEU_RESP_ERROR) {
        nlog_warn("adapter: %s recv msg type error, type: %s", adapter->name,
                  neu_reqresp_type_string(header->type));
        neu_msg_free(msg);
        return 0;
    }

    /**
     * @note
     * 
     * - 1:为了及时处理一些紧急或需要立即响应的消息，比如错误响应消息 NEU_RESP_ERROR 。
     *   这些消息可能需要马上处理，而不能等待消息队列中的消息依次处理。
     * - 2:对于消息类型NEU_REQRESP_TRANS_DATA 则由消费线程执行adapter_consumer处理
     *   消息队列中的消息
     */

    // 情况1：NEU_REQRESP_TRANS_DATA
    if (header->type == NEU_REQRESP_TRANS_DATA) {
        if (adapter_msg_q_push(adapter->msg_q, msg) < 0) {
            nlog_warn("adapter: %s trans data msg q is full, drop msg",
                      adapter->name);
            neu_trans_data_free((neu_reqresp_trans_data_t *) &header[1]);
            neu_msg_free(msg);
        }
        return 0;
    }
    
    // 情况2： NEU_RESP_ERROR
    adapter->module->intf_funs->request(
        adapter->plugin, (neu_reqresp_head_t *) header, &header[1]);

    neu_msg_free(msg);
    return 0;
}

/**
 * @brief 适配器的事件循环处理函数。
 *
 * 该函数用于处理适配器接收到的消息，根据消息类型执行相应的操作。它会监听指定的文件描述符，
 * 当有可读事件发生时，接收消息并根据消息头中的类型进行不同的处理，如订阅、取消订阅、读写操作等。
 *
 * @param type 事件类型，指示是读事件还是其他类型的事件。
 * @param fd 监听的文件描述符，用于接收消息。
 * @param usr_data 用户数据，通常是指向 `neu_adapter_t` 结构体的指针，代表适配器实例。
 * @return 总是返回 0，表示处理完成。
 */
static int adapter_loop(enum neu_event_io_type type, int fd, void *usr_data)
{
    neu_adapter_t *adapter = (neu_adapter_t *) usr_data;

    // 检查事件类型是否为读事件
    if (type != NEU_EVENT_IO_READ) {
        nlog_warn("adapter: %s recv close, exit loop, fd: %d", adapter->name,
                  fd);
        return 0;
    }

    neu_msg_t *msg = NULL;
    // 从控制文件描述符接收消息
    int        rv  = neu_recv_msg(adapter->control_fd, &msg);
    if (0 != rv) {
        nlog_warn("adapter: %s recv failed, ret: %d, errno: %s(%d)",
                  adapter->name, rv, strerror(errno), errno);
        return 0;
    }

    neu_reqresp_head_t *header = neu_msg_get_header(msg);

    nlog_info("adapter(%s) recv msg from: %s %p, type: %s", adapter->name,
              header->sender, header->ctx,
              neu_reqresp_type_string(header->type));

    // 根据消息类型做不同的处理
    switch (header->type) {
    case NEU_REQ_SUBSCRIBE_GROUP: {
        neu_req_subscribe_t *cmd = (neu_req_subscribe_t *) &header[1];
        if (adapter->module->type == NEU_NA_TYPE_DRIVER) {
            neu_adapter_driver_subscribe((neu_adapter_driver_t *) adapter, cmd);
        } else {
            adapter->module->intf_funs->request(
                adapter->plugin, (neu_reqresp_head_t *) header, &header[1]);
        }
        neu_msg_free(msg);
        break;
    }
    case NEU_REQ_UNSUBSCRIBE_GROUP: {
        neu_req_unsubscribe_t *cmd = (neu_req_unsubscribe_t *) &header[1];
        if (adapter->module->type == NEU_NA_TYPE_DRIVER) {
            neu_adapter_driver_unsubscribe((neu_adapter_driver_t *) adapter,
                                           cmd);
        } else {
            adapter->module->intf_funs->request(
                adapter->plugin, (neu_reqresp_head_t *) header, &header[1]);
        }
        neu_msg_free(msg);
        break;
    }
    case NEU_REQ_UPDATE_SUBSCRIBE_GROUP:
    case NEU_RESP_GET_DRIVER_GROUP:
    case NEU_REQRESP_NODE_DELETED:
    case NEU_RESP_GET_SUB_DRIVER_TAGS:
    case NEU_REQ_UPDATE_NODE:
    case NEU_RESP_GET_NODE_STATE:
    case NEU_RESP_GET_NODES_STATE:
    case NEU_RESP_GET_NODE_SETTING:
    case NEU_REQ_UPDATE_GROUP:
    case NEU_RESP_GET_SUBSCRIBE_GROUP:
    case NEU_RESP_ADD_TAG:
    case NEU_RESP_ADD_GTAG:
    case NEU_RESP_UPDATE_TAG:
    case NEU_RESP_GET_TAG:
    case NEU_RESP_GET_NODE:
    case NEU_RESP_GET_PLUGIN:
    case NEU_RESP_GET_GROUP:
    case NEU_RESP_ERROR:
    case NEU_REQRESP_NODES_STATE:
    case NEU_REQ_PRGFILE_PROCESS:
    case NEU_RESP_PRGFILE_PROCESS:
    case NEU_RESP_CHECK_SCHEMA:
    case NEU_RESP_DRIVER_ACTION:
        adapter->module->intf_funs->request(
            adapter->plugin, (neu_reqresp_head_t *) header, &header[1]);
        neu_msg_free(msg);
        break;
    case NEU_RESP_READ_GROUP:
        adapter->module->intf_funs->request(
            adapter->plugin, (neu_reqresp_head_t *) header, &header[1]);
        neu_resp_read_free((neu_resp_read_group_t *) &header[1]);
        neu_msg_free(msg);
        break;
    case NEU_REQ_READ_GROUP: {
        neu_resp_error_t error = { 0 };

        if (adapter->module->type == NEU_NA_TYPE_DRIVER) {
            neu_adapter_driver_read_group((neu_adapter_driver_t *) adapter,
                                          header);
        } else {
            neu_req_read_group_fini((neu_req_read_group_t *) &header[1]);
            error.error  = NEU_ERR_GROUP_NOT_ALLOW;
            header->type = NEU_RESP_ERROR;
            neu_msg_exchange(header);
            reply(adapter, header, &error);
        }

        break;
    }
    case NEU_RESP_READ_GROUP_PAGINATE:
        adapter->module->intf_funs->request(
            adapter->plugin, (neu_reqresp_head_t *) header, &header[1]);
        neu_resp_read_paginate_free(
            (neu_resp_read_group_paginate_t *) &header[1]);
        neu_msg_free(msg);
        break;
    case NEU_REQ_READ_GROUP_PAGINATE: {
        neu_resp_error_t error = { 0 };

        if (adapter->module->type == NEU_NA_TYPE_DRIVER) {
            neu_adapter_driver_read_group_paginate(
                (neu_adapter_driver_t *) adapter, header);
        } else {
            neu_req_read_group_paginate_fini(
                (neu_req_read_group_paginate_t *) &header[1]);
            error.error  = NEU_ERR_GROUP_NOT_ALLOW;
            header->type = NEU_RESP_ERROR;
            neu_msg_exchange(header);
            reply(adapter, header, &error);
        }

        break;
    }
    case NEU_REQ_WRITE_TAG: {
        neu_resp_error_t error = { 0 };

        neu_otel_trace_ctx trace = NULL;
        neu_otel_scope_ctx scope = NULL;
        if (neu_otel_control_is_started()) {
            trace = neu_otel_find_trace(header->ctx);
            if (trace) {
                scope = neu_otel_add_span(trace);
                neu_otel_scope_set_span_name(scope, "adapter write tag");
                char new_span_id[36] = { 0 };
                neu_otel_new_span_id(new_span_id);
                neu_otel_scope_set_span_id(scope, new_span_id);
                uint8_t *p_sp_id = neu_otel_scope_get_pre_span_id(scope);
                if (p_sp_id) {
                    neu_otel_scope_set_parent_span_id2(scope, p_sp_id, 8);
                }
                neu_otel_scope_add_span_attr_int(scope, "thread id",
                                                 (int64_t) pthread_self());
                neu_otel_scope_set_span_start_time(scope, neu_time_ns());
            }
        }

        bool re_flag = false;

        if (adapter->module->type == NEU_NA_TYPE_DRIVER) {
            int w_error = neu_adapter_driver_write_tag(
                (neu_adapter_driver_t *) adapter, header);
            if (NEU_ERR_SUCCESS == w_error) {
                re_flag = true;
            } else {
                if (neu_otel_control_is_started() && trace) {
                    if (w_error != NEU_ERR_SUCCESS) {
                        neu_otel_scope_set_status_code2(
                            scope, NEU_OTEL_STATUS_ERROR, w_error);
                    } else {
                        neu_otel_scope_set_status_code2(
                            scope, NEU_OTEL_STATUS_OK, w_error);
                    }
                }
            }
        } else {
            neu_req_write_tag_fini((neu_req_write_tag_t *) &header[1]);
            error.error  = NEU_ERR_GROUP_NOT_ALLOW;
            header->type = NEU_RESP_ERROR;
            neu_msg_exchange(header);
            reply(adapter, header, &error);
            if (neu_otel_control_is_started() && trace) {
                neu_otel_scope_set_status_code2(scope, NEU_OTEL_STATUS_ERROR,
                                                NEU_ERR_GROUP_NOT_ALLOW);
            }
        }

        if (neu_otel_control_is_started() && trace) {
            neu_otel_scope_set_span_end_time(scope, neu_time_ns());
            if (!re_flag) {
                neu_otel_trace_set_final(trace);
            }
        }

        break;
    }
    case NEU_REQ_WRITE_TAGS: {
        neu_resp_error_t error = { 0 };

        neu_otel_trace_ctx trace = NULL;
        neu_otel_scope_ctx scope = NULL;
        if (neu_otel_control_is_started()) {
            trace = neu_otel_find_trace(header->ctx);
            if (trace) {
                scope = neu_otel_add_span(trace);
                neu_otel_scope_set_span_name(scope, "adapter write tags");
                char new_span_id[36] = { 0 };
                neu_otel_new_span_id(new_span_id);
                neu_otel_scope_set_span_id(scope, new_span_id);
                uint8_t *p_sp_id = neu_otel_scope_get_pre_span_id(scope);
                if (p_sp_id) {
                    neu_otel_scope_set_parent_span_id2(scope, p_sp_id, 8);
                }
                neu_otel_scope_add_span_attr_int(scope, "thread id",
                                                 (int64_t) pthread_self());
                neu_otel_scope_set_span_start_time(scope, neu_time_ns());
            }
        }

        bool re_flag = false;

        if (adapter->module->type != NEU_NA_TYPE_DRIVER) {
            neu_req_write_tags_fini((neu_req_write_tags_t *) &header[1]);
            error.error  = NEU_ERR_GROUP_NOT_ALLOW;
            header->type = NEU_RESP_ERROR;
            neu_msg_exchange(header);
            reply(adapter, header, &error);
            if (neu_otel_control_is_started() && trace) {
                neu_otel_scope_set_status_code2(scope, NEU_OTEL_STATUS_ERROR,
                                                NEU_ERR_GROUP_NOT_ALLOW);
            }
        } else {
            int w_error = neu_adapter_driver_write_tags(
                (neu_adapter_driver_t *) adapter, header);

            if (NEU_ERR_SUCCESS == w_error) {
                re_flag = true;
            } else {
                if (neu_otel_control_is_started() && trace) {
                    if (neu_otel_control_is_started() && trace) {
                        if (w_error != NEU_ERR_SUCCESS) {
                            neu_otel_scope_set_status_code2(
                                scope, NEU_OTEL_STATUS_ERROR, w_error);
                        } else {
                            neu_otel_scope_set_status_code2(
                                scope, NEU_OTEL_STATUS_OK, w_error);
                        }
                    }
                }
            }
        }

        if (neu_otel_control_is_started() && trace) {
            neu_otel_scope_set_span_end_time(scope, neu_time_ns());
            if (!re_flag) {
                neu_otel_trace_set_final(trace);
            }
        }

        break;
    }
    case NEU_REQ_WRITE_GTAGS: {
        neu_resp_error_t error = { 0 };

        neu_otel_trace_ctx trace = NULL;
        neu_otel_scope_ctx scope = NULL;
        if (neu_otel_control_is_started()) {
            trace = neu_otel_find_trace(header->ctx);
            if (trace) {
                scope = neu_otel_add_span(trace);
                neu_otel_scope_set_span_name(scope, "adapter write tags");
                char new_span_id[36] = { 0 };
                neu_otel_new_span_id(new_span_id);
                neu_otel_scope_set_span_id(scope, new_span_id);
                uint8_t *p_sp_id = neu_otel_scope_get_pre_span_id(scope);
                if (p_sp_id) {
                    neu_otel_scope_set_parent_span_id2(scope, p_sp_id, 8);
                }
                neu_otel_scope_add_span_attr_int(scope, "thread id",
                                                 (int64_t) pthread_self());
                neu_otel_scope_set_span_start_time(scope, neu_time_ns());
            }
        }

        bool re_flag = false;

        if (adapter->module->type != NEU_NA_TYPE_DRIVER) {
            neu_req_write_gtags_fini((neu_req_write_gtags_t *) &header[1]);
            error.error  = NEU_ERR_GROUP_NOT_ALLOW;
            header->type = NEU_RESP_ERROR;
            neu_msg_exchange(header);
            reply(adapter, header, &error);

            if (neu_otel_control_is_started() && trace) {
                neu_otel_scope_set_status_code2(scope, NEU_OTEL_STATUS_ERROR,
                                                NEU_ERR_GROUP_NOT_ALLOW);
            }
        } else {
            int w_error = neu_adapter_driver_write_gtags(
                (neu_adapter_driver_t *) adapter, header);

            if (NEU_ERR_SUCCESS == w_error) {
                re_flag = true;
            } else {
                if (neu_otel_control_is_started() && trace) {
                    if (neu_otel_control_is_started() && trace) {
                        if (w_error != NEU_ERR_SUCCESS) {
                            neu_otel_scope_set_status_code2(
                                scope, NEU_OTEL_STATUS_ERROR, w_error);
                        } else {
                            neu_otel_scope_set_status_code2(
                                scope, NEU_OTEL_STATUS_OK, w_error);
                        }
                    }
                }
            }
        }

        if (neu_otel_control_is_started() && trace) {
            neu_otel_scope_set_span_end_time(scope, neu_time_ns());
            if (!re_flag) {
                neu_otel_trace_set_final(trace);
            }
        }
        break;
    }
    case NEU_REQ_NODE_SETTING: {
        neu_req_node_setting_t *cmd   = (neu_req_node_setting_t *) &header[1];
        neu_resp_error_t        error = { 0 };
        nlog_notice("setting node:%s params:%s", cmd->node, cmd->setting);
        error.error = neu_adapter_set_setting(adapter, cmd->setting);
        if (error.error == NEU_ERR_SUCCESS) {
            adapter_storage_setting(adapter->name, cmd->setting);
            free(cmd->setting);
        } else {
            free(cmd->setting);
        }

        header->type = NEU_RESP_ERROR;
        neu_msg_exchange(header);
        reply(adapter, header, &error);
        break;
    }
    case NEU_REQ_GET_NODE_SETTING: {
        neu_resp_get_node_setting_t resp  = { 0 };
        neu_resp_error_t            error = { 0 };

        neu_msg_exchange(header);
        error.error = neu_adapter_get_setting(adapter, &resp.setting);
        if (error.error != NEU_ERR_SUCCESS) {
            header->type = NEU_RESP_ERROR;
            reply(adapter, header, &error);
        } else {
            header->type = NEU_RESP_GET_NODE_SETTING;
            strcpy(resp.node, adapter->name);
            reply(adapter, header, &resp);
        }
        break;
    }
    case NEU_REQ_GET_NODE_STATE: {
        neu_resp_get_node_state_t *resp =
            (neu_resp_get_node_state_t *) &header[1];

        if (NULL != adapter->metrics) {
            pthread_mutex_lock(&adapter->metrics->lock);
            neu_metric_entry_t *e = NULL;
            HASH_FIND_STR(adapter->metrics->entries, NEU_METRIC_LAST_RTT_MS, e);
            resp->rtt = NULL != e ? e->value : 0;
            pthread_mutex_unlock(&adapter->metrics->lock);
        }
        resp->state  = neu_adapter_get_state(adapter);
        header->type = NEU_RESP_GET_NODE_STATE;
        neu_msg_exchange(header);
        reply(adapter, header, resp);
        break;
    }
    case NEU_REQ_GET_GROUP: {
        neu_msg_exchange(header);

        if (adapter->module->type == NEU_NA_TYPE_DRIVER) {
            neu_resp_get_group_t resp = {
                .groups = neu_adapter_driver_get_group(
                    (neu_adapter_driver_t *) adapter)
            };
            header->type = NEU_RESP_GET_GROUP;
            reply(adapter, header, &resp);
        } else {
            neu_resp_error_t error = { .error = NEU_ERR_GROUP_NOT_ALLOW };

            header->type = NEU_RESP_ERROR;
            reply(adapter, header, &error);
        }
        break;
    }
    case NEU_REQ_GET_TAG: {
        neu_req_get_tag_t *cmd   = (neu_req_get_tag_t *) &header[1];
        neu_resp_error_t   error = { .error = 0 };
        UT_array *         tags  = NULL;

        if (adapter->module->type == NEU_NA_TYPE_DRIVER) {
            error.error = neu_adapter_driver_query_tag(
                (neu_adapter_driver_t *) adapter, cmd->group, cmd->name, &tags);
        } else {
            error.error = NEU_ERR_GROUP_NOT_ALLOW;
        }

        neu_msg_exchange(header);
        if (error.error != NEU_ERR_SUCCESS) {
            header->type = NEU_RESP_ERROR;
            reply(adapter, header, &error);
        } else {
            neu_resp_get_tag_t resp = { .tags = tags };

            header->type = NEU_RESP_GET_TAG;
            reply(adapter, header, &resp);
        }

        break;
    }
    case NEU_REQ_ADD_GROUP: {
        neu_req_add_group_t *cmd   = (neu_req_add_group_t *) &header[1];
        neu_resp_error_t     error = { 0 };
        nlog_notice("add group node:%s group:%s interval:%d", cmd->driver,
                    cmd->group, cmd->interval);
        if (cmd->interval < NEU_GROUP_INTERVAL_LIMIT) {
            error.error = NEU_ERR_GROUP_PARAMETER_INVALID;
        } else {
            if (adapter->module->type == NEU_NA_TYPE_DRIVER) {
                error.error = neu_adapter_driver_add_group(
                    (neu_adapter_driver_t *) adapter, cmd->group, cmd->interval,
                    NULL);
            } else {
                error.error = NEU_ERR_GROUP_NOT_ALLOW;
            }
        }

        if (error.error == NEU_ERR_SUCCESS) {
            adapter_storage_add_group(adapter->name, cmd->group, cmd->interval,
                                      NULL);
        }

        neu_msg_exchange(header);
        header->type = NEU_RESP_ERROR;
        reply(adapter, header, &error);
        break;
    }
    case NEU_REQ_UPDATE_DRIVER_GROUP: {
        neu_req_update_group_t *cmd  = (neu_req_update_group_t *) &header[1];
        neu_resp_update_group_t resp = { 0 };
        nlog_notice("update group node:%s old_name:%s new_name:%s interval:%d",
                    header->receiver, cmd->group, cmd->new_name, cmd->interval);
        if (adapter->module->type == NEU_NA_TYPE_DRIVER) {
            resp.error = neu_adapter_driver_update_group(
                (neu_adapter_driver_t *) adapter, cmd->group, cmd->new_name,
                cmd->interval);
        } else {
            resp.error = NEU_ERR_GROUP_NOT_ALLOW;
        }

        if (resp.error == NEU_ERR_SUCCESS) {
            adapter_storage_update_group(adapter->name, cmd->group,
                                         cmd->new_name, cmd->interval);
        }

        strcpy(resp.driver, cmd->driver);
        strcpy(resp.group, cmd->group);
        strcpy(resp.new_name, cmd->new_name);
        header->type = NEU_RESP_UPDATE_DRIVER_GROUP;
        neu_msg_exchange(header);
        reply(adapter, header, &resp);
        break;
    }
    case NEU_REQ_DEL_GROUP: {
        neu_req_del_group_t *cmd   = (neu_req_del_group_t *) &header[1];
        neu_resp_error_t     error = { 0 };
        nlog_notice("del group node:%s group:%s", cmd->driver, cmd->group);
        if (adapter->module->type == NEU_NA_TYPE_DRIVER) {
            error.error = neu_adapter_driver_del_group(
                (neu_adapter_driver_t *) adapter, cmd->group);
        } else {
            adapter->module->intf_funs->request(
                adapter->plugin, (neu_reqresp_head_t *) header, &header[1]);
            neu_msg_free(msg);
            break;
        }

        if (error.error == NEU_ERR_SUCCESS) {
            adapter_storage_del_group(cmd->driver, cmd->group);
        }

        neu_msg_exchange(header);
        header->type = NEU_RESP_ERROR;
        reply(adapter, header, &error);
        break;
    }
    case NEU_REQ_NODE_CTL: {
        neu_req_node_ctl_t *cmd   = (neu_req_node_ctl_t *) &header[1];
        neu_resp_error_t    error = { 0 };

        switch (cmd->ctl) {
        case NEU_ADAPTER_CTL_START:
            error.error = neu_adapter_start(adapter);
            break;
        case NEU_ADAPTER_CTL_STOP:
            error.error = neu_adapter_stop(adapter);
            break;
        }

        neu_msg_exchange(header);
        header->type = NEU_RESP_ERROR;
        reply(adapter, header, &error);
        break;
    }
    case NEU_REQ_NODE_RENAME: {
        neu_req_node_rename_t *cmd  = (neu_req_node_rename_t *) &header[1];
        neu_resp_node_rename_t resp = { 0 };
        resp.error = neu_adapter_rename(adapter, cmd->new_name);
        strcpy(header->receiver, header->sender);
        strcpy(header->sender, cmd->new_name);
        strcpy(resp.node, cmd->node);
        strcpy(resp.new_name, cmd->new_name);
        header->type = NEU_RESP_NODE_RENAME;
        reply(adapter, header, &resp);
        break;
    }
    case NEU_REQ_DEL_TAG: {
        neu_req_del_tag_t *cmd   = (neu_req_del_tag_t *) &header[1];
        neu_resp_error_t   error = { 0 };

        if (adapter->module->type == NEU_NA_TYPE_DRIVER) {
            for (int i = 0; i < cmd->n_tag; i++) {
                nlog_notice("del tag node:%s group:%s tag:%s", cmd->driver,
                            cmd->group, cmd->tags[i]);
                int ret = neu_adapter_driver_del_tag(
                    (neu_adapter_driver_t *) adapter, cmd->group, cmd->tags[i]);
                if (0 == ret) {
                    adapter_storage_del_tag(cmd->driver, cmd->group,
                                            cmd->tags[i]);
                } else {
                    error.error = ret;
                    break;
                }
            }
        } else {
            error.error = NEU_ERR_GROUP_NOT_ALLOW;
        }

        for (uint16_t i = 0; i < cmd->n_tag; i++) {
            free(cmd->tags[i]);
        }
        free(cmd->tags);

        neu_msg_exchange(header);
        header->type = NEU_RESP_ERROR;
        reply(adapter, header, &error);
        break;
    }
    case NEU_REQ_ADD_TAG: {
        neu_req_add_tag_t *cmd  = (neu_req_add_tag_t *) &header[1];
        neu_resp_add_tag_t resp = { 0 };

        if (adapter->module->type == NEU_NA_TYPE_DRIVER) {
            for (int i = 0; i < cmd->n_tag; i++) {
                int ret = neu_adapter_driver_validate_tag(
                    (neu_adapter_driver_t *) adapter, cmd->group,
                    &cmd->tags[i]);
                if (ret == 0) {
                    resp.index += 1;
                } else {
                    resp.error = ret;
                    break;
                }
            }
        } else {
            resp.error = NEU_ERR_GROUP_NOT_ALLOW;
        }

        if (resp.index > 0) {
            int ret = neu_adapter_driver_try_add_tag(
                (neu_adapter_driver_t *) adapter, cmd->group, cmd->tags,
                resp.index);
            if (ret != 0) {
                resp.index = 0;
                resp.error = ret;
            }
        }

        for (int i = 0; i < resp.index; i++) {
            int ret = neu_adapter_driver_add_tag(
                (neu_adapter_driver_t *) adapter, cmd->group, &cmd->tags[i],
                NEU_DEFAULT_GROUP_INTERVAL);
            if (ret != 0) {
                neu_adapter_driver_try_del_tag((neu_adapter_driver_t *) adapter,
                                               resp.index - i);
                resp.index = i;
                resp.error = ret;
                break;
            }
        }

        if (resp.index) {
            // we have added some tags, try to persist
            adapter_storage_add_tags(cmd->driver, cmd->group, cmd->tags,
                                     resp.index);
        }

        for (uint16_t i = 0; i < cmd->n_tag; i++) {
            neu_tag_fini(&cmd->tags[i]);
        }
        free(cmd->tags);

        neu_msg_exchange(header);
        header->type = NEU_RESP_ADD_TAG;
        reply(adapter, header, &resp);
        break;
    }
    case NEU_REQ_ADD_GTAG: {
        neu_req_add_gtag_t *cmd  = (neu_req_add_gtag_t *) &header[1];
        neu_resp_add_tag_t  resp = { 0 };

        if (adapter->module->type != NEU_NA_TYPE_DRIVER) {
            resp.error = NEU_ERR_GROUP_NOT_ALLOW;
        } else {
            if (neu_adapter_validate_gtags(adapter, cmd, &resp) == 0 &&
                neu_adapter_try_add_gtags(adapter, cmd, &resp) == 0 &&
                neu_adapter_add_gtags(adapter, cmd, &resp) == 0) {
                for (int i = 0; i < cmd->n_group; i++) {
                    adapter_storage_add_tags(cmd->driver, cmd->groups[i].group,
                                             cmd->groups[i].tags,
                                             cmd->groups[i].n_tag);
                }
            }
        }

        for (int i = 0; i < cmd->n_group; i++) {
            for (int j = 0; j < cmd->groups[i].n_tag; j++) {
                neu_tag_fini(&cmd->groups[i].tags[j]);
            }
            free(cmd->groups[i].tags);
        }
        free(cmd->groups);

        neu_msg_exchange(header);
        header->type = NEU_RESP_ADD_GTAG;
        reply(adapter, header, &resp);
        break;
    }
    case NEU_REQ_UPDATE_TAG: {
        neu_req_update_tag_t *cmd  = (neu_req_update_tag_t *) &header[1];
        neu_resp_update_tag_t resp = { 0 };

        if (adapter->module->type == NEU_NA_TYPE_DRIVER) {

            for (int i = 0; i < cmd->n_tag; i++) {
                int ret = neu_adapter_driver_validate_tag(
                    (neu_adapter_driver_t *) adapter, cmd->group,
                    &cmd->tags[i]);
                if (ret == 0) {
                    ret = neu_adapter_driver_update_tag(
                        (neu_adapter_driver_t *) adapter, cmd->group,
                        &cmd->tags[i]);
                    if (ret == 0) {
                        adapter_storage_update_tag(cmd->driver, cmd->group,
                                                   &cmd->tags[i]);

                        resp.index += 1;
                    } else {
                        resp.error = ret;
                        break;
                    }
                } else {
                    resp.error = ret;
                    break;
                }
            }
        } else {
            resp.error = NEU_ERR_GROUP_NOT_ALLOW;
        }

        for (uint16_t i = 0; i < cmd->n_tag; i++) {
            neu_tag_fini(&cmd->tags[i]);
        }
        free(cmd->tags);

        neu_msg_exchange(header);
        header->type = NEU_RESP_UPDATE_TAG;
        reply(adapter, header, &resp);
        break;
    }
    case NEU_REQ_NODE_UNINIT: {
        neu_req_node_uninit_t *cmd = (neu_req_node_uninit_t *) &header[1];
        char                   name[NEU_NODE_NAME_LEN]     = { 0 };
        char                   receiver[NEU_NODE_NAME_LEN] = { 0 };

        neu_adapter_uninit(adapter);

        header->type = NEU_RESP_NODE_UNINIT;
        neu_msg_exchange(header);
        strcpy(header->sender, adapter->name);
        strcpy(cmd->node, adapter->name);

        neu_msg_gen(header, cmd);

        strcpy(name, adapter->name);
        strcpy(receiver, header->receiver);

        int ret = neu_send_msg(adapter->control_fd, msg);
        if (0 != ret) {
            nlog_error("%s %d send uninit msg to %s error: %s(%d)", name,
                       adapter->control_fd, receiver, strerror(errno), errno);
            neu_msg_free(msg);
        } else {
            nlog_notice("%s send uninit msg to %s succeeded", name, receiver);
        }
        break;
    }
    case NEU_REQ_UPDATE_LOG_LEVEL: {
        neu_req_update_log_level_t *cmd =
            (neu_req_update_log_level_t *) &header[1];
        neu_resp_error_t error = { 0 };
        adapter->log_level     = cmd->log_level;
        zlog_level_switch(neu_plugin_to_plugin_common(adapter->plugin)->log,
                          cmd->log_level);

        struct timeval tv = { 0 };
        gettimeofday(&tv, NULL);
        adapter->timestamp_lev = tv.tv_sec;

        neu_msg_exchange(header);
        header->type = NEU_RESP_ERROR;
        reply(adapter, header, &error);

        break;
    }
    case NEU_REQ_PRGFILE_UPLOAD: {
        adapter->module->intf_funs->request(
            adapter->plugin, (neu_reqresp_head_t *) header, &header[1]);
        break;
    }
    case NEU_REQ_SCAN_TAGS: {
        neu_adapter_driver_scan_tags((neu_adapter_driver_t *) adapter, header);
        break;
    }
    case NEU_RESP_SCAN_TAGS: {
        adapter->module->intf_funs->request(
            adapter->plugin, (neu_reqresp_head_t *) header, &header[1]);
        neu_msg_free(msg);
        break;
    }
    case NEU_REQ_TEST_READ_TAG: {
        neu_adapter_driver_test_read_tag((neu_adapter_driver_t *) adapter,
                                         header);
        break;
    }
    case NEU_RESP_TEST_READ_TAG: {
        adapter->module->intf_funs->request(
            adapter->plugin, (neu_reqresp_head_t *) header, &header[1]);
        neu_msg_free(msg);
        break;
    }
    case NEU_REQ_DRIVER_ACTION: {
        neu_resp_driver_action_t error = { 0 };
        neu_req_driver_action_t *cmd   = (neu_req_driver_action_t *) &header[1];

        neu_adapter_driver_cmd((neu_adapter_driver_t *) adapter,
                               (const char *) cmd->action);
        neu_msg_exchange(header);
        header->type = NEU_RESP_DRIVER_ACTION;
        reply(adapter, header, &error);

        free(cmd->action);
        break;
    }
    default:
        nlog_warn("adapter: %s recv msg type error, type: %s", adapter->name,
                  neu_reqresp_type_string(header->type));
        assert(false);
        break;
    }

    return 0;
}

int neu_adapter_validate_gtags(neu_adapter_t *adapter, neu_req_add_gtag_t *cmd,
                               neu_resp_add_tag_t *resp)
{
    neu_adapter_driver_t *driver_adapter = (neu_adapter_driver_t *) adapter;
    if (neu_adapter_driver_new_group_count(driver_adapter, cmd) +
            neu_adapter_driver_group_count(driver_adapter) >
        NEU_GROUP_MAX_PER_NODE) {
        resp->error = NEU_ERR_GROUP_MAX_GROUPS;
        resp->index = 0;
        return NEU_ERR_GROUP_MAX_GROUPS;
    }

    for (int group_index = 0; group_index < cmd->n_group; group_index++) {
        neu_gdatatag_t *current_group = &cmd->groups[group_index];
        for (int tag_index = 0; tag_index < current_group->n_tag; tag_index++) {
            int validation_result = neu_adapter_driver_validate_tag(
                driver_adapter, current_group->group,
                &current_group->tags[tag_index]);
            if (validation_result != 0) {
                resp->error = validation_result;
                resp->index = 0;
                return validation_result;
            }
            resp->index += 1;
        }
    }

    return 0;
}

int neu_adapter_try_add_gtags(neu_adapter_t *adapter, neu_req_add_gtag_t *cmd,
                              neu_resp_add_tag_t *resp)
{
    for (int group_index = 0; group_index < cmd->n_group; group_index++) {
        int add_result = neu_adapter_driver_try_add_tag(
            (neu_adapter_driver_t *) adapter, cmd->groups[group_index].group,
            cmd->groups[group_index].tags, cmd->groups[group_index].n_tag);
        if (add_result != 0) {
            for (int added_groups_count = 0; added_groups_count < group_index;
                 added_groups_count++) {
                neu_adapter_driver_try_del_tag(
                    (neu_adapter_driver_t *) adapter,
                    cmd->groups[added_groups_count].n_tag);
            }
            resp->index = 0;
            resp->error = add_result;
            return add_result;
        }
    }
    return 0;
}

int neu_adapter_add_gtags(neu_adapter_t *adapter, neu_req_add_gtag_t *cmd,
                          neu_resp_add_tag_t *resp)
{
    for (int group_index = 0; group_index < cmd->n_group; group_index++) {
        // ensure group created`
        neu_adapter_driver_add_group((neu_adapter_driver_t *) adapter,
                                     cmd->groups[group_index].group,
                                     cmd->groups[group_index].interval,
                                     cmd->groups[group_index].context);
        adapter_storage_add_group(adapter->name, cmd->groups[group_index].group,
                                  cmd->groups[group_index].interval,
                                  cmd->groups[group_index].context);
        for (int tag_index = 0; tag_index < cmd->groups[group_index].n_tag;
             tag_index++) {
            int add_tag_result = neu_adapter_driver_add_tag(
                (neu_adapter_driver_t *) adapter,
                cmd->groups[group_index].group,
                &cmd->groups[group_index].tags[tag_index],
                cmd->groups[group_index].interval);

            if (add_tag_result != 0) {
                for (int added_group_index = 0; added_group_index < group_index;
                     added_group_index++) {
                    for (int added_tag_index = 0;
                         added_tag_index < cmd->groups[added_group_index].n_tag;
                         added_tag_index++) {
                        neu_adapter_driver_del_tag(
                            (neu_adapter_driver_t *) adapter,
                            cmd->groups[added_group_index].group,
                            cmd->groups[added_group_index]
                                .tags[added_tag_index]
                                .name);
                    }
                }
                for (int added_tag_index = 0; added_tag_index < tag_index;
                     added_tag_index++) {
                    neu_adapter_driver_del_tag(
                        (neu_adapter_driver_t *) adapter,
                        cmd->groups[group_index].group,
                        cmd->groups[group_index].tags[added_tag_index].name);
                }

                neu_adapter_driver_try_del_tag((neu_adapter_driver_t *) adapter,
                                               cmd->groups[group_index].n_tag -
                                                   tag_index);

                for (++group_index; group_index < cmd->n_group; group_index++) {
                    neu_adapter_driver_try_del_tag(
                        (neu_adapter_driver_t *) adapter,
                        cmd->groups[group_index].n_tag);
                }

                resp->index = 0;
                resp->error = add_tag_result;
                return add_tag_result;
            }
        }
    }
    return 0;
}

void neu_adapter_destroy(neu_adapter_t *adapter)
{
    nlog_notice("adapter %s destroy", adapter->name);
    close(adapter->control_fd);
    close(adapter->trans_data_fd);

    adapter->module->intf_funs->close(adapter->plugin);

    if (NULL != adapter->metrics) {
        neu_metrics_del_node(adapter);
        neu_node_metrics_free(adapter->metrics);
    }

    if (adapter->consumer_tid != 0) {
        pthread_cancel(adapter->consumer_tid);
    }
    if (adapter->msg_q != NULL) {
        adapter_msg_q_free(adapter->msg_q);
    }

    char *setting = NULL;
    if (adapter_load_setting(adapter->name, &setting) != 0) {
        remove_logs(adapter->name);
    } else {
        free(setting);
    }

    if (adapter->name != NULL) {
        free(adapter->name);
    }
    if (NULL != adapter->setting) {
        free(adapter->setting);
    }

    neu_event_close(adapter->events);
#ifdef NEU_RELEASE
    if (adapter->handle != NULL) {
        dlclose(adapter->handle);
    }
#endif
    free(adapter);
}

int neu_adapter_uninit(neu_adapter_t *adapter)
{
    if (adapter->module->type == NEU_NA_TYPE_DRIVER) {
        neu_adapter_driver_uninit((neu_adapter_driver_t *) adapter);
    }
    adapter->module->intf_funs->uninit(adapter->plugin);

    neu_event_del_io(adapter->events, adapter->control_io);

    if (adapter->module->type == NEU_NA_TYPE_DRIVER) {
        neu_adapter_driver_destroy((neu_adapter_driver_t *) adapter);
    }

    nlog_notice("Stop the adapter(%s)", adapter->name);
    return 0;
}

/**
 * @brief 启动指定的适配器。
 *
 * 该函数用于启动一个适配器。在启动之前，会检查适配器的当前状态，
 * 只有当适配器处于就绪（NEU_NODE_RUNNING_STATE_READY）或
 * 已停止（NEU_NODE_RUNNING_STATE_STOPPED）状态时，
 * 才会尝试启动。如果适配器处于初始化（NEU_NODE_RUNNING_STATE_INIT）
 * 或正在运行（NEU_NODE_RUNNING_STATE_RUNNING）状态，则会返回相应的错误码。
 *
 * 当适配器成功启动后，其状态会更新为正在运行（NEU_NODE_RUNNING_STATE_RUNNING），
 * 并将该状态存储到持久化存储中。如果适配器是驱动类型（NEU_NA_TYPE_DRIVER），
 * 还会启动驱动组定时器。
 *
 * @param adapter 指向要启动的适配器的指针。
 *
 * @return neu_err_code_e 启动操作的结果，可能的返回值如下：
 *         - NEU_ERR_SUCCESS: 适配器成功启动。
 *         - NEU_ERR_NODE_NOT_READY: 适配器处于初始化状态，无法启动。
 *         - NEU_ERR_NODE_IS_RUNNING: 适配器已经在运行，无需再次启动。
 */
int neu_adapter_start(neu_adapter_t *adapter)
{
    const neu_plugin_intf_funs_t *intf_funs = adapter->module->intf_funs;
    neu_err_code_e                error     = NEU_ERR_SUCCESS;

    switch (adapter->state) {
    case NEU_NODE_RUNNING_STATE_INIT:
        error = NEU_ERR_NODE_NOT_READY;
        break;
    case NEU_NODE_RUNNING_STATE_RUNNING:
        error = NEU_ERR_NODE_IS_RUNNING;
        break;
    case NEU_NODE_RUNNING_STATE_READY:
    case NEU_NODE_RUNNING_STATE_STOPPED:
        break;
    }

    if (error != NEU_ERR_SUCCESS) {
        return error;
    }

    error = intf_funs->start(adapter->plugin);
    if (error == NEU_ERR_SUCCESS) {
        adapter->state = NEU_NODE_RUNNING_STATE_RUNNING;
        adapter_storage_state(adapter->name, adapter->state);
        if (NEU_NA_TYPE_DRIVER == neu_adapter_get_type(adapter)) {
            // 启动组定时器
            neu_adapter_driver_start_group_timer(
                (neu_adapter_driver_t *) adapter);
        }
    }

    return error;
}

int neu_adapter_start_single(neu_adapter_t *adapter)
{
    const neu_plugin_intf_funs_t *intf_funs = adapter->module->intf_funs;

    adapter->state = NEU_NODE_RUNNING_STATE_RUNNING;
    return intf_funs->start(adapter->plugin);
}

int neu_adapter_stop(neu_adapter_t *adapter)
{
    const neu_plugin_intf_funs_t *intf_funs = adapter->module->intf_funs;
    neu_err_code_e                error     = NEU_ERR_SUCCESS;

    switch (adapter->state) {
    case NEU_NODE_RUNNING_STATE_INIT:
    case NEU_NODE_RUNNING_STATE_READY:
        error = NEU_ERR_NODE_NOT_RUNNING;
        break;
    case NEU_NODE_RUNNING_STATE_STOPPED:
        error = NEU_ERR_NODE_IS_STOPED;
        break;
    case NEU_NODE_RUNNING_STATE_RUNNING:
        break;
    }

    if (error != NEU_ERR_SUCCESS) {
        return error;
    }

    error = intf_funs->stop(adapter->plugin);
    if (error == NEU_ERR_SUCCESS) {
        adapter->state = NEU_NODE_RUNNING_STATE_STOPPED;
        adapter_storage_state(adapter->name, adapter->state);
        if (NEU_NA_TYPE_DRIVER == neu_adapter_get_type(adapter)) {
            neu_adapter_driver_stop_group_timer(
                (neu_adapter_driver_t *) adapter);
        }
        neu_adapter_reset_metrics(adapter);
    }

    return error;
}

/**
 * @brief 为适配器设置配置信息，并根据设置结果进行相应处理。
 *
 * 此函数尝试调用适配器模块的设置接口函数来设置配置信息。如果设置成功，
 * 它会更新适配器的配置信息，并且在适配器处于初始化状态时，将其状态更
 * 新为就绪状态并启动适配器。如果设置失败，会返回相应的错误码。
 *
 * @param adapter 指向要设置配置信息的适配器的指针。
 * @param setting 指向包含配置信息的字符串的指针。
 *
 * @return int 返回设置操作的结果状态码。
 *         - 0: 表示设置成功。
 *         - NEU_ERR_NODE_SETTING_INVALID: 表示设置失败，配置信息无效。
 *         - 其他负数值: 表示调用适配器模块的设置接口函数时返回的错误码。
 */
int neu_adapter_set_setting(neu_adapter_t *adapter, const char *setting)
{
    int rv = -1;

    // 定义一个指向插件接口函数结构体的常量指针
    const neu_plugin_intf_funs_t *intf_funs;

    // 从适配器的模块中获取接口函数结构体指针
    intf_funs = adapter->module->intf_funs;

    // 调用接口函数结构体中的 setting 函数，尝试为适配器的插件设置配置信息
    rv        = intf_funs->setting(adapter->plugin, setting);

    if (rv == 0) {
        if (adapter->setting != NULL) {
            free(adapter->setting);
        }
        adapter->setting = strdup(setting);

        if (adapter->state == NEU_NODE_RUNNING_STATE_INIT) {
            // 如果是初始化状态，将状态更新为就绪状态
            adapter->state = NEU_NODE_RUNNING_STATE_READY;
            
            // 启动适配器
            neu_adapter_start(adapter);
        }
    } else {
        // 如果设置操作失败，将返回值设置为配置信息无效的错误码
        rv = NEU_ERR_NODE_SETTING_INVALID;
    }

    return rv;
}

int neu_adapter_get_setting(neu_adapter_t *adapter, char **config)
{
    if (adapter->setting != NULL) {
        *config = strdup(adapter->setting);
        return NEU_ERR_SUCCESS;
    }

    return NEU_ERR_NODE_SETTING_NOT_FOUND;
}

neu_node_state_t neu_adapter_get_state(neu_adapter_t *adapter)
{
    neu_node_state_t     state  = { 0 };
    neu_plugin_common_t *common = neu_plugin_to_plugin_common(adapter->plugin);

    state.link      = common->link_state;
    state.running   = adapter->state;
    state.log_level = adapter->log_level;

    return state;
}

neu_event_timer_t *neu_adapter_add_timer(neu_adapter_t *         adapter,
                                         neu_event_timer_param_t param)
{
    return neu_event_add_timer(adapter->events, param);
}

void neu_adapter_del_timer(neu_adapter_t *adapter, neu_event_timer_t *timer)
{
    neu_event_del_timer(adapter->events, timer);
}

int neu_adapter_register_group_metric(neu_adapter_t *adapter,
                                      const char *group_name, const char *name,
                                      const char *help, neu_metric_type_e type,
                                      uint64_t init)
{
    if (NULL == adapter->metrics) {
        return -1;
    }

    return neu_node_metrics_add(adapter->metrics, group_name, name, help, type,
                                init);
}

int neu_adapter_update_group_metric(neu_adapter_t *adapter,
                                    const char *   group_name,
                                    const char *metric_name, uint64_t n)
{
    if (NULL == adapter->metrics) {
        return -1;
    }

    return neu_node_metrics_update(adapter->metrics, group_name, metric_name,
                                   n);
}

int neu_adapter_metric_update_group_name(neu_adapter_t *adapter,
                                         const char *   group_name,
                                         const char *   new_group_name)
{
    if (NULL == adapter->metrics) {
        return -1;
    }

    return neu_node_metrics_update_group(adapter->metrics, group_name,
                                         new_group_name);
}

void neu_adapter_del_group_metrics(neu_adapter_t *adapter,
                                   const char *   group_name)
{
    if (NULL != adapter->metrics) {
        neu_node_metrics_del_group(adapter->metrics, group_name);
    }
}

inline static void reply(neu_adapter_t *adapter, neu_reqresp_head_t *header,
                         void *data)
{
    neu_msg_gen(header, data);
    int ret = neu_send_msg(adapter->control_fd, (neu_msg_t *) header);
    if (0 != ret) {
        nlog_warn("%s reply %s to %s, error: %s(%d)", header->sender,
                  neu_reqresp_type_string(header->type), header->receiver,
                  strerror(errno), errno);
    }
}
