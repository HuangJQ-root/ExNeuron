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

#ifndef _NEU_HTTP_HANDLER_H_
#define _NEU_HTTP_HANDLER_H_

#include <stdlib.h>

#include <nng/nng.h>
#include <nng/supplemental/http/http.h>

#include "utils/neu_jwt.h"
#include "json/neu_json_error.h"

#define NEU_PROCESS_HTTP_REQUEST(aio, req_type, decode_fun, func)            \
    {                                                                        \
        char *    req_data      = NULL;                                      \
        size_t    req_data_size = 0;                                         \
        req_type *req           = NULL;                                      \
                                                                             \
        if (neu_http_get_body((aio), (void **) &req_data, &req_data_size) == \
                0 &&                                                         \
            decode_fun(req_data, &req) == 0) {                               \
            { func };                                                        \
            decode_fun##_free(req);                                          \
        } else {                                                             \
            neu_http_bad_request(aio, "{\"error\": 1002}");                  \
        }                                                                    \
        free(req_data);                                                      \
    }

#define NEU_PROCESS_HTTP_REQUEST_VALIDATE_JWT(aio, req_type, decode_fun, func) \
    {                                                                          \
        if (!disable_jwt) {                                                    \
            char *jwt =                                                        \
                (char *) neu_http_get_header(aio, (char *) "Authorization");   \
                                                                               \
            NEU_JSON_RESPONSE_ERROR(neu_jwt_validate(jwt), {                   \
                if (error_code.error != NEU_ERR_SUCCESS) {                     \
                    neu_http_response(aio, error_code.error, result_error);    \
                    free(result_error);                                        \
                    return;                                                    \
                }                                                              \
            })                                                                 \
        }                                                                      \
        NEU_PROCESS_HTTP_REQUEST(aio, req_type, decode_fun, func);             \
    }

#define NEU_VALIDATE_JWT(aio)                                                \
    {                                                                        \
        if (!disable_jwt) {                                                  \
            char *jwt =                                                      \
                (char *) neu_http_get_header(aio, (char *) "Authorization"); \
                                                                             \
            NEU_JSON_RESPONSE_ERROR(neu_jwt_validate(jwt), {                 \
                if (error_code.error != NEU_ERR_SUCCESS) {                   \
                    neu_http_response(aio, error_code.error, result_error);  \
                    free(result_error);                                      \
                    return;                                                  \
                }                                                            \
            });                                                              \
        }                                                                    \
    }

#define NEU_VALIDATE_JWT_WITH_USER(aio, user)                                \
    {                                                                        \
        if (!disable_jwt) {                                                  \
            char *jwt =                                                      \
                (char *) neu_http_get_header(aio, (char *) "Authorization"); \
                                                                             \
            NEU_JSON_RESPONSE_ERROR(neu_jwt_validate(jwt), {                 \
                if (error_code.error != NEU_ERR_SUCCESS) {                   \
                    neu_http_response(aio, error_code.error, result_error);  \
                    free(result_error);                                      \
                    return;                                                  \
                } else {                                                     \
                    neu_jwt_decode_user_after_valid(jwt, user);              \
                }                                                            \
            });                                                              \
        }                                                                    \
    }

enum neu_http_method {
    NEU_HTTP_METHOD_UNDEFINE = 0x0,
    NEU_HTTP_METHOD_GET,
    NEU_HTTP_METHOD_POST,
    NEU_HTTP_METHOD_PUT,
    NEU_HTTP_METHOD_DELETE,
    NEU_HTTP_METHOD_OPTIONS
};

/**
 * @brief HTTP处理器类型枚举，定义了不同的HTTP处理器类型。
 *
 * 此枚举用于指定HTTP处理器的具体类型，包括直接处理函数、目录服务以及重定向等。
 */
enum neu_http_handler_type {
    /**
     * @brief 直接处理函数类型。
     *
     * 表示HTTP请求将由一个具体的处理函数来处理。通常用于实现自定义的业务逻辑，
     * 如处理API请求、动态生成内容等。
     */
    NEU_HTTP_HANDLER_FUNCTION = 0x0,

    /**
     * @brief 目录服务类型。
     *
     * 表示HTTP请求将被映射到服务器上的某个目录，并从该目录中提供静态文件服务。
     * 适用于提供HTML页面、CSS样式表、JavaScript脚本等静态资源。
     */
    NEU_HTTP_HANDLER_DIRECTORY,

    /**
     * @brief 重定向类型。
     *
     * 表示HTTP请求将被重定向到另一个URL。常用于URL规范化、临时移动资源或永久改变资源位置等情况。
     */
    NEU_HTTP_HANDLER_REDIRECT,
};

/**
 * @brief HTTP处理器结构体，用于定义处理HTTP请求的方法、URL、类型及其实现。
 *
 * 此结构体描述了一个HTTP处理器的所有必要信息，包括请求方法（GET、POST等）、请求的URL路径、处理器类型以及具体的处理器实现或相关信息。
 */
struct neu_http_handler {
    /**
     * @brief HTTP请求方法。
     *
     * 使用枚举类型`neu_http_method`定义，表示HTTP请求的方法，如GET、POST、PUT、DELETE等。
     */
    enum neu_http_method       method;

    /**
     * @brief 请求的URL路径。
     *
     * 字符串指针，指向具体的URL路径，用于匹配特定的HTTP请求。
     */
    char                      *url;

    /**
     * @brief HTTP处理器类型。
     *
     * 使用枚举类型`neu_http_handler_type`定义，指示处理器的具体类型。
     */
    enum neu_http_handler_type type;

    /**
     * @brief 处理器值联合体。
     *
     * 根据处理器类型的不同，此联合体可以存储不同类型的值：
     */
    union neu_http_handler_value {
        /**
         * @brief 处理器函数指针。
         *
         * 当处理器类型为直接处理器时使用，指向一个函数，该函数负责处理对应的HTTP请求。
         */
        void *handler;

        /**
         * @brief 文件路径。
         *
         * 当处理器类型为目录服务时使用，指向一个字符串，表示要提供给客户端的文件所在的目录路径。
         */
        char *path;

        /**
         * @brief 目标URL。
         *
         * 当处理器类型为重定向处理器时使用，指向一个字符串，表示重定向的目标URL。
         */
        char *dst_url;
    } value;
};

int  neu_http_add_handler(nng_http_server *              server,
                          const struct neu_http_handler *http_handler);
void neu_http_handle_cors(nng_aio *aio);

#endif
