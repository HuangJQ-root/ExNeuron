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

#ifndef ADAPTER_INFO_H
#define ADAPTER_INFO_H

#include <stdint.h>

#include "adapter.h"
#include "plugin.h"

/**
 * @brief 该结构体用于存储适配器的基本信息。
 *
 * 包含适配器的名字、句柄以及指向插件模块的指针。
 * 主要用于创建和管理适配器实例。
 */
typedef struct neu_adapter_info {
    /**
     * @brief 适配器的名字。
     *
     * 这个字段存储了适配器的名称，通常用于标识不同的适配器实例。
     */
    const char *         name;

    /**
     * @brief 适配器的句柄。
     *
     * 这个字段是一个通用指针，包含通过dlopen函数获取的动态库句柄，。
     */
    void *               handle;

    /**
     * @brief 指向插件模块信息的指针。
     *
     *包含了插件模块的具体实现和接口。
     */
    neu_plugin_module_t *module;
} neu_adapter_info_t;

#endif
