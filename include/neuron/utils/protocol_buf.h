/**
 * NEURON IIoT System for Industry 4.0
 * Copyright (C) 2020-2021 EMQ Technologies Co., Ltd All rights reserved.
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
config_ **/

#ifndef _NEU_PROTOCOL_BUF_H_
#define _NEU_PROTOCOL_BUF_H_

#include <memory.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct {
    int      used;
    uint16_t need;
} neu_buf_result_t;

/**
 * @brief
 * - 定义通用的协议缓冲区类型
 * - 定义协议打包缓冲区类型
 * - 定义协议解包缓冲区类型
 */
typedef struct {
    /**
     * @brief 缓冲区的基地址
     *
     * 指向缓冲区内存的起始位置，所有存储在该缓冲区的数据都从这个地址开始。
     */
    uint8_t *base;

    /**
     * @brief 缓冲区的总大小
     *
     * 表示缓冲区所能容纳的最大数据量，以字节为单位。
     */
    uint16_t size;

    /**
     * @brief 缓冲区的当前偏移量
     *
     * 指示当前可用于存储或读取数据的位置相对于基地址的偏移量，以字节为单位。
     */
    uint16_t offset;
} neu_protocol_buf_t, neu_protocol_pack_buf_t, neu_protocol_unpack_buf_t;

/**
 * @brief Initialization of protocol_pack_buf.
 *
 * @param[in] buf neu_protocol_pack_buf_t to be initialized.
 * @param[in] base Memory used in protocol_pack_buf.
 * @param[in] size Size of base memory.
 */
inline static void neu_protocol_pack_buf_init(neu_protocol_pack_buf_t *buf,
                                              uint8_t *base, uint16_t size)
{
    buf->base   = base;
    buf->size   = size;
    buf->offset = size;
}

/**
 * @brief Initialization of protocol_unpack_buf.
 *
 * @param[in] buf neu_protocol_unpack_buf_t to be initialized.
 * @param[in] base Memory used in protocol_unpack_buf.
 * @param[in] size Size of base memory.
 */
inline static void neu_protocol_unpack_buf_init(neu_protocol_unpack_buf_t *buf,
                                                uint8_t *base, uint16_t size)
{
    buf->base   = base;
    buf->size   = size;
    buf->offset = 0;
}

inline static uint16_t neu_protocol_buf_size(neu_protocol_buf_t *buf)
{
    return buf->size;
}

inline static neu_protocol_unpack_buf_t *
neu_protocol_unpack_buf_new(uint16_t size)
{
    neu_protocol_unpack_buf_t *buf = (neu_protocol_unpack_buf_t *) calloc(
        1, sizeof(neu_protocol_unpack_buf_t));

    buf->base   = (uint8_t *) calloc(size, sizeof(uint8_t));
    buf->size   = size;
    buf->offset = 0;

    return buf;
}

inline static neu_protocol_pack_buf_t *neu_protocol_pack_buf_new(uint16_t size)
{
    neu_protocol_pack_buf_t *buf =
        (neu_protocol_pack_buf_t *) calloc(1, sizeof(neu_protocol_pack_buf_t));

    buf->base   = (uint8_t *) calloc(size, sizeof(uint8_t));
    buf->size   = size;
    buf->offset = size;

    return buf;
}

inline static void neu_protocol_buf_free(neu_protocol_buf_t *buf)
{
    free(buf->base);
    free(buf);
}

inline static void neu_protocol_unpack_buf_reset(neu_protocol_buf_t *buf)
{
    buf->offset = 0;
    memset(buf->base, 0, buf->size);
}

inline static void neu_protocol_pack_buf_reset(neu_protocol_buf_t *buf)
{
    buf->offset = buf->size;
    memset(buf->base, 0, buf->size);
}

/**
 * @brief 从协议解包缓冲区中提取指定大小的数据。
 *
 * 该内联函数用于从 缓冲区中提取指定大小的数据。它会检查缓冲区中剩余的可用数据量是否足够，
 * 如果足够则更新缓冲区的偏移量，并返回指向提取数据的指针；如果不足，则返回 `NULL` 表示
 * 提取失败。
 *
 * @param buf 指向 `neu_protocol_unpack_buf_t` 结构体的指针，该结构体管理着协议数
 *            据的解包缓冲区，包含缓冲区的基地址、总大小和当前偏移量等信息。
 * @param size 无符号 16 位整数，表示要从缓冲区中提取的数据的字节数。
 *
 * @return 如果缓冲区中剩余的可用数据量足够，返回指向提取数据的指针；
 *         如果剩余数据量不足，返回 `NULL`。
 */
inline static uint8_t *neu_protocol_unpack_buf(neu_protocol_unpack_buf_t *buf,
                                               uint16_t                   size)
{
    // 检查缓冲区中剩余的可用数据量是否小于要提取的数据大小
    if (buf->size - buf->offset < size) {
        return NULL;
    }

    // 更新缓冲区的偏移量，将其增加要提取的数据大小
    buf->offset += size;

    // 返回指向提取数据的指针，即缓冲区基地址加上偏移量减去要提取的数据大小
    return buf->base + buf->offset - size;
}

inline static void
neu_protocol_unpack_buf_revert(neu_protocol_unpack_buf_t *buf, uint16_t size)
{
    buf->offset -= size;
}

inline static uint8_t *
neu_protocol_unpack_buf_get(neu_protocol_unpack_buf_t *buf, uint16_t size)
{
    if (buf->size - buf->offset < size) {
        return NULL;
    }

    return buf->base + buf->offset;
}

/**
 * @brief 从协议打包缓冲区中分配指定大小的内存块
 *
 * 该内联函数用于从 `neu_protocol_pack_buf_t` 类型的缓冲区中分配指定大小的内存块。
 * 它会检查缓冲区剩余空间是否足够，如果足够则更新缓冲区的偏移量，并返回分配内存块的起始地址；
 * 若空间不足则返回 `NULL`。
 *
 * @param buf  协议打包缓冲区的指针。该缓冲区用于存储协议数据。
 * @param size 需要从缓冲区中分配的内存块大小，单位为字节。
 *
 * @return 若缓冲区剩余空间足够，返回分配的内存块的起始地址；若剩余空间不足，返回 `NULL`。
 *
 * @note 该函数会修改传入的 `buf` 结构体中的 `offset` 字段，以反映缓冲区剩余可用空间的变化。
 * 
 * @warning 调用者需要确保传入的 `buf` 指针不为 `NULL`，否则可能会导致未定义行为。
 */
inline static uint8_t *neu_protocol_pack_buf(neu_protocol_pack_buf_t *buf,
                                             uint16_t                 size)
{
    if (buf->offset < size) {
        return NULL;
    }

    buf->offset -= size;

    return buf->base + buf->offset;
}

inline static uint8_t *neu_protocol_pack_buf_set(neu_protocol_pack_buf_t *buf,
                                                 uint16_t offset, uint16_t size)
{
    if (buf->offset + offset + size - 1 > buf->size) {
        return NULL;
    }

    return buf->base + buf->offset + offset;
}

inline static uint8_t *neu_protocol_pack_buf_get(neu_protocol_pack_buf_t *buf)
{
    return buf->base + buf->offset;
}

inline static uint16_t
neu_protocol_pack_buf_unused_size(neu_protocol_pack_buf_t *buf)
{
    return buf->offset;
}

inline static uint16_t
neu_protocol_pack_buf_used_size(neu_protocol_pack_buf_t *buf)
{
    return buf->size - buf->offset;
}

inline static uint16_t
neu_protocol_unpack_buf_unused_size(neu_protocol_unpack_buf_t *buf)
{
    return buf->size - buf->offset;
}

inline static uint16_t
neu_protocol_unpack_buf_used_size(neu_protocol_unpack_buf_t *buf)
{
    return buf->offset;
}

inline static void
neu_protocol_unpack_buf_use_all(neu_protocol_unpack_buf_t *buf)
{
    buf->offset = buf->size;
}

inline static uint16_t
neu_protocol_unpack_buf_size(neu_protocol_unpack_buf_t *buf)
{
    return buf->size;
}

#endif
