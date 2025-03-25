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
#include <stdlib.h>

#include "adapter.h"
#include "errcodes.h"
#include "utils/log.h"

#include "subscribe.h"

/**
 * @brief 该结构体用于表示订阅元素的关键信息。
 *
 * 包含驱动名称和组名称，用于标识特定的订阅元素。
 */
typedef struct sub_elem_key {
    /**
     * @brief 驱动名称。
     *
     * 这个字段存储了与订阅元素相关的驱动的名称
     */
    char driver[NEU_NODE_NAME_LEN];

    /**
     * @brief 组名称。
     *
     * 这个字段存储了与订阅元素相关的组的名称
     */
    char group[NEU_GROUP_NAME_LEN];
} sub_elem_key_t;

/**
 * @brief
 * 
 * 存放每个具体订阅标签，有哪些应用程序（app）进行了订阅
 */
typedef struct sub_elem {
    /**
     * @brief 订阅元素的关键字。
     *
     * 是 UT_hash 的关键字
     */
    sub_elem_key_t key;

    /**
     * @brief 应用程序列表。
     *
     * 每个元素通常是应用程序的名称。
     */
    UT_array *apps;

    /**
     * @brief 用于哈希表的句柄。
     *
     * 按照key有序排列
     */
    UT_hash_handle hh;
} sub_elem_t;

static const UT_icd app_sub_icd = { sizeof(neu_app_subscribe_t), NULL, NULL,
                                    NULL };

/**
 * @brief
 * 
 * 是订阅管理器，用于管理订阅信息
 */
struct neu_subscribe_mgr {
    sub_elem_t *ss;
};

neu_subscribe_mgr_t *neu_subscribe_manager_create()
{
    neu_subscribe_mgr_t *mgr = calloc(1, sizeof(neu_subscribe_mgr_t));

    return mgr;
}

void neu_subscribe_manager_destroy(neu_subscribe_mgr_t *mgr)
{
    sub_elem_t *el = NULL, *tmp = NULL;

    HASH_ITER(hh, mgr->ss, el, tmp)
    {
        HASH_DEL(mgr->ss, el);
        utarray_foreach(el->apps, neu_app_subscribe_t *, sub_app)
        {
            neu_app_subscribe_fini(sub_app);
        }
        utarray_free(el->apps);
        free(el);
    }

    free(mgr);
}

UT_array *neu_subscribe_manager_find(neu_subscribe_mgr_t *mgr,
                                     const char *driver, const char *group)
{
    sub_elem_t *   find = NULL;
    sub_elem_key_t key  = { 0 };

    strncpy(key.driver, driver, sizeof(key.driver));
    strncpy(key.group, group, sizeof(key.group));

    HASH_FIND(hh, mgr->ss, &key, sizeof(sub_elem_key_t), find);

    if (find) {
        return utarray_clone(find->apps);
    } else {
        return NULL;
    }
}

UT_array *neu_subscribe_manager_find_by_driver(neu_subscribe_mgr_t *mgr,
                                               const char *         driver)
{
    sub_elem_t *el = NULL, *tmp = NULL;
    UT_array *  apps = NULL;

    utarray_new(apps, &app_sub_icd);

    HASH_ITER(hh, mgr->ss, el, tmp)
    {
        if (strcmp(driver, el->key.driver) == 0) {
            utarray_concat(apps, el->apps);
        }
    }

    return apps;
}

UT_array *neu_subscribe_manager_get(neu_subscribe_mgr_t *mgr, const char *app,
                                    const char *driver, const char *group)
{
    sub_elem_t *el = NULL, *tmp = NULL;
    UT_array *  groups = NULL;
    UT_icd      icd = { sizeof(neu_resp_subscribe_info_t), NULL, NULL, NULL };

    utarray_new(groups, &icd);
    HASH_ITER(hh, mgr->ss, el, tmp)
    {
        utarray_foreach(el->apps, neu_app_subscribe_t *, sub_app)
        {
            if (strcmp(sub_app->app_name, app) == 0 &&
                (!driver || strstr(el->key.driver, driver)) &&
                (!group || strstr(el->key.group, group))) {
                neu_resp_subscribe_info_t info = { 0 };

                strncpy(info.driver, el->key.driver, sizeof(info.driver));
                strncpy(info.app, app, sizeof(info.app));
                strncpy(info.group, el->key.group, sizeof(info.group));
                info.params      = sub_app->params;      // borrowed reference
                info.static_tags = sub_app->static_tags; // borrowed reference

                utarray_push_back(groups, &info);
            }
        }
    }

    return groups;
}

size_t neu_subscribe_manager_group_count(const neu_subscribe_mgr_t *mgr,
                                         const char *               app)
{
    size_t      group_count = 0;
    sub_elem_t *current_element, *tmp;

    HASH_ITER(hh, mgr->ss, current_element, tmp)
    {
        utarray_foreach(current_element->apps, neu_app_subscribe_t *, sub_app)
        {
            if (strcmp(sub_app->app_name, app) == 0) {
                group_count++;
            }
        }
    }

    return group_count;
}

void neu_subscribe_manager_unsub_all(neu_subscribe_mgr_t *mgr, const char *app)
{
    sub_elem_t *el = NULL, *tmp = NULL;

    HASH_ITER(hh, mgr->ss, el, tmp)
    {
        utarray_foreach(el->apps, neu_app_subscribe_t *, sub_app)
        {
            if (strcmp(sub_app->app_name, app) == 0) {
                neu_subscribe_manager_unsub(mgr, el->key.driver, app,
                                            el->key.group);
                break;
            }
        }
    }
}

/**
 * @brief 处理订阅请求，将应用程序订阅到指定驱动程序的组。
 *
 * 该函数用于在订阅管理器中处理应用程序对指定驱动程序组的订阅请求。它会检查是否已经存在相同的订阅，
 * 如果不存在则创建新的订阅条目并添加到订阅管理器中。
 *
 * @param mgr 指向 `neu_subscribe_mgr_t` 结构体的指针，表示订阅管理器，包含订阅信息的哈希表等。
 * @param driver 指向字符串的指针，表示要订阅的驱动程序名称。
 * @param app 指向字符串的指针，表示发起订阅的应用程序名称。
 * @param group 指向字符串的指针，表示要订阅的组名称。
 * @param params 指向字符串的指针，表示订阅的参数，可为 NULL。
 * @param static_tags 指向字符串的指针，表示静态标签，可为 NULL。
 * @param addr `struct sockaddr_un` 类型的结构体，表示应用程序的地址。
 *
 * @return 返回订阅操作的结果状态码：
 *         - 如果内存分配失败（如 `strdup` 或 `calloc` 失败），返回 `NEU_ERR_EINTERNAL`。
 *         - 如果应用程序已经订阅了该组，返回 `NEU_ERR_GROUP_ALREADY_SUBSCRIBED`。
 *         - 如果订阅操作成功，返回 `NEU_ERR_SUCCESS`。
 */
int neu_subscribe_manager_sub(neu_subscribe_mgr_t *mgr, const char *driver,
                              const char *app, const char *group,
                              const char *params, const char *static_tags,
                              struct sockaddr_un addr)
{
    // 用于查找已存在的订阅条目的指针
    sub_elem_t *        find    = NULL;

    // 订阅条目的键，包含驱动程序和组的信息
    sub_elem_key_t      key     = { 0 };

    // 应用程序订阅信息结构体
    neu_app_subscribe_t app_sub = { 0 };

    // 将驱动程序名称复制到键结构体中
    strncpy(key.driver, driver, sizeof(key.driver));

    // 将组名称复制到键结构体中
    strncpy(key.group, group, sizeof(key.group));

    // 将应用程序名称复制到应用程序订阅信息结构体中
    strncpy(app_sub.app_name, app, sizeof(app_sub.app_name));

    // 设置应用程序的地址
    app_sub.addr = addr;

    // 如果有订阅参数，复制参数到应用程序订阅信息结构体中
    if (params && NULL == (app_sub.params = strdup(params))) {
        return NEU_ERR_EINTERNAL;
    }

    // 如果有静态标签，复制标签到应用程序订阅信息结构体中
    if (static_tags && NULL == (app_sub.static_tags = strdup(static_tags))) {
        return NEU_ERR_EINTERNAL;
    }

    // 在订阅管理器的哈希表中查找具有相同键的订阅条目
    HASH_FIND(hh, mgr->ss, &key, sizeof(sub_elem_key_t), find);

    // 如果找到了具有相同键的订阅条目
    if (find) {
        utarray_foreach(find->apps, neu_app_subscribe_t *, sub)
        {
            // 如果找到相同的应用程序名称
            if (strcmp(sub->app_name, app) == 0) {
                // 释放应用程序订阅信息结构体的资源
                neu_app_subscribe_fini(&app_sub);

                // 返回组已被订阅的错误码
                return NEU_ERR_GROUP_ALREADY_SUBSCRIBED;
            }
        }
    } 
    // 如果未找到具有相同键的订阅条目
    else {
        // 分配内存创建新的订阅条目
        find = calloc(1, sizeof(sub_elem_t));

        // 初始化应用程序订阅信息数组
        utarray_new(find->apps, &app_sub_icd);

        // 设置订阅条目的键
        find->key = key;

        // 将新的订阅条目添加到订阅管理器的哈希表中
        HASH_ADD(hh, mgr->ss, key, sizeof(sub_elem_key_t), find);
    }

    // 将新的应用程序订阅信息添加到对应的订阅条目中
    utarray_push_back(find->apps, &app_sub);
    return NEU_ERR_SUCCESS;
}

int neu_subscribe_manager_update_params(neu_subscribe_mgr_t *mgr,
                                        const char *app, const char *driver,
                                        const char *group, const char *params,
                                        const char *static_tags)
{
    sub_elem_key_t key = { 0 };
    strncpy(key.driver, driver, sizeof(key.driver));
    strncpy(key.group, group, sizeof(key.group));

    sub_elem_t *find = NULL;
    HASH_FIND(hh, mgr->ss, &key, sizeof(sub_elem_key_t), find);

    if (NULL == find) {
        return NEU_ERR_GROUP_NOT_SUBSCRIBE;
    }

    neu_app_subscribe_t *app_sub = NULL;
    utarray_foreach(find->apps, neu_app_subscribe_t *, sub)
    {
        if (strcmp(sub->app_name, app) == 0) {
            app_sub = sub;
            break;
        }
    }

    if (NULL == app_sub) {
        return NEU_ERR_GROUP_NOT_SUBSCRIBE;
    }

    char *p = NULL;
    if (params && NULL == (p = strdup(params))) {
        return NEU_ERR_EINTERNAL;
    }

    char *s = NULL;
    if (static_tags && NULL == (s = strdup(static_tags))) {
        return NEU_ERR_EINTERNAL;
    }

    free(app_sub->params);
    app_sub->params = p;
    free(app_sub->static_tags);
    app_sub->static_tags = s;
    return NEU_ERR_SUCCESS;
}

int neu_subscribe_manager_unsub(neu_subscribe_mgr_t *mgr, const char *driver,
                                const char *app, const char *group)
{
    sub_elem_t *   find = NULL;
    sub_elem_key_t key  = { 0 };

    strncpy(key.driver, driver, sizeof(key.driver));
    strncpy(key.group, group, sizeof(key.group));

    HASH_FIND(hh, mgr->ss, &key, sizeof(sub_elem_key_t), find);

    if (find) {
        utarray_foreach(find->apps, neu_app_subscribe_t *, sub)
        {
            if (strcmp(sub->app_name, app) == 0) {
                neu_app_subscribe_fini(sub);
                utarray_erase(find->apps, utarray_eltidx(find->apps, sub), 1);
                return NEU_ERR_SUCCESS;
            }
        }
    }

    return NEU_ERR_GROUP_NOT_SUBSCRIBE;
}

void neu_subscribe_manager_remove(neu_subscribe_mgr_t *mgr, const char *driver,
                                  const char *group)
{
    sub_elem_t *el = NULL, *tmp = NULL;

    HASH_ITER(hh, mgr->ss, el, tmp)
    {
        if (strcmp(driver, el->key.driver) == 0 &&
            (group == NULL || strcmp(group, el->key.group) == 0)) {
            HASH_DEL(mgr->ss, el);
            utarray_foreach(el->apps, neu_app_subscribe_t *, sub_app)
            {
                neu_app_subscribe_fini(sub_app);
            }
            utarray_free(el->apps);
            free(el);
        }
    }
}

int neu_subscribe_manager_update_app_name(neu_subscribe_mgr_t *mgr,
                                          const char *app, const char *new_name)
{
    sub_elem_t *el = NULL, *tmp = NULL;

    HASH_ITER(hh, mgr->ss, el, tmp)
    {
        utarray_foreach(el->apps, neu_app_subscribe_t *, sub_app)
        {
            if (strcmp(app, sub_app->app_name) == 0) {
                strncpy(sub_app->app_name, new_name, sizeof(sub_app->app_name));
            }
        }
    }

    return 0;
}

int neu_subscribe_manager_update_driver_name(neu_subscribe_mgr_t *mgr,
                                             const char *         driver,
                                             const char *         new_name)
{
    sub_elem_t *el = NULL, *tmp = NULL;

    HASH_ITER(hh, mgr->ss, el, tmp)
    {
        if (strcmp(driver, el->key.driver) == 0) {
            HASH_DEL(mgr->ss, el);
            strncpy(el->key.driver, new_name, sizeof(el->key.driver));
            HASH_ADD(hh, mgr->ss, key, sizeof(sub_elem_key_t), el);
        }
    }

    return 0;
}

int neu_subscribe_manager_update_group_name(neu_subscribe_mgr_t *mgr,
                                            const char *         driver,
                                            const char *         group,
                                            const char *         new_name)
{
    sub_elem_t *   find = NULL;
    sub_elem_key_t key  = { 0 };

    strncpy(key.driver, driver, sizeof(key.driver));
    strncpy(key.group, group, sizeof(key.group));

    HASH_FIND(hh, mgr->ss, &key, sizeof(sub_elem_key_t), find);

    if (NULL == find) {
        return NEU_ERR_GROUP_NOT_SUBSCRIBE;
    }

    HASH_DEL(mgr->ss, find);
    strncpy(find->key.group, new_name, sizeof(find->key.group));
    HASH_ADD(hh, mgr->ss, key, sizeof(sub_elem_key_t), find);

    return 0;
}
