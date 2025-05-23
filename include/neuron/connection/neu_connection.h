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

#ifndef NEURON_CONNECTION_H
#define NEURON_CONNECTION_H

#include <stdint.h>
#include <unistd.h>

#include "utils/protocol_buf.h"

typedef enum neu_conn_type {
    NEU_CONN_TCP_SERVER = 1,
    NEU_CONN_TCP_CLIENT,
    NEU_CONN_UDP,
    NEU_CONN_TTY_CLIENT,
    NEU_CONN_UDP_TO,
} neu_conn_type_e;

typedef enum neu_conn_tty_baud {
    NEU_CONN_TTY_BAUD_115200,
    NEU_CONN_TTY_BAUD_57600,
    NEU_CONN_TTY_BAUD_38400,
    NEU_CONN_TTY_BAUD_19200,
    NEU_CONN_TTY_BAUD_9600,
    NEU_CONN_TTY_BAUD_4800,
    NEU_CONN_TTY_BAUD_2400,
    NEU_CONN_TTY_BAUD_1800,
    NEU_CONN_TTY_BAUD_1200,
    NEU_CONN_TTY_BAUD_600,
    NEU_CONN_TTY_BAUD_300,
    NEU_CONN_TTY_BAUD_200,
    NEU_CONN_TTY_BAUD_150,
} neu_conn_tty_baud_e;

typedef enum neu_conn_tty_parity {
    NEU_CONN_TTY_PARITY_NONE,
    NEU_CONN_TTY_PARITY_ODD,
    NEU_CONN_TTY_PARITY_EVEN,
    NEU_CONN_TTY_PARITY_MARK,
    NEU_CONN_TTY_PARITY_SPACE,
} neu_conn_tty_parity_e;

typedef enum neu_conn_tty_stop {
    NEU_CONN_TTY_STOP_1,
    NEU_CONN_TTY_STOP_2,
} neu_conn_tty_stop_e;

typedef enum neu_conn_tty_data {
    NEU_CONN_TTY_DATA_5,
    NEU_CONN_TTY_DATA_6,
    NEU_CONN_TTY_DATA_7,
    NEU_CONN_TTY_DATA_8,
} neu_conn_tty_data_e;

typedef enum neu_conn_tty_flow {
    NEU_CONN_TTYP_FLOW_DISABLE,
    NEU_CONN_TTYP_FLOW_ENABLE,
} neu_conn_tty_flow_e;

typedef void (*neu_conn_callback)(void *ctx, int fd);

/**
 * @brief 表示网络连接参数的结构体，可配置不同类型的网络连接。
 * 
 * 该结构体使用联合体 `params` 来存储不同类型网络连接所需的参数，
 * 可以根据 `type` 成员指定的连接类型，选择对应的参数进行配置。
 */
typedef struct neu_conn_param {
    zlog_category_t *log;

    /**
     * @brief 连接类型。
     * 
     * 表示网络连接的类型，是一个枚举类型 `neu_conn_type_e`，
     * 取值包括 TCP 服务器、TCP 客户端、UDP 连接、UDP 目标连接、串口客户端等。
     */
    neu_conn_type_e  type;

    union {
        struct {
            char *            ip;
            uint16_t          port;
            uint16_t          timeout; // millisecond
            int               max_link;
            neu_conn_callback start_listen;
            neu_conn_callback stop_listen;
        } tcp_server;

        struct {
            char *   ip;
            uint16_t port;
            uint16_t timeout; // millisecond
        } tcp_client;

        struct {
            char *   src_ip;
            uint16_t src_port;
            char *   dst_ip;
            uint16_t dst_port;
            uint16_t timeout; // millisecond
        } udp;

        struct {
            char *   src_ip;
            uint16_t src_port;
            uint16_t timeout; // millisecond
        } udpto;

        struct {
            char *                device; // 一个指向串口设备文件路径的字符串
            neu_conn_tty_data_e   data;
            neu_conn_tty_stop_e   stop;
            neu_conn_tty_baud_e   baud;
            neu_conn_tty_parity_e parity;
            neu_conn_tty_flow_e   flow;
            uint16_t              timeout; // millisecond
        } tty_client;
    } params;
} neu_conn_param_t;

typedef struct neu_conn neu_conn_t;

/**
 * @brief 用于记录网络连接状态的结构体。
 *
 * 此结构体 `neu_conn_state_t` 主要用于跟踪和
 * 存储网络连接过程中发送和接收的字节数。它可以帮
 * 助开发者监控网络连接的使用情况，比如在进行数据
 * 采集、通信等操作时，了解数据的流量大小，从而进
 * 行性能分析、带宽管理等。
 */
typedef struct neu_conn_state {
    uint64_t send_bytes;
    uint64_t recv_bytes;
} neu_conn_state_t;

/**
 * @brief create a new connection.
 *
 * @param[in] param required to create a connection.
 * @param[in] ctx Mainly used in the neu_conn_callback callback function.
 * @param[in] connected The neu_conn_callback callback function will be
 * triggered when the connection is successful.
 * @param[in] disconnected The callback function of neu_conn_callback will be
 * triggered when the connection fails.
 * @return Connection on success, NULL on failure.
 */
neu_conn_t *neu_conn_new(neu_conn_param_t *param, void *ctx,
                         neu_conn_callback connected,
                         neu_conn_callback disconnected);

/**
 * @brief Reconfigure the connection.
 *
 * @param[in] conn Connection that need to be configured.
 * @param[in] param New configuration parameters for the connection.
 * @return Connection on success, NULL on failure.
 */
neu_conn_t *neu_conn_reconfig(neu_conn_t *conn, neu_conn_param_t *param);

/**
 * @brief Destroy connection
 *
 * @param conn
 */
void neu_conn_destory(neu_conn_t *conn);

/**
 * @brief Connect
 *
 *
 * @param[in] conn
 */
void neu_conn_connect(neu_conn_t *conn);

/**
 * @brief Get connection fd
 *
 *
 * @param[in] conn
 * @return Successfully returns a number greater than 0, -1 or 0 on
 * failure.
 */
int neu_conn_fd(neu_conn_t *conn);

/**
 * @brief Disconnect
 *
 *
 * @param[in] conn
 */
void neu_conn_disconnect(neu_conn_t *conn);

/**
 * @brief Stop the connection
 *
 *
 * @param[in] conn
 */
void neu_conn_stop(neu_conn_t *conn);

/**
 * @brief Start the connection
 *
 *
 * @param[in] conn
 */
void neu_conn_start(neu_conn_t *conn);

/**
 * @brief get state of connection
 *
 *
 * @param[in] conn
 * @return returns state.
 */
neu_conn_state_t neu_conn_state(neu_conn_t *conn);
/**
 * @brief Receive connection requests from clients.
 *
 * @param[in] conn
 * @return Successfully returns a number greater than 0, i.e. client fd, -1 on
 * failure.
 */
int neu_conn_tcp_server_accept(neu_conn_t *conn);

/**
 * @brief Close a client connection.
 *
 * @param[in] conn
 * @param[in] fd Get the fd from neu_conn_tcp_server_accept function.
 * @return 0 on success, -1 on failure.
 */
int neu_conn_tcp_server_close_client(neu_conn_t *conn, int fd);

/**
 * @brief Send data over the connection.
 *
 * @param[in] conn
 * @param[in] buf Store the data to be sent.
 * @param[in] len Length of data to be sent.
 * @return When greater than 0, returns the number of bytes successfully sent,
 * less than or equal to 0 fails.
 */
ssize_t neu_conn_send(neu_conn_t *conn, uint8_t *buf, ssize_t len);
ssize_t neu_conn_udp_sendto(neu_conn_t *conn, uint8_t *buf, ssize_t len,
                            void *dst);

/**
 * @brief Receive data via connection read.
 *
 * @param[in] conn
 * @param[in] buf The received data is stored in buf.
 * @param[in] len Length of buf.
 * @return When greater than 0, it will return the number of bytes successfully
 * received, less than or equal to 0 fails.
 */
ssize_t neu_conn_recv(neu_conn_t *conn, uint8_t *buf, ssize_t len);
ssize_t neu_conn_udp_recvfrom(neu_conn_t *conn, uint8_t *buf, ssize_t len,
                              void *src);

void neu_conn_clear_recv_buffer(neu_conn_t *conn);

/**
 * @brief Specify the client to send data.
 *
 * @param[in] conn
 * @param[in] fd Client's file descriptor.
 * @param[in] buf Store the data to be sent.
 * @param[in] len Length of data to be sent.
 * @return When greater than 0, returns the number of bytes successfully sent,
 * less than or equal to 0 fails.
 */
ssize_t neu_conn_tcp_server_send(neu_conn_t *conn, int fd, uint8_t *buf,
                                 ssize_t len);

/**
 * @brief Receive data via tcp server connection read.
 *
 * @param[in] conn
 * @param[in] fd Client's file descriptor.
 * @param[in] buf The received data is stored in buf.
 * @param[in] len Length of buf.
 * @return When greater than 0, returns the number of bytes successfully
 * received, less than or equal to 0 fails.
 */
ssize_t neu_conn_tcp_server_recv(neu_conn_t *conn, int fd, uint8_t *buf,
                                 ssize_t len);

typedef int (*neu_conn_stream_consume_fn)(
    void *context, neu_protocol_unpack_buf_t *protocol_buf);

int neu_conn_stream_consume(neu_conn_t *conn, void *context,
                            neu_conn_stream_consume_fn fn);

int neu_conn_stream_tcp_server_consume(neu_conn_t *conn, int fd, void *context,
                                       neu_conn_stream_consume_fn fn);

typedef neu_buf_result_t (*neu_conn_process_msg)(
    void *context, neu_protocol_unpack_buf_t *protocol_buf);

int neu_conn_wait_msg(neu_conn_t *conn, void *context, uint16_t n_byte,
                      neu_conn_process_msg fn);

int neu_conn_tcp_server_wait_msg(neu_conn_t *conn, int fd, void *context,
                                 uint16_t n_byte, neu_conn_process_msg fn);

int is_ipv4(const char *ip);
int is_ipv6(const char *ip);

bool neu_conn_is_connected(neu_conn_t *conn);

#endif
