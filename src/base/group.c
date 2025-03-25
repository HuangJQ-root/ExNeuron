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
#include <pthread.h>
#include <string.h>
#include <sys/time.h>

#include "define.h"
#include "errcodes.h"

#include "group.h"

/**
 * @struct tag_elem_t
 * @brief 该结构体用于表示一个标签元素的信息。
 *
 * 包含标签的名字、指向具体数据标签的指针以及UTHash句柄，主要用于管理与特定标签相关的数据。
 */
typedef struct tag_elem {
    /**
     * @brief 标签的名字。
     *
     * 这个字段存储了标签的名称，通常用于标识不同的标签实例。
     */
    char *name;

    /**
     * @brief 标签配置信息。
     *
     * 包含了与该标签相关的详细信息和配置，如数据类型、单位等。
     */
    neu_datatag_t *tag;

    /**
     * @brief UTHash句柄。
     *
     * 支持将该结构体作为哈希表中的元素进行高效管理，便于快速查找和操作标签。
     */
    UT_hash_handle hh;
} tag_elem_t;

/**
 * @struct neu_group
 * @brief 该结构体用于表示一个组的基本信息。
 *
 * 包含组名、标签列表、采样间隔、时间戳以及互斥锁。主要用于管理与特定组相关的数据和操作。
 */
struct neu_group {
    /**
     * @brief 组的名字。
     *
     * 这个字段存储了组的名称，通常用于标识不同的组实例。
     */
    char *name;

    /**
     * @brief 标签列表。
     *
     * 它主要用于存储和管理组的基本标签信息，这些信息是组的核心数据之一，
     * 与组的基本功能紧密相关。例如在 neu_group_del_tag 函数中，通过
     *  HASH_FIND_STR(group->tags, tag_name, el) 来查找特定名称
     * 的标签元素进行删除操作，这表明 neu_group_t 中的 tags 是用于直
     * 接管理组内标签的增删改查等基本操作。
     */
    tag_elem_t *tags;

    /**
     * @brief 采样间隔。
     *
     * 表示该组中数据采集的时间间隔（以毫秒为单位）。
     */
    uint32_t    interval;

    /**
     * @brief 时间戳。
     *
     * 记录了与该组相关的最新操作的时间戳（以毫秒为单位）。
     */
    int64_t         timestamp;

    /**
     * @brief 互斥锁。
     *
     * 用于保护对该组数据的并发访问，确保线程安全。
     */
    pthread_mutex_t mtx;
};

static UT_array *to_array(tag_elem_t *tags);
static void      update_timestamp(neu_group_t *group);

/**
 * @brief 创建一个新的 neu_group_t 类型的组对象。
 *
 * 此函数用于动态分配一个新的 neu_group_t 结构体，并对其进行初始化。
 * 初始化操作包括为组对象的名称分配内存并复制传入的名称，设置组的时间间隔，
 * 以及初始化与该组关联的互斥锁。
 *
 * @param name 指向以 null 结尾的字符串的指针，用于指定组的名称。
 *             该字符串将被复制到新分配的内存中，因此调用者仍然负责管理原始字符串的内存。
 * @param interval 组的时间间隔，以无符号 32 位整数表示。
 *                 该值将被赋值给组对象的 interval 成员。
 *
 * @return 成功时，返回一个指向新创建的 neu_group_t 结构体的指针；
 *         失败时，返回 NULL。失败可能是由于内存分配失败或互斥锁初始化失败导致的。
 *         调用者负责在不再使用该组对象时释放其内存，包括组对象本身以及其成员中的
 *         动态分配内存（如 name）。
 */
neu_group_t *neu_group_new(const char *name, uint32_t interval)
{
    neu_group_t *group = calloc(1, sizeof(neu_group_t));

    group->name     = strdup(name);
    group->interval = interval;
    pthread_mutex_init(&group->mtx, NULL);

    return group;
}

void neu_group_destroy(neu_group_t *group)
{
    tag_elem_t *el = NULL, *tmp = NULL;

    pthread_mutex_lock(&group->mtx);
    HASH_ITER(hh, group->tags, el, tmp)
    {
        HASH_DEL(group->tags, el);
        free(el->name);
        neu_tag_free(el->tag);
        free(el);
    }
    pthread_mutex_unlock(&group->mtx);

    pthread_mutex_destroy(&group->mtx);
    free(group->name);
    free(group);
}

const char *neu_group_get_name(const neu_group_t *group)
{
    return group->name;
}

int neu_group_set_name(neu_group_t *group, const char *name)
{
    char *new_name = NULL;
    if (NULL == name || NULL == (new_name = strdup(name))) {
        return NEU_ERR_EINTERNAL;
    }

    free(group->name);
    group->name = new_name;
    return 0;
}

uint32_t neu_group_get_interval(const neu_group_t *group)
{
    uint32_t interval = 0;

    interval = group->interval;

    return interval;
}

void neu_group_set_interval(neu_group_t *group, uint32_t interval)
{
    group->interval = interval;
}

int neu_group_update(neu_group_t *group, uint32_t interval)
{
    if (group->interval != interval) {
        group->interval = interval;
        update_timestamp(group);
    }

    return 0;
}

int neu_group_add_tag(neu_group_t *group, const neu_datatag_t *tag)
{
    tag_elem_t *el = NULL;

    pthread_mutex_lock(&group->mtx);
    HASH_FIND_STR(group->tags, tag->name, el);
    if (el != NULL) {
        pthread_mutex_unlock(&group->mtx);
        return NEU_ERR_TAG_NAME_CONFLICT;
    }

    el       = calloc(1, sizeof(tag_elem_t));
    el->name = strdup(tag->name);
    el->tag  = neu_tag_dup(tag);

    HASH_ADD_STR(group->tags, name, el);
    update_timestamp(group);
    pthread_mutex_unlock(&group->mtx);

    return 0;
}

int neu_group_update_tag(neu_group_t *group, const neu_datatag_t *tag)
{
    tag_elem_t *el  = NULL;
    int         ret = NEU_ERR_TAG_NOT_EXIST;

    pthread_mutex_lock(&group->mtx);
    HASH_FIND_STR(group->tags, tag->name, el);
    if (el != NULL) {
        neu_tag_copy(el->tag, tag);

        update_timestamp(group);
        ret = NEU_ERR_SUCCESS;
    }
    pthread_mutex_unlock(&group->mtx);

    return ret;
}

int neu_group_del_tag(neu_group_t *group, const char *tag_name)
{
    tag_elem_t *el  = NULL;
    int         ret = NEU_ERR_TAG_NOT_EXIST;

    pthread_mutex_lock(&group->mtx);
    HASH_FIND_STR(group->tags, tag_name, el);
    if (el != NULL) {
        HASH_DEL(group->tags, el);
        free(el->name);
        neu_tag_free(el->tag);
        free(el);

        update_timestamp(group);
        ret = NEU_ERR_SUCCESS;
    }
    pthread_mutex_unlock(&group->mtx);

    return ret;
}

UT_array *neu_group_get_tag(neu_group_t *group)
{
    UT_array *array = NULL;

    pthread_mutex_lock(&group->mtx);
    array = to_array(group->tags);
    pthread_mutex_unlock(&group->mtx);

    return array;
}

/**
 * @brief 根据给定的谓词过滤标签列表。
 *
 * 该函数用于遍历给定的标签列表，并使用提供的谓词函数筛选出符合条件的标签。
 * 筛选后的标签将被存储在一个新的 UT_array 数组中并返回。
 *
 * @param tags 指向 tag_elem_t 结构体的指针，表示需要过滤的标签列表。
 * @param predicate 一个函数指针，指向用于判断标签是否应包含在结果中的函数。
 *        此函数接受一个 neu_datatag_t 类型的指针和一个用户数据指针作为参数，并返回一个布尔值。
 * @param data 用户数据指针，传递给谓词函数，可用于提供额外的上下文信息。
 * @return 返回包含所有符合条件的标签的 UT_array 数组；如果没有任何符合条件的标签，则返回空数组。
 */
static inline UT_array *
filter_tags(tag_elem_t *tags,
            bool (*predicate)(const neu_datatag_t *, void *data), void *data)
{
    // 声明两个临时变量，用于遍历哈希表
    tag_elem_t *el = NULL, *tmp = NULL;

    // 声明一个指向 UT_array 的指针，用于存储筛选后的标签
    UT_array *  array = NULL;

    // 创建一个新的 UT_array，使用 neu_tag_get_icd() 提供的比较器初始化
    utarray_new(array, neu_tag_get_icd());

    // 使用 HASH_ITER 宏遍历所有的标签元素
    HASH_ITER(hh, tags, el, tmp)
    {
        // 如果当前标签通过了谓词函数的测试，则将其添加到结果数组中
        if (predicate(el->tag, data)) {
            utarray_push_back(array, el->tag);
        }
    }

    // 返回包含筛选后标签的数组
    return array;
}

static inline UT_array *filter_tags_by_page(
    tag_elem_t *tags, bool (*predicate)(const neu_datatag_t *, void *data),
    void *data, int current_page, int page_size, int *total_count)
{
    tag_elem_t *el = NULL, *tmp = NULL;
    UT_array *  array       = NULL;
    int         count       = 0;
    int         start_index = (current_page - 1) * page_size;
    int         end_index   = start_index + page_size;

    utarray_new(array, neu_tag_get_icd());
    HASH_ITER(hh, tags, el, tmp)
    {
        if (predicate(el->tag, data)) {
            if (count >= start_index && count < end_index) {
                utarray_push_back(array, el->tag);
            }
            count++;
        }
    }

    if (total_count) {
        *total_count = count;
    }

    return array;
}

/**
 * @brief 判断标签是否具有可读或可订阅属性。
 *
 * 该函数用于检查给定的数据标签是否设置了可读（NEU_ATTRIBUTE_READ）或可订阅（NEU_ATTRIBUTE_SUBSCRIBE）属性。
 * 此函数通常作为过滤器谓词函数使用，以筛选出可读或可订阅的标签。
 *
 * @param tag 指向 neu_datatag_t 结构体的指针，表示需要检查的标签。
 * @param data 用户数据指针，在此函数中未使用。
 * @return 如果标签具有可读或可订阅属性，则返回 true；否则返回 false。
 */
static inline bool is_readable(const neu_datatag_t *tag, void *data)
{
    (void) data;
    return neu_tag_attribute_test(tag, NEU_ATTRIBUTE_READ) ||
        neu_tag_attribute_test(tag, NEU_ATTRIBUTE_SUBSCRIBE);
}

static inline bool name_contains(const neu_datatag_t *tag, void *data)
{
    const char *name = data;
    return strstr(tag->name, name) != NULL ||
        (tag->description != NULL && strstr(tag->description, name) != NULL);
}

static inline bool description_contains(const neu_datatag_t *tag, void *data)
{
    const char *str = data;
    return tag->description && strstr(tag->description, str) != NULL;
}

struct query {
    char *   name;
    char *   desc;
    uint16_t n_tagname;
    char **  tagnames;
};

static inline bool match_query(const neu_datatag_t *tag, void *data)
{
    struct query *q      = data;
    bool          filter = (!q->name || name_contains(tag, q->name)) &&
        (!q->desc || description_contains(tag, q->desc));
    if (filter && q->n_tagname > 0) {
        for (uint16_t i = 0; i < q->n_tagname; i++) {
            if (strcmp(tag->name, q->tagnames[i]) == 0) {
                return true;
            }
        }
        return false;
    }
    return filter;
}

static inline bool is_readable_and_match_query(const neu_datatag_t *tag,
                                               void *               data)
{
    return is_readable(tag, NULL) && match_query(tag, data);
}

UT_array *neu_group_query_tag(neu_group_t *group, const char *name)
{
    UT_array *array = NULL;

    pthread_mutex_lock(&group->mtx);
    array = filter_tags(group->tags, name_contains, (void *) name);
    pthread_mutex_unlock(&group->mtx);

    return array;
}

UT_array *neu_group_query_read_tag(neu_group_t *group, const char *name,
                                   const char *desc, uint16_t n_tagname,
                                   char **tagnames)
{
    UT_array *   array = NULL;
    struct query q     = {
        .name      = (char *) name,
        .desc      = (char *) desc,
        .n_tagname = n_tagname,
        .tagnames  = tagnames,
    };

    pthread_mutex_lock(&group->mtx);
    array = filter_tags(group->tags, is_readable_and_match_query, &q);
    pthread_mutex_unlock(&group->mtx);

    return array;
}

UT_array *neu_group_query_read_tag_paginate(neu_group_t *group,
                                            const char *name, const char *desc,
                                            int current_page, int page_size,
                                            int *total_count)
{
    UT_array *   array = NULL;
    struct query q     = {
        .name = (char *) name,
        .desc = (char *) desc,
    };

    pthread_mutex_lock(&group->mtx);
    array = filter_tags_by_page(group->tags, match_query, &q, current_page,
                                page_size, total_count);
    pthread_mutex_unlock(&group->mtx);

    return array;
}

/**
 * @brief 获取组中所有可读的标签。
 *
 * 该函数用于获取指定组中所有可读的标签，并将它们存储在一个 UT_array 数组中返回。
 * 在访问组中的标签之前，会先锁定组的互斥锁以确保线程安全。
 *
 * @param group 指向 neu_group_t 结构体的指针，表示需要查询的组。
 * @return 返回包含所有可读标签的 UT_array 数组；如果没有任何可读标签，则返回空数组或 NULL。
 */
UT_array *neu_group_get_read_tag(neu_group_t *group)
{
    // 声明一个指向 UT_array 的指针，用于存储过滤后的可读标签
    UT_array *array = NULL;

    pthread_mutex_lock(&group->mtx);

    // 过滤出组中所有可读的标签
    array = filter_tags(group->tags, is_readable, NULL);

    pthread_mutex_unlock(&group->mtx);

    return array;
}

uint16_t neu_group_tag_size(const neu_group_t *group)
{
    uint16_t size = 0;

    size = HASH_COUNT(group->tags);

    return size;
}

neu_datatag_t *neu_group_find_tag(neu_group_t *group, const char *tag)
{
    tag_elem_t *   find   = NULL;
    neu_datatag_t *result = NULL;

    pthread_mutex_lock(&group->mtx);
    HASH_FIND_STR(group->tags, tag, find);
    if (find != NULL) {
        result = neu_tag_dup(find->tag);
    }
    pthread_mutex_unlock(&group->mtx);

    return result;
}

/**
 * @brief 检查组数据是否发生变化，若变化则调用指定的回调函数。
 *
 * 该函数会比较传入的时间戳和组对象中的时间戳，判断组数据是否发生了变化。
 * 如果两个时间戳不相等，则认为组数据发生了变化，将组内的标签转换为数组形式，
 * 并调用指定的回调函数处理组数据的变化。
 *
 * @param group 指向 neu_group_t 结构体的指针，表示要检查的组对象。
 * @param timestamp 用于比较的时间戳，通常是上次检查时记录的时间戳。
 * @param arg 传递给回调函数的用户自定义参数，可用于在回调函数中传递额外信息。
 * @param fn 指向回调函数的指针
 *
 * @return 无返回值。
 */
void neu_group_change_test(neu_group_t *group, int64_t timestamp, void *arg,
                           neu_group_change_fn fn)
{
    if (group->timestamp != timestamp) {
        UT_array *tags = to_array(group->tags);
        fn(arg, group->timestamp, tags, group->interval);
    }
}

/**
 * @brief 检查组数据是否发生变化。
 *
 * 该函数通过比较传入的时间戳和组对象中的时间戳，判断组数据是否发生了变化。
 * 如果两个时间戳不相等，则认为组数据发生了变化，返回 true；否则返回 false。
 *
 * @param group 指向 neu_group_t 结构体的指针，表示要检查的组对象。
 * @param timestamp 用于比较的时间戳，通常是上次检查时记录的时间戳。
 * @return bool 如果组数据发生变化，返回 true；否则返回 false。
 */
bool neu_group_is_change(neu_group_t *group, int64_t timestamp)
{
    bool change = false;

    change = group->timestamp != timestamp;

    return change;
}

static void update_timestamp(neu_group_t *group)
{
    struct timeval tv = { 0 };

    gettimeofday(&tv, NULL);

    group->timestamp = (int64_t) tv.tv_sec * 1000 * 1000 + (int64_t) tv.tv_usec;
}

/**
 * @brief 将由 tag_elem_t 结构体组成的哈希表转换为 UT_array 数组。
 *
 * 该函数接收一个指向 tag_elem_t 结构体的指针，该指针指向一个哈希表的头节点。
 * 函数会遍历这个哈希表，并将每个 tag_elem_t 结构体中的 tag 成员添加到一个新创建的 UT_array 中。
 * 最后返回这个 UT_array。
 *
 * @param tags 指向 tag_elem_t 结构体的指针，代表要转换的哈希表的头节点。
 * @return 
 * UT_array* 转换后的 UT_array 数组的指针。
 * 如果转换过程中出现错误，可能返回 NULL。
 *
 * @note 
 * 调用者需要负责在不再使用返回的 UT_array 时，
 * 使用 utarray_free 函数释放其内存，以避免内存泄漏。
 */
static UT_array *to_array(tag_elem_t *tags)
{
    tag_elem_t *el = NULL, *tmp = NULL;
    UT_array *  array = NULL;

    utarray_new(array, neu_tag_get_icd());
    HASH_ITER(hh, tags, el, tmp) { utarray_push_back(array, el->tag); }

    return array;
}