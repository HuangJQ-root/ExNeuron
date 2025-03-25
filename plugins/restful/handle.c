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
#include <stdint.h>
#include <stdlib.h>

#include <nng/nng.h>
#include <nng/supplemental/http/http.h>

#include "plugin.h"

#include "adapter_handle.h"
#include "cid_handle.h"
#include "datatag_handle.h"
#include "global_config_handle.h"
#include "group_config_handle.h"
#include "log_handle.h"
#include "metric_handle.h"
#include "normal_handle.h"
#include "otel_handle.h"
#include "plugin_handle.h"
#include "rw_handle.h"
#include "scan_handle.h"
#include "system_handle.h"
#include "utils/http.h"
#include "version_handle.h"

#include "handle.h"

struct neu_rest_handle_ctx {
    void *plugin;
};

static struct neu_rest_handle_ctx *rest_ctx = NULL;

static struct neu_http_handler cors_handler[] = {
    {
        .url = "/api/v2/ping",
    },
    {
        .url = "/api/v2/login",
    },
    {
        .url = "/api/v2/tags",
    },
    {
        .url = "/api/v2/gtags",
    },
    {
        .url = "/api/v2/group",
    },
    {
        .url = "/api/v2/node",
    },
    {
        .url = "/api/v2/plugin",
    },
    {
        .url = "/api/v2/tty",
    },
    {
        .url = "/api/v2/read",
    },
    {
        .url = "/api/v2/read/paginate",
    },
    {
        .url = "/api/v2/read/test",
    },
    {
        .url = "/api/v2/write",
    },
    {
        .url = "/api/v2/write/tags",
    },
    {
        .url = "/api/v2/write/gtags",
    },
    {
        .url = "/api/v2/subscribe",
    },
    {
        .url = "/api/v2/subscribes",
    },
    {
        .url = "/api/v2/schema",
    },
    {
        .url = "/api/v2/node/setting",
    },
    {
        .url = "/api/v2/node/ctl",
    },
    {
        .url = "/api/v2/node/state",
    },
    {
        .url = "/api/v2/version",
    },
    {
        .url = "/api/v2/password",
    },
    {
        .url = "/api/v2/log/level",
    },
    {
        .url = "/api/v2/log/list",
    },
    {
        .url = "/api/v2/log/file",
    },
    {
        .url = "/api/v2/global/config",
    },
    {
        .url = "/api/v2/global/drivers",
    },
    {
        .url = "/api/v2/metrics",
    },
    {
        .url = "/api/v2/scan/tags",
    },
    {
        .url = "/api/v2/otel",
    },
    {
        .url = "/api/v2/cid",
    },
    {
        .url = "/api/v2/system/ctl",
    },
    {
        .url = "/api/v2/users",
    },
};

/**
 * @brief 定义一组HTTP处理器，用于处理不同类型的HTTP请求。
 *
 * 此数组包含了多个`neu_http_handler`结构体实例，每个实例描述了一个特定的HTTP请求处理器，
 * 包括其支持的HTTP方法、处理器类型、匹配的URL路径以及具体的处理器实现或相关信息。
 */
static struct neu_http_handler rest_handlers[] = {
    // 提供静态文件服务的处理器
    {
        .method     = NEU_HTTP_METHOD_GET,                   ///< 支持GET方法
        .type       = NEU_HTTP_HANDLER_DIRECTORY,            ///< 类型为目录服务
        .url        = "/web",                                ///< 匹配URL路径"/web"
        .value.path = "./dist",                              ///< 静态文件所在的目录路径
    },
    // 重定向处理器
    {
        .method     = NEU_HTTP_METHOD_UNDEFINE,              ///< 未定义HTTP方法（适用于所有方法）
        .type       = NEU_HTTP_HANDLER_REDIRECT,             ///< 类型为重定向
        .url        = "/",                                   ///< 匹配根路径"/"
        .value.path = "/web",                                ///< 重定向到路径"/web"
    },
    // API处理器示例：获取CID
    {
        .method        = NEU_HTTP_METHOD_POST,               ///< 支持POST方法
        .type          = NEU_HTTP_HANDLER_FUNCTION,          ///< 类型为直接处理器
        .url           = "/api/v2/cid",                      ///< 匹配URL路径"/api/v2/cid"
        .value.handler = handle_cid,                         ///< 指向处理函数handle_cid
    },
    // API处理器示例：Ping测试
    {
        .method        = NEU_HTTP_METHOD_POST,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/ping",
        .value.handler = handle_ping,                         ///< 处理Ping请求
    },
    // API处理器示例：用户登录
    {
        .method        = NEU_HTTP_METHOD_POST,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/login",                     ///< 处理登录请求
        .value.handler = handle_login,              
    },
    // API处理器示例：修改密码
    {
        .method        = NEU_HTTP_METHOD_POST,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/password",
        .value.handler = handle_password,                     ///< 处理修改密码请求
    },
    // API处理器示例：添加标签
    {
        .method        = NEU_HTTP_METHOD_POST,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/tags",
        .value.handler = handle_add_tags,                   ///< 处理添加标签请求

    },
    // API处理器示例：添加全局标签
    {
        .method        = NEU_HTTP_METHOD_POST,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/gtags",
        .value.handler = handle_add_gtags,                  ///< 处理添加全局标签请求
    },
    // API处理器示例：更新标签
    {
        .method        = NEU_HTTP_METHOD_PUT,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/tags",
        .value.handler = handle_update_tags,                ///< 处理更新标签请求
    },
    // API处理器示例：获取标签
    {
        .method        = NEU_HTTP_METHOD_GET,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/tags",
        .value.handler = handle_get_tags,                   ///< 处理获取标签请求
    },
    // API处理器示例：删除标签
    {
        .method        = NEU_HTTP_METHOD_DELETE,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/tags",
        .value.handler = handle_del_tags,                    ///< 处理删除标签请求
    },
    // API处理器示例：添加组配置
    {
        .method        = NEU_HTTP_METHOD_POST,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/group",
        .value.handler = handle_add_group_config,           ///< 处理添加组配置请求
    },
    // API处理器示例：更新组配置
    {
        .method        = NEU_HTTP_METHOD_PUT,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/group",
        .value.handler = handle_update_group,               ///< 处理更新组配置请求
    },
    // API处理器示例：删除组配置
    {
        .method        = NEU_HTTP_METHOD_DELETE,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/group",
        .value.handler = handle_del_group_config,           ///< 处理删除组配置请求
    },
    // API处理器示例：获取组配置
    {
        .method        = NEU_HTTP_METHOD_GET,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/group",
        .value.handler = handle_get_group_config,           ///< 处理获取组配置请求
    },
    // API处理器示例：添加节点/适配器
    {
        .method        = NEU_HTTP_METHOD_POST,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/node",
        .value.handler = handle_add_adapter,                ///< 处理添加节点/适配器请求
    },
    // API处理器示例：更新节点/适配器
    {
        .method        = NEU_HTTP_METHOD_PUT,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/node",
        .value.handler = handle_update_adapter,             ///< 处理更新节点/适配器请求
    },
    // API处理器示例：删除节点/适配器
    {
        .method        = NEU_HTTP_METHOD_DELETE,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/node",
        .value.handler = handle_del_adapter,                ///< 处理删除节点/适配器请求
    },
    // API处理器示例：获取节点/适配器
    {
        .method        = NEU_HTTP_METHOD_GET,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/node",
        .value.handler = handle_get_adapter,                ///< 处理获取节点/适配器请求
    },
    // API处理器示例：添加插件
    {
        .method        = NEU_HTTP_METHOD_POST,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/plugin",
        .value.handler = handle_add_plugin,                 ///< 处理添加插件请求
    },
    // API处理器示例：获取插件
    {
        .method        = NEU_HTTP_METHOD_GET,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/plugin",
        .value.handler = handle_get_plugin,                 ///< 处理获取插件请求
    },
    // API处理器示例：删除插件
    {
        .method        = NEU_HTTP_METHOD_DELETE,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/plugin",
        .value.handler = handle_del_plugin,                 ///< 处理删除插件请求
    },
    // API处理器示例：更新插件
    {
        .method        = NEU_HTTP_METHOD_PUT,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/plugin",
        .value.handler = handle_update_plugin,
    },
    // API处理器示例：读取数据
    {
        .method        = NEU_HTTP_METHOD_POST,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/read",
        .value.handler = handle_read,
    },
    // API处理器示例：分页读取数据
    {
        .method        = NEU_HTTP_METHOD_POST,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/read/paginate",
        .value.handler = handle_read_paginate,
    },
    // API处理器示例：测试读取标签
    {
        .method        = NEU_HTTP_METHOD_POST,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/read/test",
        .value.handler = handle_test_read_tag,
    },
    // API处理器示例：写入数据
    {
        .method        = NEU_HTTP_METHOD_POST,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/write",
        .value.handler = handle_write,
    },
    // API处理器示例：写入标签
    {
        .method        = NEU_HTTP_METHOD_POST,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/write/tags",
        .value.handler = handle_write_tags,
    },
    // API处理器示例：写入全局标签
    {
        .method        = NEU_HTTP_METHOD_POST,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/write/gtags",
        .value.handler = handle_write_gtags,
    },
    // API处理器示例：订阅组
    {
        .method        = NEU_HTTP_METHOD_POST,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/subscribe",
        .value.handler = handle_grp_subscribe,
    },
    // API处理器示例：更新订阅
    {
        .method        = NEU_HTTP_METHOD_PUT,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/subscribe",
        .value.handler = handle_grp_update_subscribe,
    },
    // API处理器示例：取消订阅组
    {
        .method        = NEU_HTTP_METHOD_DELETE,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/subscribe",
        .value.handler = handle_grp_unsubscribe,
    },
    // API处理器示例：获取订阅组信息
    {
        .method        = NEU_HTTP_METHOD_GET,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/subscribe",
        .value.handler = handle_grp_get_subscribe,
    },
    // API处理器示例：批量订阅组
    {
        .method        = NEU_HTTP_METHOD_POST,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/subscribes",
        .value.handler = handle_grp_subscribes,
    },
    // API处理器示例：获取插件模式
    {
        .method        = NEU_HTTP_METHOD_GET,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/schema",
        .value.handler = handle_get_plugin_schema,
    },
    // API处理器示例：设置节点配置
    {
        .method        = NEU_HTTP_METHOD_POST,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/node/setting",
        .value.handler = handle_set_node_setting,
    },
    // API处理器示例：获取节点配置
    {
        .method        = NEU_HTTP_METHOD_GET,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/node/setting",
        .value.handler = handle_get_node_setting,
    },
    // API处理器示例：控制节点
    {
        .method        = NEU_HTTP_METHOD_POST,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/node/ctl",
        .value.handler = handle_node_ctl,
    },
    // API处理器示例：获取节点状态
    {
        .method        = NEU_HTTP_METHOD_GET,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/node/state",
        .value.handler = handle_get_node_state,
    },
    // API处理器示例：设置日志级别
    {
        .method        = NEU_HTTP_METHOD_PUT,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/log/level",
        .value.handler = handle_log_level,
    },
    // API处理器示例：获取日志列表
    {
        .method        = NEU_HTTP_METHOD_GET,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/log/list",
        .value.handler = handle_log_list,
    },
    // API处理器示例：获取日志文件
    {
        .method        = NEU_HTTP_METHOD_GET,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/log/file",
        .value.handler = handle_log_file,
    },
    // API处理器示例：获取版本信息
    {
        .method        = NEU_HTTP_METHOD_GET,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/version",
        .value.handler = handle_get_version,
    },
    // API处理器示例：获取全局配置
    {
        .method        = NEU_HTTP_METHOD_GET,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/global/config",
        .value.handler = handle_get_global_config,
    },
    // API处理器示例：更新全局配置
    {
        .method        = NEU_HTTP_METHOD_PUT,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/global/config",
        .value.handler = handle_put_global_config,
    },
    // API处理器示例：更新驱动程序
    {
        .method        = NEU_HTTP_METHOD_PUT,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/global/drivers",
        .value.handler = handle_put_drivers,
    },
    // API处理器示例：获取驱动程序
    {
        .method        = NEU_HTTP_METHOD_GET,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/global/drivers",
        .value.handler = handle_get_drivers,
    },
    // API处理器示例：获取度量指标
    {
        .method        = NEU_HTTP_METHOD_GET,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/metrics",
        .value.handler = handle_get_metric,
    },
    // API处理器示例：扫描标签
    {
        .method        = NEU_HTTP_METHOD_POST,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/scan/tags",
        .value.handler = handle_scan_tags,
    },
    // API处理器示例：获取系统状态
    {
        .method        = NEU_HTTP_METHOD_GET,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/status",
        .value.handler = handle_status,
    },
    // API处理器示例：处理OpenTelemetry相关POST请求
    {
        .method        = NEU_HTTP_METHOD_POST,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/otel",
        .value.handler = handle_otel,
    },
    // API处理器示例：处理OpenTelemetry相关GET请求
    {
        .method        = NEU_HTTP_METHOD_GET,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/otel",
        .value.handler = handle_otel_get,
    },
    // API处理器示例：系统控制操作
    {
        .method        = NEU_HTTP_METHOD_POST,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/system/ctl",
        .value.handler = handle_system_ctl,
    },
    // API处理器示例：获取用户信息
    {
        .method        = NEU_HTTP_METHOD_GET,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/users",
        .value.handler = handle_get_user,
    },
    // API处理器示例：添加新用户
    {
        .method        = NEU_HTTP_METHOD_POST,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/users",
        .value.handler = handle_add_user,
    },
    // API处理器示例：更新用户信息
    {
        .method        = NEU_HTTP_METHOD_PUT,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/users",
        .value.handler = handle_update_user,
    },
    // API处理器示例：删除用户
    {
        .method        = NEU_HTTP_METHOD_DELETE,
        .type          = NEU_HTTP_HANDLER_FUNCTION,
        .url           = "/api/v2/users",
        .value.handler = handle_delete_user,
    },
};

void neu_rest_handler(const struct neu_http_handler **handlers, uint32_t *size)
{
    *handlers = rest_handlers;
    *size     = sizeof(rest_handlers) / sizeof(struct neu_http_handler);
}

void neu_rest_api_cors_handler(const struct neu_http_handler **handlers,
                               uint32_t *                      size)
{
    *handlers = cors_handler;
    *size     = sizeof(cors_handler) / sizeof(struct neu_http_handler);

    for (uint32_t i = 0; i < *size; i++) {
        cors_handler[i].method        = NEU_HTTP_METHOD_OPTIONS;
        cors_handler[i].type          = NEU_HTTP_HANDLER_FUNCTION;
        cors_handler[i].value.handler = neu_http_handle_cors;
    }
}

/**
 * @brief 初始化REST处理上下文。
 *
 * 此函数为指定的插件分配并初始化一个新的REST处理上下文（neu_rest_handle_ctx_t），
 * 并将该上下文与提供的插件实例关联起来。
 *
 * @param plugin 指向插件实例的指针。
 * @return 成功时返回指向新创建的REST处理上下文的指针；如果内存分配失败，则返回NULL。
 */
neu_rest_handle_ctx_t *neu_rest_init_ctx(void *plugin)
{
    rest_ctx         = calloc(1, sizeof(neu_rest_handle_ctx_t));
    rest_ctx->plugin = plugin;

    return rest_ctx;
}

void neu_rest_free_ctx(neu_rest_handle_ctx_t *ctx)
{
    free(ctx);
}

void *neu_rest_get_plugin()
{
    return rest_ctx->plugin;
}
