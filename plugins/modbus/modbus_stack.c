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
#include <assert.h>

#include <neuron.h>

#include "modbus_req.h"
#include "modbus_stack.h"

#include "otel/otel_manager.h"

/**
 * @brief 表示 Modbus 通信栈的结构体。
 * 
 * 该结构体封装了 Modbus 通信所需的各种信息和函数指针，
 * 用于管理 Modbus 协议的通信过程，包括发送请求、处理
 * 响应和记录通信状态等。
 */
struct modbus_stack {
    /**
     * @brief 上下文指针。
     * 
     * 保存插件结构体neu_plugin_t
     */
    void *                  ctx;

    /**
     * @brief 发送函数指针。
     * 
     * 指向一个函数，该函数用于将 Modbus 请求数据包发送到目标设备。
     * 函数原型为 `modbus_stack_send`，其参数包括上下文指针、数
     * 据包长度和数据包内容。
     */
    modbus_stack_send       send_fn;

    /**
     * @brief 值处理函数指针。
     * 
     * 指向一个函数，该函数用于处理从 Modbus 设备读取到的值或响应信息。
     * 函数原型为 `modbus_stack_value`，其参数通常包括上下文指针、
     * 数据相关的信息和错误码等。
     */
    modbus_stack_value      value_fn;

    /**
     * @brief 写响应处理函数指针。
     * 
     * 指向一个函数，该函数用于处理 Modbus 写操作的响应。
     * 函数原型为 `modbus_stack_write_resp`，其参数
     * 通常包括上下文指针和写操作的相关信息。
     */
    modbus_stack_write_resp write_resp;

    /**
     * @brief Modbus 协议类型。
     * 
     * 取值包括 Modbus TCP、Modbus RTU 等，不同的协议
     * 类型在通信格式和处理方式上有所不同。
     */
    modbus_protocol_e protocol;

    /**
     * @brief 读取操作的序列号。
     * 
     * 用于标识 Modbus 读取请求的序列号，每次发送读取请求时，
     * 该序列号会递增，可以帮助跟踪和匹配请求与响应，确保通信
     * 的正确性和顺序性。
     */
    uint16_t          read_seq;

    /**
     * @brief 写入操作的序列号。
     * 
     * 用于标识 Modbus 写入请求的序列号，每次发送写入请求时，
     * 该序列号会递增，同样用于跟踪和匹配写入请求与响应。
     */
    uint16_t          write_seq;

    /**
     * @brief 缓冲区指针。
     * 
     * 指向一个用于存储 Modbus 数据包的缓冲区，该缓冲区在通信
     * 过程中用于暂存发送和接收的数据。
     */
    uint8_t *buf;

    /**
     * @brief 缓冲区大小。
     * 
     * 缓冲区的大小，用于限制存储在缓冲区中的数据量，避免缓冲区溢出。
     */
    uint16_t buf_size;

    /**
     * @brief 采样计数器。
     * 
     * 用于 OpenTelemetry 数据采样的计数器，在进行数据采样时，
     * 通过该计数器判断是否满足采样条件，以控制采样的频率和比例。
     */
    int64_t sample_mod;
};

/**
 * @brief 创建一个 Modbus 栈实例
 *
 * 该函数用于动态分配并初始化一个 Modbus 栈实例。它会为栈分配内存，
 * 并设置栈的上下文、协议类型以及相关的回调函数。同时，会为数据缓冲
 * 区分配内存。
 *
 * @param ctx 指向用户上下文的指针，该上下文可以在后续的回调函数中使用。
 * @param protocol Modbus 协议类型
 * @param send_fn 用于发送数据的回调函数
 * @param value_fn 用于处理值的回调函数
 * @param write_resp 用于处理写响应的回调函数
 *
 * @return 如果内存分配成功，返回指向新创建的 Modbus 栈实例的指针；
 *         如果内存分配失败，返回 `NULL`。
 *
 * @note 调用者需要在使用完 Modbus 栈后调用 `modbus_stack_destroy`
 *       函数来释放分配的内存，避免内存泄漏。
 */
modbus_stack_t *modbus_stack_create(void *ctx, modbus_protocol_e protocol,
                                    modbus_stack_send       send_fn,
                                    modbus_stack_value      value_fn,
                                    modbus_stack_write_resp write_resp)
{
    modbus_stack_t *stack = calloc(1, sizeof(modbus_stack_t));

    stack->ctx        = ctx;
    stack->send_fn    = send_fn;
    stack->value_fn   = value_fn;
    stack->write_resp = write_resp;
    stack->protocol   = protocol;

    stack->buf_size = 256;
    stack->buf      = calloc(stack->buf_size, 1);

    return stack;
}

/**
 * @brief 销毁 Modbus 栈实例并释放相关内存
 *
 * 该函数用于销毁一个由 `modbus_stack_create` 函数创建的 Modbus 栈实例。
 * 它会按照正确的顺序释放 Modbus 栈实例所占用的内存资源，以避免内存泄漏。
 *
 * @param stack 指向要销毁的 `modbus_stack_t` 结构体实例的指针。
 *              若传入 `NULL`，函数将不执行任何操作。
 *
 * @warning 在调用此函数后，传入的 `stack` 指针将变为无效指针，不能再继续使用。
 *          确保在不再需要该 Modbus 栈实例时及时调用此函数进行资源释放。
 */
void modbus_stack_destroy(modbus_stack_t *stack)
{
    // 先释放 Modbus 栈实例中的缓冲区内存
    free(stack->buf);

    // 再释放 Modbus 栈实例本身的内存
    free(stack);
}

/**
 * @brief 处理接收到的 Modbus 协议数据。
 *
 * 该函数负责解析接收到的 Modbus 协议数据，根据协议类型（TCP 或 RTU）进行不同的处理，
 * 并根据 Modbus 功能码执行相应的操作，如读取数据、写入数据等。同时，还会处理错误码和 CRC 校验（RTU 协议）。
 *
 * @param stack 指向 modbus_stack_t 结构体的指针，包含 Modbus 栈的上下文信息，如协议类型、序列号等。
 * @param slave_id 无符号 8 位整数，表示 Modbus 从站的 ID，用于验证接收到的数据是否来自指定从站。
 * @param buf  包含接收到的协议数据缓冲区。
 *
 * @return 返回处理结果的状态码：
 *         - 若解析过程中出现错误，返回 -1。
 *         - 若遇到 Modbus 设备错误码，返回 MODBUS_DEVICE_ERR。
 *         - 若处理成功，返回 neu_protocol_unpack_buf_used_size(buf)，表示已使用的缓冲区大小。
 */
int modbus_stack_recv(modbus_stack_t *stack, uint8_t slave_id,
                      neu_protocol_unpack_buf_t *buf)
{
    // 初始化 Modbus 协议头结构体
    struct modbus_header header   = { 0 };

    // 初始化 Modbus 功能码结构体
    struct modbus_code   code     = { 0 };

    // 存储处理结果的返回值，初始化为 0
    int                  ret      = 0;

    // 记录处理开始的时间（纳秒）
    int64_t              ts_start = neu_time_ns();

    // 如果使用的是 Modbus TCP 协议
    if (stack->protocol == MODBUS_PROTOCOL_TCP) {
        // 解析 Modbus 协议头
        ret = modbus_header_unwrap(buf, &header);
        if (ret < 0) {
            plog_warn((neu_plugin_t *) stack->ctx, "try modbus rtu driver");
        }
        if (ret <= 0) {
            return -1;
        }

        // 将上下文指针转换为 neu_plugin_t 结构体指针
        neu_plugin_t *plugin = (neu_plugin_t *) stack->ctx;

        // 检查协议头中的序列号是否符合预期
        if (plugin->check_header && header.seq + 1 != stack->read_seq &&
            header.seq + 1 != stack->write_seq) {
            return -1;
        }
    }

    // 解析 Modbus 功能码
    ret = modbus_code_unwrap(buf, &code);
    if (ret <= 0) {
        return -1;
    }

    if (code.slave_id != slave_id) {
        return -1;
    }

    switch (code.function) {
    case MODBUS_READ_COIL:
    case MODBUS_READ_INPUT:
    case MODBUS_READ_HOLD_REG:
    case MODBUS_READ_INPUT_REG: {
        // 定义 OpenTelemetry 跟踪上下文和范围上下文指针
        neu_otel_trace_ctx trace = NULL;
        neu_otel_scope_ctx scope = NULL;

        // 检查 OpenTelemetry 数据采集是否已启动
        if (neu_otel_data_is_started()) {
            // 根据读取序列号查找跟踪上下文
            trace = neu_otel_find_trace((void *) (intptr_t) stack->read_seq);
            if (trace) {
                // 生成新的跨度 ID
                char new_span_id[36] = { 0 };
                neu_otel_new_span_id(new_span_id);

                // 在跟踪上下文中添加新的跨度
                scope =
                    neu_otel_add_span2(trace, "driver cmd recv", new_span_id);

                // 为跨度添加线程 ID 属性
                neu_otel_scope_add_span_attr_int(scope, "thread id",
                                                 (int64_t)(pthread_self()));
                
                // 设置跨度的开始时间
                neu_otel_scope_set_span_start_time(scope, ts_start);
                
                // 设置跨度的结束时间
                neu_otel_scope_set_span_end_time(scope, neu_time_ns());
            }
        }

        // 初始化 Modbus 数据结构体
        struct modbus_data data  = { 0 };

        // 指向解析出的数据字节的指针
        uint8_t *          bytes = NULL;

        // 解析 Modbus 数据
        ret                      = modbus_data_unwrap(buf, &data);
        if (ret <= 0) {
            return -1;
        }

        switch (stack->protocol) {
        case MODBUS_PROTOCOL_TCP:
            if (data.n_byte == 0xff) {
                // 若数据字节数为 0xff，从缓冲区中提取数据
                bytes = neu_protocol_unpack_buf(buf,
                                                header.len -
                                                    sizeof(struct modbus_code) -
                                                    sizeof(struct modbus_data));
                if (bytes == NULL) {
                    return -1;
                }

                // 处理数据
                stack->value_fn(stack->ctx, code.slave_id,
                                header.len - sizeof(struct modbus_code) -
                                    sizeof(struct modbus_data),
                                bytes, 0, (void *) (intptr_t) stack->read_seq);
            } else {
                bytes = neu_protocol_unpack_buf(buf, data.n_byte);
                if (bytes == NULL) {
                    return -1;
                }
                stack->value_fn(stack->ctx, code.slave_id, data.n_byte, bytes,
                                0, (void *) (intptr_t) stack->read_seq);
            }
            break;
        case MODBUS_PROTOCOL_RTU:
            // 从缓冲区中提取数据
            bytes = neu_protocol_unpack_buf(buf, data.n_byte);
            if (bytes == NULL) {
                return -1;
            }

            // 处理数据
            stack->value_fn(stack->ctx, code.slave_id, data.n_byte, bytes, 0,
                            (void *) (intptr_t) stack->read_seq);
            break;
        }

        break;
    }
    case MODBUS_WRITE_S_COIL:
    case MODBUS_WRITE_M_HOLD_REG:
    case MODBUS_WRITE_M_COIL: {
        struct modbus_address address = { 0 };
        ret                           = modbus_address_unwrap(buf, &address);
        if (ret <= 0) {
            return -1;
        }
        break;
    }
    case MODBUS_READ_COIL_ERR:
        return MODBUS_DEVICE_ERR;
    case MODBUS_READ_INPUT_ERR:
        return MODBUS_DEVICE_ERR;
    case MODBUS_READ_HOLD_REG_ERR:
        return MODBUS_DEVICE_ERR;
    case MODBUS_READ_INPUT_REG_ERR:
        return MODBUS_DEVICE_ERR;
    case MODBUS_WRITE_S_COIL_ERR:
        return MODBUS_DEVICE_ERR;
    case MODBUS_WRITE_S_HOLD_REG_ERR:
        return MODBUS_DEVICE_ERR;
    case MODBUS_WRITE_M_HOLD_REG_ERR:
        return MODBUS_DEVICE_ERR;
    case MODBUS_WRITE_M_COIL_ERR:
        return MODBUS_DEVICE_ERR;
    case MODBUS_WRITE_S_HOLD_REG:
        break;
    default:
        return -1;
    }   

    // 如果使用的是 Modbus RTU 协议,需判断crc
    if (stack->protocol == MODBUS_PROTOCOL_RTU) {
        // 初始化 Modbus CRC 结构体
        struct modbus_crc crc = { 0 };

        // 解析 Modbus CRC 校验码
        ret                   = modbus_crc_unwrap(buf, &crc);
        if (ret <= 0) {
            return -1;
        }
    }
    
    // 返回已使用的缓冲区大小
    return neu_protocol_unpack_buf_used_size(buf);
}

int modbus_stack_recv_test(neu_plugin_t *plugin, void *req,
                           modbus_point_t *           point,
                           neu_protocol_unpack_buf_t *buf)
{
    struct modbus_header header = { 0 };
    struct modbus_code   code   = { 0 };
    int                  ret    = 0;

    if (plugin->stack->protocol == MODBUS_PROTOCOL_TCP) {
        ret = modbus_header_unwrap(buf, &header);
        if (ret < 0) {
            plog_warn((neu_plugin_t *) plugin->stack->ctx,
                      "try modbus rtu driver");
        }
        if (ret <= 0) {
            return -1;
        }
    }

    ret = modbus_code_unwrap(buf, &code);
    if (ret <= 0) {
        return -1;
    }

    if (code.slave_id != point->slave_id) {
        return -1;
    }

    switch (code.function) {
    case MODBUS_READ_COIL:
    case MODBUS_READ_INPUT:
    case MODBUS_READ_HOLD_REG:
    case MODBUS_READ_INPUT_REG: {
        struct modbus_data data  = { 0 };
        uint8_t *          bytes = NULL;
        ret                      = modbus_data_unwrap(buf, &data);
        if (ret <= 0) {
            return -1;
        }

        if (data.n_byte == 0xff) {
            bytes = neu_protocol_unpack_buf(buf,
                                            header.len -
                                                sizeof(struct modbus_code) -
                                                sizeof(struct modbus_data));
            if (bytes == NULL) {
                return -1;
            }

            modbus_value_handle_test(plugin, req, point, data.n_byte, bytes);
        } else {
            bytes = neu_protocol_unpack_buf(buf, data.n_byte);
            if (bytes == NULL) {
                return -1;
            }
            modbus_value_handle_test(plugin, req, point, data.n_byte, bytes);
        }

        break;
    }
    case MODBUS_WRITE_S_COIL:
    case MODBUS_WRITE_M_HOLD_REG:
    case MODBUS_WRITE_M_COIL: {
        struct modbus_address address = { 0 };
        ret                           = modbus_address_unwrap(buf, &address);
        if (ret <= 0) {
            return -1;
        }
        break;
    }
    case MODBUS_READ_COIL_ERR:
        return MODBUS_DEVICE_ERR;
    case MODBUS_READ_INPUT_ERR:
        return MODBUS_DEVICE_ERR;
    case MODBUS_READ_HOLD_REG_ERR:
        return MODBUS_DEVICE_ERR;
    case MODBUS_READ_INPUT_REG_ERR:
        return MODBUS_DEVICE_ERR;
    case MODBUS_WRITE_S_COIL_ERR:
        return MODBUS_DEVICE_ERR;
    case MODBUS_WRITE_S_HOLD_REG_ERR:
        return MODBUS_DEVICE_ERR;
    case MODBUS_WRITE_M_HOLD_REG_ERR:
        return MODBUS_DEVICE_ERR;
    case MODBUS_WRITE_M_COIL_ERR:
        return MODBUS_DEVICE_ERR;
    case MODBUS_WRITE_S_HOLD_REG:
        break;
    default:
        return -1;
    }

    return neu_protocol_unpack_buf_used_size(buf);
}

/**
 * @brief 执行 Modbus 读取操作，根据不同的 Modbus 协议（RTU 或 TCP）打包读取请求并发送。
 *
 * 该函数会根据传入的参数，构建 Modbus 读取请求数据包，包括地址、功能码、CRC 校验（RTU 协议）、
 * 头部信息（TCP 协议）等，然后调用发送函数将数据包发送出去。同时，它会根据不同的数据区域
 * 估算响应的大小，并处理发送失败的情况。此外，还支持 OpenTelemetry 数据采样和跟踪功能。
 *
 * @param stack 结构体包含 Modbus 栈的相关信息，如协议类型、发送函数、上下文等。
 * @param slave_id 表示 Modbus 从站的 ID。
 * @param area 表示要读取的数据区域，如线圈、输入、输入寄存器、保持寄存器等。
 * @param start_address 表示读取的起始地址。
 * @param n_reg 表示要读取的寄存器数量。
 * @param response_size 用于存储估算的响应大小。
 * @param is_test 表示是否为测试模式。如果为 true，则发送失败时不进行额外处理。
 *
 * @return 若发送成功，返回发送的字节数；若发送失败，返回小于等于 0 的值。
 */
int modbus_stack_read(modbus_stack_t *stack, uint8_t slave_id,
                      enum modbus_area area, uint16_t start_address,
                      uint16_t n_reg, uint16_t *response_size, bool is_test)
{
    // 静态线程局部变量，用于存储协议数据包
    static __thread uint8_t                 buf[16] = { 0 };
    
    // 静态线程局部变量，用于管理协议数据包缓冲区
    static __thread neu_protocol_pack_buf_t pbuf    = { 0 };
    
     // 存储发送结果的变量
    int                                     ret     = 0;
    
    // 初始化响应大小为 0
    *response_size                                  = 0;

    // Modbus 操作类型，初始化为默认值
    modbus_action_e m_action                        = MODBUS_ACTION_DEFAULT;

    // 记录发送开始的时间戳，单位为纳秒
    int64_t         ts_start                        = neu_time_ns();

    // 初始化协议数据包缓冲区
    neu_protocol_pack_buf_init(&pbuf, buf, sizeof(buf));

    // 如果使用的是 Modbus RTU 协议
    if (stack->protocol == MODBUS_PROTOCOL_RTU) {
        // 对协议数据包进行 CRC 包装
        modbus_crc_wrap(&pbuf);
    }

    // 对协议数据包进行地址包装，包含起始地址、寄存器数量和操作类型
    modbus_address_wrap(&pbuf, start_address, n_reg, m_action);

    // 根据不同的数据区域进行不同的处理
    switch (area) {
    case MODBUS_AREA_COIL:
        // 包装读取线圈的功能码
        modbus_code_wrap(&pbuf, slave_id, MODBUS_READ_COIL);

        // 估算响应中线圈数据的字节数
        *response_size += n_reg / 8 + ((n_reg % 8) > 0 ? 1 : 0);
        break;
    case MODBUS_AREA_INPUT:
        // 包装读取离散输入的功能码
        modbus_code_wrap(&pbuf, slave_id, MODBUS_READ_INPUT);

        // 估算响应中离散输入数据的字节数
        *response_size += n_reg / 8 + ((n_reg % 8) > 0 ? 1 : 0);
        break;
    case MODBUS_AREA_INPUT_REGISTER:
        // 包装读取输入寄存器的功能码
        modbus_code_wrap(&pbuf, slave_id, MODBUS_READ_INPUT_REG);

        // 估算响应中输入寄存器数据的字节数
        *response_size += n_reg * 2;
        break;
    case MODBUS_AREA_HOLD_REGISTER:
        // 包装读取保持寄存器的功能码
        modbus_code_wrap(&pbuf, slave_id, MODBUS_READ_HOLD_REG);

        // 估算响应中保持寄存器数据的字节数
        *response_size += n_reg * 2;
        break;
    }

    // 增加 Modbus 功能码和数据部分的固定字节数
    *response_size += sizeof(struct modbus_code);
    *response_size += sizeof(struct modbus_data);

    // 根据不同的 Modbus 协议进行不同的处理
    switch (stack->protocol) {
    case MODBUS_PROTOCOL_TCP:
        // 包装 Modbus TCP 协议的头部信息，并更新读取序列
        modbus_header_wrap(&pbuf, stack->read_seq++);

        // 增加 Modbus TCP 头部的字节数
        *response_size += sizeof(struct modbus_header);
        break;
    case MODBUS_PROTOCOL_RTU:
        // 设置 Modbus RTU 协议的 CRC 校验值
        modbus_crc_set(&pbuf);

        // 增加 CRC 校验的字节数
        *response_size += 2;
        break;
    }   

    // 调用发送函数发送协议数据包
    ret = stack->send_fn(stack->ctx, neu_protocol_pack_buf_used_size(&pbuf),
                         neu_protocol_pack_buf_get(&pbuf));
    
    // 如果发送失败且不是测试模式
    if (ret <= 0 && !is_test) {
        // 调用值处理函数，标记为连接断开错误
        stack->value_fn(stack->ctx, 0, 0, NULL, NEU_ERR_PLUGIN_DISCONNECTED,
                        NULL);
        plog_warn((neu_plugin_t *) stack->ctx, "send read req fail, %hhu!%hu",
                  slave_id, start_address);
    } else {
        // 如果 OpenTelemetry 数据采集已启动
        if (neu_otel_data_is_started()) {
            // 获取 OpenTelemetry 数据采样率
            double rate        = neu_otel_data_sample_rate();
            
            // 计算采样间隔
            int    sample_rate = 0;
            if (rate > 0.0 && rate <= 1.0) {
                sample_rate = (int) (1.0 / rate);
            }

            // 如果采样间隔不为 0
            if (sample_rate != 0) {
                // 增加采样计数器
                stack->sample_mod += 1;

                // 如果达到采样间隔
                if (stack->sample_mod % sample_rate == 0) {
                    // 生成新的跟踪 ID
                    char new_trace_id[64] = { 0 };
                    neu_otel_new_trace_id(new_trace_id);

                    // 跟踪状态信息
                    const char *trace_state  = "span.mytype=data-collection";

                    // 跟踪上下文和范围上下文指针
                    neu_otel_trace_ctx trace = NULL;
                    neu_otel_scope_ctx scope = NULL;

                    // 创建跟踪上下文
                    trace                    = neu_otel_create_trace(
                        new_trace_id, (void *) (intptr_t) stack->read_seq, 0,
                        trace_state);

                    // 添加跟踪范围
                    scope = neu_otel_add_span(trace);

                    // 设置跟踪范围的名称
                    neu_otel_scope_set_span_name(scope, "driver cmd send");

                    // 生成新的跨度 ID
                    char new_span_id[36] = { 0 };
                    neu_otel_new_span_id(new_span_id);

                    // 设置跟踪范围的跨度 ID
                    neu_otel_scope_set_span_id(scope, new_span_id);

                    // 设置跟踪范围的标志
                    neu_otel_scope_set_span_flags(scope, 0);

                    // 设置跟踪范围的开始时间
                    neu_otel_scope_set_span_start_time(scope, ts_start);

                    // 添加线程 ID 作为跟踪范围的属性
                    neu_otel_scope_add_span_attr_int(scope, "thread id",
                                                     (int64_t) pthread_self());

                    // 添加节点名称作为跟踪范围的属性
                    neu_otel_scope_add_span_attr_string(
                        scope, "node",
                        ((neu_plugin_t *) stack->ctx)->common.name);

                    // 设置跟踪范围的结束时间
                    neu_otel_scope_set_span_end_time(scope, neu_time_ns());
                }
            }
        }
    }
    return ret;
}

int modbus_stack_write(modbus_stack_t *stack, void *req, uint8_t slave_id,
                       enum modbus_area area, uint16_t start_address,
                       uint16_t n_reg, uint8_t *bytes, uint8_t n_byte,
                       uint16_t *response_size, bool response)
{
    static __thread neu_protocol_pack_buf_t pbuf     = { 0 };
    modbus_action_e                         m_action = MODBUS_ACTION_DEFAULT;

    memset(stack->buf, 0, stack->buf_size);
    neu_protocol_pack_buf_init(&pbuf, stack->buf, stack->buf_size);

    if (stack->protocol == MODBUS_PROTOCOL_RTU) {
        modbus_crc_wrap(&pbuf);
    }

    switch (area) {
    case MODBUS_AREA_COIL:
        if (n_byte > 1) {
            n_reg = n_byte;
            modbus_data_wrap(&pbuf, (n_byte + 7) / 8, bytes, m_action);
            modbus_address_wrap(&pbuf, start_address, n_reg, m_action);
            modbus_code_wrap(&pbuf, slave_id, MODBUS_WRITE_M_COIL);
            break;
        } else {
            if (*bytes > 0) {
                modbus_address_wrap(&pbuf, start_address, 0xff00, m_action);
            } else {
                modbus_address_wrap(&pbuf, start_address, 0, m_action);
            }
            modbus_code_wrap(&pbuf, slave_id, MODBUS_WRITE_S_COIL);
            break;
        }
    case MODBUS_AREA_HOLD_REGISTER:
        m_action = MODBUS_ACTION_HOLD_REG_WRITE;
        modbus_data_wrap(&pbuf, n_byte, bytes, m_action);
        modbus_address_wrap(&pbuf, start_address, n_reg, m_action);
        modbus_code_wrap(&pbuf, slave_id,
                         n_reg > 1 ? MODBUS_WRITE_M_HOLD_REG
                                   : MODBUS_WRITE_S_HOLD_REG);
        break;
    default:
        stack->write_resp(stack->ctx, req, NEU_ERR_PLUGIN_TAG_NOT_ALLOW_WRITE);
        break;
    }
    *response_size += sizeof(struct modbus_code);
    *response_size += sizeof(struct modbus_address);

    switch (stack->protocol) {
    case MODBUS_PROTOCOL_TCP:
        modbus_header_wrap(&pbuf, stack->write_seq++);
        *response_size += sizeof(struct modbus_header);
        break;
    case MODBUS_PROTOCOL_RTU:
        modbus_crc_set(&pbuf);
        *response_size += 2;
        break;
    }

    int ret = stack->send_fn(stack->ctx, neu_protocol_pack_buf_used_size(&pbuf),
                             neu_protocol_pack_buf_get(&pbuf));
    if (ret > 0) {
        if (response) {
            stack->write_resp(stack->ctx, req, NEU_ERR_SUCCESS);
            plog_notice((neu_plugin_t *) stack->ctx, "send write req, %hhu!%hu",
                        slave_id, start_address);
        }
    } else {
        if (response) {
            stack->write_resp(stack->ctx, req, NEU_ERR_PLUGIN_DISCONNECTED);
            plog_warn((neu_plugin_t *) stack->ctx,
                      "send write req fail, %hhu!%hu", slave_id, start_address);
        }
    }
    return ret;
}

bool modbus_stack_is_rtu(modbus_stack_t *stack)
{
    return stack->protocol == MODBUS_PROTOCOL_RTU;
}