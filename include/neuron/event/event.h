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

#ifndef NEURON_EVENT_H
#define NEURON_EVENT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct neu_events neu_events_t;

/**
 * @brief Creat a new event.
 * When an event is created, a corresponding thread is created, and both
 * io_event and timer_event in this event are scheduled for processing in this
 * thread.
 * @return the newly created event.
 */
neu_events_t *neu_event_new(void);

/**
 * @brief Close a event.
 *
 * @param[in] events
 * @return 0 on successful shutdown.
 */
int neu_event_close(neu_events_t *events);

typedef struct neu_event_timer neu_event_timer_t;
typedef int (*neu_event_timer_callback)(void *usr_data);

/**
 * @brief 定义了事件定时器类型的枚举。
 *
 * 此枚举用于表示不同类型的定时器行为，如阻塞或非阻塞。
 * 这些类型通常被用在需要定时器机制的场合，以区分不同的定时器操作模式。
 */
typedef enum neu_event_timer_type {
    /**
     * @brief 阻塞定时器。
     *
     * 当设置为这种类型时，定时器操作将处于阻塞模式，
     * 意味着直到定时器触发或者相关条件满足前，程序不会继续执行后续代码。
     */
    NEU_EVENT_TIMER_BLOCK = 0, ///< 阻塞定时器

    /**
     * @brief 非阻塞定时器。
     *
     * 当设置为这种类型时，定时器操作将处于非阻塞模式，
     * 程序将继续执行后续代码而不等待定时器触发。
     */
    NEU_EVENT_TIMER_NOBLOCK = 1, ///< 非阻塞定时器
} neu_event_timer_type_e;

/**
 * @brief 定时器参数结构体，包含创建定时器时所需的信息。
 *
 * 该结构体用于初始化定时器，指定其触发周期、用户数据、回调函数及定时器类型。
 */
typedef struct neu_event_timer_param {
    /**
     * @brief 定时器触发周期的秒部分。
     *
     * 表示定时器触发的时间间隔中的秒数部分。
     */
    int64_t second;

    /**
     * @brief 定时器触发周期的毫秒部分。
     *
     * 表示定时器触发的时间间隔中的毫秒数部分。与 `second` 结合使用来精确控制定时器的触发时间。
     */
    int64_t millisecond;

    /**
     * @brief 用户数据指针。
     *
     * 当定时器触发时，此指针指向的数据会被传递给回调函数。可用于向回调函数传递任意信息。
     */
    void *usr_data;

    /**
     * @brief 定时器触发时调用的回调函数。
     *
     * 每当定时器到期时，将调用此回调函数，并传入 `usr_data` 作为参数。
     */
    neu_event_timer_callback cb;

    /**
     * @brief 定时器类型。
     *
     * 定义了定时器的具体类型，如阻塞和非阻塞定时器。
     */
    neu_event_timer_type_e type;
} neu_event_timer_param_t;

/**
 * @brief Add a timer to the event.
 *
 * @param[in] events
 * @param[in] timer Parameters when creating timer.
 * @return The added event timer.
 */
neu_event_timer_t *neu_event_add_timer(neu_events_t *          events,
                                       neu_event_timer_param_t timer);

/**
 * @brief Remove timer from event.
 *
 * @param[in] events
 * @param[in] timer
 * @return 0 on success.
 */
int neu_event_del_timer(neu_events_t *events, neu_event_timer_t *timer);

/**
 * @brief 定义了事件I/O类型的枚举。
 *
 * 此枚举用于表示可能发生的不同类型的I/O事件，如读取、连接关闭或挂起等。
 * 这些类型通常被用在事件驱动的编程模型中，以区分不同的事件源。
 */
enum neu_event_io_type {
    /**
     * @brief 读取事件。
     *
     * 当文件描述符准备好进行读取操作时触发。
     */
    NEU_EVENT_IO_READ = 0x1, // 读取事件

    /**
     * @brief 连接关闭事件。
     *
     * 当文件描述符对应的连接被远程端关闭时触发。
     */
    NEU_EVENT_IO_CLOSED = 0x2, // 连接关闭事件

    /**
     * @brief 挂起事件。
     *
     * 当文件描述符对应的连接被挂起时触发，这可能意味着对等方已经异常终止。
     */
    NEU_EVENT_IO_HUP = 0x3, // 挂起事件
};

typedef struct neu_event_io neu_event_io_t;
typedef int (*neu_event_io_callback)(enum neu_event_io_type type, int fd,
                                     void *usr_data);

/**
 * @brief 结构体用于封装IO事件的相关参数。
 *
 * 该结构体包含触发IO事件的文件描述符、用户自定义数据以及事件触发时要调用的回调函数。
 * 需要 fd 成员来标识触发IO事件的文件描述符
 */
typedef struct neu_event_io_param {
    /**
     * @brief 触发IO事件的文件描述符。
     *
     * 这是一个整型变量，代表在UNIX和类UNIX系统中用于标识打开文件
     * 或套接字等非文件资源的文件描述符。
     */
    int fd;

    /**
     * @brief 传递给回调函数的用户自定义数据。
     *
     * 这是一个指向任意类型数据的指针，允许用户将自定义的数据与IO事件相关联，
     * 并在事件触发时通过回调函数访问这些数据。
     */
    void *usr_data;

    /**
     * @brief 当IO事件触发时调用的回调函数。
     *
     * 这是一个函数指针，指向一个回调函数。当IO事件发生时，这个回调函数会被调用。
     * 回调函数的签名应该与`neu_event_io_callback`类型相匹配。
     */
    neu_event_io_callback cb;
} neu_event_io_param_t;

/**
 * @brief Add io_event to the event.
 *
 * @param[in] events
 * @param[in] io
 * @return  The added event io.
 */
neu_event_io_t *neu_event_add_io(neu_events_t *events, neu_event_io_param_t io);

/**
 * @brief Delete io_event from event.
 *
 * @param[in] events
 * @param[in] io
 * @return 0 on success.
 */
int neu_event_del_io(neu_events_t *events, neu_event_io_t *io);

#ifdef __cplusplus
}
#endif

#endif
