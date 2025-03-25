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
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "event/event.h"
#include "utils/log.h"

#ifdef NEU_PLATFORM_LINUX
#include <sys/epoll.h>
#include <sys/timerfd.h>

/**
 * @brief neu_event_timer 结构体用于管理定时器事件的相关信息。
 *
 * 该结构体包含文件描述符 (`fd`)、关联的事件数据 (`event_data`)、定时器值 (`value`)、定时器类型 (`type`)、互斥锁 (`mtx`) 和停止标志 (`stop`)。
 *
 * 设计理念：
 * - 灵活性与多态性模拟：通过联合体 `ctx`（在 `event_data` 中）存储不同类型的上下文信息（如 I/O 或定时器），可以在同一套逻辑中处理多种事件类型。
 * - 内存管理与效率：使用联合体节省内存，避免为每种可能的事件类型分配独立的内存空间。
 * - 模块化与抽象：将通用的信息（如回调函数、用户数据等）放在 `event_data` 中，并通过指针将其关联到具体的事件类型（如本结构体），简化了代码结构并提高了可维护性。
 */
typedef struct neu_event_timer {
    /**
     * @brief 文件描述符，用于标识与定时器相关的文件或套接字（如果适用）。
     *
     * 对于某些定时器实现，可能需要一个文件描述符来监控定时器事件。
     */
    int fd;

    /**
     * @brief 指向 event_data 的指针，包含与该定时器事件相关的所有信息。
     *
     * 这个指针指向一个 event_data 实例，其中包含了回调函数、上下文、用户数据等。
     *
     * 设计优势：
     * - 提供了模块化设计，使得每个具体事件类型只需要关注其特有的属性，而共享的部分则由 event_data 统一管理。
     */
    struct event_data *event_data;

    /**
     * @brief 定时器值，指定定时器的初始值和间隔时间。
     *
     * 使用 itimerspec 结构体来设置定时器的初始到期时间和间隔时间。
     */
    struct itimerspec value;

    /**
     * @brief 定时器类型，表示定时器的具体类型（如一次性定时器或周期性定时器）。
     */
    neu_event_timer_type_e type;

    /**
     * @brief 互斥锁，用于保护共享资源，确保线程安全。
     *
     * 在多线程环境中使用互斥锁来保护对定时器状态的访问。
     */
    pthread_mutex_t mtx;

    /**
     * @brief 停止标志，指示是否应该停止定时器。
     *
     * 当设置为 true 时，定时器将停止运行。
     */
    bool stop;
} neu_event_timer_t;

/**
 * @brief neu_event_io 结构体用于管理 I/O 事件的相关信息。
 *
 * 该结构体包含文件描述符 (`fd`) 和关联的事件数据 (`event_data`)。
 *
 * 设计理念：
 * - 灵活性与多态性模拟：通过联合体 `ctx`（在 `event_data` 中）存储不同类型的
 *   上下文信息（如 I/O 或定时器），可以在同一套逻辑中处理多种事件类型。
 * - 内存管理与效率：使用联合体节省内存，避免为每种可能的事件类型分配独立的内存空间。
 * - 模块化与抽象：将通用的信息（如回调函数、用户数据等）放在 `event_data` 中，并
 *   通过指针将其关联到具体的事件类型（如本结构体），简化了代码结构并提高了可维护性。
 */
typedef struct neu_event_io {
    /**
     * @brief 文件描述符，用于标识需要监控的文件或套接字。
     *
     * 文件描述符用于监控读写操作或其他 I/O 事件。
     */
    int fd;

    /**
     * @brief 指向 event_data 的指针，包含与该 I/O 事件相关的所有信息。
     *
     * 这个指针指向一个 event_data 实例，其中包含了回调函数、上下文、用户数据等。
     *
     * @note
     * - 提供了模块化设计，使得每个具体事件类型只需要关注其特有的属性，
     *   而共享的部分则由 event_data 统一管理。
     */
    struct event_data *event_data;
} neu_event_io_t;

/**
 * @brief event_data 结构体用于存储和管理单个事件的相关信息。
 *
 * 创建一个通用的事件管理机制，能够统一处理不同类型的事件,并为
 * 每种事件类型提供对应的回调函数和上下文信息
 *
 * 设计理念：
 * - 1.灵活性与多态性模拟：通过联合体 `ctx` 存储不同类型的上
 *     下文信息（如 I/O 或定时器），可以在同一套逻辑中处理多种事件类型。
 * - 2.内存管理与效率：使用联合体节省内存，避免为每种可能的事
 *     件类型分配独立的内存空间。
 * - 3.模块化与抽象：将通用的信息（如回调函数、用户数据等）放在 
 *     `event_data` 中，并通过指针将其关联到具体的事件类型
 *    （如 neu_event_io 和 neu_event_timer），简化了代码结
 *     构并提高了可维护性。
 * - 4.扩展性：如果将来需要添加新的事件类型（例如信号事件），只需在
 *     event_data 中定义新的字段即可，而不需要修改整个事件管理系统的核心逻辑。
 * 
 */
typedef struct event_data {
    /**
     * @brief 事件类型枚举，表示事件是 I/O 事件还是定时器事件。
     *
     * - TIMER: 定时器事件。
     * - IO: I/O 事件。
     */
    enum {
        TIMER = 0,  ///< 定时器事件
        IO    = 1,  ///< I/O 事件
    } type;

    /**
     * @brief 回调函数联合体，根据事件类型存储相应的回调函数。
     *
     * - io: 对应于 I/O 事件的回调函数。
     * - timer: 对应于定时器事件的回调函数。
     */
    union {
        neu_event_io_callback    io;    ///< I/O 事件回调函数
        neu_event_timer_callback timer; ///< 定时器事件回调函数
    } callback;

    /**
     * @brief 上下文联合体，根据事件类型存储相应的上下文信息。
     *
     * - io: 对应于 I/O 事件的上下文信息。
     * - timer: 对应于定时器事件的上下文信息。
     */
    union {
        neu_event_io_t    io;    ///< I/O 事件上下文
        neu_event_timer_t timer; ///< 定时器事件上下文
    } ctx;

    /**
     * @brief 用户数据指针，用于存储与事件相关的任意数据。
     *
     * 这个字段可以用来传递额外的信息给回调函数。
     */
    void *usr_data;

    /**
     * @brief 文件描述符
     *
     * 文件描述符用于标识需要监控的文件或套接字。
     */
    int fd;

    /**
     * @brief 索引，用于在事件数组中定位此事件。
     *
     * 当事件被注册时，会分配一个唯一的索引来标识该事件。
     */
    int index;

    /**
     * @brief 使用标志，指示该事件是否正在使用。
     *
     * 如果设置为 true，则表示该事件已经被注册并正在使用；否则表示未使用或已释放。
     */
    bool use;
} event_data;

#define EVENT_SIZE 1400

/**
 * @brief 管理和处理事件的核心结构体。
 *
 * 该结构体封装了事件管理所需的各种资源和状态信息，使用 epoll 机制实现高效的事件多路复用，
 * 并通过多线程方式持续监听和处理注册的事件。在多线程环境中，使用互斥锁确保对共享资源的安全访问。
 */
typedef struct neu_events {
    /**
     * @brief epoll 文件描述符，用于高效的事件多路复用。
     *
     * 使用 epoll 机制可以高效地监控多个文件描述符上的事件（如读、写等），
     * 并在事件发生时通知应用程序。
     */
    int epoll_fd;

    /**
     * @brief 线程 ID，用于运行事件处理循环。
     *
     * 这个线程会持续运行，监听并处理所有注册的事件。
     */
    pthread_t thread;

    /**
     * @brief 停止标志，指示是否应该停止事件循环。
     *
     * 当设置为 true 时，事件循环将停止，并且线程将退出。
     */
    bool stop;

    /**
     * @brief 互斥锁，用于保护共享资源。
     *
     * 在多线程环境中，使用互斥锁来确保对共享资源（如 `n_event` 和 `event_datas`）的访问是安全的。
     */
    pthread_mutex_t mtx;

    /**
     * @brief 事件计数器，记录当前已注册的事件数量。
     *
     * 该计数器用于跟踪当前有多少事件正在被监控。
     */
    int n_event;

    /**
     * @brief 事件数据数组，用于存储和管理注册的事件。
     *
     * 每个元素代表一个事件，包含与该事件相关的所有必要信息（如回调函数、用户数据等）。
     */
    struct event_data event_datas[EVENT_SIZE];
} neu_events_t;

/**
 * @brief 获取一个空闲的事件索引。
 *
 * 该函数遍历 `events->event_datas` 数组，查找一个未被使用的（use 标志为 false）事件数据结构，
 * 并将其标记为已使用（use 标志设为 true），同时设置其索引值。
 *
 * @param events 指向 neu_events_t 实例的指针，包含需要查找的事件数据数组。
 *
 * @return 成功时返回找到的空闲事件的索引；如果没有找到空闲事件，则返回 -1。
 *
 * @note 
 * - 此函数在访问共享资源时使用了互斥锁来确保线程安全。
 * - EVENT_SIZE 应当是一个宏或常量，定义了事件数据数组的最大大小。
 */
static int get_free_event(neu_events_t *events)
{
    int ret = -1;

    // 锁定互斥锁以确保线程安全
    pthread_mutex_lock(&events->mtx);

    // 遍历所有事件数据，寻找第一个未被使用的事件
    for (int i = 0; i < EVENT_SIZE; i++) {
        if (events->event_datas[i].use == false) {
            events->event_datas[i].use   = true;
            events->event_datas[i].index = i;
            ret                          = i;
            break;
        }
    }

    // 解锁互斥锁
    pthread_mutex_unlock(&events->mtx);

    // 返回找到的空闲事件的索引，如果没有找到则返回 -1
    return ret;
}

/**
 * @brief 将指定索引处的事件标记为未使用。
 *
 * 该函数接收一个 neu_events_t 实例指针和一个事件索引，
 * 并将该索引处的事件数据结构标记为未使用（use 标志设为 false），
 * 同时将其索引值重置为 0。
 *
 * @param events 指向 neu_events_t 实例的指针，包含需要操作的事件数据数组。
 * @param index 需要释放的事件在 event_datas 数组中的索引。
 *
 * @note 
 * - 此函数在访问共享资源时使用了互斥锁来确保线程安全。
 */
static void free_event(neu_events_t *events, int index)
{
    // 锁定互斥锁以确保线程安全
    pthread_mutex_lock(&events->mtx);

    // 将指定索引处的事件标记为未使用
    events->event_datas[index].use   = false;

    // 重置其索引值为 0
    events->event_datas[index].index = 0;

    // 解锁互斥锁
    pthread_mutex_unlock(&events->mtx);
}

/**
 * @brief 事件循环线程的主函数。
 *
 * 该函数负责监听并处理通过 epoll 注册的事件（如 I/O 和定时器事件）。
 * 它会持续运行直到 `events->stop` 被设置为 true 或发生错误。
 *
 * @param arg 指向 neu_events_t 实例的指针，包含需要监听的事件信息。
 *
 * @return 返回 NULL，因为这是一个线程入口函数。
 *
 * @note 
 * - 此函数是一个静态函数，并且设计为在后台线程中运行。
 * - 使用 epoll_wait 来等待事件的发生，并根据事件类型调用相应的回调函数。
 * - 定时器事件处理时会检查是否需要重新设置定时器，并处理阻塞和非阻塞模式。
 * - I/O 事件处理时会检查不同的事件类型（如 EPOLLIN, EPOLLRDHUP, EPOLLHUP），并调用相应的回调函数。
 */
static void *event_loop(void *arg)
{
    neu_events_t *events   = (neu_events_t *) arg;
    int           epoll_fd = events->epoll_fd;

    // 主循环，持续运行直到 stop 标志被设置为 true
    while (!events->stop) {
        struct epoll_event event = { 0 };
        struct event_data *data  = NULL;

        // 等待事件发生，超时时间为 1000 毫秒
        int ret = epoll_wait(epoll_fd, &event, 1, 1000);
        if (ret == 0) {
            continue;
        }

        // 处理 EINTR 错误
        if (ret == -1 && errno == EINTR) {
            continue;
        }

        // 如果发生错误或 stop 标志被设置，退出循环
        if (ret == -1 || events->stop) {
            zlog_warn(neuron, "event loop exit, errno: %s(%d), stop: %d",
                      strerror(errno), errno, events->stop);
            break;
        }

        // 获取事件数据
        data = (struct event_data *) event.data.ptr;

        switch (data->type) {
        case TIMER:
            // 锁定定时器相关的互斥锁，确保线程安全
            pthread_mutex_lock(&data->ctx.timer.mtx);

            // 检查事件是否包含 EPOLLIN 标志（即定时器到期）
            if ((event.events & EPOLLIN) == EPOLLIN) {
                uint64_t t;

                // 从定时器文件描述符读取触发次数
                ssize_t size = read(data->fd, &t, sizeof(t));
                // 忽略返回值，因为已经确认了 EPOLLIN 事件
                (void) size; 

                // 如果定时器未被停止
                if (!data->ctx.timer.stop) {
                    // 如果是阻塞模式的定时器
                    if (data->ctx.timer.type == NEU_EVENT_TIMER_BLOCK) {
                        // 从 epoll 实例中删除该定时器文件描述符
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, data->fd, NULL);

                        // 调用定时器回调函数处理事件
                        ret = data->callback.timer(data->usr_data);

                        // 重设定时器的时间间隔
                        timerfd_settime(data->fd, 0, &data->ctx.timer.value,
                                        NULL);
                        
                        // 将定时器文件描述符重新添加到 epoll 实例中
                        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, data->fd, &event);
                    } else {
                        // 对于非阻塞模式的定时器，直接调用回调函数
                        ret = data->callback.timer(data->usr_data);
                    }
                }
            }

            // 解锁定时器相关的互斥锁
            pthread_mutex_unlock(&data->ctx.timer.mtx);
            break;
        case IO:
            // 如果事件包含 EPOLLHUP 标志（对端关闭连接）:读写都关闭，通常意味着连接已经完全中断
            if ((event.events & EPOLLHUP) == EPOLLHUP) {
                // 调用 I/O 回调函数处理连接挂起事件
                data->callback.io(NEU_EVENT_IO_HUP, data->fd, data->usr_data);
                break;
            }

            // 如果事件包含 EPOLLRDHUP 标志（检测到对端关闭连接或半关闭连接）:对端读关闭，连接仍然可能处于半打开状态
            if ((event.events & EPOLLRDHUP) == EPOLLRDHUP) {
                // 调用 I/O 回调函数处理连接关闭事件
                data->callback.io(NEU_EVENT_IO_CLOSED, data->fd,
                                  data->usr_data);
                break;
            }

            // 如果事件包含 EPOLLIN 标志（有数据可读）
            if ((event.events & EPOLLIN) == EPOLLIN) {
                // 调用 I/O 回调函数处理数据可读事件
                data->callback.io(NEU_EVENT_IO_READ, data->fd, data->usr_data);
                break;
            }

            break;
        }
    }

    return NULL;
};

/**
 * @brief 创建并初始化一个新的事件管理器实例。
 *
 * 该函数分配内存并初始化一个 neu_events_t 结构体实例，创建一个 epoll 文件描述符，
 * 并启动一个后台线程来处理事件循环。
 *
 * @return 返回一个指向新创建的 neu_events_t 实例的指针。
 *         如果内存分配失败或 epoll 文件描述符创建失败，则程序将终止（通过 assert）。
 *
 * @note 
 * - 此函数使用 calloc 分配内存，并确保所有字段都被初始化为零。
 * - epoll_create(1) 用于创建一个新的 epoll 实例。参数 '1' 是提示内核分配的大小，但实际大小是动态调整的。
 * - 该函数还初始化了一个互斥锁和一个后台线程，用于处理事件循环。
 */
neu_events_t *neu_event_new(void)
{
    // 分配内存并初始化 neu_events_t 结构体
    neu_events_t *events = calloc(1, sizeof(struct neu_events));

    // 创建 epoll 文件描述符
    events->epoll_fd = epoll_create(1);

    // 记录日志并断言确保 epoll 文件描述符有效
    nlog_notice("create epoll: %d(%d)", events->epoll_fd, errno);
    assert(events->epoll_fd > 0);

    // 初始化其他字段
    events->stop    = false;
    events->n_event = 0;
    pthread_mutex_init(&events->mtx, NULL);

    // 启动后台线程运行事件循环
    pthread_create(&events->thread, NULL, event_loop, events);

    return events;
}

/**
 * @brief 关闭并释放一个事件管理器实例。
 *
 * 该函数设置停止标志，关闭 epoll 文件描述符，等待后台线程结束，销毁互斥锁，并释放分配的内存。
 *
 * @param events 指向要关闭的 neu_events_t 实例的指针。
 *
 * @return 成功时返回 0。
 *
 * @note 
 * - 此函数确保所有资源都被正确释放，包括关闭 epoll 文件描述符、等待线程结束和销毁互斥锁。
 * - 调用此函数后，`events` 指针将不再有效，不应再对其进行访问。
 */
int neu_event_close(neu_events_t *events)
{
    // 设置停止标志
    events->stop = true;

    // 关闭 epoll 文件描述符
    close(events->epoll_fd);

    // 等待后台线程结束
    pthread_join(events->thread, NULL);

    // 销毁互斥锁
    pthread_mutex_destroy(&events->mtx);

    // 释放分配的内存
    free(events);

    return 0;
}

/**
 * @brief 向事件管理系统中添加一个新的定时器。
 *
 * 该函数创建一个新的定时器文件描述符，设置定时器的时间间隔，
 * 并将其注册到 epoll 实例中以便进行监控。
 *
 * @param events 指向 neu_events_t 实例的指针，包含需要操作的事件数据数组和 epoll 文件描述符。
 * @param timer 定时器参数结构体，包含定时器的初始值、间隔时间、用户数据及回调函数等信息。
 *
 * @return 成功时返回指向新定时器上下文的指针；失败时返回 NULL。
 *
 * @note 
 * - 此函数在访问共享资源时使用了互斥锁来确保线程安全。
 * - 如果没有可用的事件槽位，函数将记录错误并终止程序。
 */
neu_event_timer_t *neu_event_add_timer(neu_events_t *          events,
                                       neu_event_timer_param_t timer)
{
    int               ret      = 0;

    // 创建一个新的定时器文件描述符
    int               timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);

    // 设置定时器的初始值和间隔时间
    struct itimerspec value    = {
        .it_value.tv_sec     = timer.second,
        .it_value.tv_nsec    = timer.millisecond * 1000 * 1000,
        .it_interval.tv_sec  = timer.second,
        .it_interval.tv_nsec = timer.millisecond * 1000 * 1000,
    };

    // 获取一个空闲的事件槽位
    int index = get_free_event(events);
    if (index < 0) {
        zlog_fatal(neuron, "no free event: %d", events->epoll_fd);
    }
    assert(index >= 0);

    // 初始化定时器上下文
    neu_event_timer_t *timer_ctx = &events->event_datas[index].ctx.timer;
    timer_ctx->event_data        = &events->event_datas[index];

    // 配置 epoll 事件
    struct epoll_event event = {
        .events   = EPOLLIN,
        .data.ptr = timer_ctx->event_data,
    };

    /**
     * @brief 设置定时器并启动
     * 
     * @note 需在进行事件数据更新和定时器特定字段前设置，
     *  -timerfd_settime 函数的作用是启动或重新设置定时器的参数，
     *   包括初始到期时间和重复间隔时间。一旦调用该函数，定时器就会按照
     *   设定的参数开始计时，并且在到期时产生相应的事件。这个操作是一个
     *   相对独立的系统调用，它不依赖于后续的事件数据和定时器上下文的设置。
     * 
     */
    timerfd_settime(timer_fd, 0, &value, NULL);

    // 更新事件数据
    timer_ctx->event_data->type           = TIMER;
    timer_ctx->event_data->fd             = timer_fd;
    timer_ctx->event_data->usr_data       = timer.usr_data;
    timer_ctx->event_data->callback.timer = timer.cb;
    timer_ctx->event_data->ctx.timer = events->event_datas[index].ctx.timer;
    timer_ctx->event_data->index     = index;

    // 设置定时器特定字段
    timer_ctx->value = value;
    timer_ctx->fd    = timer_fd;
    timer_ctx->type  = timer.type;
    timer_ctx->stop  = false;
    pthread_mutex_init(&timer_ctx->mtx, NULL);

    // 将定时器文件描述符添加到 epoll 实例中
    ret = epoll_ctl(events->epoll_fd, EPOLL_CTL_ADD, timer_fd, &event);

    zlog_notice(neuron,
                "add timer, second: %" PRId64 ", millisecond: %" PRId64
                ", timer: %d in epoll %d, "
                "ret: %d, index: %d",
                timer.second, timer.millisecond, timer_fd, events->epoll_fd,
                ret, index);

    return timer_ctx;
}

/**
 * @brief 删除一个定时器事件
 *
 * 该函数用于从一个基于 epoll 的事件处理系统中删除一个定时器事件。
 * 它首先记录一条日志消息，然后停止定时器，并从 epoll 实例中删除定时器的文件描述符。
 * 最后，关闭该文件描述符并释放相关资源。
 *
 * @param events 指向包含 epoll 文件描述符和其他事件处理相关信息的结构体的指针
 * @param timer 指向要删除的定时器事件的结构体的指针
 *
 * @return 返回 0 表示成功，其他值表示失败（当前实现总是返回 0）
 *
 * @note 在调用此函数后，定时器相关的资源将被释放，定时器回调将不再执行。
 */
int neu_event_del_timer(neu_events_t *events, neu_event_timer_t *timer)
{
    zlog_notice(neuron, "del timer: %d from epoll: %d, index: %d", timer->fd,
                events->epoll_fd, timer->event_data->index);

    // 停止定时器
    timer->stop = true;

    // 从 epoll 实例中删除定时器
    epoll_ctl(events->epoll_fd, EPOLL_CTL_DEL, timer->fd, NULL);

    // 关闭文件描述符并释放资源
    pthread_mutex_lock(&timer->mtx);
    close(timer->fd);
    pthread_mutex_unlock(&timer->mtx);

    // 销毁互斥锁并释放定时器事件数据
    pthread_mutex_destroy(&timer->mtx);
    free_event(events, timer->event_data->index);
    return 0;
}

/**
 * @brief 向事件循环中添加一个I/O事件。
 *
 * 此函数使用epoll接口将一个新的I/O事件添加到事件循环中。该事件将被
 * 监控以检测可读性、错误、挂起以及由对等方关闭的连接。
 *
 * @param events 指向包含epoll文件描述符和事件数据数组的`neu_events_t`结构体的指针。
 * @param io 包含要添加的事件的参数的`neu_event_io_param_t`结构体。
 *
 * @return 
 * 指向已添加事件的`neu_event_io_t`结构体的指针。如
 * 果添加失败，则断言失败（注意：实际生产代码应改进错误处理）。
 *
 * @note 
 * 在调用此函数之前，应确保`events`结构体已被正确初始化，并且`get_free_event`函数能
 * 够返回有效的索引。此外，还应检查`io.fd`是否是一个有效的文件描述符。
 * 
 * @warning
 * io_ctx->event_data->ctx.io = events->event_datas[index].ctx.io; 操作多余
 * 因为 io_ctx 已经指向了 events.event_datas[index].ctx_io
 */
neu_event_io_t *neu_event_add_io(neu_events_t *events, neu_event_io_param_t io)
{
    // 用于存储epoll_ctl的返回值
    int ret   = 0;

    // 从事件数组中获取一个空闲的索引
    int index = get_free_event(events);

    nlog_notice("add io, fd: %d, epoll: %d, index: %d", io.fd, events->epoll_fd,
                index);
    assert(index >= 0);

    /**
     * @brief
     * 
     * 从空闲事件组中取出一个可用的 event_data 元素，并获取其内部存储的 I/O 事件上下文的地址
     * 将该地址赋值给 io_ctx 指针，以便后续对该 I/O 事件上下文进行操作和初始化
     * 这个 I/O 事件上下文将用于存储和管理新创建的 I/O 事件的相关信息
     */
    neu_event_io_t *io_ctx   = &events->event_datas[index].ctx.io;
    
    io_ctx->event_data       = &events->event_datas[index];

    // 配置epoll事件
    struct epoll_event event = {
        .events   = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP,
        .data.ptr = io_ctx->event_data, // 使用了data的ptr 成员存储数据
    };

    // 初始化事件数据
    io_ctx->event_data->type        = IO;
    io_ctx->event_data->fd          = io.fd;
    io_ctx->event_data->usr_data    = io.usr_data;
    io_ctx->event_data->callback.io = io.cb;
    io_ctx->event_data->ctx.io      = events->event_datas[index].ctx.io; 
    io_ctx->event_data->index       = index;

    io_ctx->fd = io.fd;

    // 将事件添加到epoll实例中
    ret = epoll_ctl(events->epoll_fd, EPOLL_CTL_ADD, io.fd, &event);

    nlog_notice("add io, fd: %d, epoll: %d, ret: %d(%d), index: %d", io.fd,
                events->epoll_fd, ret, errno, index);
    assert(ret == 0);

    return io_ctx;
}

/**
 * @brief 从事件循环中删除一个I/O事件。
 *
 * 此函数使用epoll接口从事件循环中删除指定的I/O事件。
 *
 * @param events 指向包含epoll文件描述符和事件数据数组的`neu_events_t`结构体的指针。
 * @param io 指向要删除的`neu_event_io_t`结构体的指针。
 *
 * @return 总是返回0，表示函数执行成功。在实际应用中，可能需要返回错误码以处理可能的失败情况。
 *
 * @note 在调用此函数之前，应确保`io`指针是有效的，并且确实指向了一个已添加到事件循环中的I/O事件。
 */
int neu_event_del_io(neu_events_t *events, neu_event_io_t *io)
{
    if (io == NULL) {
        return 0;
    }

    zlog_notice(neuron, "del io: %d from epoll: %d, index: %d", io->fd,
                events->epoll_fd, io->event_data->index);

    // 从epoll实例中删除指定的事件
    epoll_ctl(events->epoll_fd, EPOLL_CTL_DEL, io->fd, NULL);

    // 释放与该事件关联的资源
    free_event(events, io->event_data->index);

    // 返回0，表示函数执行成功
    return 0;
}

#endif