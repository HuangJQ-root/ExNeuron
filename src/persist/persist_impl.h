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

#ifndef NEU_PERSIST_PERSIST_IMPL
#define NEU_PERSIST_PERSIST_IMPL

#ifdef __cplusplus
extern "C" {
#endif

#include "persist/persist.h"

struct neu_persister_vtbl_s;

typedef struct {
    struct neu_persister_vtbl_s *vtbl;
} neu_persister_t;

/**
 * @brief
 * 
 * 虚函数表，定义了一组函数指针，这些指针指向不同的函数，
 * 这些函数实现了持久化对象的各种操作。通过使用虚函数表，
 * 可以在运行时根据实际的持久化对象类型调用相应的函数，
 * 从而实现多态性(不同的持久化对象如sqlite)。
 * 
 */
struct neu_persister_vtbl_s {
    /**
     * @brief 销毁持久化对象。
     */
    void (*destroy)(neu_persister_t *self);

    /**
     * @return 返回实现定义的底层句柄。
     */
    void *(*native_handle)(neu_persister_t *self);

    /**
     * @brief 持久化节点信息。
     */
    int (*store_node)(neu_persister_t *self, neu_persist_node_info_t *info);

    /**
     * @brief 加载节点信息。
     */
    int (*load_nodes)(neu_persister_t *self, UT_array **node_infos);

    /**
     * @brief 删除节点。
     */
    int (*delete_node)(neu_persister_t *self, const char *node_name);

    /**
     * @brief 更新节点名称。
     */
    int (*update_node)(neu_persister_t *self, const char *node_name,
                       const char *new_name);

    /**
     * @brief 更新节点状态。
     */
    int (*update_node_state)(neu_persister_t *self, const char *node_name,
                             int state);

    /**
     * @brief 持久化节点设置。
     */
    int (*store_node_setting)(neu_persister_t *self, const char *node_name,
                              const char *setting);

    /**
     * @brief 加载节点设置。
     */
    int (*load_node_setting)(neu_persister_t *self, const char *node_name,
                             const char **const setting);

    /**
     * @brief 删除节点设置。
     */
    int (*delete_node_setting)(neu_persister_t *self, const char *node_name);

    /**
     * @brief 持久化节点标签。
     */
    int (*store_tag)(neu_persister_t *self, const char *driver_name,
                     const char *group_name, const neu_datatag_t *tag);

    /**
     * 持久化多个节点标签。
     * @param driver_name               驱动程序名称。
     * @param group_name                分组名称。
     * @param tags                      标签数组。
     * @param n                         标签数量。
     * @return 成功返回 0，失败返回非零值。
     */
    int (*store_tags)(neu_persister_t *self, const char *driver_name,
                      const char *group_name, const neu_datatag_t *tags,
                      size_t n);

    /**
     * 加载节点标签信息。
     * @param node_name                 节点名称。
     * @param group_name                分组名称。
     * @param[out] tag_infos            用于返回指向堆分配的 neu_datatag_t 向量的指针。
     * @return 成功返回 0，失败返回非零值。
     */
    int (*load_tags)(neu_persister_t *self, const char *driver_name,
                     const char *group_name, UT_array **tag_infos);

    /**
     * 更新节点标签。
     * @param driver_name               驱动程序名称。
     * @param group_name                分组名称。
     * @param tag                       标签信息。
     * @return 成功返回 0，失败返回非零值。
     */
    int (*update_tag)(neu_persister_t *self, const char *driver_name,
                      const char *group_name, const neu_datatag_t *tag);

    /**
     * 更新节点标签的值。
     * @param driver_name               驱动程序名称。
     * @param group_name                分组名称。
     * @param tag                       标签信息。
     * @return 成功返回 0，失败返回非零值。
     */
    int (*update_tag_value)(neu_persister_t *self, const char *driver_name,
                            const char *group_name, const neu_datatag_t *tag);

    /**
     * 删除节点标签。
     * @param driver_name               驱动程序名称。
     * @param group_name                分组名称。
     * @param tag_name                  标签名称。
     * @return 成功返回 0，失败返回非零值。
     */
    int (*delete_tag)(neu_persister_t *self, const char *driver_name,
                      const char *group_name, const char *tag_name);

    /**
     * 持久化订阅信息。
     * @param app_name                  应用节点名称。
     * @param driver_name               驱动节点名称。
     * @param group_name                分组名称。
     * @param params                    订阅参数。
     * @param static_tags               静态标签。
     * @return 成功返回 0，失败返回非零值。
     */
    int (*store_subscription)(neu_persister_t *self, const char *app_name,
                              const char *driver_name, const char *group_name,
                              const char *params, const char *static_tags);

    /**
     * 更新订阅信息。
     * @param app_name                  应用节点名称。
     * @param driver_name               驱动节点名称。
     * @param group_name                分组名称。
     * @param params                    订阅参数。
     * @param static_tags               静态标签。
     * @return 成功返回 0，失败返回非零值。
     */
    int (*update_subscription)(neu_persister_t *self, const char *app_name,
                               const char *driver_name, const char *group_name,
                               const char *params, const char *static_tags);

    /**
     * 加载适配器订阅信息。
     * @param app_name                  应用节点名称。
     * @param[out] subscription_infos   用于返回指向堆分配的 neu_persist_subscription_info_t 向量的指针。
     * @return 成功返回 0，失败返回非零值。
     */
    int (*load_subscriptions)(neu_persister_t *self, const char *app_name,
                              UT_array **subscription_infos);

    /**
     * 删除订阅信息。
     * @param app_name                  应用节点名称。
     * @param driver_name               驱动节点名称。
     * @param group_name                分组名称。
     * @return 成功返回 0，失败返回非零值。
     */
    int (*delete_subscription)(neu_persister_t *self, const char *app_name,
                               const char *driver_name, const char *group_name);

    /**
     * 持久化组配置。
     * @param driver_name               驱动程序名称。
     * @param group_info                组配置信息。
     * @param context                   上下文信息。
     * @return 成功返回 0，失败返回非零值。
     */
    int (*store_group)(neu_persister_t *self, const char *driver_name,
                       neu_persist_group_info_t *group_info,
                       const char *              context);

    /**
     * 更新组配置。
     * @param driver_name               驱动程序名称。
     * @param group_name                组名称。
     * @param group_info                组配置信息。
     * @return 成功返回 0，失败返回非零值。
     */
    int (*update_group)(neu_persister_t *self, const char *driver_name,
                        const char *              group_name,
                        neu_persist_group_info_t *group_info);

    /**
     * 加载适配器下所有组的配置信息。
     * @param driver_name               驱动程序名称。
     * @param[out] group_infos          用于返回指向堆分配的 neu_persist_group_info_t 向量的指针。
     * @return 成功返回 0，失败返回非零值。
     */
    int (*load_groups)(neu_persister_t *self, const char *driver_name,
                       UT_array **group_infos);

    /**
     * 删除组配置。
     * @param driver_name               驱动程序名称。
     * @param group_name                组名称。
     * @return 成功返回 0，失败返回非零值。
     */
    int (*delete_group)(neu_persister_t *self, const char *driver_name,
                        const char *group_name);

    /**
     * 加载所有用户信息。
     * @param[out] user_infos           用于返回指向堆分配的 neu_persist_user_info_t 向量的指针。
     * @return 成功返回 0，失败返回非零值。
     */
    int (*load_users)(neu_persister_t *self, UT_array **user_infos);

    /**
     * 保存用户信息。
     * @param user                      用户信息。
     * @return 成功返回 0，失败返回非零值。
     */
    int (*store_user)(neu_persister_t *              self,
                      const neu_persist_user_info_t *user);

    /**
     * 更新用户信息。
     * @param user                      用户信息。
     * @return 成功返回 0，失败返回非零值。
     */
    int (*update_user)(neu_persister_t *              self,
                       const neu_persist_user_info_t *user);

    /**
     * 加载用户信息。
     * @param user_name                 用户名称。
     * @param user_p                    输出参数，用于返回用户信息。
     * @return 成功返回 0，失败返回非零值。
     */
    int (*load_user)(neu_persister_t *self, const char *user_name,
                     neu_persist_user_info_t **user_p);

    /**
     * 删除用户信息。
     * @param user_name                 用户名称。
     * @return 成功返回 0，失败返回非零值。
     */
    int (*delete_user)(neu_persister_t *self, const char *user_name);
};


// read all file contents as string
int read_file_string(const char *fn, char **out);

#ifdef __cplusplus
}
#endif

#endif
