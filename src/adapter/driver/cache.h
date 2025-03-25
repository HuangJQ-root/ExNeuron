/**
 * NEURON IIoT System for Industry 4.0
 * Copyright (C) 2020-2021 EMQ Technologies Co., Ltd All rights reserved.
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

#ifndef _NEU_DRIVER_CACHE_H_
#define _NEU_DRIVER_CACHE_H_

#include <stdint.h>

#include "type.h"

typedef struct neu_driver_cache neu_driver_cache_t;

neu_driver_cache_t *neu_driver_cache_new();
void                neu_driver_cache_destroy(neu_driver_cache_t *cache);

void neu_driver_cache_add(neu_driver_cache_t *cache, const char *group,
                          const char *tag, neu_dvalue_t value);
void neu_driver_cache_update(neu_driver_cache_t *cache, const char *group,
                             const char *tag, int64_t timestamp,
                             neu_dvalue_t value, neu_tag_meta_t *metas,
                             int n_meta);
void neu_driver_cache_update_change(neu_driver_cache_t *cache,
                                    const char *group, const char *tag,
                                    int64_t timestamp, neu_dvalue_t value,
                                    neu_tag_meta_t *metas, int n_meta,
                                    bool change);

void neu_driver_cache_del(neu_driver_cache_t *cache, const char *group,
                          const char *tag);

void neu_driver_cache_update_trace(neu_driver_cache_t *cache, const char *group,
                                   void *trace_ctx);

void *neu_driver_cache_get_trace(neu_driver_cache_t *cache, const char *group);

/**
 * @brief 缓存值结构体，用于存储标签值的相关信息。
 *
 * 此结构体包含了缓存中的数据值、获取或更新该值的时间戳以及与该值相关的元数据数组。
 * 它被广泛应用于处理不同类型的数值及其相关的附加信息（如精度、单位等）。
 * 
 * @note
 * 
 * 设计目的是在缓存中高效地存储和管理数据，提供数据的基本信息（如值、时间戳和元数据），
 * 以便快速访问和更新缓存中的数据。
 */
typedef struct {
    /**
     * @brief 数据值。
     *
     * 这是一个自定义的数据类型（neu_dvalue_t），能够表示多种不同类型的数值，
     * 包括但不限于整数、浮点数、布尔值、字符串等。它也可能包含复杂的数据结构，
     * 如数组或指针指向的动态数据。
     */
    neu_dvalue_t   value;

    /**
     * @brief 时间戳。
     *
     * 记录了该值的获取或更新时间。通常以毫秒为单位的64位整数表示，
     * 用于判断数据的新鲜度或是否过期。
     */
    int64_t        timestamp;

    /**
     * @brief 元数据数组。
     *
     * 存储与该值相关的元数据。元数据可以包括精度、单位述等信息，
     * 有助于更全面地理解和使用该值。NEU_TAG_META_SIZE 定义了该数组的最大长度。
     */
    neu_tag_meta_t metas[NEU_TAG_META_SIZE];
} neu_driver_cache_value_t;

int neu_driver_cache_meta_get(neu_driver_cache_t *cache, const char *group,
                              const char *tag, neu_driver_cache_value_t *value,
                              neu_tag_meta_t *metas, int n_meta);
int neu_driver_cache_meta_get_changed(neu_driver_cache_t *cache,
                                      const char *group, const char *tag,
                                      neu_driver_cache_value_t *value,
                                      neu_tag_meta_t *metas, int n_meta);

#endif
