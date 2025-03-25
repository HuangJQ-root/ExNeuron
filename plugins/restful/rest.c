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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nng/nng.h>
#include <nng/supplemental/http/http.h>

#include "adapter_handle.h"
#include "argparse.h"
#include "datatag_handle.h"
#include "define.h"
#include "global_config_handle.h"
#include "group_config_handle.h"
#include "handle.h"
#include "normal_handle.h"
#include "otel/otel_manager.h"
#include "plugin_handle.h"
#include "rest.h"
#include "rw_handle.h"
#include "scan_handle.h"
#include "utils/http.h"
#include "utils/log.h"
#include "utils/neu_jwt.h"
#include "utils/time.h"
#include "json/neu_json_fn.h"

#define neu_plugin_module default_dashboard_plugin_module

/**
 * @brief 插件结构体，用于描述一个插件的基本信息及其相关资源。
 *
 * 此结构体包含了插件的基本属性、HTTP服务器实例以及REST处理上下文等信息。
 * 它是插件的核心数据结构，用于管理和操作插件的各项功能。
 */
struct neu_plugin {
    /**
     * @brief 插件的公共部分。
     *
     * 包含插件的一些基础信息和通用配置，比如插件名称、版本号等。
     */
    neu_plugin_common_t    common;

    /**
     * @brief 指向nng_http_server类型的指针，表示插件使用的HTTP服务器实例。
     *
     * 通过此服务器实例，插件可以提供RESTful API服务，允许外部应用与插件进行交互。
     */
    nng_http_server       *server;

    /**
     * @brief 指向neu_rest_handle_ctx_t类型的指针，表示插件的REST处理上下文。
     *
     * 此上下文包含处理REST请求所需的各种信息和状态，如路由表、中间件等。
     */
    neu_rest_handle_ctx_t *handle_ctx;
};

/**
 * @brief 初始化并绑定HTTP服务器。
 *
 * 此函数负责初始化一个NNG HTTP服务器，并将其绑定到指定的URL地址。如果成功，
 * 它将返回指向新创建的HTTP服务器实例的指针；否则，返回NULL。
 *
 * @return 成功时返回指向nng_http_server的指针；失败时返回NULL。
 */
static nng_http_server *server_init()
{
    //解析后的URL结构体指针
    nng_url *        url;

    //HTTP服务器实例指针
    nng_http_server *server;

    nlog_notice("bind url: %s", host_port);

    // 解析host_port字符串为nng_url结构体
    int ret = nng_url_parse(&url, host_port);
    if (ret != 0) {
        // 如果解析失败，记录错误并释放已分配的资源，然后返回NULL
        nng_url_free(url);
        return NULL;
    }

    // 尝试创建并绑定HTTP服务器到解析出的URL
    ret = nng_http_server_hold(&server, url);
    if (ret != 0) {
        nlog_error("rest api server bind error: %d", ret);
        return NULL;
    }

    // 清理不再需要的URL结构体
    nng_url_free(url);

    return server;
}

/**
 * @brief 打开（初始化）仪表盘插件。
 *
 * 此函数负责创建并初始化一个仪表盘插件实例。它包括设置插件的基本信息、
 * 初始化HTTP服务器以及注册RESTful API处理器等操作。如果任何步骤失
 * 败，则会清理已分配的资源并返回NULL。
 *
 * @return 
 * 
 * 成功时返回指向neu_plugin_t类型的指针，表示初始化完成的插件实例；
 * 失败时返回NULL。
 */
static neu_plugin_t *dashb_plugin_open(void)
{
    // 用于存储函数返回值的状态变量
    int                            rv;
    // 分配内存并初始化插件实例
    neu_plugin_t *                 plugin    = calloc(1, sizeof(neu_plugin_t));
    // RESTful处理器的数量
    uint32_t                       n_handler = 0;
    // RESTful处理器数组
    const struct neu_http_handler *rest_handlers = NULL;
    // CORS处理器数组
    const struct neu_http_handler *cors          = NULL;

    // 初始化插件的公共部分
    neu_plugin_common_init(&plugin->common);

    // 初始化插件的REST上下文
    plugin->handle_ctx = neu_rest_init_ctx(plugin);

    // 初始化HTTP服务器
    plugin->server = server_init();

    if (plugin->server == NULL) {
        nlog_error("Failed to create RESTful server");
        goto server_init_fail;
    }

    // 获取RESTful处理器并添加到HTTP服务器
    neu_rest_handler(&rest_handlers, &n_handler);
    for (uint32_t i = 0; i < n_handler; i++) {
        neu_http_add_handler(plugin->server, &rest_handlers[i]);
    }

    // 获取CORS处理器并添加到HTTP服务器
    neu_rest_api_cors_handler(&cors, &n_handler);
    for (uint32_t i = 0; i < n_handler; i++) {
        neu_http_add_handler(plugin->server, &cors[i]);
    }

    // 启动HTTP服务器
    if ((rv = nng_http_server_start(plugin->server)) != 0) {
        nlog_error("Failed to start api server, error=%d", rv);
        goto server_init_fail;
    }

    nlog_notice("Success to create plugin: %s", neu_plugin_module.module_name);
    return plugin;

server_init_fail:
    // 清理代码：释放HTTP服务器和上下文资源，并释放插件实例内存
    if (plugin->server != NULL) {
        nng_http_server_release(plugin->server);
    }
    neu_rest_free_ctx(plugin->handle_ctx);
    free(plugin);
    return NULL;
}

static int dashb_plugin_close(neu_plugin_t *plugin)
{
    int rv = 0;

    nng_http_server_stop(plugin->server);
    nng_http_server_release(plugin->server);

    free(plugin);
    nlog_notice("Success to free plugin: %s", neu_plugin_module.module_name);
    return rv;
}

static int dashb_plugin_init(neu_plugin_t *plugin, bool load)
{
    (void) load;
    int rv = 0;

    (void) plugin;

    rv = neu_jwt_init(g_config_dir);

    nlog_notice("Initialize plugin: %s", neu_plugin_module.module_name);
    return rv;
}

static int dashb_plugin_uninit(neu_plugin_t *plugin)
{
    int rv = 0;

    (void) plugin;

    nlog_notice("Uninitialize plugin: %s", neu_plugin_module.module_name);
    return rv;
}

/**
 * @brief 配置仪表盘插件的函数
 */
static int dashb_plugin_config(neu_plugin_t *plugin, const char *configs)
{
    int rv = 0;

    (void) plugin;
    (void) configs;

    nlog_notice("config plugin: %s", neu_plugin_module.module_name);
    return rv;
}

static int dashb_plugin_request(neu_plugin_t *      plugin,
                                neu_reqresp_head_t *header, void *data)
{
    (void) plugin;

    if (header->ctx && nng_aio_get_input(header->ctx, 3)) {
        // catch all response messages for global config request
        handle_global_config_resp(header->ctx, header->type, data);
        return 0;
    }

    neu_otel_trace_ctx trace = NULL;
    neu_otel_scope_ctx scope = NULL;
    if (neu_otel_control_is_started() && header->ctx) {
        trace = neu_otel_find_trace(header->ctx);
        if (trace) {
            scope = neu_otel_add_span(trace);
            neu_otel_scope_set_span_name(scope, "rest response");
            char new_span_id[36] = { 0 };
            neu_otel_new_span_id(new_span_id);
            neu_otel_scope_set_span_id(scope, new_span_id);
            uint8_t *p_sp_id = neu_otel_scope_get_pre_span_id(scope);
            if (p_sp_id) {
                neu_otel_scope_set_parent_span_id2(scope, p_sp_id, 8);
            }
            neu_otel_scope_add_span_attr_int(scope, "thread id",
                                             (int64_t)(pthread_self()));
            neu_otel_scope_set_span_start_time(scope, neu_time_ns());
        }
    }

    switch (header->type) {
    case NEU_RESP_ERROR: {
        neu_resp_error_t *error = (neu_resp_error_t *) data;
        NEU_JSON_RESPONSE_ERROR(error->error, {
            neu_http_response(header->ctx, error->error, result_error);
        });
        if (neu_otel_control_is_started() && trace) {
            if (error->error != NEU_ERR_SUCCESS) {
                neu_otel_scope_set_status_code2(scope, NEU_OTEL_STATUS_ERROR,
                                                error->error);
            } else {
                neu_otel_scope_set_status_code2(scope, NEU_OTEL_STATUS_OK,
                                                error->error);
            }
        }
        break;
    }
    case NEU_RESP_WRITE_TAGS: {
        handle_write_tags_resp(header->ctx, (neu_resp_write_tags_t *) data);
        break;
    }
    case NEU_RESP_CHECK_SCHEMA: {
        handle_get_plugin_schema_resp(header->ctx,
                                      (neu_resp_check_schema_t *) data);
        if (neu_otel_control_is_started() && trace) {
            neu_otel_scope_set_status_code2(scope, NEU_OTEL_STATUS_OK, 0);
        }
        break;
    }
    case NEU_RESP_GET_PLUGIN:
        handle_get_plugin_resp(header->ctx, (neu_resp_get_plugin_t *) data);
        if (neu_otel_control_is_started() && trace) {
            neu_otel_scope_set_status_code2(scope, NEU_OTEL_STATUS_OK, 0);
        }
        break;
    case NEU_RESP_GET_NODE:
        handle_get_adapter_resp(header->ctx, (neu_resp_get_node_t *) data);
        if (neu_otel_control_is_started() && trace) {
            neu_otel_scope_set_status_code2(scope, NEU_OTEL_STATUS_OK, 0);
        }
        break;
    case NEU_RESP_GET_GROUP:
        handle_get_group_resp(header->ctx, (neu_resp_get_group_t *) data);
        if (neu_otel_control_is_started() && trace) {
            neu_otel_scope_set_status_code2(scope, NEU_OTEL_STATUS_OK, 0);
        }
        break;
    case NEU_RESP_GET_DRIVER_GROUP:
        handle_get_driver_group_resp(header->ctx,
                                     (neu_resp_get_driver_group_t *) data);
        if (neu_otel_control_is_started() && trace) {
            neu_otel_scope_set_status_code2(scope, NEU_OTEL_STATUS_OK, 0);
        }
        break;
    case NEU_RESP_ADD_TAG:
        handle_add_tags_resp(header->ctx, (neu_resp_add_tag_t *) data);
        if (neu_otel_control_is_started() && trace) {
            neu_otel_scope_add_span_attr_int(
                scope, "error", ((neu_resp_add_tag_t *) data)->error);
        }
        break;
    case NEU_RESP_ADD_GTAG:
        handle_add_gtags_resp(header->ctx, (neu_resp_add_tag_t *) data);
        if (neu_otel_control_is_started() && trace) {
            neu_otel_scope_add_span_attr_int(
                scope, "error", ((neu_resp_add_tag_t *) data)->error);
        }
        break;
    case NEU_RESP_UPDATE_TAG:
        handle_update_tags_resp(header->ctx, (neu_resp_update_tag_t *) data);
        if (neu_otel_control_is_started() && trace) {
            neu_otel_scope_add_span_attr_int(
                scope, "error", ((neu_resp_update_tag_t *) data)->error);
        }
        break;
    case NEU_RESP_GET_TAG:
        handle_get_tags_resp(header->ctx, (neu_resp_get_tag_t *) data);
        if (neu_otel_control_is_started() && trace) {
            neu_otel_scope_set_status_code2(scope, NEU_OTEL_STATUS_OK, 0);
        }
        break;
    case NEU_RESP_GET_SUBSCRIBE_GROUP:
        handle_grp_get_subscribe_resp(header->ctx,
                                      (neu_resp_get_subscribe_group_t *) data);
        if (neu_otel_control_is_started() && trace) {
            neu_otel_scope_set_status_code2(scope, NEU_OTEL_STATUS_OK, 0);
        }
        break;
    case NEU_RESP_GET_NODE_SETTING:
        handle_get_node_setting_resp(header->ctx,
                                     (neu_resp_get_node_setting_t *) data);
        if (neu_otel_control_is_started() && trace) {
            neu_otel_scope_set_status_code2(scope, NEU_OTEL_STATUS_OK, 0);
        }
        break;
    case NEU_RESP_GET_NODES_STATE:
        handle_get_nodes_state_resp(header->ctx,
                                    (neu_resp_get_nodes_state_t *) data);
        if (neu_otel_control_is_started() && trace) {
            neu_otel_scope_set_status_code2(scope, NEU_OTEL_STATUS_OK, 0);
        }
        break;
    case NEU_RESP_GET_NODE_STATE:
        handle_get_node_state_resp(header->ctx,
                                   (neu_resp_get_node_state_t *) data);
        if (neu_otel_control_is_started() && trace) {
            neu_otel_scope_set_status_code2(scope, NEU_OTEL_STATUS_OK, 0);
        }
        break;
    case NEU_RESP_READ_GROUP:
        handle_read_resp(header->ctx, (neu_resp_read_group_t *) data);
        if (neu_otel_control_is_started() && trace) {
            neu_otel_scope_set_status_code2(scope, NEU_OTEL_STATUS_OK, 0);
        }
        break;
    case NEU_RESP_READ_GROUP_PAGINATE:
        handle_read_paginate_resp(header->ctx,
                                  (neu_resp_read_group_paginate_t *) data);
        if (neu_otel_control_is_started() && trace) {
            neu_otel_scope_set_status_code2(scope, NEU_OTEL_STATUS_OK, 0);
        }
        break;
    case NEU_RESP_TEST_READ_TAG:
        handle_test_read_tag_resp(header->ctx,
                                  (neu_resp_test_read_tag_t *) data);
        if (neu_otel_control_is_started() && trace) {
            neu_otel_scope_set_status_code2(scope, NEU_OTEL_STATUS_OK, 0);
        }
        break;
    case NEU_RESP_SCAN_TAGS:
        handle_scan_tags_resp(header->ctx, (neu_resp_scan_tags_t *) data);
        if (neu_otel_control_is_started() && trace) {
            if (((neu_resp_scan_tags_t *) data)->error != NEU_ERR_SUCCESS) {
                neu_otel_scope_set_status_code2(
                    scope, NEU_OTEL_STATUS_ERROR,
                    ((neu_resp_scan_tags_t *) data)->error);
            } else {
                neu_otel_scope_set_status_code2(
                    scope, NEU_OTEL_STATUS_OK,
                    ((neu_resp_scan_tags_t *) data)->error);
            }
        }
        break;
    default:
        nlog_fatal("recv unhandle msg: %s",
                   neu_reqresp_type_string(header->type));
        assert(false);
        break;
    }

    if (neu_otel_control_is_started() && trace) {
        neu_otel_scope_set_span_end_time(scope, neu_time_ns());
        neu_otel_trace_set_final(trace);
    }

    return 0;
}

static int dashb_plugin_start(neu_plugin_t *plugin)
{
    (void) plugin;
    return 0;
}

static int dashb_plugin_stop(neu_plugin_t *plugin)
{
    (void) plugin;
    return 0;
}

static const neu_plugin_intf_funs_t plugin_intf_funs = {
    .open    = dashb_plugin_open,
    .close   = dashb_plugin_close,
    .init    = dashb_plugin_init,
    .uninit  = dashb_plugin_uninit,
    .start   = dashb_plugin_start,
    .stop    = dashb_plugin_stop,
    .setting = dashb_plugin_config,
    .request = dashb_plugin_request,
};

#define DEFAULT_DASHBOARD_PLUGIN_DESCR \
    "A restful plugin for dashboard webserver"

const neu_plugin_module_t default_dashboard_plugin_module = {
    .version      = NEURON_PLUGIN_VER_1_0,
    .module_name  = "neuron-default-dashboard",
    .module_descr = DEFAULT_DASHBOARD_PLUGIN_DESCR,
    .intf_funs    = &plugin_intf_funs,
    .kind         = NEU_PLUGIN_KIND_SYSTEM,
    .type         = NEU_NA_TYPE_APP,
};
