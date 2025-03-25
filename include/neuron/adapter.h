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

#ifndef _NEU_ADAPTER_H_
#define _NEU_ADAPTER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include <sys/un.h>

#include "define.h"
#include "metrics.h"
#include "msg.h"

typedef struct neu_adapter_driver neu_adapter_driver_t;
typedef struct neu_adapter_app    neu_adapter_app_t;

typedef int (*neu_adapter_update_metric_cb_t)(neu_adapter_t *adapter,
                                              const char *   metric_name,
                                              uint64_t n, const char *group);

/**
 * @brief 适配器注册指标的回调函数类型。
 *
 * 此回调函数用于在适配器中注册性能指标（如计数器、仪表等），以便进行监控和分析。
 * 注册后的指标可以通过监控系统获取并展示，帮助用户了解适配器的运行状态和性能。
 *
 * @param adapter 指向neu_adapter_t类型的指针，表示当前适配器实例。
 * @param name 指标名称，一个字符串，用于标识该指标。
 * @param help 指标描述，一个字符串，提供关于该指标用途或意义的简短说明。
 * @param type 指标类型，使用枚举类型neu_metric_type_e定义，如计数器、仪表等。
 * @param init 初始值，一个无符号64位整数，表示指标的初始值。
 * @return 返回状态码，0表示成功，非0表示失败。
 */
typedef int (*neu_adapter_register_metric_cb_t)(neu_adapter_t *   adapter,
                                                const char *      name,
                                                const char *      help,
                                                neu_metric_type_e type,
                                                uint64_t          init);

/**
 * @brief 适配器回调函数集合结构体，用于定义适配器与外部系统交互时所需的各种回调函数。
 *
 * 此结构体包含了处理命令、响应、发送响应、注册和更新指标等操作的回调函数，以及一个联合体，用于处理特定类型的驱动程序回调。
 */
typedef struct adapter_callbacks {
    /**
     * @brief 处理命令请求的回调函数。
     *
     * 当收到命令请求时调用此函数，允许适配器执行相应的命令逻辑。
     *
     * @param adapter 指向neu_adapter_t类型的指针，表示当前适配器实例。
     * @param head    请求响应头信息。
     * @param data    命令数据。
     * 
     * @return 返回状态码，0表示成功，非0表示失败。
     */
    int (*command)(neu_adapter_t *adapter, neu_reqresp_head_t head, void *data);

    /**
     * @brief 处理响应的回调函数。
     *
     * 当需要发送响应时调用此函数，允许适配器构造并发送响应消息。
     *
     * @param adapter 指向neu_adapter_t类型的指针，表示当前适配器实例。
     * @param head    请求响应头信息。
     * @param data    响应数据。
     * 
     * @return 返回状态码，0表示成功，非0表示失败。
     */
    int (*response)(neu_adapter_t *adapter, neu_reqresp_head_t *head,
                    void *data);

    /**
     * @brief 发送响应到指定目标的回调函数。
     *
     * 当需要将响应发送到特定的目标地址时调用此函数。
     *
     * @param adapter 指向neu_adapter_t类型的指针，表示当前适配器实例。
     * @param head 请求响应头信息。
     * @param data 响应数据。
     * @param dst 目标地址。
     * @return 返回状态码，0表示成功，非0表示失败。
     */
    int (*responseto)(neu_adapter_t *adapter, neu_reqresp_head_t *head,
                      void *data, struct sockaddr_un dst);

    /**
     * @brief 注册指标的回调函数。
     *
     * 允许适配器注册其性能指标，以便进行监控和分析。
     */
    neu_adapter_register_metric_cb_t register_metric;

    /**
     * @brief 更新指标的回调函数。
     *
     * 允许适配器更新其性能指标。
     */
    neu_adapter_update_metric_cb_t   update_metric;

    union {
        struct {
            /**
             * @brief 更新数据值的回调函数。
             *
             * 当需要更新某个标签的数据值时调用此函数。
             *
             * @param adapter 指向neu_adapter_t类型的指针，表示当前适配器实例。
             * @param group   组名称。
             * @param tag     标签名称。
             * @param value   新的数据值。
             */
            void (*update)(neu_adapter_t *adapter, const char *group,
                           const char *tag, neu_dvalue_t value);

            /**
             * @brief 带追踪上下文更新数据值的回调函数。
             *
             * 当需要更新某个标签的数据值，并提供追踪上下文时调用此函数。
             *
             * @param adapter   指向neu_adapter_t类型的指针，表示当前适配器实例。
             * @param group     组名称。
             * @param tag       标签名称。
             * @param value     新的数据值。
             * @param metas     标签元数据数组:为数据点提供额外的元数据。
             * @param n_meta    元数据的数量。
             * @param trace_ctx 追踪上下文。
             */
            void (*update_with_trace)(neu_adapter_t *adapter, const char *group,
                                      const char *tag, neu_dvalue_t value,
                                      neu_tag_meta_t *metas, int n_meta,
                                      void *trace_ctx);

            /**
             * @brief 带元数据更新数据值的回调函数。
             *
             * 当需要更新某个标签的数据值，并提供元数据时调用此函数。
             *
             * @param adapter 指向neu_adapter_t类型的指针，表示当前适配器实例。
             * @param group   组名称。
             * @param tag     标签名称。
             * @param value   新的数据值。
             * @param metas   标签元数据数组。
             * @param n_meta  元数据的数量。
             */
            void (*update_with_meta)(neu_adapter_t *adapter, const char *group,
                                     const char *tag, neu_dvalue_t value,
                                     neu_tag_meta_t *metas, int n_meta);

            /**
             * @brief 写入响应的回调函数。
             *
             * 当写入操作完成时调用此函数，返回写入结果。
             *
             * @param adapter 指向neu_adapter_t类型的指针，表示当前适配器实例。
             * @param req     请求对象。
             * @param error   错误代码，0表示成功，非0表示失败。
             */
            void (*write_response)(neu_adapter_t *adapter, void *req,
                                   int error);

            /**
             * @brief 立即更新数据值的回调函数。
             *
             * 当需要立即更新某个标签的数据值，并提供元数据时调用此函数。
             *
             * @param adapter 指向neu_adapter_t类型的指针，表示当前适配器实例。
             * @param group   组名称。
             * @param tag     标签名称。
             * @param value   新的数据值。
             * @param metas   标签元数据数组。
             * @param n_meta  元数据的数量。
             */
            void (*update_im)(neu_adapter_t *adapter, const char *group,
                              const char *tag, neu_dvalue_t value,
                              neu_tag_meta_t *metas, int n_meta);
      
            /**
             * @brief 扫描标签响应的回调函数。
             *
             * 当扫描标签操作完成时调用此函数，返回扫描结果。
             *
             * @param adapter   指向neu_adapter_t类型的指针，表示当前适配器实例。
             * @param r         请求对象。
             * @param resp_scan 扫描结果。
             */
            void (*scan_tags_response)(neu_adapter_t *adapter, void *r,
                                       neu_resp_scan_tags_t *resp_scan);

            /**
             * @brief 测试读取标签响应的回调函数。
             *
             * 当测试读取标签操作完成时调用此函数，返回读取结果。
             *
             * @param adapter 指向neu_adapter_t类型的指针，表示当前适配器实例。
             * @param r       请求对象。
             * @param t       数据类型。
             * @param type    标签类型。
             * @param value   读取到的值。
             * @param error   错误代码，0表示成功，非0表示失败。
             */
            void (*test_read_tag_response)(neu_adapter_t *adapter, void *r,
                                           neu_json_type_e t, neu_type_e type,
                                           neu_json_value_u value,
                                           int64_t          error);
        } driver;
    };
} adapter_callbacks_t;

#ifdef __cplusplus
}
#endif

#endif