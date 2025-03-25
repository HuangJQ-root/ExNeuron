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
#ifndef _NEU_M_PLUGIN_MODBUS_H_
#define _NEU_M_PLUGIN_MODBUS_H_

#include <stdbool.h>
#include <stdint.h>

#include <neuron.h>

/* LL -> LE 1,2,3,4
   LB -> LE 2,1,4,3
   BB -> BE 3,4,1,2
   BL -> BE 4,3,2,1
   L64 -> 1,2,3,4,5,6,7,8
   B64 -> 8,7,6,5,4,3,2,1
*/

/* big-endian order */

typedef enum modbus_action {
    MODBUS_ACTION_DEFAULT        = 0,
    MODBUS_ACTION_HOLD_REG_WRITE = 1
} modbus_action_e;

typedef enum modbus_function {
    MODBUS_READ_COIL            = 0x1,
    MODBUS_READ_INPUT           = 0x02,
    MODBUS_READ_HOLD_REG        = 0x03,
    MODBUS_READ_INPUT_REG       = 0x04,
    MODBUS_WRITE_S_COIL         = 0x05,
    MODBUS_WRITE_S_HOLD_REG     = 0x06,
    MODBUS_WRITE_M_HOLD_REG     = 0x10,
    MODBUS_WRITE_M_COIL         = 0x0F,
    MODBUS_READ_COIL_ERR        = 0x81,
    MODBUS_READ_INPUT_ERR       = 0x82,
    MODBUS_READ_HOLD_REG_ERR    = 0x83,
    MODBUS_READ_INPUT_REG_ERR   = 0x84,
    MODBUS_WRITE_S_COIL_ERR     = 0x85,
    MODBUS_WRITE_S_HOLD_REG_ERR = 0x86,
    MODBUS_WRITE_M_HOLD_REG_ERR = 0x90,
    MODBUS_WRITE_M_COIL_ERR     = 0x8F,
    MODBUS_DEVICE_ERR           = -2
} modbus_function_e;

/**
 * @brief 用于表示Modbus数据区域类型的枚举。
 * 
 * 指定在Modbus通信中要访问的数据类型和位置。
 */
typedef enum modbus_area {
    /**
     * @brief 表示Modbus线圈区域。
     * 
     * 此区域用于存储和操作布尔型数据，
     * 通常用于控制设备的输出状态，如继电器的开关等。
     */
    MODBUS_AREA_COIL           = 0,

    /**
     * @brief 表示Modbus离散输入区域。
     * 
     * 该区域用于读取外部设备的离散输入状态，
     * 例如开关的闭合或打开状态等，数据为布尔型。
     */
    MODBUS_AREA_INPUT          = 1,

    /**
     * @brief 表示Modbus输入寄存器区域。
     * 
     */
    MODBUS_AREA_INPUT_REGISTER = 3,

    /**
     * @brief 表示Modbus保持寄存器区域。
     * 
     */
    MODBUS_AREA_HOLD_REGISTER  = 4,
} modbus_area_e;

typedef enum modbus_endianess {
    MODBUS_ABCD = 1,
    MODBUS_BADC = 2,
    MODBUS_DCBA = 3,
    MODBUS_CDAB = 4,
} modbus_endianess;

/**
 * @brief 用于表示 Modbus 地址基的枚举类型。
 * 
 * 在 Modbus 通信中，地址基用于确定数据点位的地址起始值。
 * 此枚举类型定义了两种可能的地址基，分别是地址基 0 和地址基 1。
 * 不同的地址基可能会影响到 Modbus 设备对地址的解析和数据的读取。
 */
typedef enum modbus_address_base {
    base_0 = 0,
    base_1 = 1,
} modbus_address_base;

/**
 * @brief 定义 Modbus 协议的头部结构
 *
 * 该结构体用于表示 Modbus 协议消息的头部信息，包含序列号、协议类
 * 型和消息长度等关键信息。
 * 
 * @note
 * - 使用 `__attribute__((packed))` 告诉编译器取消结构体在内存
 *   中的对齐方式，让结构体成员紧密排列，中间不插入任何填充字节确保
 *   结构体在内存中的布局与Modbus 协议规定的字节顺序一致，便于网络
 *   数据的处理。
 */
struct modbus_header {
    /**
     * @brief 消息序列号
     *
     * 用于标识 Modbus 消息的顺序或唯一性，
     * 帮助接收方对消息进行排序和匹配响应。
     */
    uint16_t seq;

    /**
     * @brief 协议类型标识符
     *
     * 表示 Modbus 消息遵循的具体协议版本或变体。
     */
    uint16_t protocol;

    /**
     * @brief 消息长度
     *
     * 存储 Modbus 消息中数据部分的字节数，接收方
     * 根据该长度信息正确解析消息内容。
     */
    uint16_t len;
} __attribute__((packed));

void modbus_header_wrap(neu_protocol_pack_buf_t *buf, uint16_t seq);
int  modbus_header_unwrap(neu_protocol_unpack_buf_t *buf,
                          struct modbus_header *     out_header);

/**
 * @brief 定义 Modbus 协议的功能码及从站 ID 结构
 *
 */
struct modbus_code {
    /**
     * @brief 从站设备的 ID
     *
     * 用于标识 Modbus 网络中的从站设备，取值范围通常为 1 到 247。
     * 主站通过该 ID 来确定要与哪个从站设备进行通信。
     */
    uint8_t slave_id;

    /**
     * @brief Modbus 功能码
     *
     * 定义了主站对从站设备执行的操作类型，例如读取线圈状态、写入保持寄存器等。
     * 不同的功能码对应不同的操作指令，从站设备根据功能码来执行相应的操作。
     */
    uint8_t function;
} __attribute__((packed));

void modbus_code_wrap(neu_protocol_pack_buf_t *buf, uint8_t slave_id,
                      uint8_t function);
int  modbus_code_unwrap(neu_protocol_unpack_buf_t *buf,
                        struct modbus_code *       out_code);

/**
 * @brief 定义 Modbus 协议中的地址相关结构
 */
struct modbus_address {
    /**
     * @brief 起始地址
     *
     * 在 Modbus 协议中，用于指定操作（如读取、写入）开始的寄存器地址。
     * 主站通过该起始地址来确定在从站设备的寄存器空间中操作的起始位置。
     */
    uint16_t start_address;

    /**
     * @brief 寄存器数量
     *
     * 表示从起始地址开始，要进行操作（如读取、写入）的连续寄存器的数量。
     * 主站和从站根据该数量来确定数据传输的长度和范围。
     */
    uint16_t n_reg;
} __attribute__((packed));

void modbus_address_wrap(neu_protocol_pack_buf_t *buf, uint16_t start,
                         uint16_t n_register, enum modbus_action m_action);
int  modbus_address_unwrap(neu_protocol_unpack_buf_t *buf,
                           struct modbus_address *    out_address);

/**
 * @brief 定义 Modbus 协议中的数据部分结构
 */
struct modbus_data {
    /**
     * @brief 数据字节数
     *
     * 数组中存储的实际数据的字节数量。接收方可
     * 以依据这个数值来确定要读取的数据长度，避
     * 免读取超出实际数据范围的内容。
     */
    uint8_t n_byte;

    /**
     * @brief 数据内容
     *
     * 这是一个柔性数组，用于存储 Modbus 协议消息
     * 中的实际数据。其长度由 `n_byte` 字段指定，
     * 可存储不同长度的数据。
     */
    uint8_t byte[];
} __attribute__((packed));

void modbus_data_wrap(neu_protocol_pack_buf_t *buf, uint8_t n_byte,
                      uint8_t *bytes, enum modbus_action action);
int  modbus_data_unwrap(neu_protocol_unpack_buf_t *buf,
                        struct modbus_data *       out_data);

/**
 * @brief 定义 Modbus 协议中的循环冗余校验（CRC）结构
 *
 * CRC 用于检测消息在传输过程中是否发生错误。
 */
struct modbus_crc {
    /**
     * @brief 循环冗余校验（CRC）值
     *
     * 这是一个 16 位的无符号整数，用于存储 Modbus 消息的 CRC 
     * 校验值。发送方在发送消息前会计算消息的 CRC 值，并将其附加
     * 到消息末尾。接收方在接收到消息后，会重新计算消息的 CRC 值，
     * 并与接收到的 CRC 值进行比较。如果两者相同，则认为消息在传
     * 输过程中没有发生错误；否则，认为消息可能已损坏。
     */
    uint16_t crc;
} __attribute__((packed));

void modbus_crc_set(neu_protocol_pack_buf_t *buf);
void modbus_crc_wrap(neu_protocol_pack_buf_t *buf);
int  modbus_crc_unwrap(neu_protocol_unpack_buf_t *buf,
                       struct modbus_crc *        out_crc);

const char *modbus_area_to_str(modbus_area_e area);

#endif