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
#include "errcodes.h"
#include "utils/log.h"

#include "adapter/storage.h"
#include "storage.h"

void manager_strorage_plugin(neu_manager_t *manager)
{
    UT_array *plugin_infos = NULL;

    plugin_infos = neu_manager_get_plugins(manager);

    int rv = neu_persister_store_plugins(plugin_infos);
    if (0 != rv) {
        nlog_error("failed to store plugins infos");
    }

    neu_persist_plugin_infos_free(plugin_infos);
    return;
}

void manager_storage_add_node(neu_manager_t *manager, const char *node)
{
    int                     rv        = 0;
    neu_persist_node_info_t node_info = {};

    rv = neu_manager_get_node_info(manager, node, &node_info);
    if (0 != rv) {
        nlog_error("unable to get adapter:%s info", node);
        return;
    }

    rv = neu_persister_store_node(&node_info);
    if (0 != rv) {
        nlog_error("failed to store adapter info");
    }

    neu_persist_node_info_fini(&node_info);
}

void manager_storage_update_node(neu_manager_t *manager, const char *node,
                                 const char *new_name)
{
    (void) manager;
    neu_persister_update_node(node, new_name);
}

void manager_storage_del_node(neu_manager_t *manager, const char *node)
{
    (void) manager;
    neu_persister_delete_node(node);
}

void manager_storage_subscribe(neu_manager_t *manager, const char *app,
                               const char *driver, const char *group,
                               const char *params, const char *static_tags)
{
    (void) manager;
    int rv = neu_persister_store_subscription(app, driver, group, params,
                                              static_tags);
    if (0 != rv) {
        nlog_error("fail store subscription app:%s driver:%s group:%s", app,
                   driver, group);
    }
}

void manager_storage_update_subscribe(neu_manager_t *manager, const char *app,
                                      const char *driver, const char *group,
                                      const char *params,
                                      const char *static_tags)
{
    (void) manager;
    int rv = neu_persister_update_subscription(app, driver, group, params,
                                               static_tags);
    if (0 != rv) {
        nlog_error("fail update subscription app:%s driver:%s group:%s", app,
                   driver, group);
    }
}

void manager_storage_unsubscribe(neu_manager_t *manager, const char *app,
                                 const char *driver, const char *group)
{
    (void) manager;
    int rv = neu_persister_delete_subscription(app, driver, group);
    if (0 != rv) {
        nlog_error("fail delete subscription app:%s driver:%s group:%s", app,
                   driver, group);
    }
}

/**
 * @brief 从持久化存储中加载插件信息，并将插件添加到管理器中。
 *
 * 该函数首先调用 neu_persister_load_plugins 函数从持久化存储中加载插件信息，
 * 然后遍历加载的插件信息，依次调用 neu_manager_add_plugin 函数将插件添加到管理器中，
 * 并记录每个插件加载的结果（成功或失败）。最后，释放存储插件信息所占用的内存。
 *
 * @param manager 指向 neu_manager_t 类型的管理器的指针，用于添加插件。
 * @return 如果所有操作都成功，返回最后一次调用 neu_manager_add_plugin 的返回值；
 *         如果 neu_persister_load_plugins 失败，返回该函数的错误返回值。
 */
int manager_load_plugin(neu_manager_t *manager)
{
    // 用于存储从持久化存储中加载的插件信息
    UT_array *plugin_infos = NULL;

    // 调用 neu_persister_load_plugins 函数加载插件信息
    int rv = neu_persister_load_plugins(&plugin_infos);
    if (rv != 0) {
        return rv;
    }
    
    /**
     * @note
     * - 向 utarray 数组中添加元素时，需要传递元素的地址。所以将char *str1
     *   类型的变量添加到数组，utarray_push_back(plugin_infos, &str1);
     * 
     * - 每次循环时，会将当前元素的地址赋值给 name 变量。由于数组元素是 char * 类型，
     *   所以 name 就是一个 char ** 类型的指针，通过 *name 就可以访问到当前元素
     *  （即字符串指针），进而访问该指针所指向的字符串。
     */
    utarray_foreach(plugin_infos, char **, name)
    {
        // 调用 neu_manager_add_plugin 函数将插件添加到管理器中
        rv                    = neu_manager_add_plugin(manager, *name);
        const char *ok_or_err = (0 == rv) ? "success" : "fail";
        nlog_notice("load plugin %s, lib:%s", ok_or_err, *name);
    }

    // 遍历数组，释放每个插件名称所占用的内存
    utarray_foreach(plugin_infos, char **, name) { free(*name); }
    utarray_free(plugin_infos);

    return rv;
}

/**
 * @brief 从持久化存储中加载节点信息并添加到管理器
 *
 * 该函数的主要功能是从持久化存储中加载节点信息，然后将这些节点信息逐个添加到 `neu_manager_t`
 * 类型的管理器中。如果加载或添加过程中出现错误，会记录相应的错误日志。
 *
 * @param manager 指向 `neu_manager_t` 类型的管理器结构体指针，用于存储和管理加载的节点信息。
 *
 * @return 函数执行结果的状态码：
 *         - 若加载节点信息失败，返回 -1。
 *         - 若所有节点信息添加成功，返回最后一次添加操作的返回值（通常为 0 表示成功）。
 *         - 若部分节点添加失败，返回最后一次添加操作的返回值（可能为非 0 表示失败）。
 *
 * @note 调用该函数前，确保 `manager` 指针有效。函数会自动释放加载节点信息所使用的内存。
 */
int manager_load_node(neu_manager_t *manager)
{
    UT_array *node_infos = NULL;
    int       rv         = 0;

    // 从持久化存储中加载节点信息
    rv = neu_persister_load_nodes(&node_infos);
    if (0 != rv) {
        nlog_error("failed to load adapter infos");
        return -1;
    }

    utarray_foreach(node_infos, neu_persist_node_info_t *, node_info)
    {
        rv                    = neu_manager_add_node(manager, node_info->name,
                                  node_info->plugin_name, NULL,
                                  node_info->state, true);
        const char *ok_or_err = (0 == rv) ? "success" : "fail";
        nlog_notice("load adapter %s type:%d, name:%s plugin:%s state:%d",
                    ok_or_err, node_info->type, node_info->name,
                    node_info->plugin_name, node_info->state);
    }

    // 释放存储节点信息的数组
    utarray_free(node_infos);
    return rv;
}

int manager_load_subscribe(neu_manager_t *manager)
{
    UT_array *nodes =
        neu_node_manager_get(manager->node_manager, NEU_NA_TYPE_APP);

    utarray_foreach(nodes, neu_resp_node_info_t *, node)
    {
        int       rv        = 0;
        UT_array *sub_infos = NULL;

        rv = neu_persister_load_subscriptions(node->node, &sub_infos);
        if (0 != rv) {
            nlog_warn("load %s subscribetion infos error", node->node);
        } else {
            utarray_foreach(sub_infos, neu_persist_subscription_info_t *, info)
            {
                uint16_t app_port = 0;
                rv                = neu_manager_subscribe(
                    manager, node->node, info->driver_name, info->group_name,
                    info->params, info->static_tags, &app_port);
                const char *ok_or_err = (0 == rv) ? "success" : "fail";
                nlog_notice(
                    "%s load subscription app:%s driver:%s grp:%s, static:%s",
                    ok_or_err, node->node, info->driver_name, info->group_name,
                    info->static_tags);
                if (0 == rv) {
                    neu_manager_send_subscribe(manager, node->node,
                                               info->driver_name,
                                               info->group_name, app_port,
                                               info->params, info->static_tags);
                }
            }

            utarray_free(sub_infos);
        }
    }

    utarray_free(nodes);
    return 0;
}
