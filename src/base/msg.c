/**
 * NEURON IIoT System for Industry 4.0
 * Copyright (C) 2020-2023 EMQ Technologies Co., Ltd All rights reserved.
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

#include "msg.h"
#include "utils/log.h"

#include "msg_internal.h"

/**
 * @brief 根据请求/响应头部和数据生成完整的消息。
 *
 * 此函数用于根据提供的请求/响应头部和数据生成一个完整的消息。它首先计算消息体的大小，
 * 然后检查消息头部声明的长度是否足够容纳头部和数据部分。
 * 最后，将数据部分复制到消息头部之后的位置，完成消息的构建。
 *
 * @param header 指向 `neu_reqresp_head_t` 结构体的指针，表示消息的头部信息。
 * @param data  消息体数据，具体格式取决于请求/响应类型。
 */
void neu_msg_gen(neu_reqresp_head_t *header, void *data)
{
    // 计算消息体的大小
    size_t data_size = neu_reqresp_size(header->type);

    // 确保消息头部声明的长度足够容纳头部和数据部分，保证memcpy不会覆盖别的数据
    assert(header->len >= sizeof(neu_reqresp_head_t) + data_size);

    /**
     * @brief
     * 
     * &header[1] 表示 header 结构体之后的内存地址:
     * 后续可确定出是neu_msg_t的body数组中
     */
    memcpy((uint8_t *) &header[1], data, data_size);
}