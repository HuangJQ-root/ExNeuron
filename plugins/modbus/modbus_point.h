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
#ifndef _NEU_PLUGIN_MODBUS_POINT_H_
#define _NEU_PLUGIN_MODBUS_POINT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include <neuron.h>

#include "modbus.h"

/**
 * @brief 用于表示 Modbus 点位信息的结构体。
 * 
 * 封装了与 Modbus 点位相关的各种属性信息，这
 * 些信息对于在 Modbus 通信中准确访问和处理特
 * 定的数据点位至关重要，可用于配置、数据采集以
 * 及设备交互等场景。
 */
typedef struct modbus_point {
    /**
     * @brief Modbus 从站设备的 ID。
     * 
     */
    uint8_t       slave_id;

    /**
     * @brief Modbus 数据区域类型。
     * 
     */
    modbus_area_e area;

    /**
     * @brief 点位在数据区域中的起始地址。
     * 
     * 结合 `area` 和 `start_address` 
     * 可以准确地定位到设备中的具体数据位置。
     */
    uint16_t      start_address;

    /**
     * @brief 点位所占用的寄存器数量。
     * 
     * 表示该点位连续占用的 Modbus 寄存器的数量，
     * 对于一些复杂的数据类型（如浮点数、双字等）
     * 需要多个寄存器来存储。
     */
    uint16_t      n_register;

    /**
     * @brief 点位的数据类型。
     * 
     */
    neu_type_e                type;

    /**
     * @brief 点位的地址选项信息。
     * 
     * 用于存储与该点位地址相关的额外选项信息，
     * 例如地址的偏移、掩码等，
     */
    neu_datatag_addr_option_u option;

    /**
     * @brief 点位的名称。
     * 
     */
    char                      name[NEU_TAG_NAME_LEN];
} modbus_point_t;

typedef struct modbus_point_write {
    modbus_point_t point;
    neu_value_u    value;
} modbus_point_write_t;

int modbus_tag_to_point(const neu_datatag_t *tag, modbus_point_t *point,
                        modbus_address_base address_base);
int modbus_write_tag_to_point(const neu_plugin_tag_value_t *tag,
                              modbus_point_write_t *        point,
                              modbus_address_base           address_base);

/**
 * @brief 用于表示 Modbus 读取命令的结构体。
 * 
 * 包含了执行 Modbus 读取操作所需的关键信息
 */
typedef struct modbus_read_cmd {
    /**
     * @brief Modbus 从站设备的 ID。
     * 
     * 指定要与之通信的 Modbus 从站设备的
     * 唯一标识符，取值范围通常为 1 到 255
     */
    uint8_t       slave_id;

    /**
     * @brief Modbus 数据区域类型。
     * 
     * 指定要读取的数据在 Modbus 设备中的数据区域，
     * 例如线圈区域、保持寄存器区域等
     */
    modbus_area_e area;

    /**
     * @brief 读取数据的起始地址。
     * 
     */
    uint16_t      start_address;

    /**
     * @brief 要读取的寄存器数量。
     * 
     * 指定从起始地址开始要连续读取的寄存器数量，
     */
    uint16_t      n_register;

    /**
     * @brief 指向包含 Modbus 点位信息的动态数组的指针。
     * 
     * 元素类型：modbus_point_t **。
     */
    UT_array *tags; 
} modbus_read_cmd_t;

/**
 * @brief 用于存储 Modbus 读取命令排序信息的结构体。
 * 
 * 该结构体封装了一系列经过排序的 Modbus 读取命令，通过合理的排序可以优化 Modbus 通信过程，
 * 提高数据读取的效率和性能，减少通信延迟和资源消耗。
 */
typedef struct modbus_read_cmd_sort {
    /**
     * @brief Modbus 读取命令的数量。
     * 
     */
    uint16_t           n_cmd;

    /**
     * @brief 指向 Modbus 读取命令数组的指针。
     * 
     * 是一个指向 `modbus_read_cmd_t` 结构体数组的指针。
     * 这个数组中存储了一系列经过排序的 Modbus 读取命令，
     * 每个 `modbus_read_cmd_t` 结构体包含了执行一次 
     * Modbus 读取操作所需的详细信息，如从站 ID、数据区域、
     * 起始地址和寄存器数量等。
     */
    modbus_read_cmd_t *cmd;
} modbus_read_cmd_sort_t;

typedef struct modbus_write_cmd {
    uint8_t       slave_id;
    modbus_area_e area;
    uint16_t      start_address;
    uint16_t      n_register;
    uint8_t       n_byte;
    uint8_t *     bytes;

    UT_array *tags;
} modbus_write_cmd_t;

typedef struct modbus_write_cmd_sort {
    uint16_t            n_cmd;
    modbus_write_cmd_t *cmd;
} modbus_write_cmd_sort_t;

modbus_read_cmd_sort_t * modbus_tag_sort(UT_array *tags, uint16_t max_byte);
modbus_write_cmd_sort_t *modbus_write_tags_sort(UT_array *       tags,
                                                modbus_endianess endianess);
void                     modbus_tag_sort_free(modbus_read_cmd_sort_t *cs);

void modbus_convert_endianess(neu_value_u *value, modbus_endianess endianess);

#ifdef __cplusplus
}
#endif

#endif