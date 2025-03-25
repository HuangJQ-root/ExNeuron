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

#ifndef NEURON_MSG_INTERNAL_H
#define NEURON_MSG_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include "msg.h"

/**
 * @brief NEU_REQRESP_TYPE_MAP 宏展开
 * 
 * NEU_REQRESP_TYPE_MAP(XX) 是一个宏调用，NEU_REQRESP_TYPE_MAP 这个宏
 * 会多次调用 XX 宏，每次调用时会传入不同的 type 和 structure 参数。
 */
#define NEU_REQRESP_TYPE_MAP(XX)                                     \
    XX(NEU_RESP_ERROR, neu_resp_error_t)                             \
    XX(NEU_REQ_READ_GROUP, neu_req_read_group_t)                     \
    XX(NEU_RESP_READ_GROUP, neu_resp_read_group_t)                   \
    XX(NEU_REQ_READ_GROUP_PAGINATE, neu_req_read_group_paginate_t)   \
    XX(NEU_RESP_READ_GROUP_PAGINATE, neu_resp_read_group_paginate_t) \
    XX(NEU_REQ_TEST_READ_TAG, neu_req_test_read_tag_t)               \
    XX(NEU_RESP_TEST_READ_TAG, neu_resp_test_read_tag_t)             \
    XX(NEU_REQ_WRITE_TAG, neu_req_write_tag_t)                       \
    XX(NEU_REQ_WRITE_TAGS, neu_req_write_tags_t)                     \
    XX(NEU_REQ_WRITE_GTAGS, neu_req_write_gtags_t)                   \
    XX(NEU_REQ_SUBSCRIBE_GROUP, neu_req_subscribe_t)                 \
    XX(NEU_REQ_UNSUBSCRIBE_GROUP, neu_req_unsubscribe_t)             \
    XX(NEU_REQ_UPDATE_SUBSCRIBE_GROUP, neu_req_subscribe_t)          \
    XX(NEU_REQ_SUBSCRIBE_GROUPS, neu_req_subscribe_groups_t)         \
    XX(NEU_REQ_GET_SUBSCRIBE_GROUP, neu_req_get_subscribe_group_t)   \
    XX(NEU_RESP_GET_SUBSCRIBE_GROUP, neu_resp_get_subscribe_group_t) \
    XX(NEU_REQ_GET_SUB_DRIVER_TAGS, neu_req_get_sub_driver_tags_t)   \
    XX(NEU_RESP_GET_SUB_DRIVER_TAGS, neu_resp_get_sub_driver_tags_t) \
    XX(NEU_REQ_NODE_INIT, neu_req_node_init_t)                       \
    XX(NEU_REQ_NODE_UNINIT, neu_req_node_uninit_t)                   \
    XX(NEU_RESP_NODE_UNINIT, neu_resp_node_uninit_t)                 \
    XX(NEU_REQ_ADD_NODE, neu_req_add_node_t)                         \
    XX(NEU_REQ_UPDATE_NODE, neu_req_update_node_t)                   \
    XX(NEU_REQ_DEL_NODE, neu_req_del_node_t)                         \
    XX(NEU_REQ_GET_NODE, neu_req_get_node_t)                         \
    XX(NEU_RESP_GET_NODE, neu_resp_get_node_t)                       \
    XX(NEU_REQ_NODE_SETTING, neu_req_node_setting_t)                 \
    XX(NEU_REQ_GET_NODE_SETTING, neu_req_get_node_setting_t)         \
    XX(NEU_RESP_GET_NODE_SETTING, neu_resp_get_node_setting_t)       \
    XX(NEU_REQ_GET_NODE_STATE, neu_req_get_node_state_t)             \
    XX(NEU_RESP_GET_NODE_STATE, neu_resp_get_node_state_t)           \
    XX(NEU_REQ_GET_NODES_STATE, neu_req_get_nodes_state_t)           \
    XX(NEU_RESP_GET_NODES_STATE, neu_resp_get_nodes_state_t)         \
    XX(NEU_REQ_NODE_CTL, neu_req_node_ctl_t)                         \
    XX(NEU_REQ_NODE_RENAME, neu_req_node_rename_t)                   \
    XX(NEU_RESP_NODE_RENAME, neu_resp_node_rename_t)                 \
    XX(NEU_REQ_ADD_GROUP, neu_req_add_group_t)                       \
    XX(NEU_REQ_DEL_GROUP, neu_req_del_group_t)                       \
    XX(NEU_REQ_UPDATE_GROUP, neu_req_update_group_t)                 \
    XX(NEU_REQ_UPDATE_DRIVER_GROUP, neu_req_update_group_t)          \
    XX(NEU_RESP_UPDATE_DRIVER_GROUP, neu_resp_update_group_t)        \
    XX(NEU_REQ_GET_GROUP, neu_req_get_group_t)                       \
    XX(NEU_RESP_GET_GROUP, neu_resp_get_group_t)                     \
    XX(NEU_REQ_GET_DRIVER_GROUP, neu_req_get_group_t)                \
    XX(NEU_RESP_GET_DRIVER_GROUP, neu_resp_get_driver_group_t)       \
    XX(NEU_REQ_ADD_TAG, neu_req_add_tag_t)                           \
    XX(NEU_RESP_ADD_TAG, neu_resp_add_tag_t)                         \
    XX(NEU_REQ_ADD_GTAG, neu_req_add_gtag_t)                         \
    XX(NEU_RESP_ADD_GTAG, neu_resp_add_tag_t)                        \
    XX(NEU_REQ_DEL_TAG, neu_req_del_tag_t)                           \
    XX(NEU_REQ_UPDATE_TAG, neu_req_update_tag_t)                     \
    XX(NEU_RESP_UPDATE_TAG, neu_resp_update_tag_t)                   \
    XX(NEU_REQ_GET_TAG, neu_req_get_tag_t)                           \
    XX(NEU_RESP_GET_TAG, neu_resp_get_tag_t)                         \
    XX(NEU_REQ_ADD_PLUGIN, neu_req_add_plugin_t)                     \
    XX(NEU_REQ_DEL_PLUGIN, neu_req_del_plugin_t)                     \
    XX(NEU_REQ_UPDATE_PLUGIN, neu_req_update_plugin_t)               \
    XX(NEU_REQ_GET_PLUGIN, neu_req_get_plugin_t)                     \
    XX(NEU_RESP_GET_PLUGIN, neu_resp_get_plugin_t)                   \
    XX(NEU_REQRESP_TRANS_DATA, neu_reqresp_trans_data_t)             \
    XX(NEU_REQRESP_NODES_STATE, neu_reqresp_nodes_state_t)           \
    XX(NEU_REQRESP_NODE_DELETED, neu_reqresp_node_deleted_t)         \
    XX(NEU_REQ_ADD_DRIVERS, neu_req_driver_array_t)                  \
    XX(NEU_REQ_UPDATE_LOG_LEVEL, neu_req_update_log_level_t)         \
    XX(NEU_REQ_PRGFILE_UPLOAD, neu_req_prgfile_upload_t)             \
    XX(NEU_REQ_PRGFILE_PROCESS, neu_req_prgfile_process_t)           \
    XX(NEU_RESP_PRGFILE_PROCESS, neu_resp_prgfile_process_t)         \
    XX(NEU_REQ_SCAN_TAGS, neu_req_scan_tags_t)                       \
    XX(NEU_RESP_SCAN_TAGS, neu_resp_scan_tags_t)                     \
    XX(NEU_REQ_CHECK_SCHEMA, neu_req_check_schema_t)                 \
    XX(NEU_RESP_CHECK_SCHEMA, neu_resp_check_schema_t)               \
    XX(NEU_REQ_DRIVER_ACTION, neu_req_driver_action_t)               \
    XX(NEU_RESP_DRIVER_ACTION, neu_resp_driver_action_t)

/**
 *   
 * @brief XX 宏的作用
 * #define XX(type, structure) \ case type: \ return sizeof(structure); 
 * 定义了一个宏 XX，它接受两个参数 type 和 structure。这个宏的作用是生成一个 case 语句，
 * 当 switch 语句中的变量值等于 type 时，返回 structure 结构体的大小。
 * 
 * @note
 * -宏定义只是给一段代码片段起了一个别名。在实际编译之前的预编译阶段，
 *  预处理器会将代码中所有宏调用的地方进行文本替换。
 * -"#undef XX": 宏取消定义
 */

static inline size_t neu_reqresp_size(neu_reqresp_type_e t)
{
    switch (t) {
    #define XX(type, structure) \
        case type:              \
            return sizeof(structure);
        NEU_REQRESP_TYPE_MAP(XX)
    #undef XX
        default:
            assert(false);
        }

    return 0;
}

#define XX(type, structure) structure type##_data;
union neu_reqresp_u {
    NEU_REQRESP_TYPE_MAP(XX)
};
#define NEU_REQRESP_MAX_SIZE sizeof(union neu_reqresp_u)
#undef XX

/**
 * @brief 定义消息结构体，包含消息头和柔性数组存储消息体
 */
struct neu_msg_s {
    /** 消息请求响应头 */
    neu_reqresp_head_t head;

    /** 
     * @brief 柔性数组，用于存储消息体数据
     * @note 该柔性数组本身不占用结构体大小，可动态分配内存存储不同长度的数据。
     *       在数据复制时，会将其他结构体的数据按字节复制到该柔性数组对应的内存区域；
     *       在数据解释时，需通过强制类型转换将其解释为实际存储的数据类型。
     * 
     * @warning
     * NOTE: we keep the data layout intact only to save refactor efforts.
     * FIXME: potential alignment problem here.
     */
    neu_reqresp_head_t body[];
};

typedef struct neu_msg_s neu_msg_t;

/**
 * @brief 创建一个新的消息对象。
 *
 * 此函数用于创建一个新的消息对象，并根据请求/响应类型分配足够的内存空间以容纳消息体数据。
 * 它首先计算所需的消息体大小，然后分配内存并初始化消息头和消息体。如果在操作过程中遇到任何错误
 * （如内存分配失败），则返回 `NULL`。
 *
 * @param t 请求/响应类型。
 * @param ctx 上下文信息，可以是任意指针，通常用于关联请求与响应。
 * @param data 消息体数据，其具体格式取决于请求/响应类型。
 * @return 成功时返回指向新创建的消息对象的指针；如果发生错误（如内存分配失败），则返回 `NULL`。
 */
static inline neu_msg_t *neu_msg_new(neu_reqresp_type_e t, void *ctx,
                                     void *data)
{
    //获取消息对象字节大小
    size_t data_size = neu_reqresp_size(t);

    //存储消息体的大小，初始化为 0
    size_t body_size = 0;
    
    //根据消息类型 t 来计算消息体（body）的大小。
    switch (t) {
    case NEU_REQ_CHECK_SCHEMA:
        body_size = neu_reqresp_size(NEU_RESP_CHECK_SCHEMA);
        break;
    case NEU_REQ_GET_PLUGIN:
        body_size = neu_reqresp_size(NEU_RESP_GET_PLUGIN);
        break;
    case NEU_REQ_UPDATE_GROUP:
    case NEU_REQ_UPDATE_DRIVER_GROUP:
        body_size = neu_reqresp_size(NEU_RESP_UPDATE_DRIVER_GROUP);
        break;
    case NEU_REQ_UPDATE_NODE:
    case NEU_REQ_NODE_RENAME:
        body_size = neu_reqresp_size(NEU_RESP_NODE_RENAME);
        break;
    case NEU_REQ_DEL_NODE:
        body_size = neu_reqresp_size(NEU_RESP_NODE_UNINIT);
        break;
    case NEU_REQ_GET_NODE_SETTING:
        body_size = neu_reqresp_size(NEU_RESP_GET_NODE_SETTING);
        break;
    case NEU_REQ_GET_NODES_STATE:
        body_size = neu_reqresp_size(NEU_RESP_GET_NODES_STATE);
        break;
    default:
        body_size = data_size;
    }

    //计算消息对象的总大小: sizeof(neu_msg_t)只计算柔性输入之前的大小即neu_msg_s
    size_t     total = sizeof(neu_msg_t) + body_size;

    neu_msg_t *msg   = calloc(1, total);
    if (msg) {
        msg->head.type = t;                      // 设置为传入的消息类型
        msg->head.len  = total;                  // 设置为消息对象的总大小
        msg->head.ctx  = ctx;                    // 设置为传入的上下文指针
        if (data) {                              // 传入的数据指针 data 不为空
            memcpy(msg->body, data, data_size);
        }
    }
    return msg;
}

static inline neu_msg_t *neu_msg_copy(const neu_msg_t *other)
{
    neu_msg_t *msg = calloc(1, other->head.len);
    if (msg) {
        memcpy(msg, other, other->head.len);
    }
    return msg;
}

/**
 * @brief 释放消息对象占用的内存。
 *
 * 此函数用于释放由 `neu_msg_new` 或其他类似函数分配的消息对象所占用的内存。
 * 它首先检查传入的消息指针是否非空，然后调用 `free` 函数释放该消息对象。
 *
 * @param msg 指向 `neu_msg_t` 结构体的指针，表示要释放的消息对象。
 *            可以为 `NULL`，此时函数不做任何操作。
 */
static inline void neu_msg_free(neu_msg_t *msg)
{
    if (msg) {
        free(msg);
    }
}

static inline size_t neu_msg_size(neu_msg_t *msg)
{
    return msg->head.len;
}

static inline size_t neu_msg_body_size(neu_msg_t *msg)
{
    return msg->head.len - sizeof(msg->head);
}

static inline void *neu_msg_get_header(neu_msg_t *msg)
{
    return &msg->head;
}

static inline void *neu_msg_get_body(neu_msg_t *msg)
{
    return &msg->body;
}

/**
 * @brief 通过文件描述符发送消息。
 *
 * 此函数用于通过指定的文件描述符发送一个 `neu_msg_t` 类型的消息。它将消息指针本身（而不是消息的内容）
 * 发送出去，并检查发送的字节数是否与预期相符。如果发送成功且发送的字节数与消息指针的大小相匹配，
 * 则返回 0 表示成功；否则返回实际发送的字节数作为错误码。
 *
 * @warning 
 * 
 * 此实现仅发送消息指针的值，而不是消息的实际内容。这通常不是期望的行为，因为接收方无法直接使用这个
 * 指针值（除非在特定共享内存环境下）。正常情况下，应该发送消息的实际内容而非指针。
 *
 * @param fd 文件描述符，用于指定发送的目的地。
 * @param msg 指向 `neu_msg_t` 结构体的指针，表示要发送的消息对象。
 * @return 成功时返回 0；如果发送的字节数与预期不符，则返回实际发送的字节数作为错误码。
 * 
 * @note  
 * -ret 表示实际发送的字节数；发送失败则为-1
 * -使用inline：函数体短小，频繁调用，无递归，无复杂控制流
 */
inline static int neu_send_msg(int fd, neu_msg_t *msg)
{
    int ret = send(fd, &msg, sizeof(neu_msg_t *), 0);
    return sizeof(neu_msg_t *) == ret ? 0 : ret;
}

/**
 * @brief 从指定的文件描述符接收一条消息。
 *
 * 此函数用于从给定的文件描述符接收一条消息，并将其指针存储在 `msg_p` 参数指向的位置。
 * 它通过调用 `recv` 函数读取数据，并检查是否成功接收到预期大小的数据
 *（即一个 `neu_msg_t*` 指针的大小）。如果接收到的数据大小不匹配或 `recv` 返回 0（表示连接关闭），
 * 则函数将返回错误码；否则，将接收到的消息指针赋值给 `msg_p` 并返回 0 表示成功。
 *
 * @param fd 文件描述符，用于接收数据。
 * @param msg_p 双重指针，用于存储接收到的消息指针。
 * @return 成功时返回 0；如果接收到的数据大小不匹配，则返回 -1 或实际接收到的字节数。
 * 
 * @note 
 * -函数参数传递采用的是值传递方式,为了在函数内部修改调用者的变量，可以传递该变量的地址（指针）。
 * -消息指针本身是一个指针变量，为了能够在函数内部修改这个指针变量的值，需要传递该指针变量的地址
 */
inline static int neu_recv_msg(int fd, neu_msg_t **msg_p)
{
    neu_msg_t *msg = NULL;
    int        ret = recv(fd, &msg, sizeof(neu_msg_t *), 0);
    if (sizeof(neu_msg_t *) != ret) {
        //如果 ret 为 0，表示对方已经关闭了连接，返回 -1 表示错误
        return 0 == ret ? -1 : ret;
    }
    *msg_p = msg;
    return 0;
}

/**
 * @brief 通过指定的 Unix 域套接字地址发送消息。
 *
 * 此函数用于通过指定的文件描述符和 Unix 域套接字地址发送一个 `neu_msg_t` 类型的消息。
 * 它将消息指针本身（而不是消息的内容）发送出去，并检查发送的字节数是否与预期相符。
 * 如果发送成功且发送的字节数与消息指针的大小相匹配，则返回 0 表示成功；否则返回实际发送的字节数作为错误码。
 *
 * @warning
 * 
 * 此实现仅发送消息指针的值，而不是消息的实际内容。这通常不是期望的行为，
 * 因为接收方无法直接使用这个指针值（除非在特定共享内存环境下）。正常情况下，应该发送消息的实际内容而非指针。
 *
 * @param fd   文件描述符，用于指定发送的目的地。
 * @param addr 指向 `sockaddr_un` 结构体的指针，表示目标地址信息。
 * @param msg  指向 `neu_msg_t` 结构体的指针，表示要发送的消息对象。
 * @return 成功时返回 0；如果发送的字节数与预期不符，则返回实际发送的字节数作为错误码。
 */
inline static int neu_send_msg_to(int fd, struct sockaddr_un *addr,
                                  neu_msg_t *msg)
{
    int ret = sendto(fd, &msg, sizeof(neu_msg_t *), 0, (struct sockaddr *) addr,
                     sizeof(*addr));
    return sizeof(neu_msg_t *) == ret ? 0 : ret;
}

inline static int neu_recv_msg_from(int fd, struct sockaddr_un *addr,
                                    neu_msg_t **msg_p)
{
    neu_msg_t *msg      = NULL;
    socklen_t  addr_len = sizeof(struct sockaddr_un);
    int        ret      = recvfrom(fd, &msg, sizeof(neu_msg_t *), 0,
                       (struct sockaddr *) addr, &addr_len);
    if (sizeof(neu_msg_t *) != ret) {
        // recvfrom may return 0 bytes
        return 0 == ret ? -1 : ret;
    }
    *msg_p = msg;
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif
