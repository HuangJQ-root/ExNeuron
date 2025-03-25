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
#include <dlfcn.h>

#include "utils/log.h"
#include "json/neu_json_param.h"

#include "adapter.h"
#include "errcodes.h"

#include "adapter/adapter_internal.h"
#include "adapter/driver/driver_internal.h"
#include "base/msg_internal.h"

#include "manager_internal.h"
#include "storage.h"

int neu_manager_add_plugin(neu_manager_t *manager, const char *library)
{
    return neu_plugin_manager_add(manager->plugin_manager, library);
}

int neu_manager_del_plugin(neu_manager_t *manager, const char *plugin)
{
    return neu_plugin_manager_del(manager->plugin_manager, plugin);
}

UT_array *neu_manager_get_plugins(neu_manager_t *manager)
{
    return neu_plugin_manager_get(manager->plugin_manager);
}

/**
 * @brief 向节点管理器中添加一个新的节点。
 *
 * 此函数会查找指定的插件，检查插件是否为单例模式，若为单例模式则不允许创建新实例。
 * 接着检查节点是否已存在，若不存在则实例化插件和适配器，进行初始化和设置操作。
 *
 * @param manager 指向节点管理器的指针，用于管理节点的添加和删除等操作。
 * @param node_name 要添加的节点的名称，用于标识节点。
 * @param plugin_name 要使用的插件的名称，用于查找和实例化插件。
 * @param setting 节点的设置信息，可为 NULL。用于配置节点的具体参数。
 * @param state 节点的运行状态，由枚举类型 neu_node_running_state_e 定义。
 * @param load 布尔值，指示是否加载适配器。
 *
 * @return int 返回操作结果的错误码。
 *         - NEU_ERR_SUCCESS: 操作成功。
 *         - NEU_ERR_LIBRARY_NOT_FOUND: 未找到指定的插件库。
 *         - NEU_ERR_LIBRARY_NOT_ALLOW_CREATE_INSTANCE: 插件为单例模式，不允许创建实例。
 *         - NEU_ERR_NODE_EXIST: 节点已存在。
 *         - NEU_ERR_LIBRARY_FAILED_TO_OPEN: 插件库打开失败。
 *         - 其他错误码: 由 neu_adapter_error() 或 neu_adapter_set_setting() 返回。
 */
int neu_manager_add_node(neu_manager_t *manager, const char *node_name,
                         const char *plugin_name, const char *setting,
                         neu_node_running_state_e state, bool load)
{
    // 初始化适配器指针，用于后续操作
    neu_adapter_t *       adapter      = NULL;
    // 初始化插件实例结构体，存储插件实例信息
    neu_plugin_instance_t instance     = { 0 };
    // 初始化适配器信息结构体，设置节点名称
    neu_adapter_info_t    adapter_info = {
        .name = node_name,
    };
    // 初始化插件信息响应结构体，用于存储查找插件得到的信息
    neu_resp_plugin_info_t info = { 0 };

    // 调用插件管理器的查找函数，查找指定名称的插件
    int                    ret =
        neu_plugin_manager_find(manager->plugin_manager, plugin_name, &info);

    // 若查找插件失败，返回库未找到错误码
    if (ret != 0) {
        return NEU_ERR_LIBRARY_NOT_FOUND;
    }

    // 不允许创建单例插件实例,只有dashboard
    if (info.single) {
        return NEU_ERR_LIBRARY_NOT_ALLOW_CREATE_INSTANCE;
    }

    // 在节点管理器中查找指定名称的节点，防止创建同名节点
    adapter = neu_node_manager_find(manager->node_manager, node_name);
    if (adapter != NULL) {
        return NEU_ERR_NODE_EXIST;
    }

    // 实例化插件，调用插件管理器的创建实例函数
    ret = neu_plugin_manager_create_instance(manager->plugin_manager, info.name,
                                             &instance);
    if (ret != 0) {
        return NEU_ERR_LIBRARY_FAILED_TO_OPEN;
    }
    
    // 将插件实例的句柄赋值给适配器信息结构体
    adapter_info.handle = instance.handle;
    // 将插件实例的模块赋值给适配器信息结构体
    adapter_info.module = instance.module;

    // 根据适配器信息结构体创建适配器实例
    adapter = neu_adapter_create(&adapter_info, load);
    if (adapter == NULL) {
        return neu_adapter_error();
    }

    // 将适配器添加到节点管理器中
    neu_node_manager_add(manager->node_manager, adapter);

    /**
     * @note 
     * 
     * - 初始化适配器，设置其运行状态并向管理器发送 NEU_REQ_NODE_INIT
     *   消息,被manager捕获，在manager_loop中处理，启动adapter
     *  （启动组定时器）
     */
    neu_adapter_init(adapter, state);

    if (NULL != setting &&
        0 != (ret = neu_adapter_set_setting(adapter, setting))) {
        neu_node_manager_del(manager->node_manager, node_name);
        neu_adapter_uninit(adapter);
        neu_adapter_destroy(adapter);
        return ret;
    }

    return NEU_ERR_SUCCESS;
}

int neu_manager_del_node(neu_manager_t *manager, const char *node_name)
{
    neu_adapter_t *adapter =
        neu_node_manager_find(manager->node_manager, node_name);

    if (adapter == NULL) {
        return NEU_ERR_NODE_NOT_EXIST;
    }

    neu_adapter_destroy(adapter);
    neu_subscribe_manager_remove(manager->subscribe_manager, node_name, NULL);
    neu_node_manager_del(manager->node_manager, node_name);
    return NEU_ERR_SUCCESS;
}

UT_array *neu_manager_get_nodes(neu_manager_t *manager, int type,
                                const char *plugin, const char *node)
{
    return neu_node_manager_filter(manager->node_manager, type, plugin, node);
}

int neu_manager_update_node_name(neu_manager_t *manager, const char *node,
                                 const char *new_name)
{
    int ret = 0;
    if (neu_node_manager_is_driver(manager->node_manager, node)) {
        ret = neu_subscribe_manager_update_driver_name(
            manager->subscribe_manager, node, new_name);
    } else {
        ret = neu_subscribe_manager_update_app_name(manager->subscribe_manager,
                                                    node, new_name);
    }
    if (0 == ret) {
        ret =
            neu_node_manager_update_name(manager->node_manager, node, new_name);
    }
    return ret;
}

int neu_manager_update_group_name(neu_manager_t *manager, const char *driver,
                                  const char *group, const char *new_name)
{
    return neu_subscribe_manager_update_group_name(manager->subscribe_manager,
                                                   driver, group, new_name);
}

static inline neu_plugin_instance_t *
new_plugin_instance(neu_plugin_manager_t *plugin_manager, const char *plugin)
{
    neu_plugin_instance_t *inst = calloc(1, sizeof(*inst));
    if (NULL == inst) {
        return NULL;
    }

    if (0 != neu_plugin_manager_create_instance(plugin_manager, plugin, inst)) {
        free(inst);
        return NULL;
    }

    return inst;
}

static inline void free_plugin_instance(neu_plugin_instance_t *inst)
{
    if (inst) {
        dlclose(inst->handle);
        free(inst);
    }
}

UT_array *neu_manager_get_driver_group(neu_manager_t *manager)
{
    UT_array *drivers =
        neu_node_manager_get(manager->node_manager, NEU_NA_TYPE_DRIVER);
    UT_array *driver_groups = NULL;
    UT_icd    icd = { sizeof(neu_resp_driver_group_info_t), NULL, NULL, NULL };

    utarray_new(driver_groups, &icd);

    utarray_foreach(drivers, neu_resp_node_info_t *, driver)
    {
        neu_adapter_t *adapter =
            neu_node_manager_find(manager->node_manager, driver->node);
        UT_array *groups =
            neu_adapter_driver_get_group((neu_adapter_driver_t *) adapter);

        utarray_foreach(groups, neu_resp_group_info_t *, g)
        {
            neu_resp_driver_group_info_t dg = { 0 };

            strcpy(dg.driver, driver->node);
            strcpy(dg.group, g->name);
            dg.interval  = g->interval;
            dg.tag_count = g->tag_count;

            utarray_push_back(driver_groups, &dg);
        }

        utarray_free(groups);
    }

    utarray_free(drivers);

    return driver_groups;
}

/**
 * @brief 执行实际的订阅操作，这是一个内部辅助函数。
 *
 * 该函数主要用于在管理器中执行订阅相关的逻辑，包括查找驱动程序适配器、检查驱动程序组是否存在、
 * 获取应用程序的地址，并最终调用 `neu_subscribe_manager_sub` 函数完成订阅操作。
 *
 * @param manager 指向 `neu_manager_t` 结构体的指针，包含管理器的各种信息，如节点管理器和订阅管理器等。
 * @param app 指向字符串的指针，表示要订阅的应用程序名称。
 * @param driver 指向字符串的指针，表示驱动程序的名称。
 * @param group 指向字符串的指针，表示要订阅的组名称。
 * @param params 指向字符串的指针，表示订阅的参数信息。
 * @param static_tags 指向字符串的指针，表示静态标签信息。
 *
 * @return 返回订阅操作的结果状态码：
 *         - 如果驱动程序节点不存在，返回 `NEU_ERR_NODE_NOT_EXIST`。
 *         - 如果驱动程序组不存在或存在其他与组相关的错误，返回 `neu_adapter_driver_group_exist` 函数返回的错误码。
 *         - 如果所有检查通过并成功调用 `neu_subscribe_manager_sub` 函数，返回 `neu_subscribe_manager_sub` 函数的返回值，
 *           该返回值表示订阅操作的最终结果，`NEU_ERR_SUCCESS` 表示成功，其他值表示不同的错误情况。
 */
static inline int manager_subscribe(neu_manager_t *manager, const char *app,
                                    const char *driver, const char *group,
                                    const char *params, const char *static_tags)
{
    // 初始化返回值为成功状态码
    int                ret  = NEU_ERR_SUCCESS;

    // 初始化 Unix 域套接字地址结构体
    struct sockaddr_un addr = { 0 };

    // 在节点管理器中查找指定名称的驱动程序适配器
    neu_adapter_t *    adapter =
        neu_node_manager_find(manager->node_manager, driver);

    // 如果未找到驱动程序适配器，说明驱动程序节点不存在
    if (adapter == NULL) {
        return NEU_ERR_NODE_NOT_EXIST;
    }
    
    // 检查驱动程序组是否存在
    ret =
        neu_adapter_driver_group_exist((neu_adapter_driver_t *) adapter, group);
    if (ret != NEU_ERR_SUCCESS) {
        return ret;
    }

    // 获取应用程序的地址
    addr = neu_node_manager_get_addr(manager->node_manager, app);

    // 完成订阅操作
    return neu_subscribe_manager_sub(manager->subscribe_manager, driver, app,
                                     group, params, static_tags, addr);
}

/**
 * @brief 向指定的应用程序订阅数据。
 *
 * 该函数用于处理向指定应用程序订阅数据的请求，它会执行一系列的检查和操作，
 * 包括查找应用程序适配器、获取端口号、检查 MQTT 主题参数的有效性，以及
 * 检查应用程序节点是否允许订阅。如果所有检查都通过，则调用内部的
 * `manager_subscribe` 函数完成订阅操作。
 *
 * @param manager 代表管理器实例，包含节点管理等相关信息。
 * @param app 指向字符串的指针，表示要订阅的应用程序名称。
 * @param driver 指向字符串的指针，表示驱动程序的名称。
 * @param group 指向字符串的指针，表示要订阅的组名称。
 * @param params 指向字符串的指针，表示订阅的参数，可能包含 MQTT 主题等信息。
 * @param static_tags 指向字符串的指针，表示静态标签信息。
 * @param app_port 指向无符号 16 位整数的指针，用于存储应用程序的数据传输端口号。
 *
 * @return 返回订阅操作的结果状态码：
 *         - 如果应用程序节点不存在，返回 `NEU_ERR_NODE_NOT_EXIST`。
 *         - 如果 MQTT 订阅参数（主题）无效，返回 `NEU_ERR_MQTT_SUBSCRIBE_FAILURE`。
 *         - 如果应用程序节点不允许订阅，返回 `NEU_ERR_NODE_NOT_ALLOW_SUBSCRIBE`。
 *         - 如果所有检查通过并成功调用 `manager_subscribe` 函数，返回 `manager_subscribe` 函数的返回值，
 *           该返回值表示订阅操作的最终结果。
 */
int neu_manager_subscribe(neu_manager_t *manager, const char *app,
                          const char *driver, const char *group,
                          const char *params, const char *static_tags,
                          uint16_t *app_port)
{
    // 在节点管理器中查找指定名称的应用程序适配器
    neu_adapter_t *adapter = neu_node_manager_find(manager->node_manager, app);

    // 如果未找到应用程序适配器，说明应用程序节点不存在
    if (adapter == NULL) {
        return NEU_ERR_NODE_NOT_EXIST;
    }

    // 获取应用程序的数据传输端口号，并存储到 app_port 指向的位置
    *app_port = neu_adapter_trans_data_port(adapter);

    /**
     * @warning  检查参数是否存在，并且应用程序的模块名称为 "MQTT"
     * 
     * 防止 MQTT Topic 参数为空。对于当前的架构，这不是一个优雅的解决方案
     */
    if (params && 0 == strcmp(adapter->module->module_name, "MQTT")) {
        // 定义一个 JSON 元素结构体，用于解析 MQTT 主题参数
        neu_json_elem_t elem = { .name      = "topic",
                                 .t         = NEU_JSON_STR,
                                 .v.val_str = NULL };
        
        // 解析参数，尝试提取 MQTT 主题
        int             ret  = neu_parse_param(params, NULL, 1, &elem);

        // 如果解析失败，或者提取的主题字符串为空
        if (ret != 0 || (elem.v.val_str && 0 == strlen(elem.v.val_str))) {
            // 释放解析过程中分配的内存
            free(elem.v.val_str);

            // 返回 MQTT 订阅失败的错误码
            return NEU_ERR_MQTT_SUBSCRIBE_FAILURE;
        }
        free(elem.v.val_str);
    }

    // 检查应用程序适配器的类型是否为应用程序类型
    if (NEU_NA_TYPE_APP != neu_adapter_get_type(adapter)) {
        // 如果不是应用程序类型，返回节点不允许订阅的错误码
        return NEU_ERR_NODE_NOT_ALLOW_SUBSCRIBE;
    }

    // 完成订阅操作
    return manager_subscribe(manager, app, driver, group, params, static_tags);
}

int neu_manager_update_subscribe(neu_manager_t *manager, const char *app,
                                 const char *driver, const char *group,
                                 const char *params, const char *static_tags)
{
    return neu_subscribe_manager_update_params(
        manager->subscribe_manager, app, driver, group, params, static_tags);
}

int neu_manager_send_subscribe(neu_manager_t *manager, const char *app,
                               const char *driver, const char *group,
                               uint16_t app_port, const char *params,
                               const char *static_tags)
{
    neu_req_subscribe_t cmd = { 0 };
    strcpy(cmd.app, app);
    strcpy(cmd.driver, driver);
    strcpy(cmd.group, group);
    cmd.port = app_port;

    if (params && NULL == (cmd.params = strdup(params))) {
        return NEU_ERR_EINTERNAL;
    }

    if (static_tags && NULL == (cmd.static_tags = strdup(static_tags))) {
        return NEU_ERR_EINTERNAL;
    }

    neu_msg_t *msg = neu_msg_new(NEU_REQ_SUBSCRIBE_GROUP, NULL, &cmd);
    if (NULL == msg) {
        free(cmd.params);
        return NEU_ERR_EINTERNAL;
    }
    neu_reqresp_head_t *header = neu_msg_get_header(msg);
    strcpy(header->sender, "manager");
    strcpy(header->receiver, app);

    struct sockaddr_un addr =
        neu_node_manager_get_addr(manager->node_manager, app);

    int ret = neu_send_msg_to(manager->server_fd, &addr, msg);
    if (0 != ret) {
        nlog_warn("send %s to %s app failed",
                  neu_reqresp_type_string(NEU_REQ_SUBSCRIBE_GROUP), app);
        free(cmd.params);
        neu_msg_free(msg);
    } else {
        nlog_notice("send %s to %s app",
                    neu_reqresp_type_string(NEU_REQ_SUBSCRIBE_GROUP), app);
    }
    cmd.params = NULL;

    msg = neu_msg_new(NEU_REQ_SUBSCRIBE_GROUP, NULL, &cmd);
    if (NULL == msg) {
        return NEU_ERR_EINTERNAL;
    }
    header = neu_msg_get_header(msg);
    strcpy(header->sender, "manager");
    strcpy(header->receiver, driver);
    addr = neu_node_manager_get_addr(manager->node_manager, driver);

    ret = neu_send_msg_to(manager->server_fd, &addr, msg);
    if (0 != ret) {
        nlog_warn("send %s to %s driver failed",
                  neu_reqresp_type_string(NEU_REQ_SUBSCRIBE_GROUP), driver);
        neu_msg_free(msg);
    } else {
        nlog_notice("send %s to %s driver",
                    neu_reqresp_type_string(NEU_REQ_SUBSCRIBE_GROUP), driver);
    }

    return 0;
}

int neu_manager_unsubscribe(neu_manager_t *manager, const char *app,
                            const char *driver, const char *group)
{
    return neu_subscribe_manager_unsub(manager->subscribe_manager, driver, app,
                                       group);
}

UT_array *neu_manager_get_sub_group(neu_manager_t *manager, const char *app)
{
    return neu_subscribe_manager_get(manager->subscribe_manager, app, NULL,
                                     NULL);
}

UT_array *neu_manager_get_sub_group_deep_copy(neu_manager_t *manager,
                                              const char *   app,
                                              const char *   driver,
                                              const char *   group)
{
    UT_array *subs = neu_subscribe_manager_get(manager->subscribe_manager, app,
                                               driver, group);

    utarray_foreach(subs, neu_resp_subscribe_info_t *, sub)
    {
        if (sub->params) {
            sub->params = strdup(sub->params);
        }
        if (sub->static_tags) {
            sub->static_tags = strdup(sub->static_tags);
        }
    }

    // set vector element destructor
    subs->icd.dtor = (void (*)(void *)) neu_resp_subscribe_info_fini;

    return subs;
}

int neu_manager_get_node_info(neu_manager_t *manager, const char *name,
                              neu_persist_node_info_t *info)
{
    neu_adapter_t *adapter = neu_node_manager_find(manager->node_manager, name);

    if (adapter != NULL) {
        info->name        = strdup(name);
        info->type        = adapter->module->type;
        info->plugin_name = strdup(adapter->module->module_name);
        info->state       = adapter->state;
        return 0;
    }

    return -1;
}

static int del_node(neu_manager_t *manager, const char *node)
{
    neu_adapter_t *adapter = neu_node_manager_find(manager->node_manager, node);
    if (NULL == adapter) {
        return 0;
    }

    if (neu_node_manager_is_single(manager->node_manager, node)) {
        return NEU_ERR_NODE_NOT_ALLOW_DELETE;
    }

    if (neu_adapter_get_type(adapter) == NEU_NA_TYPE_APP) {
        UT_array *subscriptions = neu_subscribe_manager_get(
            manager->subscribe_manager, node, NULL, NULL);
        neu_subscribe_manager_unsub_all(manager->subscribe_manager, node);

        utarray_foreach(subscriptions, neu_resp_subscribe_info_t *, sub)
        {
            // NOTE: neu_req_unsubscribe_t and neu_resp_subscribe_info_t
            //       have compatible memory layout
            neu_msg_t *msg = neu_msg_new(NEU_REQ_UNSUBSCRIBE_GROUP, NULL, sub);
            if (NULL == msg) {
                break;
            }
            neu_reqresp_head_t *hd = neu_msg_get_header(msg);
            strcpy(hd->receiver, sub->driver);
            strcpy(hd->sender, "manager");
            forward_msg(manager, hd, hd->receiver);
        }
        utarray_free(subscriptions);
    }

    if (neu_adapter_get_type(adapter) == NEU_NA_TYPE_DRIVER) {
        neu_reqresp_node_deleted_t resp = { 0 };
        strcpy(resp.node, node);

        UT_array *apps = neu_subscribe_manager_find_by_driver(
            manager->subscribe_manager, node);
        utarray_foreach(apps, neu_app_subscribe_t *, app)
        {
            neu_msg_t *msg = neu_msg_new(NEU_REQRESP_NODE_DELETED, NULL, &resp);
            if (NULL == msg) {
                break;
            }
            neu_reqresp_head_t *hd = neu_msg_get_header(msg);
            strcpy(hd->receiver, app->app_name);
            strcpy(hd->sender, "manager");
            forward_msg(manager, hd, hd->receiver);
        }
        utarray_free(apps);
    }

    neu_adapter_uninit(adapter);
    neu_manager_del_node(manager, node);
    manager_storage_del_node(manager, node);
    return 0;
}

static inline int add_driver(neu_manager_t *manager, neu_req_driver_t *driver)
{
    int ret = del_node(manager, driver->node);
    if (0 != ret) {
        return ret;
    }

    ret = neu_manager_add_node(manager, driver->node, driver->plugin,
                               driver->setting, false, false);
    if (0 != ret) {
        return ret;
    }

    neu_adapter_t *adapter =
        neu_node_manager_find(manager->node_manager, driver->node);

    neu_resp_add_tag_t resp = { 0 };
    neu_req_add_gtag_t cmd  = {
        .groups  = driver->groups,
        .n_group = driver->n_group,
    };

    if (0 != neu_adapter_validate_gtags(adapter, &cmd, &resp) ||
        0 != neu_adapter_try_add_gtags(adapter, &cmd, &resp) ||
        0 != neu_adapter_add_gtags(adapter, &cmd, &resp)) {
        neu_adapter_uninit(adapter);
        neu_manager_del_node(manager, driver->node);
    }

    return resp.error;
}

int neu_manager_add_drivers(neu_manager_t *manager, neu_req_driver_array_t *req)
{
    int ret = 0;

    // fast check
    for (uint16_t i = 0; i < req->n_driver; ++i) {
        neu_resp_plugin_info_t info   = { 0 };
        neu_req_driver_t *     driver = &req->drivers[i];

        ret = neu_plugin_manager_find(manager->plugin_manager, driver->plugin,
                                      &info);

        if (ret != 0) {
            return NEU_ERR_LIBRARY_NOT_FOUND;
        }

        if (info.single) {
            return NEU_ERR_LIBRARY_NOT_ALLOW_CREATE_INSTANCE;
        }

        if (NEU_NA_TYPE_DRIVER != info.type) {
            return NEU_ERR_PLUGIN_TYPE_NOT_SUPPORT;
        }

        if (driver->n_group > NEU_GROUP_MAX_PER_NODE) {
            return NEU_ERR_GROUP_MAX_GROUPS;
        }
    }

    for (uint16_t i = 0; i < req->n_driver; ++i) {
        ret = add_driver(manager, &req->drivers[i]);
        if (0 != ret) {
            nlog_notice("add i:%" PRIu16 " driver:%s fail", i,
                        req->drivers[i].node);
            while (i-- > 0) {
                nlog_notice("rollback i:%" PRIu16 " driver:%s", i,
                            req->drivers[i].node);
                neu_adapter_t *adapter = neu_node_manager_find(
                    manager->node_manager, req->drivers[i].node);
                neu_adapter_uninit(adapter);
                neu_manager_del_node(manager, req->drivers[i].node);
            }
            nlog_error("fail to add %" PRIu16 " drivers", req->n_driver);
            break;
        }
        nlog_notice("add i:%" PRIu16 " driver:%s success", i,
                    req->drivers[i].node);
    }

    return ret;
}
