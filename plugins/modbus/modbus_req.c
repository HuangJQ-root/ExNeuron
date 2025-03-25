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
#include <time.h>

#include "modbus_point.h"
#include "modbus_stack.h"

#include "modbus_req.h"

#define MAX_SLAVES 256

uint8_t failed_cycles[MAX_SLAVES];
bool    skip[MAX_SLAVES];

/**
 * @brief 用于存储 Modbus 组相关数据的结构体。
 * 
 * 标签信息、组名称、命令排序信息以及地址基
 */
struct modbus_group_data {
    /**
     * @brief 存储 Modbus 标签信息的数组。
     * 
     * 其中存储的元素通常是指向 `modbus_point_t` 结构体的指针。
     * `modbus_point_t` 结构体包含了 Modbus 点位的详细信息，
     * 如从站 ID、数据区域、起始地址、寄存器数量等。通过这个数组，
     * 可以方便地管理和访问该组内的所有 Modbus 点位。
     */
    UT_array *              tags;

    /**
     * @brief Modbus 组的名称。
     * 
     * 这是一个指向以 null 结尾的字符串的指针，用于标识该 Modbus 组。
     * 组名称可以帮助用户在系统中区分不同的 Modbus 组，便于配置和管理。
     * 该字符串通常在初始化时分配内存并赋值，使用完毕后需要手动释放内存。
     */
    char *                  group;

    /**
     * @brief Modbus 读取命令的排序信息。
     * 
     * 包含了一系列经过排序的Modbus 读取命令，
     * 排序的目的是优化 Modbus 通信过程，减少
     * 通信延迟和提高效率。通过该指针，可以获取
     * 到该组内所有Modbus 读取命令的排序信息。
     * 
     * @note 排序函数：modbus_tag_sort
     */
    modbus_read_cmd_sort_t *cmd_sort;

    /**
     * @brief Modbus 地址基。
     * 
     */
    modbus_address_base     address_base;
};

struct modbus_write_tags_data {
    UT_array *               tags;
    modbus_write_cmd_sort_t *cmd_sort;
};

static void plugin_group_free(neu_plugin_group_t *pgp);
static int  process_protocol_buf(neu_plugin_t *plugin, uint8_t slave_id,
                                 uint16_t response_size);
static int  process_protocol_buf_test(neu_plugin_t *plugin, void *req,
                                      modbus_point_t *point,
                                      uint16_t        response_size);

void modbus_conn_connected(void *data, int fd)
{
    struct neu_plugin *plugin = (struct neu_plugin *) data;
    (void) fd;

    plugin->common.link_state = NEU_NODE_LINK_STATE_CONNECTED;
}

void modbus_conn_disconnected(void *data, int fd)
{
    struct neu_plugin *plugin = (struct neu_plugin *) data;
    (void) fd;

    plugin->common.link_state = NEU_NODE_LINK_STATE_DISCONNECTED;
}

void modbus_tcp_server_listen(void *data, int fd)
{
    struct neu_plugin *  plugin = (struct neu_plugin *) data;
    neu_event_io_param_t param  = {
        .cb       = modbus_tcp_server_io_callback,
        .fd       = fd,
        .usr_data = (void *) plugin,
    };

    plugin->tcp_server_io = neu_event_add_io(plugin->events, param);
}

void modbus_tcp_server_stop(void *data, int fd)
{
    struct neu_plugin *plugin = (struct neu_plugin *) data;
    (void) fd;

    neu_event_del_io(plugin->events, plugin->tcp_server_io);
}

int modbus_tcp_server_io_callback(enum neu_event_io_type type, int fd,
                                  void *usr_data)
{
    neu_plugin_t *plugin = (neu_plugin_t *) usr_data;

    switch (type) {
    case NEU_EVENT_IO_READ: {
        int client_fd = neu_conn_tcp_server_accept(plugin->conn);
        if (client_fd > 0) {
            plugin->client_fd = client_fd;
        }

        break;
    }
    case NEU_EVENT_IO_CLOSED:
    case NEU_EVENT_IO_HUP:
        plog_warn(plugin, "tcp server recv: %d, conn closed, fd: %d", type, fd);
        neu_event_del_io(plugin->events, plugin->tcp_server_io);
        neu_conn_disconnect(plugin->conn);
        break;
    }

    return 0;
}

/**
 * @brief 发送 Modbus 消息到目标设备。
 *
 * 此函数负责将 Modbus 消息发送到目标设备，支持 Modbus 服务器和客户端模式。
 * 在发送消息之前，会清空接收缓冲区，并记录发送的协议数据。
 * 如果是客户端模式且连接断开，会尝试切换到备用连接。
 *
 * @param ctx 上下文指针，通常指向 neu_plugin_t 结构体，包含插件的相关信息。
 * @param n_byte 要发送的字节数，即消息的长度。
 * @param bytes 指向要发送的消息数据的指针，是一个 uint8_t 类型的数组。
 *
 * @return 若发送成功，返回发送的字节数；若发送失败，返回-1
 */
int modbus_send_msg(void *ctx, uint16_t n_byte, uint8_t *bytes)
{   
    // 将上下文指针转换为 neu_plugin_t 结构体指针，方便后续访问插件相关信息
    neu_plugin_t *plugin = (neu_plugin_t *) ctx;

    // 用于存储发送操作的返回结果，初始化为 0
    int           ret    = 0;

    // 清空连接的接收缓冲区，确保接收缓冲区中没有残留数据，避免影响后续数据接收
    neu_conn_clear_recv_buffer(plugin->conn);

    plog_send_protocol(plugin, bytes, n_byte);

    // 判断当前是否处于 Modbus 服务器模式
    if (plugin->is_server) {
        // 向客户端发送消息
        ret = neu_conn_tcp_server_send(plugin->conn, plugin->client_fd, bytes,
                                       n_byte);
    } else {
        // 检查是否配置了备用连接，并且当前主连接处于断开状态
        if (plugin->backup && neu_conn_is_connected(plugin->conn) == false) {
            // 若当前使用的不是备用连接，且已经完成了第一次连接尝试
            if (plugin->current_backup == false && plugin->first_attempt_done) {
                // 记录日志，提示切换到备用 IP 和端口
                plog_notice(plugin, "switch to backup ip:port %s:%hu",
                            plugin->param_backup.params.tcp_client.ip,
                            plugin->param_backup.params.tcp_client.port);
                
                // 标记当前使用备用连接
                plugin->current_backup = true;

                // 重新配置连接，使用备用连接参数
                plugin->conn =
                    neu_conn_reconfig(plugin->conn, &plugin->param_backup);
            } 
            // 其他情况，通常是使用备用连接失败后，切回原始连接
            else {
                plog_notice(plugin, "switch to original ip:port %s:%hu",
                            plugin->param.params.tcp_client.ip,
                            plugin->param.params.tcp_client.port);
                
                // 标记当前使用原始连接
                plugin->current_backup = false;

                // 重新配置连接，使用原始连接参数
                plugin->conn = neu_conn_reconfig(plugin->conn, &plugin->param);

                // 标记第一次连接尝试已完成
                plugin->first_attempt_done = true;
            }
        }

        // 发送消息
        ret = neu_conn_send(plugin->conn, bytes, n_byte);
    }

    return ret;
}

int modbus_stack_read_retry(neu_plugin_t *plugin, struct modbus_group_data *gd,
                            uint16_t i, uint16_t j, uint16_t *response_size,
                            uint64_t *read_tms)
{
    struct timespec t3 = { .tv_sec = plugin->retry_interval / 1000,
                           .tv_nsec =
                               1000 * 1000 * (plugin->retry_interval % 1000) };
    struct timespec t4 = { 0 };
    nanosleep(&t3, &t4);
    plog_notice(plugin, "Resend read req. Times:%hu", j + 1);
    *read_tms = neu_time_ms();
    return modbus_stack_read(
        plugin->stack, gd->cmd_sort->cmd[i].slave_id, gd->cmd_sort->cmd[i].area,
        gd->cmd_sort->cmd[i].start_address, gd->cmd_sort->cmd[i].n_register,
        response_size, false);
}

void handle_modbus_error(neu_plugin_t *plugin, struct modbus_group_data *gd,
                         uint16_t cmd_index, int error_code,
                         const char *error_message)
{
    modbus_value_handle(plugin, gd->cmd_sort->cmd[cmd_index].slave_id, 0, NULL,
                        error_code, NULL);
    if (error_message) {
        plog_error(plugin, "%s, skip, %hhu!%hu", error_message,
                   gd->cmd_sort->cmd[cmd_index].slave_id,
                   gd->cmd_sort->cmd[cmd_index].start_address);
    }
}

void finalize_modbus_read_result(neu_plugin_t *            plugin,
                                 struct modbus_group_data *gd,
                                 uint16_t cmd_index, int ret_r, int ret_buf,
                                 uint64_t read_tms, int64_t *rtt,
                                 bool *slave_err)
{
    if (ret_r <= 0) {
        handle_modbus_error(plugin, gd, cmd_index, NEU_ERR_PLUGIN_DISCONNECTED,
                            "send message failed");
        *rtt = NEU_METRIC_LAST_RTT_MS_MAX;
        neu_conn_disconnect(plugin->conn);
    } else if (ret_buf <= 0) {
        switch (ret_buf) {
        case 0:
            handle_modbus_error(plugin, gd, cmd_index,
                                NEU_ERR_PLUGIN_DEVICE_NOT_RESPONSE,
                                "no modbus response received");
            *rtt = neu_time_ms() - read_tms;
            slave_err[gd->cmd_sort->cmd[cmd_index].slave_id] = true;
            break;
        case -1:
            handle_modbus_error(plugin, gd, cmd_index,
                                NEU_ERR_PLUGIN_PROTOCOL_DECODE_FAILURE,
                                "modbus message error");
            *rtt = NEU_METRIC_LAST_RTT_MS_MAX;
            neu_conn_disconnect(plugin->conn);
            break;
        case -2:
            handle_modbus_error(plugin, gd, cmd_index,
                                NEU_ERR_PLUGIN_READ_FAILURE,
                                "modbus device response error");
            *rtt = neu_time_ms() - read_tms;
            break;
        default:
            break;
        }
    } else {
        *rtt = neu_time_ms() - read_tms;
        failed_cycles[gd->cmd_sort->cmd[cmd_index].slave_id] = 0;
    }
}

/**
 * @brief 检查 Modbus 读取操作的结果，并处理可能的重试和数据处理逻辑。
 *
 * 该函数负责执行 Modbus 读取操作，检查读取结果的有效性。如果读取失败，
 * 会根据配置进行重试。同时，会调用相应的函数处理接收到的协议缓冲区数据，
 * 并最终完成读取结果的处理。
 *
 * @param plugin    代表 Modbus 插件实例，包含插件的配置信息、状态和回调函数等。
 * @param gd        结构体包含 Modbus 组的相关数据，如标签、命令排序等信息。
 * @param cmd_index 表示当前要执行的 Modbus 读取命令在命令排序数组中的索引。
 * @param rtt       用于记录 Modbus 读取操作的往返时间（Round-Trip Time）。
 * @param slave_err 用于标记每个从站是否出现读取错误
 */
void check_modbus_read_result(neu_plugin_t *            plugin,
                              struct modbus_group_data *gd, uint16_t cmd_index,
                              int64_t *rtt, bool *slave_err)
{
    // 用于存储 Modbus 响应的字节数
    uint16_t response_size = 0;

    // 记录开始读取的时间戳，单位为毫秒
    uint64_t read_tms      = neu_time_ms();

    // 执行 Modbus 读取操作
    int ret_r = modbus_stack_read(
        plugin->stack, gd->cmd_sort->cmd[cmd_index].slave_id,
        gd->cmd_sort->cmd[cmd_index].area,
        gd->cmd_sort->cmd[cmd_index].start_address,
        gd->cmd_sort->cmd[cmd_index].n_register, &response_size, false);

    // 用于存储处理协议缓冲区数据的返回结果
    int ret_buf = 0;

    // 如果读取操作成功（返回值大于 0）
    if (ret_r > 0) {
        // 处理接收到的协议缓冲区数据
        ret_buf = process_protocol_buf(
            plugin, gd->cmd_sort->cmd[cmd_index].slave_id, response_size);
    }

    // 如果读取操作失败（返回值小于等于 0）或者协议缓冲区处理失败（返回值为 0）
    if (ret_r <= 0 || ret_buf == 0) {
        // 进行重试，最多重试 plugin->max_retries 次
        for (uint16_t j = 0; j < plugin->max_retries; ++j) {
            // 重试读取操作
            ret_r = modbus_stack_read_retry(plugin, gd, cmd_index, j,
                                            &response_size, &read_tms);
            
            // 如果重试读取操作成功
            if (ret_r > 0) {
                // 再次处理接收到的协议缓冲区数据
                ret_buf = process_protocol_buf(
                    plugin, gd->cmd_sort->cmd[cmd_index].slave_id,
                    response_size);

                // 如果协议缓冲区处理仍然失败，继续重试
                if (ret_buf == 0) {
                    continue;
                }
            }
            break;
        }
    }

    // 完成 Modbus 读取结果的最终处理
    finalize_modbus_read_result(plugin, gd, cmd_index, ret_r, ret_buf, read_tms,
                                rtt, slave_err);
}

void update_metrics_after_read(neu_plugin_t *plugin, int64_t rtt,
                               neu_plugin_group_t *group,
                               neu_conn_state_t *  state)
{
    *state = neu_conn_state(plugin->conn);
    neu_adapter_update_metric_cb_t update_metric =
        plugin->common.adapter_callbacks->update_metric;
    struct modbus_group_data *gd =
        (struct modbus_group_data *) group->user_data;

    update_metric(plugin->common.adapter, NEU_METRIC_SEND_BYTES,
                  state->send_bytes, NULL);
    update_metric(plugin->common.adapter, NEU_METRIC_RECV_BYTES,
                  state->recv_bytes, NULL);
    update_metric(plugin->common.adapter, NEU_METRIC_LAST_RTT_MS, rtt, NULL);
    update_metric(plugin->common.adapter, NEU_METRIC_GROUP_LAST_SEND_MSGS,
                  gd->cmd_sort->n_cmd, group->group_name);
}

typedef struct {
    uint8_t  slave_id;
    uint16_t degrade_time;
} degrade_timer_data_t;

void *degrade_timer(void *arg)
{
    degrade_timer_data_t *data = (degrade_timer_data_t *) arg;

    struct timespec t1 = { .tv_sec = data->degrade_time, .tv_nsec = 0 };
    struct timespec t2 = { 0 };
    nanosleep(&t1, &t2);

    skip[data->slave_id] = false;

    free(data);
    return NULL;
}

void set_skip_timer(uint8_t slave_id, uint32_t degrade_time)
{
    degrade_timer_data_t *data =
        (degrade_timer_data_t *) malloc(sizeof(degrade_timer_data_t));
    data->slave_id     = slave_id;
    data->degrade_time = degrade_time;

    failed_cycles[slave_id] = 0;

    pthread_t timer_thread;
    pthread_create(&timer_thread, NULL, degrade_timer, data);

    pthread_detach(timer_thread);
}

/**
 * @brief 定时执行 Modbus 组数据读取操作的函数。
 *
 * 该函数会根据传入的插件和组信息，对 Modbus 设备进行周期性
 * 的数据读取操作。它会处理组数据的初始化、命令排序、错误处理
 * 以及性能指标更新等任务，确保 Modbus 数据采集的稳定和高效。
 *
 * @param plugin   代表 Modbus 插件实例，包含插件的配置信息、状态和回调函数等。
 * @param group    代表一个 Modbus 数据组，包含该组的标签、组名等信息。
 * @param max_byte 一个无符号 16 位整数，表示每次读取操作允许的最大字节数，
 *                 用于命令排序和优化读取效率。
 *
 * @return 函数执行成功返回 0。
 */
int modbus_group_timer(neu_plugin_t *plugin, neu_plugin_group_t *group,
                       uint16_t max_byte)
{
    // 初始化连接状态结构体，用于记录 Modbus 通信过程中的发送和接收字节数
    neu_conn_state_t          state = { 0 };

    // 存储和管理当前组的 Modbus 数据
    struct modbus_group_data *gd    = NULL;

    // 记录 Modbus 通信的往返时间（RTT），初始化为最大允许值
    int64_t                   rtt   = NEU_METRIC_LAST_RTT_MS_MAX;
    
    // 临时指针，用于检查组的用户数据是否已经初始化或地址基是否匹配
    struct modbus_group_data *gdt =
        (struct modbus_group_data *) group->user_data;

    // 检查组的用户数据是否为空，或者地址基是否与插件的地址基不匹配
    if (group->user_data == NULL || gdt->address_base != plugin->address_base) {
        // 如果组的用户数据已经存在，释放之前分配的资源
        if (group->user_data != NULL) {
            plugin_group_free(group);
        }
        gd = calloc(1, sizeof(struct modbus_group_data));

        // 将新分配的 modbus_group_data 结构体指针赋值给组的用户数据
        group->user_data  = gd;
        // 设置组的释放函数，用于后续资源清理
        group->group_free = plugin_group_free;
        // 初始化存储 Modbus 点位指针的动态数组
        utarray_new(gd->tags, &ut_ptr_icd);

        utarray_foreach(group->tags, neu_datatag_t *, tag)
        {
            // 为每个标签分配一个 modbus_point_t 结构体的内存，并初始化为 0
            modbus_point_t *p = calloc(1, sizeof(modbus_point_t));
            
            // 将标签信息转换为 Modbus 点位信息
            int ret = modbus_tag_to_point(tag, p, plugin->address_base);
            if (ret != NEU_ERR_SUCCESS) {
                plog_error(plugin, "invalid tag: %s, address: %s", tag->name,
                           tag->address);
            }
            
            // 将转换后的 Modbus 点位指针添加到动态数组中
            utarray_push_back(gd->tags, &p);
        }

        // 复制组的名称到 modbus_group_data 结构体中
        gd->group        = strdup(group->group_name);
        // 对 Modbus 点位进行排序，生成优化后的读取命令序列
        gd->cmd_sort     = modbus_tag_sort(gd->tags, max_byte);
        // 设置 modbus_group_data 结构体的地址基为插件的地址基
        gd->address_base = plugin->address_base;
    }

    // 获取组的用户数据指针
    gd                        = (struct modbus_group_data *) group->user_data;
    
    // 初始化
    plugin->plugin_group_data = gd;

    // 初始化从站错误记录数组，用于标记每个从站是否出现过错误
    bool slave_err_record[MAX_SLAVES] = { false };

    for (uint16_t i = 0; i < gd->cmd_sort->n_cmd; i++) {
        // 初始化当前命令的从站错误数组，用于标记每个从站在本次命令执行中的错误状态
        bool    slave_err[MAX_SLAVES] = { false };

        // 获取当前命令的从站 ID
        uint8_t slave_id              = gd->cmd_sort->cmd[i].slave_id;

        // 设置插件的当前命令索引
        plugin->cmd_idx               = i;

        // 如果该从站已经记录过错误，跳过本次命令执行
        if (slave_err_record[slave_id] == true) {
            continue;
        }

        // 如果插件未启用降级模式，或者该从站未被标记为跳过
        if (plugin->degradation == false || skip[slave_id] == false) {
            // 执行 Modbus 读取操作，并检查读取结果
            check_modbus_read_result(plugin, gd, i, &rtt, slave_err);
        } else {
            continue;
        }

        // 如果插件启用了降级模式
        if (plugin->degradation) {
            // 如果该从站在本次命令执行中出现错误
            if (slave_err[slave_id]) {
                // 增加该从站的失败次数计数器
                failed_cycles[slave_id]++;

                // 标记该从站出现过错误
                slave_err_record[slave_id] = true;
            }

            // 如果该从站的失败次数达到了降级周期阈值
            if (failed_cycles[slave_id] >= plugin->degrade_cycle) {
                // 标记该从站为跳过状态
                skip[slave_id] = true;

                // 记录警告日志，提示跳过该从站
                plog_warn(plugin, "Skip slave %hhu", slave_id);

                // 设置跳过该从站的定时器，在一定时间后恢复对该从站的读取
                set_skip_timer(slave_id, plugin->degrade_time);
            }
        }

        // 如果插件设置了读取间隔时间
        if (plugin->interval > 0) {
            struct timespec t1 = { .tv_sec  = plugin->interval / 1000,
                                   .tv_nsec = 1000 * 1000 *
                                       (plugin->interval % 1000) };
            struct timespec t2 = { 0 };
            nanosleep(&t1, &t2);
        }
    }

    // 读取操作完成后，更新性能指标，如 RTT、发送和接收字节数等
    update_metrics_after_read(plugin, rtt, group, &state);
    return 0;
}

/**
 * @brief 处理 Modbus 读取到的值，并更新到适配器中。
 *
 * 该函数用于处理从 Modbus 设备读取到的值，根据不同的错误情况和数据类型进行相应的处理，
 * 并将处理后的值更新到适配器中。如果有跟踪信息，还会调用带有跟踪信息的更新函数。
 *
 * @param ctx 上下文指针，通常指向 `neu_plugin_t` 结构体，包含插件的相关信息。
 * @param slave_id Modbus 从站的 ID。
 * @param n_byte 读取到的字节数。
 * @param bytes 指向读取到的数据的指针。
 * @param error 错误码，用于判断读取是否成功或出现何种错误。
 * @param trace 跟踪信息指针，用于记录操作的跟踪信息，可为 NULL。
 *
 * @return 总是返回 0，表示函数执行完成。
 */
int modbus_value_handle(void *ctx, uint8_t slave_id, uint16_t n_byte,
                        uint8_t *bytes, int error, void *trace)
{
    // 将上下文指针转换为 neu_plugin_t 结构体指针
    neu_plugin_t *            plugin = (neu_plugin_t *) ctx;
    
    // 获取插件组数据指针
    struct modbus_group_data *gd =
        (struct modbus_group_data *) plugin->plugin_group_data;

    // 获取当前命令的起始地址
    uint16_t start_address = gd->cmd_sort->cmd[plugin->cmd_idx].start_address;

    // 获取当前命令要读取的寄存器数量
    uint16_t n_register    = gd->cmd_sort->cmd[plugin->cmd_idx].n_register;

    // 处理连接断开错误
    if (error == NEU_ERR_PLUGIN_DISCONNECTED) {
        // 初始化一个 neu_dvalue_t 结构体，用于存储值
        neu_dvalue_t dvalue = { 0 };

        // 设置值的类型为错误类型
        dvalue.type      = NEU_TYPE_ERROR;

        // 设置错误码
        dvalue.value.i32 = error;

        // 调用适配器的更新函数，更新组数据
        plugin->common.adapter_callbacks->driver.update(
            plugin->common.adapter, gd->group, NULL, dvalue);
        return 0;
    } 
    // 处理其他非成功错误
    else if (error != NEU_ERR_SUCCESS) {
        // 遍历当前命令中的所有标签
        utarray_foreach(gd->cmd_sort->cmd[plugin->cmd_idx].tags,
                        modbus_point_t **, p_tag)
        {
            // 初始化一个 neu_dvalue_t 结构体，用于存储值
            neu_dvalue_t dvalue = { 0 };

            // 设置值的类型为错误类型
            dvalue.type         = NEU_TYPE_ERROR;

            // 设置错误码
            dvalue.value.i32    = error;

            // 调用适配器的更新函数，更新标签数据
            plugin->common.adapter_callbacks->driver.update(
                plugin->common.adapter, gd->group, (*p_tag)->name, dvalue);
        }
        return 0;
    }

    // 遍历当前命令中的所有标签
    utarray_foreach(gd->cmd_sort->cmd[plugin->cmd_idx].tags, modbus_point_t **,
                    p_tag)
    {
        // 初始化一个 neu_dvalue_t 结构体，用于存储值
        neu_dvalue_t dvalue = { 0 };

        // 检查标签的起始地址和寄存器数量是否超出范围，或者从站 ID 是否不匹配
        if ((*p_tag)->start_address + (*p_tag)->n_register >
                start_address + n_register ||
            slave_id != (*p_tag)->slave_id) {
            // 设置值的类型为错误类型
            dvalue.type      = NEU_TYPE_ERROR;

            // 设置错误码为读取失败
            dvalue.value.i32 = NEU_ERR_PLUGIN_READ_FAILURE;
        } else {
            // 根据标签所在的 Modbus 区域进行不同处理
            switch ((*p_tag)->area) {
            case MODBUS_AREA_HOLD_REGISTER:
            case MODBUS_AREA_INPUT_REGISTER:
                // 检查读取的字节数是否足够
                if (n_byte >= ((*p_tag)->start_address - start_address) * 2 +
                        (*p_tag)->n_register * 2) {
                    
                    // 从读取的数据中复制相应的字节到 dvalue 中
                    memcpy(dvalue.value.bytes.bytes,
                           bytes +
                               ((*p_tag)->start_address - start_address) * 2,
                           (*p_tag)->n_register * 2);

                    // 设置字节长度
                    dvalue.value.bytes.length = (*p_tag)->n_register * 2;
                }
                break;
            case MODBUS_AREA_COIL:
            case MODBUS_AREA_INPUT: {
                uint16_t offset = (*p_tag)->start_address - start_address;
                if (n_byte > offset / 8) {
                    neu_value8_u u8 = { .value = bytes[offset / 8] };

                    dvalue.value.u8 = neu_value8_get_bit(u8, offset % 8);
                }
                break;
            }
            }

            // 设置值的类型为标签的类型
            dvalue.type = (*p_tag)->type;

            // 根据值的类型进行不同的处理
            switch ((*p_tag)->type) {
            case NEU_TYPE_UINT16:
            case NEU_TYPE_INT16:
                dvalue.value.u16 = ntohs(dvalue.value.u16);
                break;
            case NEU_TYPE_FLOAT:
            case NEU_TYPE_INT32:
            case NEU_TYPE_UINT32:
                if ((*p_tag)->option.value32.is_default) {
                    modbus_convert_endianess(&dvalue.value, plugin->endianess);
                }
                dvalue.value.u32 = ntohl(dvalue.value.u32);
                break;
            case NEU_TYPE_DOUBLE:
            case NEU_TYPE_INT64:
            case NEU_TYPE_UINT64:
                dvalue.value.u64 = neu_ntohll(dvalue.value.u64);
                break;
            case NEU_TYPE_BIT: {
                switch ((*p_tag)->area) {
                case MODBUS_AREA_HOLD_REGISTER:
                case MODBUS_AREA_INPUT_REGISTER: {
                    neu_value16_u v16 = { 0 };
                    v16.value = htons(*(uint16_t *) dvalue.value.bytes.bytes);
                    memset(&dvalue.value, 0, sizeof(dvalue.value));
                    dvalue.value.u8 =
                        neu_value16_get_bit(v16, (*p_tag)->option.bit.bit);
                    break;
                }
                case MODBUS_AREA_COIL:
                case MODBUS_AREA_INPUT:
                    break;
                }
                break;
            }
            case NEU_TYPE_STRING: {
                switch ((*p_tag)->option.string.type) {
                case NEU_DATATAG_STRING_TYPE_H:
                    break;
                case NEU_DATATAG_STRING_TYPE_L:
                    neu_datatag_string_ltoh(dvalue.value.str,
                                            strlen(dvalue.value.str));
                    break;
                case NEU_DATATAG_STRING_TYPE_D:
                    break;
                case NEU_DATATAG_STRING_TYPE_E:
                    break;
                }

                if (!neu_datatag_string_is_utf8(dvalue.value.str,
                                                strlen(dvalue.value.str))) {
                    dvalue.value.str[0] = '?';
                    dvalue.value.str[1] = 0;
                }
                break;
            }
            default:
                break;
            }
        }

        // 如果有跟踪信息
        if (trace) {
            // 调用带有跟踪信息的适配器更新函数，更新标签数据
            plugin->common.adapter_callbacks->driver.update_with_trace(
                plugin->common.adapter, gd->group, (*p_tag)->name, dvalue, NULL,
                0, trace);
        } else {
            // 调用适配器的更新函数，更新标签数据
            plugin->common.adapter_callbacks->driver.update(
                plugin->common.adapter, gd->group, (*p_tag)->name, dvalue);
        }
    }
    return 0;
}

int modbus_value_handle_test(neu_plugin_t *plugin, void *req,
                             modbus_point_t *point, uint16_t n_byte,
                             uint8_t *bytes)
{
    (void) req;
    neu_json_value_u jvalue = { 0 };
    neu_json_type_e  jtype;
    uint8_t          recv_bytes[256] = { 0 };

    switch (point->area) {
    case MODBUS_AREA_HOLD_REGISTER:
    case MODBUS_AREA_INPUT_REGISTER:
        if (n_byte >= point->n_register * 2) {
            memcpy(recv_bytes, bytes, point->n_register * 2);
        }
        break;
    case MODBUS_AREA_COIL:
    case MODBUS_AREA_INPUT: {
        neu_value8_u u8 = { .value = bytes[0] };
        jvalue.val_bit  = neu_value8_get_bit(u8, 0);
    } break;
    }

    switch (point->type) {
    case NEU_TYPE_UINT16: {
        jtype = NEU_JSON_INT;
        uint16_t tmp_val16;
        memcpy(&tmp_val16, recv_bytes, sizeof(uint16_t));
        jvalue.val_int = ntohs(tmp_val16);
        break;
    }
    case NEU_TYPE_INT16: {
        jtype = NEU_JSON_INT;
        int16_t tmp_val16;
        memcpy(&tmp_val16, recv_bytes, sizeof(int16_t));
        jvalue.val_int = (int16_t) ntohs(tmp_val16);
        break;
    }
    case NEU_TYPE_FLOAT: {
        uint32_t tmp_valf;
        memcpy(&tmp_valf, recv_bytes, sizeof(uint32_t));
        jtype          = NEU_JSON_FLOAT;
        jvalue.val_int = ntohl(tmp_valf);
        break;
    }
    case NEU_TYPE_INT32: {
        jtype = NEU_JSON_INT;
        int32_t tmp_val32;
        memcpy(&tmp_val32, recv_bytes, sizeof(int32_t));
        jvalue.val_int = (int32_t) ntohl(tmp_val32);
        break;
    }
    case NEU_TYPE_UINT32: {
        jtype = NEU_JSON_INT;
        uint32_t tmp_val32;
        memcpy(&tmp_val32, recv_bytes, sizeof(uint32_t));
        jvalue.val_int = ntohl(tmp_val32);
        break;
    }
    case NEU_TYPE_DOUBLE: {
        uint64_t tmp_vald;
        memcpy(&tmp_vald, recv_bytes, sizeof(uint64_t));
        jtype          = NEU_JSON_DOUBLE;
        jvalue.val_int = neu_ntohll(tmp_vald);
        break;
    }
    case NEU_TYPE_INT64: {
        jtype = NEU_JSON_INT;
        int64_t tmp_val64;
        memcpy(&tmp_val64, recv_bytes, sizeof(int64_t));
        jvalue.val_int = (int64_t) neu_ntohll(tmp_val64);
        break;
    }
    case NEU_TYPE_UINT64: {
        jtype = NEU_JSON_INT;
        uint64_t tmp_val64;
        memcpy(&tmp_val64, recv_bytes, sizeof(uint64_t));
        jvalue.val_int = neu_ntohll(tmp_val64);
        break;
    }
    case NEU_TYPE_BIT: {
        switch (point->area) {
        case MODBUS_AREA_HOLD_REGISTER:
        case MODBUS_AREA_INPUT_REGISTER: {
            jtype             = NEU_JSON_BIT;
            neu_value16_u v16 = { 0 };
            v16.value         = htons(*(uint16_t *) recv_bytes);
            jvalue.val_bit    = neu_value16_get_bit(v16, point->option.bit.bit);
            break;
        }
        case MODBUS_AREA_COIL:
        case MODBUS_AREA_INPUT:
            jtype = NEU_JSON_INT;
            break;
        }
        break;
    }
    case NEU_TYPE_STRING: {
        jtype             = NEU_JSON_STR;
        size_t str_length = point->n_register * 2;
        jvalue.val_str    = calloc(1, str_length + 1);
        strncpy(jvalue.val_str, (char *) recv_bytes, str_length);

        switch (point->option.string.type) {
        case NEU_DATATAG_STRING_TYPE_H:
            break;
        case NEU_DATATAG_STRING_TYPE_L:
            neu_datatag_string_ltoh(jvalue.val_str, str_length);
            break;
        case NEU_DATATAG_STRING_TYPE_D:
            break;
        case NEU_DATATAG_STRING_TYPE_E:
            break;
        }

        if (!neu_datatag_string_is_utf8(jvalue.val_str, str_length)) {
            jvalue.val_str[0] = '?';
            jvalue.val_str[1] = 0;
        }
        break;
    }
    default:
        break;
    }

    plugin->common.adapter_callbacks->driver.test_read_tag_response(
        plugin->common.adapter, req, jtype, point->type, jvalue,
        NEU_ERR_SUCCESS);

    return 0;
}

int modbus_test_read_tag(neu_plugin_t *plugin, void *req, neu_datatag_t tag)
{
    modbus_point_t   point = { 0 };
    neu_json_value_u error_value;
    error_value.val_int = 0;

    int err = modbus_tag_to_point(&tag, &point, plugin->address_base);
    if (err != NEU_ERR_SUCCESS) {
        plugin->common.adapter_callbacks->driver.test_read_tag_response(
            plugin->common.adapter, req, NEU_JSON_INT, NEU_TYPE_ERROR,
            error_value, err);
        return 0;
    }

    uint16_t response_size = 0;
    int      ret = modbus_stack_read(plugin->stack, point.slave_id, point.area,
                                point.start_address, point.n_register,
                                &response_size, true);
    if (ret <= 0) {
        plugin->common.adapter_callbacks->driver.test_read_tag_response(
            plugin->common.adapter, req, NEU_JSON_INT, NEU_TYPE_ERROR,
            error_value, NEU_ERR_PLUGIN_READ_FAILURE);
        return 0;
    }

    ret = process_protocol_buf_test(plugin, req, &point, response_size);
    if (ret == 0) {
        plugin->common.adapter_callbacks->driver.test_read_tag_response(
            plugin->common.adapter, req, NEU_JSON_INT, NEU_TYPE_ERROR,
            error_value, NEU_ERR_PLUGIN_DEVICE_NOT_RESPONSE);
        return 0;
    } else if (ret == -1) {
        plugin->common.adapter_callbacks->driver.test_read_tag_response(
            plugin->common.adapter, req, NEU_JSON_INT, NEU_TYPE_ERROR,
            error_value, NEU_ERR_PLUGIN_PROTOCOL_DECODE_FAILURE);
        return 0;
    } else if (ret == -2) {
        plugin->common.adapter_callbacks->driver.test_read_tag_response(
            plugin->common.adapter, req, NEU_JSON_INT, NEU_TYPE_ERROR,
            error_value, NEU_ERR_PLUGIN_READ_FAILURE);
        return 0;
    }

    return 0;
}

static uint8_t convert_value(neu_plugin_t *plugin, neu_value_u *value,
                             neu_datatag_t *tag, modbus_point_t *point)
{
    uint8_t n_byte = 0;
    switch (tag->type) {
    case NEU_TYPE_UINT16:
    case NEU_TYPE_INT16:
        value->u16 = htons(value->u16);
        n_byte     = sizeof(uint16_t);
        break;
    case NEU_TYPE_FLOAT:
    case NEU_TYPE_UINT32:
    case NEU_TYPE_INT32:
        if (point->option.value32.is_default) {
            modbus_convert_endianess(value, plugin->endianess);
        }
        value->u32 = htonl(value->u32);
        n_byte     = sizeof(uint32_t);
        break;
    case NEU_TYPE_DOUBLE:
    case NEU_TYPE_INT64:
    case NEU_TYPE_UINT64:
        value->u64 = neu_htonll(value->u64);
        n_byte     = sizeof(uint64_t);
        break;
    case NEU_TYPE_BIT:
        n_byte = sizeof(uint8_t);
        break;
    case NEU_TYPE_STRING: {
        switch (point->option.string.type) {
        case NEU_DATATAG_STRING_TYPE_H:
            break;
        case NEU_DATATAG_STRING_TYPE_L:
            neu_datatag_string_ltoh(value->str, point->option.string.length);
            break;
        case NEU_DATATAG_STRING_TYPE_D:
            break;
        case NEU_DATATAG_STRING_TYPE_E:
            break;
        }
        n_byte = point->option.string.length;
        break;
    }
    case NEU_TYPE_BYTES:
        n_byte = point->option.bytes.length;
        break;
    default:
        assert(false);
        break;
    }
    return n_byte;
}

static int write_modbus_point(neu_plugin_t *plugin, void *req,
                              modbus_point_t *point, neu_value_u value,
                              uint8_t n_byte)
{
    uint16_t response_size = 0;
    int      ret           = modbus_stack_write(
        plugin->stack, req, point->slave_id, point->area, point->start_address,
        point->n_register, value.bytes.bytes, n_byte, &response_size, true);

    if (ret > 0) {
        process_protocol_buf(plugin, point->slave_id, response_size);
    }

    return ret;
}

static int write_modbus_points(neu_plugin_t *      plugin,
                               modbus_write_cmd_t *write_cmd, void *req)
{
    uint16_t response_size = 0;

    int ret = modbus_stack_write(plugin->stack, req, write_cmd->slave_id,
                                 write_cmd->area, write_cmd->start_address,
                                 write_cmd->n_register, write_cmd->bytes,
                                 write_cmd->n_byte, &response_size, false);

    if (ret > 0) {
        process_protocol_buf(plugin, write_cmd->slave_id, response_size);
    }

    return ret;
}

int modbus_write_tag(neu_plugin_t *plugin, void *req, neu_datatag_t *tag,
                     neu_value_u value)
{
    modbus_point_t point = { 0 };
    int            ret = modbus_tag_to_point(tag, &point, plugin->address_base);
    assert(ret == 0);

    uint8_t n_byte = convert_value(plugin, &value, tag, &point);
    return write_modbus_point(plugin, req, &point, value, n_byte);
}

int modbus_write_tags(neu_plugin_t *plugin, void *req, UT_array *tags)
{
    struct modbus_write_tags_data *gtags = NULL;
    int                            ret   = 0;
    int                            rv    = 0;

    gtags = calloc(1, sizeof(struct modbus_write_tags_data));

    utarray_new(gtags->tags, &ut_ptr_icd);
    utarray_foreach(tags, neu_plugin_tag_value_t *, tag)
    {
        modbus_point_write_t *p = calloc(1, sizeof(modbus_point_write_t));
        ret = modbus_write_tag_to_point(tag, p, plugin->address_base);
        assert(ret == 0);

        utarray_push_back(gtags->tags, &p);
    }
    gtags->cmd_sort = modbus_write_tags_sort(gtags->tags, plugin->endianess);
    for (uint16_t i = 0; i < gtags->cmd_sort->n_cmd; i++) {
        ret = write_modbus_points(plugin, &gtags->cmd_sort->cmd[i], req);
        if (ret <= 0) {
            rv = 1;
        }
        if (plugin->interval > 0) {
            struct timespec t1 = { .tv_sec  = plugin->interval / 1000,
                                   .tv_nsec = 1000 * 1000 *
                                       (plugin->interval % 1000) };
            struct timespec t2 = { 0 };
            nanosleep(&t1, &t2);
        }
    }

    if (rv == 0) {
        plugin->common.adapter_callbacks->driver.write_response(
            plugin->common.adapter, req, NEU_ERR_SUCCESS);
    } else {
        plugin->common.adapter_callbacks->driver.write_response(
            plugin->common.adapter, req, NEU_ERR_PLUGIN_DISCONNECTED);
    }

    for (uint16_t i = 0; i < gtags->cmd_sort->n_cmd; i++) {
        utarray_free(gtags->cmd_sort->cmd[i].tags);
        free(gtags->cmd_sort->cmd[i].bytes);
    }
    free(gtags->cmd_sort->cmd);
    free(gtags->cmd_sort);
    utarray_foreach(gtags->tags, modbus_point_write_t **, tag) { free(*tag); }
    utarray_free(gtags->tags);
    free(gtags);
    return ret;
}

int modbus_write_resp(void *ctx, void *req, int error)
{
    neu_plugin_t *plugin = (neu_plugin_t *) ctx;

    plugin->common.adapter_callbacks->driver.write_response(
        plugin->common.adapter, req, error);
    return 0;
}

static void plugin_group_free(neu_plugin_group_t *pgp)
{
    struct modbus_group_data *gd = (struct modbus_group_data *) pgp->user_data;

    modbus_tag_sort_free(gd->cmd_sort);

    utarray_foreach(gd->tags, modbus_point_t **, tag) { free(*tag); }

    utarray_free(gd->tags);
    free(gd->group);

    free(gd);
}

/**
 * @brief 从 Modbus 连接中接收数据。
 *
 * 此函数根据插件是否处于服务器模式，选择不同的接收函数来接收数据。
 * 如果插件是服务器模式，调用 `neu_conn_tcp_server_recv` 从指定客户端接收数据；
 * 如果是客户端模式，调用 `neu_conn_recv` 从连接接收数据。
 *
 * @param plugin 代表 Modbus 插件实例
 * @param buffer 指向用于存储接收到的数据的缓冲区的指针。
 * @param size 要接收的数据的字节数。
 *
 * @return 返回实际接收到的字节数。如果接收过程中出现错误，返回值可能为 -1
 */
static ssize_t recv_data(neu_plugin_t *plugin, uint8_t *buffer, size_t size)
{
    // 判断插件是否处于服务器模式
    if (plugin->is_server) {
        return neu_conn_tcp_server_recv(plugin->conn, plugin->client_fd, buffer,
                                        size);
    } else {
        return neu_conn_recv(plugin->conn, buffer, size);
    }
}

/**
 * @brief 处理接收到的 Modbus 协议数据。
 *
 * 此函数用于对从 Modbus 设备接收到的数据进行处理。首先，若接收到的数据量小于 512 字节，
 * 会记录接收的协议数据日志。接着，初始化一个用于解包的缓冲区，然后调用 `modbus_stack_recv`
 * 函数处理接收到的数据。最后，根据处理结果和接收到的数据量是否符合预期来返回相应的状态码。
 *
 * @param plugin 代表 Modbus 插件实例，包含插件的配置信息、状态和回调函数等。
 * @param recv_buf 指向存储接收到的数据的缓冲区的指针。
 * @param recv_size 接收到的数据的实际字节数。
 * @param expected_size 预期接收到的数据的字节数。
 * @param slave_id 无符号 8 位整数，表示 Modbus 从站的 ID，用于标识响应数据来自哪个从站。
 *
 * @return 返回处理结果的状态码：
 *         - 如果 `modbus_stack_recv` 函数返回 `MODBUS_DEVICE_ERR`，表示设备出现错误，返回 -2。
 *         - 如果接收到的数据量等于预期数据量，返回 `modbus_stack_recv` 函数的返回值。
 *         - 如果接收到的数据量不等于预期数据量，返回 -1，表示接收的数据不符合预期。
 */
static int process_received_data(neu_plugin_t *plugin, uint8_t *recv_buf,
                                 ssize_t recv_size, uint16_t expected_size,
                                 uint8_t slave_id)
{
    // 若接收到的数据量小于 512 字节，记录接收的协议数据日志
    if (recv_size < 512) {
        plog_recv_protocol(plugin, recv_buf, recv_size);
    }

    // 定义一个用于解包的缓冲区结构体，并初始化为 0
    neu_protocol_unpack_buf_t pbuf = { 0 };

    // 初始化解包缓冲区，传入接收到的数据缓冲区和实际数据量
    neu_protocol_unpack_buf_init(&pbuf, recv_buf, recv_size);

    // 处理接收到的数据
    int ret = modbus_stack_recv(plugin->stack, slave_id, &pbuf);

    // 若返回 MODBUS_DEVICE_ERR，表示设备出现错误
    if (ret == MODBUS_DEVICE_ERR) {
        return -2;
    }

    // 判断接收到的数据量是否等于预期数据量  
    return recv_size == expected_size ? ret : -1;
}

static int process_received_data_test(neu_plugin_t *plugin, uint8_t *recv_buf,
                                      ssize_t recv_size, uint16_t expected_size,
                                      void *req, modbus_point_t *point)
{
    if (recv_size < 512) {
        plog_recv_protocol(plugin, recv_buf, recv_size);
    }
    neu_protocol_unpack_buf_t pbuf = { 0 };
    neu_protocol_unpack_buf_init(&pbuf, recv_buf, recv_size);
    int ret = modbus_stack_recv_test(plugin, req, point, &pbuf);
    if (ret == MODBUS_DEVICE_ERR) {
        return -2;
    }
    return recv_size == expected_size ? ret : -1;
}

static int valid_modbus_tcp_response(neu_plugin_t *plugin, uint8_t *recv_buf,
                                     uint16_t response_size)
{
    ssize_t ret = recv_data(plugin, recv_buf, sizeof(struct modbus_header));
    if (ret <= 0) {
        return 0;
    }

    if (ret != sizeof(struct modbus_header)) {
        return -1;
    }

    struct modbus_header *header = (struct modbus_header *) recv_buf;

    if (htons(header->len) > response_size - sizeof(struct modbus_header)) {
        return -1;
    }

    ret = recv_data(plugin, recv_buf + sizeof(struct modbus_header),
                    htons(header->len));

    return ret == htons(header->len)
        ? (ssize_t)(ret + sizeof(struct modbus_header))
        : (ssize_t) -1;
}

static int process_modbus_tcp(neu_plugin_t *plugin, uint8_t *recv_buf,
                              uint16_t response_size, uint8_t slave_id)
{
    int total_recv = valid_modbus_tcp_response(plugin, recv_buf, response_size);
    if (total_recv > 0) {
        return process_received_data(plugin, recv_buf, total_recv,
                                     response_size, slave_id);
    }
    return total_recv;
}

static int process_modbus_tcp_test(neu_plugin_t *plugin, uint8_t *recv_buf,
                                   uint16_t response_size, void *req,
                                   modbus_point_t *point)
{
    int total_recv = valid_modbus_tcp_response(plugin, recv_buf, response_size);
    if (total_recv > 0) {
        return process_received_data_test(plugin, recv_buf, total_recv,
                                          response_size, req, point);
    }
    return total_recv;
}

/**
 * @brief 处理 Modbus RTU 协议的响应数据。

 *
 * @param plugin  代表 Modbus 插件实例
 * @param recv_buf 指向用于存储接收到的数据的缓冲区的指针，缓冲区应在调用前分配足够的空间。
 * @param response_size 无符号 16 位整数，表示预期接收到的响应数据的字节数，用于指定接收数据的长度。
 * @param slave_id 无符号 8 位整数，表示 Modbus 从站的 ID，用于标识响应数据来自哪个从站。
 *
 * @return 如果接收数据失败（返回值为 0 或 -1），则返回 0；
 *         否则，返回 `process_received_data` 函数的返回值，该返回值具体含义取决于该函数的实现，
 *         通常表示对接收数据进行处理的结果状态。
 */
static int process_modbus_rtu(neu_plugin_t *plugin, uint8_t *recv_buf,
                              uint16_t response_size, uint8_t slave_id)
{
    // 从连接中接收数据，并将实际接收的字节数存储在 ret 中
    ssize_t ret = recv_data(plugin, recv_buf, response_size);
    if (ret == 0 || ret == -1) {
        return 0;
    }
    
    // 接收成功，对接收到的数据进行进一步处理
    return process_received_data(plugin, recv_buf, ret, response_size,
                                 slave_id);
}

/**
 * @brief 处理 Modbus 协议的响应缓冲区数据。
 *
 * 该函数根据插件所使用的 Modbus 协议类型（TCP 或 RTU），
 * 动态分配内存用于存储接收到的响应数据，然后调用相应的处理函数对数据进行处理。
 * 处理完成后，会释放之前分配的内存。
 *
 * @param plugin        代表 Modbus 插件实例
 * @param slave_id      表示 Modbus 从站的 ID，用于标识响应数据来自哪个从站。
 * @param response_size 表示预期接收到的响应数据的字节数，用于分配存储响应数据
 *                      的缓冲区大小。
 *
 * @return 处理结果的返回值。如果内存分配失败，返回 -1；
 */
static int process_protocol_buf(neu_plugin_t *plugin, uint8_t slave_id,
                                uint16_t response_size)
{
    // 动态分配内存用于存储接收到的响应数据，初始化为 0
    uint8_t *recv_buf = (uint8_t *) calloc(response_size, 1);
    
    // 检查内存分配是否成功
    if (!recv_buf) {
        return -1;
    }

    // 用于存储处理结果的返回值
    int ret = 0;

    // 判断插件使用的 Modbus 协议类型
    if (plugin->protocol == MODBUS_PROTOCOL_TCP) {
        ret = process_modbus_tcp(plugin, recv_buf, response_size, slave_id);
    } else if (plugin->protocol == MODBUS_PROTOCOL_RTU) {
        ret = process_modbus_rtu(plugin, recv_buf, response_size, slave_id);
    }

    // 释放之前分配的用于存储响应数据的内存
    free(recv_buf);

    // 返回处理结果的返回值
    return ret;
}

static int process_protocol_buf_test(neu_plugin_t *plugin, void *req,
                                     modbus_point_t *point,
                                     uint16_t        response_size)
{
    uint8_t *recv_buf = (uint8_t *) calloc(response_size, 1);
    if (!recv_buf) {
        return -1;
    }

    int ret = -1;
    if (plugin->protocol == MODBUS_PROTOCOL_TCP) {
        ret = process_modbus_tcp_test(plugin, recv_buf, response_size, req,
                                      point);
    }

    free(recv_buf);
    return ret;
}