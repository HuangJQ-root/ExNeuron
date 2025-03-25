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
#ifndef NEURON_UTILS_ROLLING_COUNTER_H
#define NEURON_UTILS_ROLLING_COUNTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief 滚动计数器。
 *
 * 此计数器用于在某个最近的时间跨度内对值进行计数，例如最近5秒内的网络字节发送量等。
 */
typedef struct {
    /**
     * @brief 累加器，用于存储累计的值。
     */
    uint64_t val;      

    /**
     * @brief 头部时间戳，以毫秒为单位。
     *
     * 记录当前滚动窗口的起始时间戳。每次更新计数器时，
     * 会根据这个时间戳和当前时间的差值来确定需要更新哪些计数器。
     */
    uint64_t ts;       

    /**
     * @brief 时间分辨率，以毫秒为单位。
     *
     * 用于定义每个计数器的时间间隔大小。
     */
    uint32_t res : 21; 

    /**
     * @brief 头部位置，表示当前计数器的头部位置。
     * 
     * 它是一个循环索引，用于指示当前计数器数组中的哪个元素是头部。
     * 每次更新计数器时，头部位置会根据时间的推移进行更新。
     */
    uint32_t hd : 5;  

    /**
     * @brief 计数器数量，表示计数器数组中的计数器个数。
     * 
     * 不同的时间跨度会对应不同的计数器数量
     */
    uint32_t n : 6;   

    /**
     * @brief 计数器数组，包含多个计数器的存储单元。
     * 
     * 每个元素代表一个特定时间间隔内的计数值
     * 例如在计算最近 5 秒的网络字节发送量时，计数器数组中的每个元素可能代表 1 秒内的字节发送量。
     */
    uint32_t counts[]; 
} neu_rolling_counter_t;

/** Create rolling counter.
 *
 * @param   span   time span in milliseconds
 */
static inline neu_rolling_counter_t *neu_rolling_counter_new(unsigned span)
{
    unsigned n = span <= 6000 ? 4 : span <= 32000 ? 8 : span <= 64000 ? 16 : 32;
    assert(span / n < (1 << 22)); // should not overflow ti

    neu_rolling_counter_t *counter = (neu_rolling_counter_t *) calloc(
        1, sizeof(*counter) + sizeof(counter->counts[0]) * n);
    if (counter) {
        counter->res = span / n;
        counter->n   = n;
    }
    return counter;
}

/** Destructs the rolling counter.
 */
static inline void neu_rolling_counter_free(neu_rolling_counter_t *counter)
{
    if (counter) {
        free(counter);
    }
}

/**
 * @brief 增量滚动计数器并返回更新后的值。
 *
 * 此函数用于增量滚动计数器，并根据提供的时间戳和增量值更新计数器的状态。它首先计算自上次更新以来
 * 经过了多少个分辨率单位（`step`），然后根据这个步数更新计数器的历史数据。如果有多个步长未被更新，
 * 则依次更新每个步长的数据，确保计数器只保留最近 `n` 个分辨率单位的数据。最后，增加当前计数值，并更新时间戳。
 *
 * @param counter 表示滚动计数器对象。
 * @param ts      时间戳，以毫秒为单位
 * @param dt      要增加的增量值。
 * @return 返回更新后的滚动计数器的值。
 */
static inline uint64_t neu_rolling_counter_inc(neu_rolling_counter_t *counter,
                                               uint64_t ts, unsigned dt)
{
    //计算时间步长
    uint64_t step = (ts - counter->ts) / counter->res;

    //滑动窗口更新
    for (unsigned i = 0; i < step && i < counter->n; ++i) {
        //更新头部位置，确保头部位置始终在 0 到 counter->n - 1 的范围内。
        counter->hd = (counter->hd + 1) & (counter->n - 1);  

        //更新累计值，将当前头部位置的计数值从累计值 val 中减去
        counter->val -= counter->counts[counter->hd];

        //重置计数值，将当前头部位置的计数值重置为 0，为新的时间间隔做准备。
        counter->counts[counter->hd] = 0;
    }

    //增加增量值
    counter->val += dt;
    counter->counts[counter->hd] += dt;

    //更新头部时间戳
    counter->ts += step * counter->res;
    
    return counter->val;
}

/** Reset the counter.
 */
static inline void neu_rolling_counter_reset(neu_rolling_counter_t *counter)
{
    counter->val = 0;
    counter->hd  = 0;
    memset(counter->counts, 0, counter->n * sizeof(counter->counts[0]));
}

/** Return the counter value.
 *
 * NOTE: may return stale value if the counter is not updated frequent enough.
 */
static inline uint64_t neu_rolling_counter_value(neu_rolling_counter_t *counter)
{
    return counter->val;
}

#ifdef __cplusplus
}
#endif

#endif
