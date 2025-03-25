/**
 * NEURON IIoT System for Industry 4.0
 * Copyright (C) 2020-2024 EMQ Technologies Co., Ltd All rights reserved.
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
#include <pthread.h>

#include "utils/log.h"
#include "utils/utextend.h"

#include "msg_q.h"

/**
 * @brief 用于实现消息队列的单个节点结构。
 *
 * 此结构体表示消息队列中的一个节点，每个节点包含一个指向 `neu_msg_t` 类型消息的指针
 * 以及指向前一个和后一个节点的指针，从而形成双向链表。
 *
 */
struct item {
    neu_msg_t *  msg;
    struct item *prev, *next;
};

/**
 * @brief 适配器消息队列结构体。
 *
 * 此结构体用于管理适配器的消息队列，包括消息列表、最大容量、当前数量、
 * 名称以及线程同步所需的互斥锁和条件变量。
 */
struct adapter_msg_q {
    /**
     * @brief 消息列表指针。
     *
     * 指向一个包含消息项的链表或数组的指针。
     */
    struct item *list;

    /**
     * @brief 队列的最大容量。
     *
     * 表示该消息队列可以容纳的最大消息数量。
     */
    uint32_t     max;

    /**
     * @brief 当前队列中的消息数量。
     *
     * 表示当前存储在队列中的实际消息数量。
     */
    uint32_t     current;

    /**
     * @brief 队列名称。
     *
     * 用于标识该消息队列的名称字符串。
     */
    char *       name;

    /**
     * @brief 互斥锁。
     *
     * 用于保护对消息队列的访问，确保线程安全。
     */
    pthread_mutex_t mtx;

    /**
     * @brief 条件变量。
     *
     * 用于通知等待消息的线程队列状态的变化（如新消息到达）。
     */
    pthread_cond_t  cond;
};

/**
 * @brief 创建并初始化一个新的适配器消息队列。
 *
 * 此函数用于创建并初始化一个新的适配器消息队列对象。它分配内存以存储队列结构，
 * 并初始化互斥锁和条件变量。它还设置队列的最大容量和当前大小，并复制传入的队列名称。
 *
 * @param name 队列的名称，将被复制到新创建的队列对象中。
 * @param size 队列的最大容量。
 * @return 成功时返回指向新创建的 `adapter_msg_q_t` 结构体的指针；
 *         如果内存分配失败，则返回 `NULL`。
 */
adapter_msg_q_t *adapter_msg_q_new(const char *name, uint32_t size)
{
    // 分配内存以存储队列结构
    struct adapter_msg_q *q = calloc(1, sizeof(struct adapter_msg_q));

    // 初始化互斥锁和条件变量
    pthread_mutex_init(&q->mtx, NULL);
    pthread_cond_init(&q->cond, NULL);

    // 设置队列的最大容量和当前大小，并复制队列名称
    q->max     = size;
    q->name    = strdup(name);
    q->current = 0;

    return q;
}

void adapter_msg_q_free(adapter_msg_q_t *q)
{
    struct item *tmp = NULL, *elt = NULL;
    nlog_warn("app: %s, drop %u msg", q->name, q->current);
    pthread_mutex_destroy(&q->mtx);
    pthread_cond_destroy(&q->cond);

    DL_FOREACH_SAFE(q->list, elt, tmp)
    {
        DL_DELETE(q->list, elt);
        neu_reqresp_head_t *header = neu_msg_get_header(elt->msg);
        neu_trans_data_free((neu_reqresp_trans_data_t *) &header[1]);
        neu_msg_free(elt->msg);
        free(elt);
    }
    free(q->name);
    free(q);
}

/**
 * @brief 将消息推入适配器消息队列。
 *
 * 此函数用于将一条消息推入指定的消息队列中。如果队列未满，则创建一个新的节点来存储消息，
 * 并将其添加到队列的末尾。如果队列已满，则记录警告日志并返回错误码 `-1`。成功插入消息后，
 * 该函数会发出条件信号以通知等待的消费者线程有新消息可用。
 *
 * @param q 指向 `adapter_msg_q_t` 结构体的指针，表示要操作的消息队列。
 * @param msg 要推入队列的消息指针。
 * @return 成功时返回 0；如果队列已满，则返回 -1。
 */
int adapter_msg_q_push(adapter_msg_q_t *q, neu_msg_t *msg)
{
    int ret = -1;
    pthread_mutex_lock(&q->mtx);
    if (q->current < q->max) {
        q->current += 1;
        struct item *elt = calloc(1, sizeof(struct item));
        elt->msg         = msg;
        DL_APPEND(q->list, elt);
        ret = 0;
    }
    pthread_mutex_unlock(&q->mtx);

    if (ret == -1) {
        nlog_warn("app: %s, msg q is full, %u(%u)", q->name, q->current,
                  q->max);
    } else {
        //唤醒正在等待队列中有元素的消费者线程。
        pthread_cond_signal(&q->cond);
    }

    return ret;
}

/**
 * @brief 从适配器消息队列中弹出一条消息。
 *
 * 此函数用于从适配器消息队列中取出一条消息。如果队列为空，则线程将等待直到有新消息到达。
 * 取出的消息通过 `p_data` 参数返回给调用者。函数还会更新队列的状态（如当前消息数量），
 * 并释放与取出的消息相关的内存资源。
 *
 * @param q 指向 `adapter_msg_q_t` 结构体的指针，表示要操作的消息队列。
 * @param p_data 指针的指针，用于存储从队列中取出的消息。函数成功执行后，此指针指向取出的消息。
 * @return 返回当前队列中的消息数量；如果队列为空且没有消息可取，则返回 0。
 */
uint32_t adapter_msg_q_pop(adapter_msg_q_t *q, neu_msg_t **p_data)
{
    uint32_t ret = 0;

    pthread_mutex_lock(&q->mtx);
    while (q->current == 0) {
        /**
         * @brief
         * 
         * 当前线程进入等待状态，直到有其他线程向队列中添加消息并发出信号
         * （通过 pthread_cond_signal 或 pthread_cond_broadcast）。
         * 
         * @note pthread_cond_wait 函数会自动解锁互斥锁 q->mtx
         */
        pthread_cond_wait(&q->cond, &q->mtx);
    }

    //获取队列中的最后一个元素
    struct item *elt = DL_LAST(q->list);

    if (elt != NULL) {
        DL_DELETE(q->list, elt);
        *p_data = elt->msg;
        free(elt);
        q->current -= 1;
        ret = q->current;
    }

    pthread_mutex_unlock(&q->mtx);
    return ret;
}
