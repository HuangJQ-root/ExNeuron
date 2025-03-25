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

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <fcntl.h>
#include <termios.h>

#include "utils/log.h"

#include "connection/neu_connection.h"

#ifndef CMSPAR
#define CMSPAR 010000000000 /* mark or space (stick) parity */
#endif

struct tcp_client {
    int                fd;
    struct sockaddr_in client;
};

/**
 * @brief 表示网络连接的结构体，封装了网络连接所需的各种参数、状态和回调函数。
 * 
 * 该结构体用于管理不同类型的网络连接：TCP UDP TTY
 */
struct neu_conn {
    /**
     * @brief 连接参数。
     *
     */
    neu_conn_param_t param;

    /**
     * @brief 连接的额外数据指针。
     * 
     * 可用于存储与连接相关的额外信息
     */
    void *           data;
    
    /**
     * @brief 连接状态标志。
     * 
     * 表示当前连接是否已成功建立，true 表示已连接，
     * false 表示未连接。
     */
    bool             is_connected;

    /**
     * @brief 停止标志。
     * 
     * 用于控制连接操作是否停止，设置为 true 
     * 时表示停止连接相关的操作。
     */
    bool             stop;

    /**
     * @brief 连接正常标志。
     * 
     * 表示连接是否处于正常状态，true 表示连接正常，
     * false 表示连接可能存在问题。
     */
    bool             connection_ok;

    /**
     * @brief 连接成功回调函数指针。
     * 
     * 当连接成功建立时，会调用该回调函数
     */
    neu_conn_callback connected;

    /**
     * @brief 连接断开回调函数指针。
     * 
     * 当连接断开时，会调用该回调函数
     */
    neu_conn_callback disconnected;

    /**
     * @brief 回调触发标志。
     * 
     * 用于控制是否触发连接成功或断开的回调函数，
     * true 表示触发，false 表示不触发。
     */
    bool              callback_trigger;

    /**
     * @brief 互斥锁。
     * 
     * 用于保护结构体中的共享资源，确保在多线程环境下
     * 对连接状态和数据的安全访问。
     */
    pthread_mutex_t mtx;

    /**
     * @brief 文件描述符。
     * 
     */
    int  fd;

    /**
     * @brief 阻塞标志。
     * 
     * 表示连接操作是否为阻塞模式，
     * true 表示阻塞，false 表示非阻塞。
     */
    bool block;

    /**
     * @brief 连接状态信息。
     * 
     * 存储连接的状态信息：发送和接收的字节数等
     */
    neu_conn_state_t state;

    /**
     * @brief TCP 服务器相关信息。
     * 
     * 包含 TCP 服务器的客户端信息、客户端数量和监听状态等，用于管理 TCP 服务器连接。
     */
    struct {
            /**
         * @brief TCP 客户端数组指针。
         * 
         * 指向一个存储 TCP 客户端信息的数组，每个元素为 struct tcp_client 类型。
         */
        struct tcp_client *clients;

        /**
         * @brief TCP 客户端数量。
         * 
         * 表示当前连接到 TCP 服务器的客户端数量。
         */
        int                n_client;

        /**
         * @brief 监听状态标志。
         * 
         * 表示 TCP 服务器是否正在监听连接请求，true 表示正在监听，false 表示未监听。
         */
        bool               is_listen;
    } tcp_server;

    /**
     * @brief 缓冲区指针。
     * 
     * 指向一个用于存储网络数据的缓冲区，用于暂存从网络接收或要发送的数据。
     */
    uint8_t *buf;

    /**
     * @brief 缓冲区大小。
     * 
     * 表示 buf 缓冲区的总大小，用于限制缓冲区可存储的数据量。
     */
    uint16_t buf_size;

    /**
     * @brief 缓冲区偏移量。
     * 
     * 表示当前缓冲区中已使用的字节数，用于指示下一次读写操作的位置。
     */
    uint16_t offset;
};

static void conn_tcp_server_add_client(neu_conn_t *conn, int fd,
                                       struct sockaddr_in client);
static void conn_tcp_server_del_client(neu_conn_t *conn, int fd);
static int  conn_tcp_server_replace_client(neu_conn_t *conn, int fd,
                                           struct sockaddr_in client);

static void conn_tcp_server_listen(neu_conn_t *conn);
static void conn_tcp_server_stop(neu_conn_t *conn);

static void conn_connect(neu_conn_t *conn);
static void conn_disconnect(neu_conn_t *conn);

static void conn_free_param(neu_conn_t *conn);
static void conn_init_param(neu_conn_t *conn, neu_conn_param_t *param);

neu_conn_t *neu_conn_new(neu_conn_param_t *param, void *data,
                         neu_conn_callback connected,
                         neu_conn_callback disconnected)
{
    neu_conn_t *conn = calloc(1, sizeof(neu_conn_t));

    conn_init_param(conn, param);
    conn->is_connected     = false;
    conn->callback_trigger = false;
    conn->data             = data;
    conn->disconnected     = disconnected;
    conn->connected        = connected;

    conn->buf_size = 8192;
    conn->buf      = calloc(conn->buf_size, 1);
    conn->offset   = 0;
    conn->stop     = false;

    conn_tcp_server_listen(conn);

    pthread_mutex_init(&conn->mtx, NULL);

    return conn;
}

void neu_conn_stop(neu_conn_t *conn)
{
    pthread_mutex_lock(&conn->mtx);
    if (conn->tcp_server.is_listen) {
        conn_tcp_server_stop(conn);
    }
    conn->stop = true;
    conn_disconnect(conn);
    pthread_mutex_unlock(&conn->mtx);
}

void neu_conn_start(neu_conn_t *conn)
{
    pthread_mutex_lock(&conn->mtx);
    conn->state.recv_bytes = 0;
    conn->state.send_bytes = 0;
    conn->stop             = false;
    pthread_mutex_unlock(&conn->mtx);
}

neu_conn_t *neu_conn_reconfig(neu_conn_t *conn, neu_conn_param_t *param)
{
    pthread_mutex_lock(&conn->mtx);

    conn_disconnect(conn);
    conn_free_param(conn);
    conn_tcp_server_stop(conn);

    conn_init_param(conn, param);
    conn_tcp_server_listen(conn);

    conn->state.recv_bytes = 0;
    conn->state.send_bytes = 0;

    pthread_mutex_unlock(&conn->mtx);

    return conn;
}

void neu_conn_destory(neu_conn_t *conn)
{
    pthread_mutex_lock(&conn->mtx);

    conn_tcp_server_stop(conn);
    conn_disconnect(conn);
    conn_free_param(conn);

    pthread_mutex_unlock(&conn->mtx);

    pthread_mutex_destroy(&conn->mtx);

    free(conn->buf);
    free(conn);
}

neu_conn_state_t neu_conn_state(neu_conn_t *conn)
{
    return conn->state;
}

int neu_conn_tcp_server_accept(neu_conn_t *conn)
{
    struct sockaddr_in client     = { 0 };
    socklen_t          client_len = sizeof(struct sockaddr_in);
    int                fd         = 0;

    pthread_mutex_lock(&conn->mtx);
    if (conn->param.type != NEU_CONN_TCP_SERVER) {
        pthread_mutex_unlock(&conn->mtx);
        return -1;
    }

    fd = accept(conn->fd, (struct sockaddr *) &client, &client_len);
    if (fd <= 0) {
        zlog_error(conn->param.log, "%s:%d accpet error: %s",
                   conn->param.params.tcp_server.ip,
                   conn->param.params.tcp_server.port, strerror(errno));
        pthread_mutex_unlock(&conn->mtx);
        return -1;
    }

    if (conn->block) {
        struct timeval tv = {
            .tv_sec  = conn->param.params.tcp_server.timeout / 1000,
            .tv_usec = (conn->param.params.tcp_server.timeout % 1000) * 1000,
        };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }

    if (conn->tcp_server.n_client >= conn->param.params.tcp_server.max_link) {
        int free_fd = conn_tcp_server_replace_client(conn, fd, client);
        if (free_fd > 0) {
            zlog_warn(conn->param.log, "replace old client %d with %d", free_fd,
                      fd);
        } else {
            close(fd);
            zlog_warn(conn->param.log, "%s:%d accpet max link: %d, reject",
                      conn->param.params.tcp_server.ip,
                      conn->param.params.tcp_server.port,
                      conn->param.params.tcp_server.max_link);
            pthread_mutex_unlock(&conn->mtx);
            return -1;
        }
    } else {
        conn_tcp_server_add_client(conn, fd, client);
    }

    conn->is_connected = true;
    conn->connected(conn->data, fd);
    conn->callback_trigger = true;

    zlog_notice(conn->param.log, "%s:%d accpet new client: %s:%d, fd: %d",
                conn->param.params.tcp_server.ip,
                conn->param.params.tcp_server.port, inet_ntoa(client.sin_addr),
                ntohs(client.sin_port), fd);

    pthread_mutex_unlock(&conn->mtx);

    return fd;
}

int neu_conn_tcp_server_close_client(neu_conn_t *conn, int fd)
{
    pthread_mutex_lock(&conn->mtx);
    if (conn->param.type != NEU_CONN_TCP_SERVER) {
        pthread_mutex_unlock(&conn->mtx);
        return -1;
    }

    conn->disconnected(conn->data, fd);
    conn_tcp_server_del_client(conn, fd);
    conn->offset = 0;
    memset(conn->buf, 0, conn->buf_size);

    pthread_mutex_unlock(&conn->mtx);
    return 0;
}

/**
 * @brief 向 TCP 服务器的指定客户端发送数据。
 *
 * 该函数用于将指定长度的数据从缓冲区发送到 TCP 服务器的特定客户端。
 * 在发送数据之前，会检查连接是否停止，并尝试监听新的客户端连接。
 * 发送成功后，会更新连接的发送字节统计信息。如果发送失败且不是由于
 * 非阻塞操作暂时无数据可处理的情况，会调用断开连接回调函数并从客户端
 * 列表中移除该客户端。
 *
 * @param conn 指向 `neu_conn_t` 结构体的指针，表示 TCP 服务器连接。
 * @param fd 要发送数据的客户端的文件描述符。
 * @param buf 指向要发送的数据缓冲区的指针。
 * @param len 要发送的数据的长度（字节数）。
 *
 * @return 返回实际发送的字节数。如果发送失败，返回0 。
 */
ssize_t neu_conn_tcp_server_send(neu_conn_t *conn, int fd, uint8_t *buf,
                                 ssize_t len)
{
    // 用于存储发送操作的返回值，即实际发送的字节数
    ssize_t ret = 0;

    pthread_mutex_lock(&conn->mtx);

    // 检查连接是否已停止，如果停止则解锁并返回 0
    if (conn->stop) {
        pthread_mutex_unlock(&conn->mtx);
        return ret;
    }

    // 监听新的客户端连接
    conn_tcp_server_listen(conn);

    /**
     * @note 以非阻塞模式且忽略 SIGPIPE 信号的方式向客户端发送数据
     * 
     * - 当使用 send 函数向一个已经关闭连接的套接字发送数据时，
     *   如果没有设置 MSG_NOSIGNAL 标志，系统会向当前进程发送
     *   一个 SIGPIPE 信号。默认情况下，SIGPIPE 信号的处理动
     *   作是终止进程，这可能会导致程序意外退出。而当设置了 
     *   MSG_NOSIGNAL 标志后，send 函数在遇到这种情况时不会发
     *   送 SIGPIPE 信号，而是返回 -1 并将 errno 设置为 EPIPE，
     *   这样程序可以通过检查返回值和 errno 来处理这种错误情况，
     *   避免进程被意外终止。
     * 
     * - 将 send 函数设置为非阻塞模式。在默认情况下，send 函数是阻
     *   塞的，即如果套接字的发送缓冲区已满，send 函数会阻塞当前线程，
     *   直到有足够的空间可以发送数据。而当设置了 MSG_DONTWAIT 标志
     *   后，send 函数会立即返回，如果发送缓冲区已满，send 函数会返回
     *   -1 并将 errno 设置为 EAGAIN 或 EWOULDBLOCK，表示当前操作
     *   无法立即完成。
     */
    ret = send(fd, buf, len, MSG_NOSIGNAL | MSG_DONTWAIT);

    // 如果发送成功，更新连接的发送字节统计信息
    if (ret > 0) {
        conn->state.send_bytes += ret;
    }

    // 如果发送失败且错误码不是 EAGAIN（表示非阻塞操作暂时无数据可处理）
    if (ret <= 0 && errno != EAGAIN) {
        // 调用断开连接回调函数，通知连接断开
        conn->disconnected(conn->data, fd);

        // 从客户端列表中移除该客户端
        conn_tcp_server_del_client(conn, fd);
    }

    pthread_mutex_unlock(&conn->mtx);

    // 返回实际发送的字节数
    return ret;
}

ssize_t neu_conn_tcp_server_recv(neu_conn_t *conn, int fd, uint8_t *buf,
                                 ssize_t len)
{
    ssize_t ret = 0;

    pthread_mutex_lock(&conn->mtx);
    if (conn->stop) {
        pthread_mutex_unlock(&conn->mtx);
        return ret;
    }

    if (conn->block) {
        ret = recv(fd, buf, len, MSG_WAITALL);
    } else {
        ret = recv(fd, buf, len, 0);
    }

    if (ret > 0) {
        conn->state.recv_bytes += ret;
    }

    if (ret <= 0) {
        zlog_error(conn->param.log,
                   "conn fd: %d, recv error, "
                   "errno: %s(%d)",
                   conn->fd, strerror(errno), errno);
    }

    pthread_mutex_unlock(&conn->mtx);

    return ret;
}

/**
 * @brief 向指定的网络连接发送数据。
 *
 * 该函数用于将指定长度的数据从缓冲区发送到指定的网络连接。
 * 它会处理连接状态，包括在连接断开时尝试重新连接，
 * 并根据连接类型（如 TCP 客户端、UDP、串口客户端等）选择合适的发送方式。
 * 同时，它会处理发送失败的情况，包括重试机制和断开连接的处理。
 *
 * @param conn 表示要发送数据的网络连接。
 * @param buf 指向要发送的数据缓冲区的指针。
 * @param len 要发送的数据的长度（字节数）。
 *
 * @return 若发送成功，返回实际发送的字节数；若发送失败，返回 -1 并设置相应的 errno；
 *         若连接已停止，返回 0。
 */
ssize_t neu_conn_send(neu_conn_t *conn, uint8_t *buf, ssize_t len)
{
    // 用于存储实际发送的字节数，初始化为 0
    ssize_t ret = 0;

    pthread_mutex_lock(&conn->mtx);

    // 检查连接是否已停止，如果停止则解锁并返回 0
    if (conn->stop) {
        pthread_mutex_unlock(&conn->mtx);
        return ret;
    }

    // 如果连接未建立，尝试连接
    if (!conn->is_connected) {
        conn_connect(conn);
    }

    // 如果连接已建立
    if (conn->is_connected) {
        // 重试次数计数器
        int retry = 0;
        
        // 循环发送数据，直到发送完所有数据或出现错误
        while (ret < len) {
            // 存储每次发送操作的返回值
            int rc = 0;

            // 根据连接类型选择不同的发送方式
            switch (conn->param.type) {
            case NEU_CONN_UDP_TO:
            case NEU_CONN_TCP_SERVER:
                // UDP 目标连接和 TCP 服务器连接类型不应该使用此函数发送数据，断言失败
                assert(false);
                break;
            case NEU_CONN_TCP_CLIENT:
            case NEU_CONN_UDP:
                if (conn->block) {
                    // 阻塞模式下发送数据，忽略 SIGPIPE 信号
                    rc = send(conn->fd, buf + ret, len - ret, MSG_NOSIGNAL);
                } else {
                    // 非阻塞模式下发送数据，忽略 SIGPIPE 信号并等待所有数据发送完成
                    rc = send(conn->fd, buf + ret, len - ret,
                              MSG_NOSIGNAL | MSG_WAITALL);
                }
                break;
            case NEU_CONN_TTY_CLIENT:
                // 向串口设备写入数据
                rc = write(conn->fd, buf + ret, len - ret);
                break;
            }

            // 如果发送成功
            if (rc > 0) {
                // 更新已发送的字节数
                ret += rc;

                // 如果已发送的字节数等于要发送的总字节数，跳出循环
                if (ret == len) {
                    break;
                }
            } 
            // 发送失败的情况
            else {
                // 如果是非阻塞模式，且错误码为 EAGAIN（表示暂时无数据可处理）
                if (!conn->block && rc == -1 && errno == EAGAIN) {
                    // 如果重试次数超过 10 次
                    if (retry > 10) {
                        zlog_error(conn->param.log,
                                   "conn fd: %d, send buf len: %zd, ret: %zd, "
                                   "errno: %s(%d)",
                                   conn->fd, len, ret, strerror(errno), errno);
                        break;
                    } 
                    // 重试次数未超过 10 次
                    else {
                        // 定义一个 50 毫秒的时间间隔
                        struct timespec t1 = {
                            .tv_sec  = 0,
                            .tv_nsec = 1000 * 1000 * 50,
                        };

                        // 用于存储实际睡眠的时间
                        struct timespec t2 = { 0 };

                        // 线程睡眠 50 毫秒
                        nanosleep(&t1, &t2);

                        // 重试次数加 1
                        retry++;
                        zlog_warn(conn->param.log,
                                  "not all data send, retry: %d, ret: "
                                  "%zd(%d), len: %zd",
                                  retry, ret, rc, len);
                    }
                } else {
                    // 更新返回值为错误码
                    ret = rc;
                    break;
                }
            }
        }

        // 如果发送失败
        if (ret == -1) {
            // 如果错误码不是 EAGAIN
            if (errno != EAGAIN) {
                // 断开连接
                conn_disconnect(conn);
            } 
            // 如果错误码是 EAGAIN 且连接状态正常
            else {
                if (conn->connection_ok == true) {
                    // 断开连接
                    conn_disconnect(conn);
                }
            }
        }

        // 如果发送成功且连接成功回调函数未触发
        if (ret > 0 && conn->callback_trigger == false) {
            // 调用连接成功回调函数
            conn->connected(conn->data, conn->fd);

            // 标记连接成功回调函数已触发
            conn->callback_trigger = true;
        }
    }

    // 如果发送成功
    if (ret > 0) {
        // 更新连接的发送字节统计信息
        conn->state.send_bytes += ret;

        // 标记连接状态正常
        conn->connection_ok = true;
    }

    pthread_mutex_unlock(&conn->mtx);

    return ret;
}

/**
 * @brief 清空网络连接的接收缓冲区。
 * 
 * 该函数用于清空指定网络连接的接收缓冲区，确保缓冲区中没有残留数据。
 * 目前仅支持 TCP 客户端连接类型，对于其他连接类型不做处理。
 * 
 * @param conn 指向 neu_conn_t 结构体的指针，表示要清空接收缓冲区的网络连接。
 */
void neu_conn_clear_recv_buffer(neu_conn_t *conn)
{
    // 检查连接是否已建立，如果未建立则直接返回
    if (!conn->is_connected) {
        return;
    }

    // 临时缓冲区，用于存储从接收缓冲区读取的数据
    uint8_t temp_buf[256];

    // 存储 recv 函数的返回值，表示读取的字节数
    ssize_t ret;

    switch (conn->param.type) {
    case NEU_CONN_TCP_CLIENT:
        do {
            // 以非阻塞模式从套接字读取数据到临时缓冲区
            ret = recv(conn->fd, temp_buf, sizeof(temp_buf), MSG_DONTWAIT);

            // 如果读取到数据，则继续循环读取
            if (ret > 0) {
                continue;
            } else if (ret == 0) {
                zlog_info(conn->param.log,
                          "Connection closed while clearing buffer.");
                break;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 如果错误码为 EAGAIN 或 EWOULDBLOCK，表示没有更多数据可读取
                break;
            } else {
                zlog_error(conn->param.log, "Error clearing buffer: %s",
                           strerror(errno));
                break;
            }
        } while (ret > 0);
        break;
    default:
        break;
    }
}

/**
 * @brief 从网络连接中接收数据。
 *
 * 该函数用于从指定的网络连接中接收数据，根据连接的类型（TCP 客户端、UDP、串口客户端等）
 * 选择合适的系统调用（`recv` 或 `read`）来接收数据。在接收数据前后会对连接状态进行检查，
 * 若连接已停止则直接返回；若接收失败则根据不同的连接类型进行相应的错误处理，如记录日志、断开连接等。
 * 若接收成功，则更新连接的接收字节统计信息。
 *
 * @param conn 指向 `neu_conn_t` 结构体的指针，表示要接收数据的网络连接，
 *             该结构体包含连接的参数、状态、回调函数等信息。
 * @param buf 指向用于存储接收到的数据的缓冲区的指针，
 * @param len 要接收的数据的字节数，即期望从连接中读取的数据长度。
 *
 * @return 返回实际接收到的字节数。若接收成功，返回值大于 0；若接收失败，返回值小于等于 0，
 *         此时根据不同的连接类型和错误情况进行相应处理。若连接已停止，返回 0。
 */
ssize_t neu_conn_recv(neu_conn_t *conn, uint8_t *buf, ssize_t len)
{
    // 用于存储实际接收的字节数，初始化为 0
    ssize_t ret = 0;

    pthread_mutex_lock(&conn->mtx);

    // 检查连接是否已停止，如果停止则解锁并返回 0
    if (conn->stop) {
        pthread_mutex_unlock(&conn->mtx);
        return ret;
    }

    // 根据连接类型选择不同的接收方式
    switch (conn->param.type) {
    case NEU_CONN_UDP_TO:
    case NEU_CONN_TCP_SERVER:
        zlog_fatal(conn->param.log, "neu_conn_recv cann't recv tcp server msg");
        assert(1 == 0);
        break;
    case NEU_CONN_TCP_CLIENT:
        if (conn->block) {
            // 阻塞模式下接收数据，等待所有数据接收完成
            ret = recv(conn->fd, buf, len, MSG_WAITALL);
        } else {
            ret = recv(conn->fd, buf, len, 0);
        }
        break;
    case NEU_CONN_UDP:
        ret = recv(conn->fd, buf, len, 0);
        break;
    case NEU_CONN_TTY_CLIENT:
        ret = read(conn->fd, buf, len);
        while (ret > 0 && ret < len) {
            ssize_t rv = read(conn->fd, buf + ret, len - ret);
            if (rv <= 0) {
                ret = rv;
                break;
            }
            
            ret += rv;
        }
        break;
    }

    // 根据不同的连接类型进行错误处理
    if (conn->param.type == NEU_CONN_TTY_CLIENT) {
        if (ret == -1) {
            zlog_error(
                conn->param.log,
                "tty conn fd: %d, recv buf len %zd, ret: %zd, errno: %s(%d)",
                conn->fd, len, ret, strerror(errno), errno);
            conn_disconnect(conn);
        }
    } else if (conn->param.type == NEU_CONN_UDP ||
               conn->param.type == NEU_CONN_UDP_TO) {
        if (ret <= 0) {
            zlog_error(
                conn->param.log,
                "udp conn fd: %d, recv buf len %zd, ret: %zd, errno: %s(%d)",
                conn->fd, len, ret, strerror(errno), errno);
            conn_disconnect(conn);
        }
    } else {
        if (ret <= 0) {
            zlog_error(
                conn->param.log,
                "tcp conn fd: %d, recv buf len %zd, ret: %zd, errno: %s(%d)",
                conn->fd, len, ret, strerror(errno), errno);
            if (ret == 0 || (ret == -1 && errno != EAGAIN)) {
                conn_disconnect(conn);
            }
        }
    }

    pthread_mutex_unlock(&conn->mtx);

    if (ret > 0) {
        conn->state.recv_bytes += ret;
    }

    return ret;
}

ssize_t neu_conn_udp_sendto(neu_conn_t *conn, uint8_t *buf, ssize_t len,
                            void *dst)
{
    ssize_t ret = 0;

    pthread_mutex_lock(&conn->mtx);
    if (conn->stop) {
        pthread_mutex_unlock(&conn->mtx);
        return ret;
    }

    if (!conn->is_connected) {
        conn_connect(conn);
    }

    if (conn->is_connected) {
        switch (conn->param.type) {
        case NEU_CONN_TCP_CLIENT:
        case NEU_CONN_TCP_SERVER:
        case NEU_CONN_TTY_CLIENT:
        case NEU_CONN_UDP:
            assert(false);
            break;
        case NEU_CONN_UDP_TO:
            if (conn->block) {
                ret = sendto(conn->fd, buf, len, MSG_NOSIGNAL,
                             (struct sockaddr *) dst, sizeof(struct sockaddr));
            } else {
                ret = sendto(conn->fd, buf, len, MSG_NOSIGNAL | MSG_DONTWAIT,
                             (struct sockaddr *) dst, sizeof(struct sockaddr));
            }
            break;
        }
        if (ret != len) {
            zlog_error(
                conn->param.log,
                "conn udp fd: %d, sendto (%s:%d) buf len: %zd, ret: %zd, "
                "errno: %s(%d)",
                conn->fd, inet_ntoa(((struct sockaddr_in *) dst)->sin_addr),
                htons(((struct sockaddr_in *) dst)->sin_port), len, ret,
                strerror(errno), errno);
        }

        if (ret == -1 && errno != EAGAIN) {
            conn_disconnect(conn);
        }

        if (ret > 0 && conn->callback_trigger == false) {
            conn->connected(conn->data, conn->fd);
            conn->callback_trigger = true;
        }
    }

    if (ret > 0) {
        conn->state.send_bytes += ret;
    }

    pthread_mutex_unlock(&conn->mtx);

    return ret;
}

ssize_t neu_conn_udp_recvfrom(neu_conn_t *conn, uint8_t *buf, ssize_t len,
                              void *src)
{
    ssize_t   ret      = 0;
    socklen_t addr_len = sizeof(struct sockaddr_in);

    pthread_mutex_lock(&conn->mtx);
    if (conn->stop) {
        pthread_mutex_unlock(&conn->mtx);
        return ret;
    }

    switch (conn->param.type) {
    case NEU_CONN_TCP_SERVER:
    case NEU_CONN_TCP_CLIENT:
    case NEU_CONN_TTY_CLIENT:
    case NEU_CONN_UDP:
        assert(1 == 0);
        break;
    case NEU_CONN_UDP_TO:
        ret =
            recvfrom(conn->fd, buf, len, 0, (struct sockaddr *) src, &addr_len);
        break;
    }
    if (ret <= 0) {
        zlog_error(conn->param.log,
                   "conn udp fd: %d, recv buf len %zd, ret: %zd, errno: %s(%d)",
                   conn->fd, len, ret, strerror(errno), errno);
        if (ret == 0 || (ret == -1 && errno != EAGAIN)) {
            conn_disconnect(conn);
        }
    }

    pthread_mutex_unlock(&conn->mtx);

    if (ret > 0) {
        conn->state.recv_bytes += ret;
    }

    return ret;
}

void neu_conn_connect(neu_conn_t *conn)
{
    pthread_mutex_lock(&conn->mtx);
    conn_connect(conn);
    pthread_mutex_unlock(&conn->mtx);
}

int neu_conn_fd(neu_conn_t *conn)
{
    int fd = -1;
    pthread_mutex_lock(&conn->mtx);
    fd = conn->fd;
    pthread_mutex_unlock(&conn->mtx);
    return fd;
}

void neu_conn_disconnect(neu_conn_t *conn)
{
    pthread_mutex_lock(&conn->mtx);
    conn_disconnect(conn);
    pthread_mutex_unlock(&conn->mtx);
}

static void conn_free_param(neu_conn_t *conn)
{
    switch (conn->param.type) {
    case NEU_CONN_TCP_SERVER:
        free(conn->param.params.tcp_server.ip);
        free(conn->tcp_server.clients);
        conn->tcp_server.n_client = 0;
        break;
    case NEU_CONN_TCP_CLIENT:
        free(conn->param.params.tcp_client.ip);
        break;
    case NEU_CONN_UDP:
        free(conn->param.params.udp.src_ip);
        free(conn->param.params.udp.dst_ip);
        break;
    case NEU_CONN_UDP_TO:
        free(conn->param.params.udpto.src_ip);
        break;
    case NEU_CONN_TTY_CLIENT:
        free(conn->param.params.tty_client.device);
        break;
    }
}

static void conn_init_param(neu_conn_t *conn, neu_conn_param_t *param)
{
    conn->param.type = param->type;
    conn->param.log  = param->log;

    switch (param->type) {
    case NEU_CONN_TCP_SERVER:
        conn->param.params.tcp_server.ip = strdup(param->params.tcp_server.ip);
        conn->param.params.tcp_server.port = param->params.tcp_server.port;
        conn->param.params.tcp_server.timeout =
            param->params.tcp_server.timeout;
        conn->param.params.tcp_server.max_link =
            param->params.tcp_server.max_link;
        conn->param.params.tcp_server.start_listen =
            param->params.tcp_server.start_listen;
        conn->param.params.tcp_server.stop_listen =
            param->params.tcp_server.stop_listen;
        conn->tcp_server.clients = calloc(
            conn->param.params.tcp_server.max_link, sizeof(struct tcp_client));
        if (conn->param.params.tcp_server.timeout > 0) {
            conn->block = true;
        } else {
            conn->block = false;
        }
        break;
    case NEU_CONN_TCP_CLIENT:
        conn->param.params.tcp_client.ip = strdup(param->params.tcp_client.ip);
        conn->param.params.tcp_client.port = param->params.tcp_client.port;
        conn->param.params.tcp_client.timeout =
            param->params.tcp_client.timeout;
        conn->block = conn->param.params.tcp_client.timeout > 0;
        break;
    case NEU_CONN_UDP:
        conn->param.params.udp.src_ip   = strdup(param->params.udp.src_ip);
        conn->param.params.udp.src_port = param->params.udp.src_port;
        conn->param.params.udp.dst_ip   = strdup(param->params.udp.dst_ip);
        conn->param.params.udp.dst_port = param->params.udp.dst_port;
        conn->param.params.udp.timeout  = param->params.udp.timeout;
        conn->block                     = conn->param.params.udp.timeout > 0;
        break;
    case NEU_CONN_UDP_TO:
        conn->param.params.udpto.src_ip   = strdup(param->params.udpto.src_ip);
        conn->param.params.udpto.src_port = param->params.udpto.src_port;
        conn->param.params.udpto.timeout  = param->params.udpto.timeout;
        conn->block = conn->param.params.udpto.timeout > 0;
        break;
    case NEU_CONN_TTY_CLIENT:
        conn->param.params.tty_client.device =
            strdup(param->params.tty_client.device);
        conn->param.params.tty_client.data   = param->params.tty_client.data;
        conn->param.params.tty_client.stop   = param->params.tty_client.stop;
        conn->param.params.tty_client.baud   = param->params.tty_client.baud;
        conn->param.params.tty_client.parity = param->params.tty_client.parity;
        conn->param.params.tty_client.flow   = param->params.tty_client.flow;
        conn->param.params.tty_client.timeout =
            param->params.tty_client.timeout;
        conn->block = conn->param.params.tty_client.timeout > 0;
        break;
    }
}

/**
 * @brief 启动 TCP 服务器监听。
 *
 * 该函数用于检查 TCP 服务器连接是否已经开始监听，如果未监听，
 * 则根据配置的 IP 地址类型（IPv4 或 IPv6）创建套接字、绑定
 * 地址和端口，并开始监听客户端连接请求。如果监听成功，会更新
 * 连接的相关状态，调用开始监听的回调函数，并记录日志。
 *
 * @param conn 指向 neu_conn_t 结构体的指针，表示 TCP 服务器连接。
 */
static void conn_tcp_server_listen(neu_conn_t *conn)
{
    // 检查连接类型是否为 TCP 服务器，并且服务器是否未处于监听状态
    if (conn->param.type == NEU_CONN_TCP_SERVER &&
        conn->tcp_server.is_listen == false) {
        int fd = -1, ret = 0;

        // 检查配置的 IP 地址是否为 IPv4 地址
        if (is_ipv4(conn->param.params.tcp_server.ip)) {
            // 定义 IPv4 本地地址结构体
            struct sockaddr_in local = {
                .sin_family      = AF_INET,
                .sin_port        = htons(conn->param.params.tcp_server.port),
                .sin_addr.s_addr = inet_addr(conn->param.params.tcp_server.ip),
            };

            // 创建一个非阻塞的 TCP 套接字
            fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);

            // 将套接字绑定到本地地址和端口
            ret = bind(fd, (struct sockaddr *) &local, sizeof(local));
        } else if (is_ipv6(conn->param.params.tcp_server.ip)) {
            // 定义 IPv6 本地地址结构体
            struct sockaddr_in6 local = { 0 };

            // 地址族为 IPv6
            local.sin6_family         = AF_INET6;

            // 将端口号从主机字节序转换为网络字节序
            local.sin6_port = htons(conn->param.params.tcp_server.port);

            // 将 IPv6 地址从文本形式转换为二进制形式
            inet_pton(AF_INET6, conn->param.params.tcp_server.ip,
                      &local.sin6_addr);

            // 创建一个非阻塞的 TCP 套接字
            fd = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);

            // 将套接字绑定到本地地址和端口
            ret = bind(fd, (struct sockaddr *) &local, sizeof(local));
        } else {
            // 如果 IP 地址既不是 IPv4 也不是 IPv6
            zlog_error(conn->param.log, "invalid ip: %s",
                       conn->param.params.tcp_server.ip);
            return;
        }

        // 如果绑定操作失败
        if (ret != 0) {
            // 关闭套接字
            close(fd);

            // 记录错误日志，提示绑定失败及错误信息
            zlog_error(conn->param.log, "tcp bind %s:%d fail, errno: %s",
                       conn->param.params.tcp_server.ip,
                       conn->param.params.tcp_server.port, strerror(errno));
            return;
        }

        // 开始监听客户端连接请求，最大允许 1 个未处理的连接请求
        ret = listen(fd, 1);

        // 如果监听操作失败
        if (ret != 0) {
            close(fd);
            zlog_error(conn->param.log, "tcp bind %s:%d fail, errno: %s",
                       conn->param.params.tcp_server.ip,
                       conn->param.params.tcp_server.port, strerror(errno));
            return;
        }

        // 将监听套接字的文件描述符赋值给连接结构体
        conn->fd                   = fd;

        // 标记服务器已开始监听
        conn->tcp_server.is_listen = true;

        // 调用开始监听的回调函数，通知相关模块服务器已开始监听
        conn->param.params.tcp_server.start_listen(conn->data, fd);

        // 记录通知日志，提示服务器监听成功及相关信息
        zlog_notice(conn->param.log, "tcp server listen %s:%d success, fd: %d",
                    conn->param.params.tcp_server.ip,
                    conn->param.params.tcp_server.port, fd);
    }
}

static void conn_tcp_server_stop(neu_conn_t *conn)
{
    if (conn->param.type == NEU_CONN_TCP_SERVER &&
        conn->tcp_server.is_listen == true) {
        zlog_notice(conn->param.log, "tcp server close %s:%d, fd: %d",
                    conn->param.params.tcp_server.ip,
                    conn->param.params.tcp_server.port, conn->fd);

        conn->param.params.tcp_server.stop_listen(conn->data, conn->fd);

        for (int i = 0; i < conn->param.params.tcp_server.max_link; i++) {
            if (conn->tcp_server.clients[i].fd > 0) {
                close(conn->tcp_server.clients[i].fd);
                conn->tcp_server.clients[i].fd = 0;
            }
        }

        if (conn->fd > 0) {
            close(conn->fd);
            conn->fd = 0;
        }

        conn->tcp_server.n_client  = 0;
        conn->tcp_server.is_listen = false;
    }
}

static void conn_connect(neu_conn_t *conn)
{
    int ret = 0;
    int fd  = 0;

    switch (conn->param.type) {
    case NEU_CONN_TCP_SERVER:
        break;
    case NEU_CONN_TCP_CLIENT: {
        if (conn->block) {
            struct timeval tv = {
                .tv_sec = conn->param.params.tcp_client.timeout / 1000,
                .tv_usec =
                    (conn->param.params.tcp_client.timeout % 1000) * 1000,
            };

            if (is_ipv4(conn->param.params.tcp_client.ip)) {
                fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            } else {
                fd = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
            }

            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        } else {
            if (is_ipv4(conn->param.params.tcp_client.ip)) {
                fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
            } else {
                fd = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
            }
        }

        if (is_ipv4(conn->param.params.tcp_client.ip)) {
            struct sockaddr_in remote = {
                .sin_family      = AF_INET,
                .sin_port        = htons(conn->param.params.tcp_client.port),
                .sin_addr.s_addr = inet_addr(conn->param.params.tcp_client.ip),
            };

            ret = connect(fd, (struct sockaddr *) &remote,
                          sizeof(struct sockaddr_in));
        } else if (is_ipv6(conn->param.params.tcp_client.ip)) {
            struct sockaddr_in6 remote_ip6 = { 0 };
            remote_ip6.sin6_family         = AF_INET6;
            remote_ip6.sin6_port = htons(conn->param.params.tcp_client.port);
            inet_pton(AF_INET6, conn->param.params.tcp_client.ip,
                      &remote_ip6.sin6_addr);

            ret = connect(fd, (struct sockaddr *) &remote_ip6,
                          sizeof(remote_ip6));
        } else {
            zlog_error(conn->param.log, "invalid ip: %s",
                       conn->param.params.tcp_server.ip);
            return;
        }

        if ((conn->block && ret == 0) ||
            (!conn->block && ret != 0 && errno == EINPROGRESS)) {
            zlog_notice(conn->param.log, "connect %s:%d success",
                        conn->param.params.tcp_client.ip,
                        conn->param.params.tcp_client.port);
            conn->is_connected = true;
            conn->fd           = fd;
        } else {
            close(fd);
            zlog_error(conn->param.log, "connect %s:%d error: %s(%d)",
                       conn->param.params.tcp_client.ip,
                       conn->param.params.tcp_client.port, strerror(errno),
                       errno);
            conn->is_connected = false;
            return;
        }

        break;
    }
    case NEU_CONN_UDP: {
        if (conn->block) {
            struct timeval tv = {
                .tv_sec  = conn->param.params.udp.timeout / 1000,
                .tv_usec = (conn->param.params.udp.timeout % 1000) * 1000,
            };

            if (is_ipv4(conn->param.params.udp.dst_ip)) {
                fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            } else {
                fd = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
            }

            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        } else {
            if (is_ipv4(conn->param.params.udp.dst_ip)) {
                fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP);
            } else {
                fd = socket(AF_INET6, SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP);
            }
        }

        int so_broadcast = 1;
        setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &so_broadcast,
                   sizeof(so_broadcast));

        if (is_ipv4(conn->param.params.udp.src_ip)) {
            struct sockaddr_in local = {
                .sin_family      = AF_INET,
                .sin_port        = htons(conn->param.params.udp.src_port),
                .sin_addr.s_addr = inet_addr(conn->param.params.udp.src_ip),
            };

            ret = bind(fd, (struct sockaddr *) &local,
                       sizeof(struct sockaddr_in));
        } else if (is_ipv6(conn->param.params.udp.src_ip)) {
            struct sockaddr_in6 local = { 0 };
            local.sin6_family         = AF_INET6;
            local.sin6_port           = htons(conn->param.params.udp.src_port);
            inet_pton(AF_INET6, conn->param.params.udp.src_ip,
                      &local.sin6_addr);

            ret = bind(fd, (struct sockaddr *) &local,
                       sizeof(struct sockaddr_in6));
        } else {
            zlog_error(conn->param.log, "invalid ip: %s",
                       conn->param.params.tcp_server.ip);
            return;
        }

        if (ret != 0) {
            close(fd);
            zlog_error(conn->param.log, "bind %s:%d error: %s(%d)",
                       conn->param.params.udp.src_ip,
                       conn->param.params.udp.src_port, strerror(errno), errno);
            conn->is_connected = false;
            return;
        }

        if (is_ipv4(conn->param.params.udp.dst_ip)) {
            struct sockaddr_in remote = {
                .sin_family      = AF_INET,
                .sin_port        = htons(conn->param.params.udp.dst_port),
                .sin_addr.s_addr = inet_addr(conn->param.params.udp.dst_ip),
            };
            ret = connect(fd, (struct sockaddr *) &remote,
                          sizeof(struct sockaddr_in));
        } else if (is_ipv6(conn->param.params.udp.dst_ip)) {
            struct sockaddr_in6 remote = { 0 };
            remote.sin6_family         = AF_INET6;
            remote.sin6_port           = htons(conn->param.params.udp.dst_port);
            inet_pton(AF_INET6, conn->param.params.udp.dst_ip,
                      &remote.sin6_addr);

            ret = connect(fd, (struct sockaddr *) &remote,
                          sizeof(struct sockaddr_in6));
        } else {
            zlog_error(conn->param.log, "invalid ip: %s",
                       conn->param.params.tcp_server.ip);
            return;
        }

        if (ret != 0 && errno != EINPROGRESS) {
            close(fd);
            zlog_error(conn->param.log, "connect %s:%d error: %s(%d)",
                       conn->param.params.udp.dst_ip,
                       conn->param.params.udp.dst_port, strerror(errno), errno);
            conn->is_connected = false;
            return;
        } else {
            zlog_notice(conn->param.log, "connect %s:%d success",
                        conn->param.params.udp.dst_ip,
                        conn->param.params.udp.dst_port);
            conn->is_connected = true;
            conn->fd           = fd;
        }
        break;
    }
    case NEU_CONN_UDP_TO: {
        if (conn->block) {
            struct timeval tv = {
                .tv_sec  = conn->param.params.udpto.timeout / 1000,
                .tv_usec = (conn->param.params.udpto.timeout % 1000) * 1000,
            };

            if (is_ipv4(conn->param.params.udpto.src_ip)) {
                fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            } else {
                fd = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
            }
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        } else {
            if (is_ipv4(conn->param.params.udpto.src_ip)) {
                fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP);
            } else {
                fd = socket(AF_INET6, SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP);
            }
        }
        int so_broadcast = 1;
        setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &so_broadcast,
                   sizeof(so_broadcast));

        if (is_ipv4(conn->param.params.udpto.src_ip)) {
            fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP);

            struct sockaddr_in local = {
                .sin_family      = AF_INET,
                .sin_port        = htons(conn->param.params.udpto.src_port),
                .sin_addr.s_addr = inet_addr(conn->param.params.udpto.src_ip),
            };

            ret = bind(fd, (struct sockaddr *) &local,
                       sizeof(struct sockaddr_in));
        } else if (is_ipv6(conn->param.params.udp.dst_ip)) {
            fd = socket(AF_INET6, SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP);

            struct sockaddr_in6 local = { 0 };
            local.sin6_family         = AF_INET6;
            local.sin6_port = htons(conn->param.params.udpto.src_port);

            inet_pton(AF_INET6, conn->param.params.udpto.src_ip,
                      &local.sin6_addr);

            ret = bind(fd, (struct sockaddr *) &local,
                       sizeof(struct sockaddr_in6));
        } else {
            zlog_error(conn->param.log, "invalid ip: %s",
                       conn->param.params.tcp_server.ip);
            return;
        }

        if (ret != 0) {
            close(fd);
            zlog_error(conn->param.log, "bind %s:%d error: %s(%d)",
                       conn->param.params.udpto.src_ip,
                       conn->param.params.udpto.src_port, strerror(errno),
                       errno);
            conn->is_connected = false;
            return;
        }

        conn->is_connected = true;
        conn->fd           = fd;

        break;
    }
    case NEU_CONN_TTY_CLIENT: {
        struct termios tty_opt = { 0 };
#ifdef NEU_SMART_LINK
#include "connection/neu_smart_link.h"
        ret =
            neu_conn_smart_link_auto_set(conn->param.params.tty_client.device);
        zlog_notice(conn->param.log, "smart link ret: %d", ret);
        if (ret > 0) {
            fd = ret;
        }
#else
        fd = open(conn->param.params.tty_client.device, O_RDWR | O_NOCTTY, 0);
#endif
        if (fd <= 0) {
            zlog_error(conn->param.log, "open %s error: %s(%d)",
                       conn->param.params.tty_client.device, strerror(errno),
                       errno);
            return;
        }

        tcgetattr(fd, &tty_opt);
        tty_opt.c_cc[VTIME] = conn->param.params.tty_client.timeout / 100;
        tty_opt.c_cc[VMIN]  = 0;

        switch (conn->param.params.tty_client.flow) {
        case NEU_CONN_TTYP_FLOW_DISABLE:
            tty_opt.c_cflag &= ~CRTSCTS;
            break;
        case NEU_CONN_TTYP_FLOW_ENABLE:
            tty_opt.c_cflag |= CRTSCTS;
            break;
        }

        switch (conn->param.params.tty_client.baud) {
        case NEU_CONN_TTY_BAUD_115200:
            cfsetospeed(&tty_opt, B115200);
            cfsetispeed(&tty_opt, B115200);
            break;
        case NEU_CONN_TTY_BAUD_57600:
            cfsetospeed(&tty_opt, B57600);
            cfsetispeed(&tty_opt, B57600);
            break;
        case NEU_CONN_TTY_BAUD_38400:
            cfsetospeed(&tty_opt, B38400);
            cfsetispeed(&tty_opt, B38400);
            break;
        case NEU_CONN_TTY_BAUD_19200:
            cfsetospeed(&tty_opt, B19200);
            cfsetispeed(&tty_opt, B19200);
            break;
        case NEU_CONN_TTY_BAUD_9600:
            cfsetospeed(&tty_opt, B9600);
            cfsetispeed(&tty_opt, B9600);
            break;
        case NEU_CONN_TTY_BAUD_4800:
            cfsetospeed(&tty_opt, B4800);
            cfsetispeed(&tty_opt, B4800);
            break;
        case NEU_CONN_TTY_BAUD_2400:
            cfsetospeed(&tty_opt, B2400);
            cfsetispeed(&tty_opt, B2400);
            break;
        case NEU_CONN_TTY_BAUD_1800:
            cfsetospeed(&tty_opt, B1800);
            cfsetispeed(&tty_opt, B1800);
            break;
        case NEU_CONN_TTY_BAUD_1200:
            cfsetospeed(&tty_opt, B1200);
            cfsetispeed(&tty_opt, B1200);
            break;
        case NEU_CONN_TTY_BAUD_600:
            cfsetospeed(&tty_opt, B600);
            cfsetispeed(&tty_opt, B600);
            break;
        case NEU_CONN_TTY_BAUD_300:
            cfsetospeed(&tty_opt, B300);
            cfsetispeed(&tty_opt, B300);
            break;
        case NEU_CONN_TTY_BAUD_200:
            cfsetospeed(&tty_opt, B200);
            cfsetispeed(&tty_opt, B200);
            break;
        case NEU_CONN_TTY_BAUD_150:
            cfsetospeed(&tty_opt, B150);
            cfsetispeed(&tty_opt, B150);
            break;
        }

        switch (conn->param.params.tty_client.parity) {
        case NEU_CONN_TTY_PARITY_NONE:
            tty_opt.c_cflag &= ~PARENB;
            break;
        case NEU_CONN_TTY_PARITY_ODD:
            tty_opt.c_cflag |= PARENB;
            tty_opt.c_cflag |= PARODD;
            tty_opt.c_iflag = INPCK;
            break;
        case NEU_CONN_TTY_PARITY_EVEN:
            tty_opt.c_cflag |= PARENB;
            tty_opt.c_cflag &= ~PARODD;
            tty_opt.c_iflag = INPCK;
            break;
        case NEU_CONN_TTY_PARITY_MARK:
            tty_opt.c_cflag |= PARENB;
            tty_opt.c_cflag |= PARODD;
            tty_opt.c_cflag |= CMSPAR;
            tty_opt.c_iflag = INPCK;
            break;
        case NEU_CONN_TTY_PARITY_SPACE:
            tty_opt.c_cflag |= PARENB;
            tty_opt.c_cflag |= CMSPAR;
            tty_opt.c_cflag &= ~PARODD;
            tty_opt.c_iflag = INPCK;
            break;
        }

        tty_opt.c_cflag &= ~CSIZE;
        switch (conn->param.params.tty_client.data) {
        case NEU_CONN_TTY_DATA_5:
            tty_opt.c_cflag |= CS5;
            break;
        case NEU_CONN_TTY_DATA_6:
            tty_opt.c_cflag |= CS6;
            break;
        case NEU_CONN_TTY_DATA_7:
            tty_opt.c_cflag |= CS7;
            break;
        case NEU_CONN_TTY_DATA_8:
            tty_opt.c_cflag |= CS8;
            break;
        }

        switch (conn->param.params.tty_client.stop) {
        case NEU_CONN_TTY_STOP_1:
            tty_opt.c_cflag &= ~CSTOPB;
            break;
        case NEU_CONN_TTY_STOP_2:
            tty_opt.c_cflag |= CSTOPB;
            break;
        }

        tty_opt.c_cflag |= CREAD | CLOCAL;
        tty_opt.c_lflag &= ~ICANON;
        tty_opt.c_lflag &= ~ECHO;
        tty_opt.c_lflag &= ~ECHOE;
        tty_opt.c_lflag &= ~ECHONL;
        tty_opt.c_lflag &= ~ISIG;

        tty_opt.c_iflag &= ~(IXON | IXOFF | IXANY);
        tty_opt.c_iflag &=
            ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
        tty_opt.c_oflag &= ~OPOST;
        tty_opt.c_oflag &= ~ONLCR;

        tcflush(fd, TCIOFLUSH);
        tcsetattr(fd, TCSANOW, &tty_opt);

        conn->fd           = fd;
        conn->is_connected = true;
        zlog_notice(conn->param.log, "open %s success, fd: %d",
                    conn->param.params.tty_client.device, fd);
        break;
    }
    }
}

static void conn_disconnect(neu_conn_t *conn)
{
    conn->is_connected  = false;
    conn->connection_ok = false;
    if (conn->callback_trigger == true) {
        conn->disconnected(conn->data, conn->fd);
        conn->callback_trigger = false;
    }
    conn->offset = 0;
    memset(conn->buf, 0, conn->buf_size);

    switch (conn->param.type) {
    case NEU_CONN_TCP_SERVER:
        for (int i = 0; i < conn->param.params.tcp_server.max_link; i++) {
            if (conn->tcp_server.clients[i].fd > 0) {
                close(conn->tcp_server.clients[i].fd);
                conn->tcp_server.clients[i].fd = 0;
            }
        }
        conn->tcp_server.n_client = 0;
        break;
    case NEU_CONN_TCP_CLIENT:
    case NEU_CONN_UDP:
    case NEU_CONN_UDP_TO:
    case NEU_CONN_TTY_CLIENT:
        if (conn->fd > 0) {
            close(conn->fd);
            conn->fd = 0;
        }
        break;
    }
}

static void conn_tcp_server_add_client(neu_conn_t *conn, int fd,
                                       struct sockaddr_in client)
{
    for (int i = 0; i < conn->param.params.tcp_server.max_link; i++) {
        if (conn->tcp_server.clients[i].fd == 0) {
            conn->tcp_server.clients[i].fd     = fd;
            conn->tcp_server.clients[i].client = client;
            conn->tcp_server.n_client += 1;
            return;
        }
    }

    assert(1 == 0);
    return;
}

/**
 * @brief 从 TCP 服务器的客户端列表中移除指定文件描述符对应的客户端。
 *
 * 该函数用于在 TCP 服务器中，根据传入的文件描述符找到对应的客户端，
 * 并将其从客户端列表中移除。移除操作包括关闭客户端的套接字连接，
 * 将该客户端在列表中的文件描述符置为 0，并减少客户端数量计数。
 *
 * @param conn 表示 TCP 服务器连接。
 * @param fd 要移除的客户端的文件描述符。该文件描述符用于唯一标识一个客户端连接。
 */
static void conn_tcp_server_del_client(neu_conn_t *conn, int fd)
{
    // 遍历客户端列表，最大遍历次数为服务器允许的最大连接数
    for (int i = 0; i < conn->param.params.tcp_server.max_link; i++) {
        // 检查文件描述符是否有效（大于 0），并且当前客户端的文件描述符与传入的文件描述符匹配
        if (fd > 0 && conn->tcp_server.clients[i].fd == fd) {
            // 关闭客户端的套接字连接
            close(fd);

            // 将该客户端在列表中的文件描述符置为 0，表示该位置空闲
            conn->tcp_server.clients[i].fd = 0;

            // 减少客户端数量计数
            conn->tcp_server.n_client -= 1;

            // 找到并移除客户端后，直接返回
            return;
        }
    }
}

static int conn_tcp_server_replace_client(neu_conn_t *conn, int fd,
                                          struct sockaddr_in client)
{
    for (int i = 0; i < conn->param.params.tcp_server.max_link; i++) {
        if (conn->tcp_server.clients[i].fd > 0) {
            int ret = conn->tcp_server.clients[i].fd;

            close(conn->tcp_server.clients[i].fd);
            conn->tcp_server.clients[i].fd = 0;

            conn->tcp_server.clients[i].fd     = fd;
            conn->tcp_server.clients[i].client = client;
            return ret;
        }
    }

    return 0;
}

int neu_conn_stream_consume(neu_conn_t *conn, void *context,
                            neu_conn_stream_consume_fn fn)
{
    ssize_t ret = neu_conn_recv(conn, conn->buf + conn->offset,
                                conn->buf_size - conn->offset);
    if (ret > 0) {
        zlog_recv_protocol(conn->param.log, conn->buf + conn->offset, ret);
        conn->offset += ret;
        neu_protocol_unpack_buf_t protocol_buf = { 0 };
        neu_protocol_unpack_buf_init(&protocol_buf, conn->buf, conn->offset);
        while (neu_protocol_unpack_buf_unused_size(&protocol_buf) > 0) {
            int used = fn(context, &protocol_buf);

            zlog_debug(conn->param.log, "buf used: %d offset: %d", used,
                       conn->offset);
            if (used == 0) {
                break;
            } else if (used == -1) {
                neu_conn_disconnect(conn);
                break;
            } else {
                pthread_mutex_lock(&conn->mtx);
                if (conn->offset - used < 0) {
                    pthread_mutex_unlock(&conn->mtx);
                    zlog_warn(conn->param.log, "reset offset: %d, used: %d",
                              conn->offset, used);
                    return -1;
                }
                conn->offset -= used;
                memmove(conn->buf, conn->buf + used, conn->offset);
                neu_protocol_unpack_buf_init(&protocol_buf, conn->buf,
                                             conn->offset);
                pthread_mutex_unlock(&conn->mtx);
            }
        }
    }

    return ret;
}

int neu_conn_stream_tcp_server_consume(neu_conn_t *conn, int fd, void *context,
                                       neu_conn_stream_consume_fn fn)
{
    ssize_t ret = neu_conn_tcp_server_recv(conn, fd, conn->buf + conn->offset,
                                           conn->buf_size - conn->offset);
    if (ret > 0) {
        conn->offset += ret;
        neu_protocol_unpack_buf_t protocol_buf = { 0 };
        neu_protocol_unpack_buf_init(&protocol_buf, conn->buf, conn->offset);
        while (neu_protocol_unpack_buf_unused_size(&protocol_buf) > 0) {
            int used = fn(context, &protocol_buf);

            if (used == 0) {
                break;
            } else if (used == -1) {
                neu_conn_tcp_server_close_client(conn, fd);
                break;
            } else {
                pthread_mutex_lock(&conn->mtx);
                if (conn->offset - used < 0) {
                    pthread_mutex_unlock(&conn->mtx);
                    zlog_warn(conn->param.log, "reset offset: %d, used: %d",
                              conn->offset, used);
                    return -1;
                }
                conn->offset -= used;
                memmove(conn->buf, conn->buf + used, conn->offset);
                neu_protocol_unpack_buf_init(&protocol_buf, conn->buf,
                                             conn->offset);
                pthread_mutex_unlock(&conn->mtx);
            }
        }
    }
    return ret;
}

int neu_conn_wait_msg(neu_conn_t *conn, void *context, uint16_t n_byte,
                      neu_conn_process_msg fn)
{
    ssize_t                   ret  = neu_conn_recv(conn, conn->buf, n_byte);
    neu_protocol_unpack_buf_t pbuf = { 0 };
    conn->offset                   = 0;

    while (ret > 0) {
        zlog_recv_protocol(conn->param.log, conn->buf + conn->offset, ret);
        conn->offset += ret;
        neu_protocol_unpack_buf_init(&pbuf, conn->buf, conn->offset);
        neu_buf_result_t result = fn(context, &pbuf);
        if (result.need > 0) {
            if (result.need <= conn->buf_size - conn->offset) {
                ret =
                    neu_conn_recv(conn, conn->buf + conn->offset, result.need);
            } else {
                zlog_error(conn->param.log,
                           "no enough recv buf, need: %" PRIu16
                           " , buf size: %" PRIu16 " offset: %" PRIu16,
                           result.need, conn->buf_size, conn->offset);
                return -1;
            }
        } else if (result.need == 0) {
            return result.used;
        } else {
            neu_conn_disconnect(conn);
            return result.used;
        }
    }

    return ret;
}

int neu_conn_tcp_server_wait_msg(neu_conn_t *conn, int fd, void *context,
                                 uint16_t n_byte, neu_conn_process_msg fn)
{
    ssize_t ret = neu_conn_tcp_server_recv(conn, fd, conn->buf, n_byte);
    neu_protocol_unpack_buf_t pbuf = { 0 };
    conn->offset                   = 0;

    while (ret > 0) {
        zlog_recv_protocol(conn->param.log, conn->buf + conn->offset, ret);
        conn->offset += ret;
        neu_protocol_unpack_buf_init(&pbuf, conn->buf, conn->offset);
        neu_buf_result_t result = fn(context, &pbuf);
        if (result.need > 0) {
            assert(result.need <= conn->buf_size - conn->offset);
            if (result.need <= conn->buf_size - conn->offset) {
                ret = neu_conn_tcp_server_recv(
                    conn, fd, conn->buf + conn->offset, result.need);
            } else {
                zlog_error(conn->param.log,
                           "no enough recv buf, need: %" PRIu16
                           " , buf size: %" PRIu16 " offset: %" PRIu16,
                           result.need, conn->buf_size, conn->offset);
                return -1;
            }
        } else if (result.need == 0) {
            return result.used;
        } else {
            neu_conn_tcp_server_close_client(conn, fd);
            return result.used;
        }
    }

    return ret;
}

int is_ipv4(const char *ip)
{
    struct sockaddr_in sa;
    return inet_pton(AF_INET, ip, &(sa.sin_addr)) != 0;
}

int is_ipv6(const char *ip)
{
    struct sockaddr_in6 sa;
    return inet_pton(AF_INET6, ip, &(sa.sin6_addr)) != 0;
}

bool neu_conn_is_connected(neu_conn_t *conn)
{
    return conn->is_connected;
}