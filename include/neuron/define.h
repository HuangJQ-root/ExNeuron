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

#ifndef _NEU_DEFINE_H_
#define _NEU_DEFINE_H_

#include <stdbool.h>

#define NEU_VERSION_MAJOR 2
#define NEU_VERSION_MINOR 11
#define NEU_VERSION_FIX 0

#define NEU_GET_VERSION_MAJOR(V) ((0xffff0000 & V) >> 16)
#define NEU_GET_VERSION_MINOR(V) ((0x0000ff00 & V) >> 8)
#define NEU_GET_VERSION_FIX(V) ((0x000000ff & V))
#define NEU_VERSION(MAJOR, MINOR, FIX) ((MAJOR << 16) | (MINOR << 8) | FIX)

#define NEU_TAG_NAME_LEN 128
#define NEU_TAG_ADDRESS_LEN 128
#define NEU_TAG_DESCRIPTION_LEN 128
#define NEU_GROUP_MAX_PER_NODE 512
#define NEU_GROUP_NAME_LEN 128
#define NEU_GROUP_INTERVAL_LIMIT 100
#define NEU_DEFAULT_GROUP_INTERVAL 100
#define NEU_NODE_NAME_LEN 128
#define NEU_PLUGIN_NAME_LEN 32
#define NEU_PLUGIN_LIBRARY_LEN 64
#define NEU_PLUGIN_DESCRIPTION_LEN 512
#define NEU_TEMPLATE_NAME_LEN 128
#define NEU_DRIVER_TAG_CACHE_EXPIRE_TIME 60
#define NEU_APP_SUBSCRIBE_MSG_SIZE 4
#define NEU_TAG_FLOAG_PRECISION_MAX 17
#define NEU_USER_PASSWORD_MIN_LEN 4
#define NEU_USER_PASSWORD_MAX_LEN 16
#define NEU_USER_NAME_MIN_LEN 4
#define NEU_USER_NAME_MAX_LEN 32
#define NEU_LOG_LEVEL_LEN 9
#define NEU_FILE_PATH_LEN 128
#define NEU_MSG_MAX_SIZE 2048

#define NEU_TAG_META_LENGTH 20
#define NEU_TAG_META_SIZE 32
#define NEU_TAG_FORMAT_LENGTH 16

#define NEU_LOG_LEVEL_DEBUG "debug"
#define NEU_LOG_LEVEL_INFO "info"
#define NEU_LOG_LEVEL_NOTICE "notice"
#define NEU_LOG_LEVEL_WARN "warn"
#define NEU_LOG_LEVEL_ERROR "error"
#define NEU_LOG_LEVEL_FATAL "fatal"

#define NEU_CID_LNCLASS_LEN 12
#define NEU_CID_LNTYPE_LEN 32
#define NEU_CID_LNPREFIX_LEN 10
#define NEU_CID_LNINST_LEN 4
#define NEU_CID_LDEVICE_LEN 32
#define NEU_CID_LNO_NAME_LEN 32
#define NEU_CID_LNO_TYPE_LEN 32
#define NEU_CID_INST_LEN 16
#define NEU_CID_SDI_NAME_LEN 16
#define NEU_CID_DO_NAME_LEN 32
#define NEU_CID_FCDA_NAME_LEN 32
#define NEU_CID_FCDA_CLASS_LEN 16
#define NEU_CID_FCDA_INST_LEN 4
#define NEU_CID_FCDA_PREFIX_LEN 16
#define NEU_CID_DO_ID_LEN 32
#define NEU_CID_DA_NAME_LEN 32
#define NEU_CID_DATASET_NAME_LEN 32
#define NEU_CID_REPORT_NAME_LEN 32
#define NEU_CID_REPORT_ID_LEN 64
#define NEU_CID_ID_LEN 32
#define NEU_CID_REF_TYPE_LEN 32
#define NEU_CID_IED_NAME_LEN 32
#define NEU_CID_ACCESS_POINT_NAME_LEN 16

#define NEU_CID_LEN4 4
#define NEU_CID_LEN8 8
#define NEU_CID_LEN16 16
#define NEU_CID_LEN32 32
#define NEU_CID_LEN64 64

#define CHECK_NODE_NAME_LENGTH_ERR                                            \
    do {                                                                      \
        NEU_JSON_RESPONSE_ERROR(NEU_ERR_NODE_NAME_TOO_LONG, {                 \
            neu_http_response(aio, NEU_ERR_NODE_NAME_TOO_LONG, result_error); \
        });                                                                   \
    } while (0)

#define CHECK_GROUP_NAME_LENGTH_ERR                                            \
    do {                                                                       \
        NEU_JSON_RESPONSE_ERROR(NEU_ERR_GROUP_NAME_TOO_LONG, {                 \
            neu_http_response(aio, NEU_ERR_GROUP_NAME_TOO_LONG, result_error); \
        });                                                                    \
    } while (0)

#define CHECK_TAG_NAME_LENGTH_ERR                                            \
    do {                                                                     \
        NEU_JSON_RESPONSE_ERROR(NEU_ERR_TAG_NAME_TOO_LONG, {                 \
            neu_http_response(aio, NEU_ERR_TAG_NAME_TOO_LONG, result_error); \
        });                                                                  \
    } while (0)

#define CHECK_GROUP_INTERVAL_ERR                                    \
    do {                                                            \
        NEU_JSON_RESPONSE_ERROR(NEU_ERR_GROUP_PARAMETER_INVALID, {  \
            neu_http_response(aio, NEU_ERR_GROUP_PARAMETER_INVALID, \
                              result_error);                        \
        });                                                         \
    } while (0)

extern int  default_log_level;
extern bool disable_jwt;
extern char host_port[32];
extern char g_status[32];

/**
 * @brief neu_plugin_kind_e 枚举用于标识插件的种类。
 *
 * 插件种类用于区分插件在系统中的角色和功能，例如静态插件、系统插件和自定义插件。
 */
typedef enum neu_plugin_kind {
    /**
     * @brief 静态插件。
     *
     * 这种插件通常是系统核心的一部分，无法动态加载或卸载。
     * 它们在系统启动时自动加载，并且在整个系统运行期间一直存在。
     */
    NEU_PLUGIN_KIND_STATIC = 0,

    /**
     * @brief 系统插件。
     *
     * 这种插件是系统提供的标准插件，通常用于提供基础功能和服务。
     * 它们可以被动态加载和卸载，但通常由系统管理员配置和管理。
     */
    NEU_PLUGIN_KIND_SYSTEM = 1,

    /**
     * @brief 自定义插件。
     *
     * 这种插件是由用户或第三方开发者创建的，用于扩展系统的功能。
     * 它们可以被动态加载和卸载，并且可以根据需要进行定制和扩展。
     */
    NEU_PLUGIN_KIND_CUSTOM = 2,
} neu_plugin_kind_e;

/**
 * @brief neu_adapter_type_e 和 neu_node_type_e 枚举用于标识适配器的类型。
 *
 * 适配器类型/节点类型 用于区分适配器在系统中的角色和功能，例如驱动程序适配器和应用程序适配器。
 */
typedef enum {
    /**
     * @brief 驱动程序适配器。
     *
     * 这种适配器/节点 通常用于与硬件设备进行交互，负责数据的采集和控制命令的发送。
     */
    NEU_NA_TYPE_DRIVER = 1,

    /**
     * @brief 应用程序适配器。
     *
     * 这种适配器/节点 通常用于与其他软件系统或服务进行交互，负责数据处理和业务逻辑的实现。
     */
    NEU_NA_TYPE_APP = 2,
} neu_adapter_type_e, neu_node_type_e;

typedef enum {
    NEU_NODE_LINK_STATE_DISCONNECTED = 0,
    NEU_NODE_LINK_STATE_CONNECTED    = 1,
} neu_node_link_state_e;

typedef enum {
    NEU_NODE_RUNNING_STATE_INIT    = 1,
    NEU_NODE_RUNNING_STATE_READY   = 2,
    NEU_NODE_RUNNING_STATE_RUNNING = 3,
    NEU_NODE_RUNNING_STATE_STOPPED = 4,
} neu_node_running_state_e;

/**
 * @brief 定义标签缓存的类型。
 *
 * 此枚举类型描述了不同的缓存策略，可以用来控制数据标签在缓存中的更新方式。
 * 例如，是否基于时间间隔更新缓存，或者根本不更新缓存等。
 */
typedef enum {
    /**
     * @brief 基于时间间隔更新缓存。
     *
     * 当设置为这个值时，意味着缓存将根据设定的时间间隔进行更新。
     */
    NEU_TAG_CACHE_TYPE_INTERVAL = 0,

    /**
     * @brief 从不更新缓存。
     *
     *获取数据都需要直接从数据源获取最新的数据，而不使用缓存中的旧数据
     *适合数据实时性要求极高的场景
     */
    NEU_TAG_CACHE_TYPE_NEVER,
} neu_tag_cache_type_e;

// forward declaration for neu_adapter_t
typedef struct neu_adapter neu_adapter_t;

#endif
