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

#ifndef _NEU_SUBSCRIBE_H_
#define _NEU_SUBSCRIBE_H_

#include <netinet/in.h>
#include <sys/un.h>

#include "define.h"
#include "utils/utextend.h"

typedef struct neu_subscribe_mgr neu_subscribe_mgr_t;

/**
 * @brief 定义应用程序订阅信息的结构体。
 *
 * 该结构体用于存储应用程序对特定资源进行订阅时的相关信息，
 * 包括应用程序名称、订阅参数、静态标签以及应用程序的地址。
 * 这些信息有助于管理和识别不同应用程序的订阅请求和状态。
 */
typedef struct neu_app_subscribe {
    /**
     * @brief 应用程序的名称。
     *
     * 存储应用程序的名称
     */
    char app_name[NEU_NODE_NAME_LEN];

    /**
     * @brief 订阅的参数。
     *
     * 指向一个字符串，该字符串包含订阅时传递的额外参数信息。
     * 这些参数可以用于配置订阅的具体行为，例如订阅的频率、过滤条件等。
     * 该指针在使用时需要动态分配内存，使用完毕后需要释放。
     */
    char *params;

    /**
     * @brief 静态标签。
     *
     * 指向一个字符串，该字符串包含与订阅相关的静态标签信息。
     * 静态标签可以用于对订阅进行分类、标记或其他自定义用途。
     * 该指针在使用时需要动态分配内存，使用完毕后需要释放。
     */
    char *static_tags;

    /**
     * @brief 应用程序的地址。
     *
     * 存储应用程序的地址信息，使用 `struct sockaddr_un` 结构体表示。
     * 该地址用于在订阅过程中与应用程序进行通信，例如发送订阅数据或通知。
     */
    struct sockaddr_un addr;
} neu_app_subscribe_t;

static inline void neu_app_subscribe_fini(neu_app_subscribe_t *app_sub)
{
    free(app_sub->params);
    free(app_sub->static_tags);
}

neu_subscribe_mgr_t *neu_subscribe_manager_create();
void                 neu_subscribe_manager_destroy(neu_subscribe_mgr_t *mgr);

//  neu_app_subscribe_t array
UT_array *neu_subscribe_manager_find(neu_subscribe_mgr_t *mgr,
                                     const char *driver, const char *group);
UT_array *neu_subscribe_manager_find_by_driver(neu_subscribe_mgr_t *mgr,
                                               const char *         driver);
UT_array *neu_subscribe_manager_get(neu_subscribe_mgr_t *mgr, const char *app,
                                    const char *driver, const char *group);
size_t    neu_subscribe_manager_group_count(const neu_subscribe_mgr_t *mgr,
                                            const char *               app);
void neu_subscribe_manager_unsub_all(neu_subscribe_mgr_t *mgr, const char *app);
int  neu_subscribe_manager_sub(neu_subscribe_mgr_t *mgr, const char *driver,
                               const char *app, const char *group,
                               const char *params, const char *static_tags,
                               struct sockaddr_un addr);
int  neu_subscribe_manager_unsub(neu_subscribe_mgr_t *mgr, const char *driver,
                                 const char *app, const char *group);
void neu_subscribe_manager_remove(neu_subscribe_mgr_t *mgr, const char *driver,
                                  const char *group);

int neu_subscribe_manager_update_app_name(neu_subscribe_mgr_t *mgr,
                                          const char *         app,
                                          const char *         new_name);
int neu_subscribe_manager_update_driver_name(neu_subscribe_mgr_t *mgr,
                                             const char *         driver,
                                             const char *         new_name);
int neu_subscribe_manager_update_group_name(neu_subscribe_mgr_t *mgr,
                                            const char *         driver,
                                            const char *         group,
                                            const char *         new_name);
int neu_subscribe_manager_update_params(neu_subscribe_mgr_t *mgr,
                                        const char *app, const char *driver,
                                        const char *group, const char *params,
                                        const char *static_tags);

#endif
