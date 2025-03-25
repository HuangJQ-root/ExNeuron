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

#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <string.h>

#include <jansson.h>

#include "utils/uthash.h"

#include "define.h"
#include "tag.h"

#include "cache.h"

extern bool sub_filter_err;

/**
 * @struct tkey_t
 * @brief 该结构体用于唯一标识缓存中的元素。
 *
 * 包含组名和标签名，通常用于作为哈希表中的键，以快速查找特定的数据项。
 */
typedef struct {
    char group[NEU_GROUP_NAME_LEN];
    char tag[NEU_TAG_NAME_LEN];
} tkey_t;

/**
 * @struct elem
 * @brief 该结构体用于表示缓存中的具体元素数据。
 *
 * 包含时间戳、变更标志、当前值与旧值、元数据数组、键以及UTHash句柄，主要用于管理和保护缓存中的具体数据项。
 */
struct elem {
    /**
     * @brief 时间戳。
     *
     * 记录了与该元素相关的最新操作的时间戳（以毫秒为单位）。
     */
    int64_t timestamp;

    /**
     * @brief 变更标志。
     *
     * 标识该元素的值是否发生了变化。
     */
    bool    changed;

    /**
     * @brief 缓存元素当前值。
     *
     * 包含该元素当前的数据值。
     */
    neu_dvalue_t value;

    /**
     * @brief 旧值。
     *
     * 包含该元素上一次更新前的数据值，可用于比较或回滚操作。
     */
    neu_dvalue_t value_old;

    /**
     * @brief 元数据数组。
     *
     * 存储与该元素相关的元数据信息，如单位、数据类型等，大小由 NEU_TAG_META_SIZE 定义。
     */
    neu_tag_meta_t metas[NEU_TAG_META_SIZE];

    /**
     * @brief 键。
     *
     * 唯一标识该元素的键值，类型为 tkey_t。
     */
    tkey_t         key;

    /**
     * @brief UTHash句柄。
     *
     * tkey_t为索引元素。
     */
    UT_hash_handle hh;
};

/**
 * @struct group_trace_t
 * @brief 该结构体用于表示组的追踪信息。
 *
 * 包含组名、追踪上下文以及UTHash句柄，主要用于管理与特定组相关的追踪数据。
 */
typedef struct {
    /**
     * @brief 组的名字。
     *
     * 这个字段存储了组的名称，通常用于标识不同的组实例，最大长度由 NEU_GROUP_NAME_LEN 定义。
     */
    char           key[NEU_GROUP_NAME_LEN];

    /**
     * @brief 追踪上下文。
     *
     * 一个通用指针，用于存储与追踪相关的上下文信息或数据。
     */
    void *         trace_ctx;

    /**
     * @brief UTHash句柄。
     *
     * 支持将该结构体作为哈希表中的元素进行高效管理，便于快速查找和操作组的追踪信息。
     */
    UT_hash_handle hh;
} group_trace_t;

/**
 * @brief 该结构体用于表示驱动适配器的缓存信息。
 *
 * 包含一个互斥锁、追踪表以及元素表。主要用于
 * 管理和保护驱动适配器相关的缓存数据。
 */
struct neu_driver_cache {
    /**
     * @brief 互斥锁。
     *
     * 用于保护对该缓存数据的并发访问，确保线程安全。
     */
    pthread_mutex_t mtx;

    /**
     * @brief 追踪表。
     *
     * 包含与组相关的追踪信息。
     */
    group_trace_t * trace_table;

    /**
     * @brief 元素表。
     *
     * 存储缓存中的具体元素数据。
     */
    struct elem *   table;
};

// static void update_tag_error(neu_driver_cache_t *cache, const char *group,
// const char *tag, int64_t timestamp, int error);

inline static tkey_t to_key(const char *group, const char *tag)
{
    tkey_t key = { 0 };

    strcpy(key.group, group);
    strcpy(key.tag, tag);

    return key;
}

neu_driver_cache_t *neu_driver_cache_new()
{
    neu_driver_cache_t *cache = calloc(1, sizeof(neu_driver_cache_t));

    pthread_mutex_init(&cache->mtx, NULL);

    return cache;
}

/**
 * @brief 销毁驱动缓存并释放所有相关资源。
 *
 * 此函数用于销毁给定的驱动缓存对象，并释放与之相关的所有资源。
 *
 * @param cache 表示要销毁的驱动缓存对象。
 */
void neu_driver_cache_destroy(neu_driver_cache_t *cache)
{
    struct elem *elem = NULL; // 遍历时指向当前元素
    struct elem *tmp  = NULL; // 遍历时用于临时保存下一个元素的指针

    pthread_mutex_lock(&cache->mtx);
    HASH_ITER(hh, cache->table, elem, tmp)
    {
        // 每个元素从哈希表中删除，并释放其占用的内存
        HASH_DEL(cache->table, elem);
        if (elem->value.type == NEU_TYPE_PTR) {
            if (elem->value.value.ptr.ptr != NULL) {
                free(elem->value.value.ptr.ptr);
                elem->value.value.ptr.ptr = NULL;
            }
        } else if (elem->value.type == NEU_TYPE_CUSTOM) {
            if (elem->value.value.json != NULL) {
                json_decref(elem->value.value.json);
                elem->value.value.json = NULL;
            }
        } else if (elem->value.type == NEU_TYPE_ARRAY_STRING) {
            for (int i = 0; i < elem->value.value.strs.length; i++) {
                free(elem->value.value.strs.strs[i]);
                elem->value.value.strs.strs[i] = NULL;
            }
        }

        free(elem);
    }

    group_trace_t *elem1 = NULL;
    group_trace_t *tmp1  = NULL;

    HASH_ITER(hh, cache->trace_table, elem1, tmp1)
    {
        HASH_DEL(cache->trace_table, elem1);
        free(elem1);
    }

    pthread_mutex_unlock(&cache->mtx);

    pthread_mutex_destroy(&cache->mtx);

    free(cache);
}

// void neu_driver_cache_error(neu_driver_cache_t *cache, const char *group,
// const char *tag, int64_t timestamp, int32_t error)
//{
// update_tag_error(cache, group, tag, timestamp, error);
//}

/**
 * @brief 向驱动缓存中添加或更新一个标签的值。
 *
 * 该函数会根据传入的组名和标签名生成一个键，然后在驱动缓存的哈希表中查找对应的元素。
 * 如果找到匹配的元素，则更新该元素的值；如果没有找到，则创建一个新的元素并添加到哈希表中。
 * 为了保证线程安全，在操作过程中会使用互斥锁对缓存进行加锁和解锁。
 *
 * @param cache 指向 neu_driver_cache_t 结构体的指针，表示要操作的驱动缓存。
 * @param group 指向字符串的指针，表示要添加或更新的数据所在的组名。
 * @param tag 指向字符串的指针，表示要添加或更新的数据的标签名。
 * @param value neu_dvalue_t 类型的值，表示要添加或更新的数据的值。
 *
 * @return 无返回值。
 */
void neu_driver_cache_add(neu_driver_cache_t *cache, const char *group,
                          const char *tag, neu_dvalue_t value)
{
    struct elem *elem = NULL;
    tkey_t       key  = to_key(group, tag);

    pthread_mutex_lock(&cache->mtx);
    HASH_FIND(hh, cache->table, &key, sizeof(tkey_t), elem);

    if (elem == NULL) {
        elem = calloc(1, sizeof(struct elem));

        strcpy(elem->key.group, group);
        strcpy(elem->key.tag, tag);

        HASH_ADD(hh, cache->table, key, sizeof(tkey_t), elem);
    }

    elem->timestamp = 0;
    elem->changed   = false;
    elem->value     = value;

    pthread_mutex_unlock(&cache->mtx);
}

/**
 * @brief 更新驱动缓存中的跟踪信息。
 *
 * 此函数用于更新指定组的跟踪上下文。如果该组不存在于跟踪表中，
 * 则会创建一个新的条目并将提供的跟踪上下文存储在其中。
 * 如果该组已存在，则更新其跟踪上下文。
 *
 * @param cache 指向驱动缓存对象的指针。
 * @param group 组名称。
 * @param trace_ctx 跟踪上下文，用于记录或追踪操作。
 */
void neu_driver_cache_update_trace(neu_driver_cache_t *cache, const char *group,
                                   void *trace_ctx)
{
    group_trace_t *elem                    = NULL;  // 用于存储找到的组跟踪信息
    char           key[NEU_GROUP_NAME_LEN] = { 0 }; // 存储组名作为查找键

    strcpy(key, group);

    pthread_mutex_lock(&cache->mtx);
    HASH_FIND(hh, cache->trace_table, &key, sizeof(key), elem);

    if (elem == NULL) {
        // 如果找不到对应的组跟踪信息，则分配新的内存并初始化
        elem = calloc(1, sizeof(group_trace_t));

        // 设置组名和跟踪上下文
        strcpy(elem->key, group);
        elem->trace_ctx = trace_ctx;

        // 将新元素添加到哈希表中
        HASH_ADD(hh, cache->trace_table, key, sizeof(key), elem);
    }

    // 如果找到了对应的组跟踪信息，则仅更新其跟踪上下文
    elem->trace_ctx = trace_ctx;

    pthread_mutex_unlock(&cache->mtx);
}

void *neu_driver_cache_get_trace(neu_driver_cache_t *cache, const char *group)
{
    group_trace_t *elem                    = NULL;
    char           key[NEU_GROUP_NAME_LEN] = { 0 };

    void *trace = NULL;

    strcpy(key, group);

    pthread_mutex_lock(&cache->mtx);
    HASH_FIND(hh, cache->trace_table, &key, sizeof(key), elem);

    if (elem != NULL) {
        trace = elem->trace_ctx;
    }

    pthread_mutex_unlock(&cache->mtx);

    return trace;
}

/**
 * @brief 更新驱动缓存中的数据值或变化事件。
 *
 * 该函数用于更新指定组和标签的数据值到驱动缓存中。它首先锁定缓存以确保线程安全，
 * 然后查找对应的缓存元素，并根据传入的新值和类型更新该元素。如果新值与旧值不同，
 * 则设置 `changed` 标志位为 true。此函数还支持过滤错误类型的变化。
 *
 * @param cache 指向 neu_driver_cache_t 结构体的指针，表示需要更新的驱动缓存。
 * @param group 组名字符串，标识所属的组。
 * @param tag 标签名字符串，标识组内的具体标签。
 * @param timestamp 时间戳，标识数据的时间。
 * @param value 包含新值及类型的 neu_dvalue_t 结构体。
 * @param metas 元数据数组，包含与该值相关的元数据信息。
 * @param n_meta 元数据的数量。
 * @param change 如果为 true，则表示这是一个变化事件；否则不是。
 */
void neu_driver_cache_update_change(neu_driver_cache_t *cache,
                                    const char *group, const char *tag,
                                    int64_t timestamp, neu_dvalue_t value,
                                    neu_tag_meta_t *metas, int n_meta,
                                    bool change)
{
    struct elem *elem = NULL;
    tkey_t       key  = to_key(group, tag); //tag为nul,后续elem为null

    // 锁定缓存以确保线程安全
    pthread_mutex_lock(&cache->mtx);

    // 查找哈希表中是否存在对应键的元素
    HASH_FIND(hh, cache->table, &key, sizeof(tkey_t), elem);
    if (elem != NULL) {
        elem->timestamp = timestamp;

        // 若启用错误过滤且新值类型为错误类型，则不报告变化
        if (sub_filter_err && value.type == NEU_TYPE_ERROR) {
            goto error_not_report;
        }

        /**
         * @note
         *  判断元素是否发生变化
         *  -1.类型改变
         *      当未启用错误过滤（!sub_filter_err）且元素的旧类型和新类型不同时，
         *      或者启用了错误过滤且元素的旧类型既不是错误类型也和新类型不同时，
         *      将 elem->changed 标记为 true，表示元素发生了变化。
         *  -2.旧类型为错误类型，新类型不同
         *      当启用了错误过滤，元素的旧类型为错误类型且和新类型不同时，进入此分支。
         *      根据新值的类型，使用 memcmp 或其他比较方法比较实际值是否发生变化，
         *      如果变化则将 elem->changed 标记为 true。
         *  -3.其他情况
         *      在其他情况下，同样根据新值的类型，使用 memcmp 或其他比较方法比较实际值是否发生变化，
         *      如果变化则将 elem->changed 标记为 true。 
         */
        if ((!sub_filter_err && elem->value.type != value.type) ||
            (sub_filter_err && elem->value.type != value.type &&
             elem->value.type != NEU_TYPE_ERROR)) {
            elem->changed = true;
        } else if (sub_filter_err && elem->value.type != value.type &&
                   elem->value.type == NEU_TYPE_ERROR) { 
            switch (value.type) {
            case NEU_TYPE_INT8:
            case NEU_TYPE_UINT8:
            case NEU_TYPE_INT16:
            case NEU_TYPE_UINT16:
            case NEU_TYPE_INT32:
            case NEU_TYPE_UINT32:
            case NEU_TYPE_INT64:
            case NEU_TYPE_UINT64:
            case NEU_TYPE_BIT:
            case NEU_TYPE_BOOL:
            case NEU_TYPE_STRING:
            case NEU_TYPE_TIME:
            case NEU_TYPE_DATA_AND_TIME:
            case NEU_TYPE_WORD:
            case NEU_TYPE_DWORD:
            case NEU_TYPE_LWORD:
            case NEU_TYPE_ARRAY_CHAR:
                if (memcmp(&elem->value_old.value, &value.value,
                           sizeof(value.value)) != 0) {
                    elem->changed = true;
                }
                break;
            case NEU_TYPE_BYTES:
                if (elem->value_old.value.bytes.length !=
                    value.value.bytes.length) {
                    elem->changed = true;
                } else {
                    if (memcmp(elem->value_old.value.bytes.bytes,
                               value.value.bytes.bytes,
                               value.value.bytes.length) != 0) {
                        elem->changed = true;
                    }
                }
                break;
            case NEU_TYPE_ARRAY_BOOL:
                if (elem->value_old.value.bools.length !=
                    value.value.bools.length) {
                    elem->changed = true;
                } else {
                    if (memcmp(elem->value_old.value.bools.bools,
                               value.value.bools.bools,
                               value.value.bools.length) != 0) {
                        elem->changed = true;
                    }
                }
                break;
            case NEU_TYPE_ARRAY_INT8:
                if (elem->value_old.value.i8s.length !=
                    value.value.i8s.length) {
                    elem->changed = true;
                } else {
                    if (memcmp(elem->value_old.value.i8s.i8s,
                               value.value.i8s.i8s,
                               value.value.i8s.length) != 0) {
                        elem->changed = true;
                    }
                }
                break;
            case NEU_TYPE_ARRAY_UINT8:
                if (elem->value_old.value.u8s.length !=
                    value.value.u8s.length) {
                    elem->changed = true;
                } else {
                    if (memcmp(elem->value_old.value.u8s.u8s,
                               value.value.u8s.u8s,
                               value.value.u8s.length) != 0) {
                        elem->changed = true;
                    }
                }
                break;
            case NEU_TYPE_ARRAY_INT16:
                if (elem->value_old.value.i16s.length !=
                    value.value.i16s.length) {
                    elem->changed = true;
                } else {
                    if (memcmp(elem->value_old.value.i16s.i16s,
                               value.value.i16s.i16s,
                               value.value.i16s.length * sizeof(int16_t)) !=
                        0) {
                        elem->changed = true;
                    }
                }
                break;
            case NEU_TYPE_ARRAY_UINT16:
                if (elem->value_old.value.u16s.length !=
                    value.value.u16s.length) {
                    elem->changed = true;
                } else {
                    if (memcmp(elem->value_old.value.u16s.u16s,
                               value.value.u16s.u16s,
                               value.value.u16s.length * sizeof(uint16_t)) !=
                        0) {
                        elem->changed = true;
                    }
                }
                break;
            case NEU_TYPE_ARRAY_INT32:
                if (elem->value_old.value.i32s.length !=
                    value.value.i32s.length) {
                    elem->changed = true;
                } else {
                    if (memcmp(elem->value_old.value.i32s.i32s,
                               value.value.i32s.i32s,
                               value.value.i32s.length * sizeof(int32_t)) !=
                        0) {
                        elem->changed = true;
                    }
                }
                break;
            case NEU_TYPE_ARRAY_UINT32:
                if (elem->value_old.value.u32s.length !=
                    value.value.u32s.length) {
                    elem->changed = true;
                } else {
                    if (memcmp(elem->value_old.value.u32s.u32s,
                               value.value.u32s.u32s,
                               value.value.u32s.length * sizeof(uint32_t)) !=
                        0) {
                        elem->changed = true;
                    }
                }
                break;
            case NEU_TYPE_ARRAY_INT64:
                if (elem->value_old.value.i64s.length !=
                    value.value.i64s.length) {
                    elem->changed = true;
                } else {
                    if (memcmp(elem->value_old.value.i64s.i64s,
                               value.value.i64s.i64s,
                               value.value.i64s.length * sizeof(int64_t)) !=
                        0) {
                        elem->changed = true;
                    }
                }
                break;
            case NEU_TYPE_ARRAY_UINT64:
                if (elem->value_old.value.u64s.length !=
                    value.value.u64s.length) {
                    elem->changed = true;
                } else {
                    if (memcmp(elem->value_old.value.u64s.u64s,
                               value.value.u64s.u64s,
                               value.value.u64s.length * sizeof(uint64_t)) !=
                        0) {
                        elem->changed = true;
                    }
                }
                break;
            case NEU_TYPE_ARRAY_FLOAT:
                if (elem->value_old.value.f32s.length !=
                    value.value.f32s.length) {
                    elem->changed = true;
                } else {
                    if (memcmp(elem->value_old.value.f32s.f32s,
                               value.value.f32s.f32s,
                               value.value.f32s.length * sizeof(float)) != 0) {
                        elem->changed = true;
                    }
                }
                break;
            case NEU_TYPE_ARRAY_DOUBLE:
                if (elem->value_old.value.f64s.length !=
                    value.value.f64s.length) {
                    elem->changed = true;
                } else {
                    if (memcmp(elem->value_old.value.f64s.f64s,
                               value.value.f64s.f64s,
                               value.value.f64s.length * sizeof(double)) != 0) {
                        elem->changed = true;
                    }
                }
                break;
            case NEU_TYPE_ARRAY_STRING:
                if (elem->value_old.value.strs.length !=
                    value.value.strs.length) {
                    elem->changed = true;
                } else {
                    if (memcmp(elem->value_old.value.strs.strs,
                               value.value.strs.strs,
                               value.value.strs.length * sizeof(char *)) != 0) {
                        elem->changed = true;
                    }
                }
                break;
            case NEU_TYPE_CUSTOM: {
                if (json_equal(elem->value_old.value.json, value.value.json) !=
                    0) {
                    elem->changed = true;
                }
                break;
            }
            case NEU_TYPE_PTR: {
                if (elem->value_old.value.ptr.length !=
                    value.value.ptr.length) {
                    elem->changed = true;
                } else {
                    if (memcmp(elem->value_old.value.ptr.ptr,
                               value.value.ptr.ptr,
                               value.value.ptr.length) != 0) {
                        elem->changed = true;
                    }
                }

                break;
            }
            case NEU_TYPE_FLOAT:
                if (elem->value_old.precision == 0) {
                    elem->changed =
                        elem->value_old.value.f32 != value.value.f32;
                } else {
                    if (fabs(elem->value_old.value.f32 - value.value.f32) >
                        pow(0.1, elem->value_old.precision)) {
                        elem->changed = true;
                    }
                }
                break;
            case NEU_TYPE_DOUBLE:
                if (elem->value_old.precision == 0) {
                    elem->changed =
                        elem->value_old.value.d64 != value.value.d64;
                } else {
                    if (fabs(elem->value_old.value.d64 - value.value.d64) >
                        pow(0.1, elem->value_old.precision)) {
                        elem->changed = true;
                    }
                }

                break;
            case NEU_TYPE_ERROR:
                break;
            }
        } else {
            switch (value.type) {
            case NEU_TYPE_INT8:
            case NEU_TYPE_UINT8:
            case NEU_TYPE_INT16:
            case NEU_TYPE_UINT16:
            case NEU_TYPE_INT32:
            case NEU_TYPE_UINT32:
            case NEU_TYPE_INT64:
            case NEU_TYPE_UINT64:
            case NEU_TYPE_BIT:
            case NEU_TYPE_BOOL:
            case NEU_TYPE_STRING:
            case NEU_TYPE_TIME:
            case NEU_TYPE_DATA_AND_TIME:
            case NEU_TYPE_WORD:
            case NEU_TYPE_DWORD:
            case NEU_TYPE_LWORD:
            case NEU_TYPE_ARRAY_CHAR:
                if (memcmp(&elem->value.value, &value.value,
                           sizeof(value.value)) != 0) {
                    elem->changed = true;
                }
                break;
            case NEU_TYPE_BYTES:
                if (elem->value.value.bytes.length !=
                    value.value.bytes.length) {
                    elem->changed = true;
                } else {
                    if (memcmp(elem->value.value.bytes.bytes,
                               value.value.bytes.bytes,
                               value.value.bytes.length) != 0) {
                        elem->changed = true;
                    }
                }
                break;
            case NEU_TYPE_ARRAY_BOOL:
                if (elem->value.value.bools.length !=
                    value.value.bools.length) {
                    elem->changed = true;
                } else {
                    if (memcmp(elem->value.value.bools.bools,
                               value.value.bools.bools,
                               value.value.bools.length) != 0) {
                        elem->changed = true;
                    }
                }
                break;
            case NEU_TYPE_ARRAY_INT8:
                if (elem->value.value.i8s.length != value.value.i8s.length) {
                    elem->changed = true;
                } else {
                    if (memcmp(elem->value.value.i8s.i8s, value.value.i8s.i8s,
                               value.value.i8s.length) != 0) {
                        elem->changed = true;
                    }
                }
                break;
            case NEU_TYPE_ARRAY_UINT8:
                if (elem->value.value.u8s.length != value.value.u8s.length) {
                    elem->changed = true;
                } else {
                    if (memcmp(elem->value.value.u8s.u8s, value.value.u8s.u8s,
                               value.value.u8s.length) != 0) {
                        elem->changed = true;
                    }
                }
                break;
            case NEU_TYPE_ARRAY_INT16:
                if (elem->value.value.i16s.length != value.value.i16s.length) {
                    elem->changed = true;
                } else {
                    if (memcmp(
                            elem->value.value.i16s.i16s, value.value.i16s.i16s,
                            value.value.i16s.length * sizeof(int16_t)) != 0) {
                        elem->changed = true;
                    }
                }
                break;
            case NEU_TYPE_ARRAY_UINT16:
                if (elem->value.value.u16s.length != value.value.u16s.length) {
                    elem->changed = true;
                } else {
                    if (memcmp(
                            elem->value.value.u16s.u16s, value.value.u16s.u16s,
                            value.value.u16s.length * sizeof(uint16_t)) != 0) {
                        elem->changed = true;
                    }
                }
                break;
            case NEU_TYPE_ARRAY_INT32:
                if (elem->value.value.i32s.length != value.value.i32s.length) {
                    elem->changed = true;
                } else {
                    if (memcmp(
                            elem->value.value.i32s.i32s, value.value.i32s.i32s,
                            value.value.i32s.length * sizeof(int32_t)) != 0) {
                        elem->changed = true;
                    }
                }
                break;
            case NEU_TYPE_ARRAY_UINT32:
                if (elem->value.value.u32s.length != value.value.u32s.length) {
                    elem->changed = true;
                } else {
                    if (memcmp(
                            elem->value.value.u32s.u32s, value.value.u32s.u32s,
                            value.value.u32s.length * sizeof(uint32_t)) != 0) {
                        elem->changed = true;
                    }
                }
                break;
            case NEU_TYPE_ARRAY_INT64:
                if (elem->value.value.i64s.length != value.value.i64s.length) {
                    elem->changed = true;
                } else {
                    if (memcmp(
                            elem->value.value.i64s.i64s, value.value.i64s.i64s,
                            value.value.i64s.length * sizeof(int64_t)) != 0) {
                        elem->changed = true;
                    }
                }
                break;
            case NEU_TYPE_ARRAY_UINT64:
                if (elem->value.value.u64s.length != value.value.u64s.length) {
                    elem->changed = true;
                } else {
                    if (memcmp(
                            elem->value.value.u64s.u64s, value.value.u64s.u64s,
                            value.value.u64s.length * sizeof(uint64_t)) != 0) {
                        elem->changed = true;
                    }
                }
                break;
            case NEU_TYPE_ARRAY_FLOAT:
                if (elem->value.value.f32s.length != value.value.f32s.length) {
                    elem->changed = true;
                } else {
                    if (memcmp(elem->value.value.f32s.f32s,
                               value.value.f32s.f32s,
                               value.value.f32s.length * sizeof(float)) != 0) {
                        elem->changed = true;
                    }
                }
                break;
            case NEU_TYPE_ARRAY_DOUBLE:
                if (elem->value.value.f64s.length != value.value.f64s.length) {
                    elem->changed = true;
                } else {
                    if (memcmp(elem->value.value.f64s.f64s,
                               value.value.f64s.f64s,
                               value.value.f64s.length * sizeof(double)) != 0) {
                        elem->changed = true;
                    }
                }
                break;
            case NEU_TYPE_ARRAY_STRING:
                if (elem->value.value.strs.length != value.value.strs.length) {
                    elem->changed = true;
                } else {
                    if (memcmp(elem->value.value.strs.strs,
                               value.value.strs.strs,
                               value.value.strs.length * sizeof(char *)) != 0) {
                        elem->changed = true;
                    }
                }
                break;
            case NEU_TYPE_PTR: {
                if (elem->value.value.ptr.length != value.value.ptr.length) {
                    elem->changed = true;
                } else {
                    if (memcmp(elem->value.value.ptr.ptr, value.value.ptr.ptr,
                               value.value.ptr.length) != 0) {
                        elem->changed = true;
                    }
                }

                break;
            }
            case NEU_TYPE_CUSTOM: {
                if (json_equal(elem->value.value.json, value.value.json) != 0) {
                    elem->changed = true;
                }
                break;
            }
            case NEU_TYPE_FLOAT:
                if (elem->value.precision == 0) {
                    elem->changed = elem->value.value.f32 != value.value.f32;
                } else {
                    if (fabs(elem->value.value.f32 - value.value.f32) >
                        pow(0.1, elem->value.precision)) {
                        elem->changed = true;
                    }
                }
                break;
            case NEU_TYPE_DOUBLE:
                if (elem->value.precision == 0) {
                    elem->changed = elem->value.value.d64 != value.value.d64;
                } else {
                    if (fabs(elem->value.value.d64 - value.value.d64) >
                        pow(0.1, elem->value.precision)) {
                        elem->changed = true;
                    }
                }

                break;
            case NEU_TYPE_ERROR:
                elem->changed = true;
                break;
            }
        }

        //更新旧值
        if (sub_filter_err && value.type != NEU_TYPE_ERROR) {
            elem->value_old.type      = value.type;
            elem->value_old.value     = value.value;
            elem->value_old.precision = value.precision;
        }

    error_not_report:

        //强制标记变化
        if (change) {
            elem->changed = true;
        }
        
        //根据新值类型进行相应的内存管理和数据更新

        // 根据新值的类型进行不同的处理
        if (value.type == NEU_TYPE_PTR) {
            elem->value.value.ptr.length = value.value.ptr.length;
            elem->value.value.ptr.type   = value.value.ptr.type;
            if (elem->value.value.ptr.ptr != NULL) {
                // 释放旧指针指向的内存
                free(elem->value.value.ptr.ptr);
            }
            elem->value.value.ptr.ptr = calloc(1, value.value.ptr.length);
            memcpy(elem->value.value.ptr.ptr, value.value.ptr.ptr,
                   value.value.ptr.length);
        } else if (value.type == NEU_TYPE_CUSTOM) {
            if (elem->value.type == NEU_TYPE_CUSTOM) {
                if (elem->value.value.json != NULL) {
                    // 减少旧JSON对象的引用计数
                    json_decref(elem->value.value.json);

                    // 将旧JSON对象指针置空
                    elem->value.value.json = NULL;
                }
            }

            // 更新为新的JSON对象
            elem->value.value.json = value.value.json;

        } else if (value.type == NEU_TYPE_ARRAY_STRING) {
            if (elem->value.type == NEU_TYPE_ARRAY_STRING) {
                for (int i = 0; i < elem->value.value.strs.length; i++) {
                    free(elem->value.value.strs.strs[i]);
                    elem->value.value.strs.strs[i] = NULL;
                }
            }
            elem->value.value.strs.length = value.value.strs.length;
            for (int i = 0; i < value.value.strs.length; i++) {
                elem->value.value.strs.strs[i] = value.value.strs.strs[i];
            }

        } else if (value.type == NEU_TYPE_ERROR) {
            //处理自定义类型
            if (elem->value.type == NEU_TYPE_CUSTOM) {
                //该自定义类型的值所包含的 JSON 对象指针不为空。
                if (elem->value.value.json != NULL) {
                    //减少 JSON 对象的引用计数,当引用计数降为 0 时，JSON 对象可能会被释放。
                    json_decref(elem->value.value.json);
                    elem->value.value.json = NULL;
                }
            }
            //处理字符串数组类型
            if (elem->value.type == NEU_TYPE_ARRAY_STRING) {
                for (int i = 0; i < elem->value.value.strs.length; i++) {
                    //释放每个字符串所占用的内存
                    free(elem->value.value.strs.strs[i]);
                    //将释放后的指针置为 NULL，避免悬空指针。
                    elem->value.value.strs.strs[i] = NULL;
                }
            }
            //将 elem 的值更新为新的错误值 value,反映当前出现错误的状态。
            elem->value.value = value.value;
        } else {
            elem->value.value = value.value;
        }
        elem->value.type = value.type;

        memset(elem->metas, 0, sizeof(neu_tag_meta_t) * NEU_TAG_META_SIZE);
        for (int i = 0; i < n_meta; i++) {
            memcpy(&elem->metas[i], &metas[i], sizeof(neu_tag_meta_t));
        }
    }

    pthread_mutex_unlock(&cache->mtx);
}

/**
 * @brief 更新驱动缓存中的数据值。
 *
 * 该函数用于更新指定组和标签的数据值到驱动缓存中。它实际上是调用了一个更通用的函数
 * `neu_driver_cache_update_change` 来完成实际的工作，并传递了额外的参数 `false` 表示这不是一个变化事件。
 *
 * @param cache 指向 neu_driver_cache_t 结构体的指针，表示需要更新的驱动缓存。
 * @param group 组名字符串，标识所属的组。
 * @param tag 标签名字符串，标识组内的具体标签。
 * @param timestamp 时间戳，标识数据的时间。
 * @param value 包含新值及类型的 neu_dvalue_t 结构体。
 * @param metas 元数据数组，包含与该值相关的元数据信息。
 * @param n_meta 元数据的数量。
 */
void neu_driver_cache_update(neu_driver_cache_t *cache, const char *group,
                             const char *tag, int64_t timestamp,
                             neu_dvalue_t value, neu_tag_meta_t *metas,
                             int n_meta)
{
    neu_driver_cache_update_change(cache, group, tag, timestamp, value, metas,
                                   n_meta, false);
}

/**
 * @brief 从缓存中获取指定组和标签的值及其元数据。
 *
 * 此函数根据提供的组名和标签名，在缓存中查找对应的元素，并将该元素的值和元数据复制到提供的结构体中。
 * 如果找到相应的元素，则返回0；否则返回-1。
 *
 * @param cache 指向缓存对象的指针。
 * @param group 组名称。
 * @param tag 标签名称。
 * @param value 指向存储结果的缓存值结构体的指针。
 * @param metas 元数据数组。
 * @param n_meta 元数据数组的大小。
 * @return 成功获取值时返回0，未找到对应元素时返回-1。
 */
int neu_driver_cache_meta_get(neu_driver_cache_t *cache, const char *group,
                              const char *tag, neu_driver_cache_value_t *value,
                              neu_tag_meta_t *metas, int n_meta)
{
    struct elem *elem = NULL;                 // 缓存元素指针
    int          ret  = -1;                   // 默认返回值，表示未找到或错误
    tkey_t       key  = to_key(group, tag);   // 创建用于查找的键

    pthread_mutex_lock(&cache->mtx);

    // 在哈希表中查找元素
    HASH_FIND(hh, cache->table, &key, sizeof(tkey_t), elem);

    if (elem != NULL) { // 如果找到了元素
        // 更新时间戳和值类型及精度
        value->timestamp       = elem->timestamp;
        value->value.type      = elem->value.type;
        value->value.precision = elem->value.precision;

        // 确保元数据数组足够大
        assert(n_meta <= NEU_TAG_META_SIZE);
        memcpy(metas, elem->metas, sizeof(neu_tag_meta_t) * NEU_TAG_META_SIZE);

        // 根据值类型处理不同的值
        switch (elem->value.type) {
        case NEU_TYPE_INT8:
        case NEU_TYPE_UINT8:
        case NEU_TYPE_BIT:
            value->value.value.u8 = elem->value.value.u8;
            break;
        case NEU_TYPE_INT16:
        case NEU_TYPE_UINT16:
        case NEU_TYPE_WORD:
            value->value.value.u16 = elem->value.value.u16;
            break;
        case NEU_TYPE_INT32:
        case NEU_TYPE_UINT32:
        case NEU_TYPE_DWORD:
        case NEU_TYPE_FLOAT:
        case NEU_TYPE_ERROR:
            value->value.value.u32 = elem->value.value.u32;
            break;
        case NEU_TYPE_INT64:
        case NEU_TYPE_UINT64:
        case NEU_TYPE_DOUBLE:
        case NEU_TYPE_LWORD:
            value->value.value.u64 = elem->value.value.u64;
            break;
        case NEU_TYPE_BOOL:
            value->value.value.boolean = elem->value.value.boolean;
            break;
        case NEU_TYPE_STRING:
        case NEU_TYPE_TIME:
        case NEU_TYPE_DATA_AND_TIME:
        case NEU_TYPE_ARRAY_CHAR:
            memcpy(value->value.value.str, elem->value.value.str,
                   sizeof(elem->value.value.str));
            break;
        case NEU_TYPE_BYTES:
            value->value.value.bytes.length = elem->value.value.bytes.length;
            memcpy(value->value.value.bytes.bytes,
                   elem->value.value.bytes.bytes,
                   elem->value.value.bytes.length);
            break;
        case NEU_TYPE_ARRAY_BOOL:
            value->value.value.bools.length = elem->value.value.bools.length;
            memcpy(value->value.value.bools.bools,
                   elem->value.value.bools.bools,
                   elem->value.value.bools.length);
            break;
        case NEU_TYPE_ARRAY_INT8:
            value->value.value.i8s.length = elem->value.value.i8s.length;
            memcpy(value->value.value.i8s.i8s, elem->value.value.i8s.i8s,
                   elem->value.value.i8s.length);
            break;
        case NEU_TYPE_ARRAY_UINT8:
            value->value.value.u8s.length = elem->value.value.u8s.length;
            memcpy(value->value.value.u8s.u8s, elem->value.value.u8s.u8s,
                   elem->value.value.u8s.length);
            break;
        case NEU_TYPE_ARRAY_INT16:
            value->value.value.i16s.length = elem->value.value.i16s.length;
            memcpy(value->value.value.i16s.i16s, elem->value.value.i16s.i16s,
                   elem->value.value.i16s.length * sizeof(int16_t));
            break;
        case NEU_TYPE_ARRAY_UINT16:
            value->value.value.u16s.length = elem->value.value.u16s.length;
            memcpy(value->value.value.u16s.u16s, elem->value.value.u16s.u16s,
                   elem->value.value.u16s.length * sizeof(uint16_t));
            break;
        case NEU_TYPE_ARRAY_INT32:
            value->value.value.i32s.length = elem->value.value.i32s.length;
            memcpy(value->value.value.i32s.i32s, elem->value.value.i32s.i32s,
                   elem->value.value.i32s.length * sizeof(int32_t));
            break;
        case NEU_TYPE_ARRAY_UINT32:
            value->value.value.u32s.length = elem->value.value.u32s.length;
            memcpy(value->value.value.u32s.u32s, elem->value.value.u32s.u32s,
                   elem->value.value.u32s.length * sizeof(uint32_t));
            break;
        case NEU_TYPE_ARRAY_INT64:
            value->value.value.i64s.length = elem->value.value.i64s.length;
            memcpy(value->value.value.i64s.i64s, elem->value.value.i64s.i64s,
                   elem->value.value.i64s.length * sizeof(int64_t));
            break;
        case NEU_TYPE_ARRAY_UINT64:
            value->value.value.u64s.length = elem->value.value.u64s.length;
            memcpy(value->value.value.u64s.u64s, elem->value.value.u64s.u64s,
                   elem->value.value.u64s.length * sizeof(uint64_t));
            break;
        case NEU_TYPE_ARRAY_FLOAT:
            value->value.value.f32s.length = elem->value.value.f32s.length;
            memcpy(value->value.value.f32s.f32s, elem->value.value.f32s.f32s,
                   elem->value.value.f32s.length * sizeof(float));
            break;
        case NEU_TYPE_ARRAY_DOUBLE:
            value->value.value.f64s.length = elem->value.value.f64s.length;
            memcpy(value->value.value.f64s.f64s, elem->value.value.f64s.f64s,
                   elem->value.value.f64s.length * sizeof(double));
            break;
        case NEU_TYPE_ARRAY_STRING:
            value->value.value.strs.length = elem->value.value.strs.length;
            for (int i = 0; i < elem->value.value.strs.length; i++) {
                value->value.value.strs.strs[i] =
                    strdup(elem->value.value.strs.strs[i]);
            }
            break;
        case NEU_TYPE_PTR:
            value->value.value.ptr.length = elem->value.value.ptr.length;
            value->value.value.ptr.type   = elem->value.value.ptr.type;
            value->value.value.ptr.ptr =
                calloc(1, elem->value.value.ptr.length);
            memcpy(value->value.value.ptr.ptr, elem->value.value.ptr.ptr,
                   elem->value.value.ptr.length);
            break;
        case NEU_TYPE_CUSTOM:
            value->value.value.json = json_deep_copy(elem->value.value.json);
            break;
        }

        // 复制元数据
        for (int i = 0; i < NEU_TAG_META_SIZE; i++) {
            if (strlen(elem->metas[i].name) > 0) {
                memcpy(&value->metas[i], &elem->metas[i],
                       sizeof(neu_tag_meta_t));
            }
        }

        ret = 0;
    }

    pthread_mutex_unlock(&cache->mtx);

    return ret;
}

/**
 * @brief 从缓存中获取指定组和标签的变化值。
 *
 * 此函数检查给定组和标签的缓存元素是否发生了变化。当指定的标签数据发生变化时，将变化后的数据值附加到
 * value 中，同时将标签的元数据复制到 metas 中，并返回0；否则返回-1。
 *
 * @param cache 指向缓存对象的指针。
 * @param group 组名称。
 * @param tag 标签名称。
 * @param value 指向存储结果的缓存值结构体的指针。
 * @param metas 元数据数组。
 * @param n_meta 元数据数组的大小。
 * @return 成功获取变化值时返回0，否则返回-1。
 * 
 * @note
 * - 对于订阅的标签和未订阅的标签可能需要采取不同的处理策略。例如，订阅的标签可能需要实时关注其值的变化，
 *   只有当值发生变化时才进行处理，这样可以减少不必要的处理开销，提高系统的效率
 */
int neu_driver_cache_meta_get_changed(neu_driver_cache_t *cache,
                                      const char *group, const char *tag,
                                      neu_driver_cache_value_t *value,
                                      neu_tag_meta_t *metas, int n_meta)
{
    struct elem *elem = NULL;                 // 缓存元素指针
    int          ret  = -1;                   // 返回值，默认为失败
    tkey_t       key  = to_key(group, tag);   // 创建标签唯一标识

    pthread_mutex_lock(&cache->mtx);

    // 查找哈希表中的元素
    HASH_FIND(hh, cache->table, &key, sizeof(tkey_t), elem);

    if (elem != NULL && elem->changed) {                 // 如果找到元素且已更改
        value->timestamp       = elem->timestamp;        // 更新时间戳
        value->value.type      = elem->value.type;       // 更新类型
        value->value.precision = elem->value.precision;  // 更新精度

        // 确保元数据数组足够大
        assert(n_meta <= NEU_TAG_META_SIZE);
        memcpy(metas, elem->metas, sizeof(neu_tag_meta_t) * NEU_TAG_META_SIZE);

        // 根据类型处理不同的值
        switch (elem->value.type) {
        case NEU_TYPE_INT8:
        case NEU_TYPE_UINT8:
        case NEU_TYPE_BIT:
            value->value.value.u8 = elem->value.value.u8;
            break;
        case NEU_TYPE_INT16:
        case NEU_TYPE_UINT16:
        case NEU_TYPE_WORD:
            value->value.value.u16 = elem->value.value.u16;
            break;
        case NEU_TYPE_INT32:
        case NEU_TYPE_UINT32:
        case NEU_TYPE_FLOAT:
        case NEU_TYPE_ERROR:
        case NEU_TYPE_DWORD:
            value->value.value.u32 = elem->value.value.u32;
            break;
        case NEU_TYPE_INT64:
        case NEU_TYPE_UINT64:
        case NEU_TYPE_DOUBLE:
        case NEU_TYPE_LWORD:
            value->value.value.u64 = elem->value.value.u64;
            break;
        case NEU_TYPE_BOOL:
            value->value.value.boolean = elem->value.value.boolean;
            break;
        case NEU_TYPE_STRING:
        case NEU_TYPE_TIME:
        case NEU_TYPE_DATA_AND_TIME:
        case NEU_TYPE_ARRAY_CHAR:
            memcpy(value->value.value.str, elem->value.value.str,
                   sizeof(elem->value.value.str));
            break;
        case NEU_TYPE_BYTES:
            value->value.value.bytes.length = elem->value.value.bytes.length;
            memcpy(value->value.value.bytes.bytes,
                   elem->value.value.bytes.bytes,
                   elem->value.value.bytes.length);
            break;
        case NEU_TYPE_ARRAY_BOOL:
            value->value.value.bools.length = elem->value.value.bools.length;
            memcpy(value->value.value.bools.bools,
                   elem->value.value.bools.bools,
                   elem->value.value.bools.length);
            break;
        case NEU_TYPE_ARRAY_INT8:
            value->value.value.i8s.length = elem->value.value.i8s.length;
            memcpy(value->value.value.i8s.i8s, elem->value.value.i8s.i8s,
                   elem->value.value.i8s.length);
            break;
        case NEU_TYPE_ARRAY_UINT8:
            value->value.value.u8s.length = elem->value.value.u8s.length;
            memcpy(value->value.value.u8s.u8s, elem->value.value.u8s.u8s,
                   elem->value.value.u8s.length);
            break;
        case NEU_TYPE_ARRAY_INT16:
            value->value.value.i16s.length = elem->value.value.i16s.length;
            memcpy(value->value.value.i16s.i16s, elem->value.value.i16s.i16s,
                   elem->value.value.i16s.length * sizeof(int16_t));
            break;
        case NEU_TYPE_ARRAY_UINT16:
            value->value.value.u16s.length = elem->value.value.u16s.length;
            memcpy(value->value.value.u16s.u16s, elem->value.value.u16s.u16s,
                   elem->value.value.u16s.length * sizeof(uint16_t));
            break;
        case NEU_TYPE_ARRAY_INT32:
            value->value.value.i32s.length = elem->value.value.i32s.length;
            memcpy(value->value.value.i32s.i32s, elem->value.value.i32s.i32s,
                   elem->value.value.i32s.length * sizeof(int32_t));
            break;
        case NEU_TYPE_ARRAY_UINT32:
            value->value.value.u32s.length = elem->value.value.u32s.length;
            memcpy(value->value.value.u32s.u32s, elem->value.value.u32s.u32s,
                   elem->value.value.u32s.length * sizeof(uint32_t));
            break;
        case NEU_TYPE_ARRAY_INT64:
            value->value.value.i64s.length = elem->value.value.i64s.length;
            memcpy(value->value.value.i64s.i64s, elem->value.value.i64s.i64s,
                   elem->value.value.i64s.length * sizeof(int64_t));
            break;
        case NEU_TYPE_ARRAY_UINT64:
            value->value.value.u64s.length = elem->value.value.u64s.length;
            memcpy(value->value.value.u64s.u64s, elem->value.value.u64s.u64s,
                   elem->value.value.u64s.length * sizeof(uint64_t));
            break;
        case NEU_TYPE_ARRAY_FLOAT:
            value->value.value.f32s.length = elem->value.value.f32s.length;
            memcpy(value->value.value.f32s.f32s, elem->value.value.f32s.f32s,
                   elem->value.value.f32s.length * sizeof(float));
            break;
        case NEU_TYPE_ARRAY_DOUBLE:
            value->value.value.f64s.length = elem->value.value.f64s.length;
            memcpy(value->value.value.f64s.f64s, elem->value.value.f64s.f64s,
                   elem->value.value.f64s.length * sizeof(double));
            break;
        case NEU_TYPE_ARRAY_STRING:
            value->value.value.strs.length = elem->value.value.strs.length;
            for (int i = 0; i < elem->value.value.strs.length; i++) {
                value->value.value.strs.strs[i] =
                    strdup(elem->value.value.strs.strs[i]);
            }
            break;
        case NEU_TYPE_PTR:
            value->value.value.ptr.length = elem->value.value.ptr.length;
            value->value.value.ptr.type   = elem->value.value.ptr.type;
            value->value.value.ptr.ptr =
                calloc(1, elem->value.value.ptr.length);
            memcpy(value->value.value.ptr.ptr, elem->value.value.ptr.ptr,
                   elem->value.value.ptr.length);
            break;
        case NEU_TYPE_CUSTOM:
            value->value.value.json = json_deep_copy(elem->value.value.json);
            break;
        }

        for (int i = 0; i < NEU_TAG_META_SIZE; i++) {
            if (strlen(elem->metas[i].name) > 0) {
                memcpy(&value->metas[i], &elem->metas[i],
                       sizeof(neu_tag_meta_t));
            }
        }

        // 如果不是错误类型，重置changed标志
        if (elem->value.type != NEU_TYPE_ERROR) {
            elem->changed = false;
        }

        // 设置成功返回值
        ret = 0;
    }

    pthread_mutex_unlock(&cache->mtx);

    return ret;
}

/**
 * @brief 从驱动缓存中删除指定组和标签的数据。
 *
 * 该函数会根据传入的组名和标签名生成一个键，然后在驱动缓存的哈希表中查找对应的元素。
 * 如果找到匹配的元素，将其从哈希表中删除，并根据元素的值类型释放其占用的内存。
 * 为了保证线程安全，在操作过程中会使用互斥锁对缓存进行加锁和解锁。
 *
 * @param cache 指向 neu_driver_cache_t 结构体的指针，表示要操作的驱动缓存。
 * @param group 指向字符串的指针，表示要删除数据所在的组名。
 * @param tag 指向字符串的指针，表示要删除的数据的标签名。
 *
 * @return 无返回值。
 */
void neu_driver_cache_del(neu_driver_cache_t *cache, const char *group,
                          const char *tag)
{
    struct elem *elem = NULL;
    tkey_t       key  = to_key(group, tag);

    pthread_mutex_lock(&cache->mtx);
    HASH_FIND(hh, cache->table, &key, sizeof(tkey_t), elem);

    if (elem != NULL) {
        // 将元素从哈希表中删除
        HASH_DEL(cache->table, elem);

        // 根据元素的值类型，释放其占用的内存
        if (elem->value.type == NEU_TYPE_PTR) {
            if (elem->value.value.ptr.ptr != NULL) {
                free(elem->value.value.ptr.ptr);
                elem->value.value.ptr.ptr = NULL;
            }
        } else if (elem->value.type == NEU_TYPE_CUSTOM) {
            if (elem->value.value.json != NULL) {
                json_decref(elem->value.value.json);
                elem->value.value.json = NULL;
            }
        } else if (elem->value.type == NEU_TYPE_ARRAY_STRING) {
            for (int i = 0; i < elem->value.value.strs.length; i++) {
                free(elem->value.value.strs.strs[i]);
                elem->value.value.strs.strs[i] = NULL;
            }
        }
        
        // 释放元素本身的内存
        free(elem);
    }

    pthread_mutex_unlock(&cache->mtx);
}
