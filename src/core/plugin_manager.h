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

#ifndef _NEU_PLUGIN_MANAGER_H_
#define _NEU_PLUGIN_MANAGER_H_

#include "utils/utextend.h"

#include "adapter.h"
#include "define.h"
#include "plugin.h"

#define DEFAULT_DASHBOARD_PLUGIN_NAME "default-dashboard"

/**
 * @brief 
 * 
 * - 管理动态加载的插件库.它是插件在运行时的一个实例化表示，
 *   重点在于插件库的加载、引用和资源管理
 * 
 * @note
 * - 动态库管理：handle 成员是通过 dlopen 函数返回的动态链接库句柄，
 *   它为系统提供了对插件库进行操作的入口。借助这个句柄，系统可以使用 
 *   dlsym 函数查找库中的符号（如函数或变量），或者使用 dlclose 函
 *   数关闭动态链接库，释放相关资源。
 * 
 * - 插件功能访问：module 成员是指向 neu_plugin_module_t 类型的
 *   指针，该类型包含了插件的版本信息、名称、描述、接口函数指针等内容。
 *   通过这个指针，系统可以访问插件所提供的各种功能，例如调用插件的初
 *   始化函数、数据处理函数等。
 */
typedef struct neu_plugin_instance {
    /**
     * @brief
     * 
     * 动态链接库的句柄，由dlopen函数返回。
     */
    void *               handle; 

    /**
     * @brief
     * 
     * 指向插件模块的指针，通常包含插件的功能接口。
     */
    neu_plugin_module_t *module; 
} neu_plugin_instance_t;

typedef struct neu_plugin_manager neu_plugin_manager_t;

neu_plugin_manager_t *neu_plugin_manager_create();
void                  neu_plugin_manager_destroy(neu_plugin_manager_t *mgr);

int neu_plugin_manager_add(neu_plugin_manager_t *mgr,
                           const char *          plugin_lib_name);
int neu_plugin_manager_del(neu_plugin_manager_t *mgr, const char *plugin_name);

// neu_resp_plugin_info_t array
UT_array *neu_plugin_manager_get(neu_plugin_manager_t *mgr);
// neu_resp_plugin_info_t array
UT_array *neu_plugin_manager_get_single(neu_plugin_manager_t *mgr);
int  neu_plugin_manager_find(neu_plugin_manager_t *mgr, const char *plugin_name,
                             neu_resp_plugin_info_t *info);
bool neu_plugin_manager_exists(neu_plugin_manager_t *mgr,
                               const char *          plugin_name);
bool neu_plugin_manager_is_single(neu_plugin_manager_t *mgr,
                                  const char *          plugin_name);

int  neu_plugin_manager_create_instance(neu_plugin_manager_t * mgr,
                                        const char *           plugin_name,
                                        neu_plugin_instance_t *instance);
void neu_plugin_manager_destroy_instance(neu_plugin_manager_t * mgr,
                                         neu_plugin_instance_t *instance);

void neu_plugin_manager_load_static(neu_plugin_manager_t * mgr,
                                    const char *           plugin_name,
                                    neu_plugin_instance_t *instance);

bool neu_plugin_manager_create_instance_by_path(neu_plugin_manager_t *mgr,
                                                const char *plugin_path,
                                                neu_plugin_instance_t *instance,
                                                int *                  error);

bool neu_plugin_manager_create_instance_by_lib_name(
    neu_plugin_manager_t *mgr, const char *lib_name,
    neu_plugin_instance_t *instance);

bool neu_plugin_manager_remove_library(neu_plugin_manager_t *mgr,
                                       const char *          library);

int neu_plugin_manager_update(neu_plugin_manager_t *mgr,
                              const char *          plugin_lib_name);

bool neu_plugin_manager_schema_exist(neu_plugin_manager_t *mgr,
                                     const char *          schema);

#endif