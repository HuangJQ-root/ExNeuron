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

#include <stdlib.h>
#include <string.h>

#include "errcodes.h"

#include "adapter/adapter_internal.h"
#include "node_manager.h"

/**
 * @brief 一个通用的节点实体的信息。
 *
 *定义节点的基本属性和通用行为，例如节点的名称、类型、ID 等标识信息，
 *以及一些与节点本身关联的通用配置。它更侧重于描述节点的 "静态特征" 
 *和基本的抽象概念，是一个相对宽泛和基础的节点定义。
 */
typedef struct node_entity {
    /**
     * @brief 节点实体的名字。
     */
    char *name;

    /**
     * @brief 指向适配器结构体，其用于描述一个适配器的基础信息及其相关资源。。
     *
     * 这个字段代表了与节点实体相关的某种适配器
     */
    neu_adapter_t *    adapter;

    /**
     * @brief 标识该节点实体是否是静态的。
     *
     * 如果设置为true，则表示此节点实体是静态的。
     */
    bool               is_static;

    /**
     * @brief 控制是否显示此节点实体的信息。
     *
     * 如果设置为true，则表示应当显示此节点实体的信息。
     */
    bool               display;

    /**
     * @brief 标识该节点实体是否代表单一实例或具有唯一性等属性。
     *
     * 如果设置为true，则表示此节点实体代表单一实例。
     */
    bool               single;
    
    /**
     * @brief 标识该节点实体是否是监控节点。
     *
     */
    bool               is_monitor;

    /**
     * @brief Unix域套接字地址结构体。用于本地进程间的高效通信
     *
     * 包含了一个本地通信端点的地址信息，侧重于节点内部的管理和协调
     * ，是节点的一种基础通信能力。
     */
    struct sockaddr_un addr;

    /**
     * @brief UTHash句柄，支持将该结构体作为哈希表中的元素进行高效管理。
     *
     * 按照name有序排列
     */
    UT_hash_handle hh;
} node_entity_t;

/**
 * @brief 节点管理器结构体，用于管理系统中的所有节点实体。
 *
 * 该结构体作为节点管理的核心组件，负责维护和操作系统内的节点信息。
 * 通指向一个由 `node_entity_t` 结构体实例组成的集合，这些实例
 * 代表了系统中的各个节点。可以方便地对节点集合进行遍历、查找特定
 * 节点、添加新节点或移除现有节点等操作
 */
struct neu_node_manager {
    node_entity_t *nodes;
    UT_array *     monitors;
};

neu_node_manager_t *neu_node_manager_create()
{
    neu_node_manager_t *node_manager = calloc(1, sizeof(neu_node_manager_t));

    if (node_manager) {
        utarray_new(node_manager->monitors, &ut_ptr_icd);
    }

    return node_manager;
}

void neu_node_manager_destroy(neu_node_manager_t *mgr)
{
    node_entity_t *el = NULL, *tmp = NULL;

    HASH_ITER(hh, mgr->nodes, el, tmp)
    {
        HASH_DEL(mgr->nodes, el);
        free(el->name);
        free(el);
    }

    utarray_free(mgr->monitors);
    free(mgr);
}

/**
 * @brief 向节点管理器中添加一个新的节点实体。
 *
 * 该函数的主要作用是将一个适配器包装成节点实体，并添加到节点管理器的节点列表中。
 * 它会分配内存来创建一个新的节点实体，设置节点实体的相关属性，然后使用哈希表将
 * 其添加到节点管理器中。
 *
 * @param mgr 指向节点管理器的指针，用于管理所有的节点实体。
 * @param adapter 指向适配器的指针，该适配器将被包装成节点实体添加到节点管理器中。
 *
 * @warning
 * 
 * 总是返回 0 表示操作成功，目前未考虑错误情况。在实际应用中，可能需要添加对内存分配
 * 失败等情况的错误处理。
 */
int neu_node_manager_add(neu_node_manager_t *mgr, neu_adapter_t *adapter)
{
    node_entity_t *node = calloc(1, sizeof(node_entity_t));

    node->adapter = adapter;
    node->name    = strdup(adapter->name);
    node->display = true;
    node->is_monitor =
        (0 == strcmp(node->adapter->module->module_name, "Monitor"));

    HASH_ADD_STR(mgr->nodes, name, node);

    if (node->is_monitor) {
        utarray_push_back(mgr->monitors, &node);
    }

    return 0;
}

/**
 * @brief 向节点管理器中添加一个静态适配器节点。
 *
 * 此函数用于将一个静态的 neu_adapter_t 类型的适配器节点添加到 neu_node_manager_t 类型的管理器中。
 * 它会为节点分配内存，初始化节点信息，并将节点添加到管理器的哈希表中。
 *
 * @param mgr 指向 neu_node_manager_t 类型的管理器指针，用于管理节点。
 * @param adapter 指向 neu_adapter_t 类型的适配器指针，该适配器将被添加到管理器中。
 *
 * @return int 若添加成功，返回 0；当前代码未处理可能的错误情况，如内存分配失败等。
 *
 * @note 该函数假设传入的 mgr 和 adapter 指针不为 NULL。如果传入 NULL 指针，可能会导致未定义行为。
 *       同时，函数使用 HASH_ADD_STR 宏添加节点到哈希表，需要确保哈希表的正确初始化。
 */
int neu_node_manager_add_static(neu_node_manager_t *mgr, neu_adapter_t *adapter)
{
    node_entity_t *node = calloc(1, sizeof(node_entity_t));

    node->adapter   = adapter;
    node->name      = strdup(adapter->name);
    node->is_static = true;
    node->display   = true;

    HASH_ADD_STR(mgr->nodes, name, node);

    return 0;
}

int neu_node_manager_add_single(neu_node_manager_t *mgr, neu_adapter_t *adapter,
                                bool display)
{
    node_entity_t *node = calloc(1, sizeof(node_entity_t));

    node->adapter = adapter;
    node->name    = strdup(adapter->name);
    node->display = display;
    node->single  = true;

    HASH_ADD_STR(mgr->nodes, name, node);

    return 0;
}

int neu_node_manager_update_name(neu_node_manager_t *mgr, const char *node_name,
                                 const char *new_node_name)
{
    node_entity_t *node = NULL;

    HASH_FIND_STR(mgr->nodes, node_name, node);
    if (NULL == node) {
        return NEU_ERR_NODE_NOT_EXIST;
    }

    char *new_name = strdup(new_node_name);
    if (NULL == new_name) {
        return NEU_ERR_EINTERNAL;
    }

    HASH_DEL(mgr->nodes, node);
    free(node->name);
    node->name = new_name;
    HASH_ADD_STR(mgr->nodes, name, node);

    return 0;
}

int neu_node_manager_update(neu_node_manager_t *mgr, const char *name,
                            struct sockaddr_un addr)
{
    node_entity_t *node = NULL;

    HASH_FIND_STR(mgr->nodes, name, node);
    if (NULL == node) {
        return -1;
    }
    node->addr = addr;

    return 0;
}

void neu_node_manager_del(neu_node_manager_t *mgr, const char *name)
{
    node_entity_t *node = NULL;

    HASH_FIND_STR(mgr->nodes, name, node);
    if (node != NULL) {
        HASH_DEL(mgr->nodes, node);
        if (node->is_monitor) {
            utarray_erase(mgr->monitors, utarray_eltidx(mgr->monitors, node),
                          1);
        }
        free(node->name);
        free(node);
    }
}

uint16_t neu_node_manager_size(neu_node_manager_t *mgr)
{
    return HASH_COUNT(mgr->nodes);
}

bool neu_node_manager_exist_uninit(neu_node_manager_t *mgr)
{
    node_entity_t *el = NULL, *tmp = NULL;

    HASH_ITER(hh, mgr->nodes, el, tmp)
    {
        if (el->addr.sun_path[1] == 0) {
            return true;
        }
    }

    return false;
}

UT_array *neu_node_manager_get(neu_node_manager_t *mgr, int type)
{
    UT_array *     array = NULL;
    UT_icd         icd   = { sizeof(neu_resp_node_info_t), NULL, NULL, NULL };
    node_entity_t *el = NULL, *tmp = NULL;

    utarray_new(array, &icd);

    HASH_ITER(hh, mgr->nodes, el, tmp)
    {
        if (!el->is_static && el->display) {
            if (el->adapter->module->type & type) {
                neu_resp_node_info_t info = { 0 };
                strcpy(info.node, el->adapter->name);
                strcpy(info.plugin, el->adapter->module->module_name);
                utarray_push_back(array, &info);
            }
        }
    }

    return array;
}

static int neu_resp_node_info_sort(const void *a, const void *b)
{
    const neu_resp_node_info_t *info_a = (const neu_resp_node_info_t *) a;
    const neu_resp_node_info_t *info_b = (const neu_resp_node_info_t *) b;

    if (info_a->delay < info_b->delay) {
        return -1;
    } else if (info_a->delay > info_b->delay) {
        return 1;
    }
    return 0;
}

UT_array *neu_node_manager_filter(neu_node_manager_t *mgr, int type,
                                  const char *plugin, const char *node,
                                  bool sort_delay, bool q_state, int state,
                                  bool q_link, int link)
{
    UT_array *     array = NULL;
    UT_icd         icd   = { sizeof(neu_resp_node_info_t), NULL, NULL, NULL };
    node_entity_t *el = NULL, *tmp = NULL;

    utarray_new(array, &icd);

    HASH_ITER(hh, mgr->nodes, el, tmp)
    {
        if (!el->is_static && el->display) {
            if (el->adapter->module->type & type) {
                if (strlen(plugin) > 0 &&
                    strcmp(el->adapter->module->module_name, plugin) != 0) {
                    continue;
                }
                if (strlen(node) > 0 &&
                    strstr(el->adapter->name, node) == NULL) {
                    continue;
                }
                if (q_state) {
                    if (el->adapter->state !=
                        (neu_node_running_state_e) state) {
                        continue;
                    }
                }
                if (q_link) {
                    neu_node_state_t node_state =
                        neu_adapter_get_state(el->adapter);
                    if (node_state.link != (neu_node_link_state_e) link) {
                        continue;
                    }
                }

                neu_resp_node_info_t info = { 0 };
                strcpy(info.node, el->adapter->name);
                strcpy(info.plugin, el->adapter->module->module_name);
                if (sort_delay) {
                    neu_metric_entry_t *e = NULL;
                    if (NULL != el->adapter->metrics) {
                        HASH_FIND_STR(el->adapter->metrics->entries,
                                      NEU_METRIC_LAST_RTT_MS, e);
                    }
                    info.delay = NULL != e ? e->value : 0;
                } else {
                    info.delay = 0;
                }
                utarray_push_back(array, &info);
            }
        }
    }

    if (sort_delay) {
        utarray_sort(array, neu_resp_node_info_sort);
    }

    return array;
}

UT_array *neu_node_manager_get_all(neu_node_manager_t *mgr)
{
    UT_array *     array = NULL;
    UT_icd         icd   = { sizeof(neu_resp_node_info_t), NULL, NULL, NULL };
    node_entity_t *el = NULL, *tmp = NULL;

    utarray_new(array, &icd);

    HASH_ITER(hh, mgr->nodes, el, tmp)
    {
        neu_resp_node_info_t info = { 0 };
        strcpy(info.node, el->adapter->name);
        strcpy(info.plugin, el->adapter->module->module_name);
        utarray_push_back(array, &info);
    }

    return array;
}

UT_array *neu_node_manager_get_adapter(neu_node_manager_t *mgr, int type)
{
    UT_array *     array = NULL;
    node_entity_t *el = NULL, *tmp = NULL;

    utarray_new(array, &ut_ptr_icd);

    HASH_ITER(hh, mgr->nodes, el, tmp)
    {
        if (!el->is_static && el->display) {
            if (el->adapter->module->type & type) {
                utarray_push_back(array, &el->adapter);
            }
        }
    }

    return array;
}

neu_adapter_t *neu_node_manager_find(neu_node_manager_t *mgr, const char *name)
{
    neu_adapter_t *adapter = NULL;
    node_entity_t *node    = NULL;

    HASH_FIND_STR(mgr->nodes, name, node);
    if (node != NULL) {
        adapter = node->adapter;
    }

    return adapter;
}

bool neu_node_manager_is_single(neu_node_manager_t *mgr, const char *name)
{
    node_entity_t *node = NULL;

    HASH_FIND_STR(mgr->nodes, name, node);
    if (node != NULL) {
        return node->single;
    }

    return false;
}

bool neu_node_manager_is_driver(neu_node_manager_t *mgr, const char *name)
{
    node_entity_t *node = NULL;

    HASH_FIND_STR(mgr->nodes, name, node);
    if (node != NULL) {
        return NEU_NA_TYPE_DRIVER == node->adapter->module->type;
    }

    return false;
}

UT_array *neu_node_manager_get_addrs(neu_node_manager_t *mgr, int type)
{
    UT_icd         icd   = { sizeof(struct sockaddr_un), NULL, NULL, NULL };
    UT_array *     addrs = NULL;
    node_entity_t *el = NULL, *tmp = NULL;

    utarray_new(addrs, &icd);

    HASH_ITER(hh, mgr->nodes, el, tmp)
    {
        if (!el->is_static) {
            if (el->adapter->module->type & type) {
                utarray_push_back(addrs, &el->addr);
            }
        }
    }

    return addrs;
}

UT_array *neu_node_manager_get_addrs_all(neu_node_manager_t *mgr)
{
    UT_icd         icd   = { sizeof(struct sockaddr_un), NULL, NULL, NULL };
    UT_array *     addrs = NULL;
    node_entity_t *el = NULL, *tmp = NULL;

    utarray_new(addrs, &icd);

    HASH_ITER(hh, mgr->nodes, el, tmp) { utarray_push_back(addrs, &el->addr); }

    return addrs;
}

struct sockaddr_un neu_node_manager_get_addr(neu_node_manager_t *mgr,
                                             const char *        name)
{
    struct sockaddr_un addr = { 0 };
    node_entity_t *    node = NULL;

    HASH_FIND_STR(mgr->nodes, name, node);
    if (node != NULL) {
        addr = node->addr;
    }

    return addr;
}

bool neu_node_manager_is_monitor(neu_node_manager_t *mgr, const char *name)
{
    node_entity_t *node = NULL;
    HASH_FIND_STR(mgr->nodes, name, node);
    return node ? node->is_monitor : false;
}

/*int neu_node_manager_for_each_monitor(neu_node_manager_t *mgr,
                                      int (*cb)(const char *       name,
                                                struct sockaddr_un addr,
                                                void *             data),
                                      void *data)
{
    int rv = 0;
    utarray_foreach(mgr->monitors, node_entity_t **, node)
    {
        rv = cb((*node)->name, (*node)->addr, data);
        if (0 != rv) {
            break;
        }
    }
    return rv;
}*/

UT_array *neu_node_manager_get_state(neu_node_manager_t *mgr)
{
    UT_icd         icd    = { sizeof(neu_nodes_state_t), NULL, NULL, NULL };
    UT_array *     states = NULL;
    node_entity_t *el = NULL, *tmp = NULL;

    utarray_new(states, &icd);

    HASH_ITER(hh, mgr->nodes, el, tmp)
    {
        neu_nodes_state_t state = { 0 };

        if (!el->is_static && el->display) {
            strcpy(state.node, el->adapter->name);
            state.state.running = el->adapter->state;
            state.state.link =
                neu_plugin_to_plugin_common(el->adapter->plugin)->link_state;
            state.state.log_level = el->adapter->log_level;
            state.is_driver = (el->adapter->module->type == NEU_NA_TYPE_DRIVER)
                ? true
                : false;
            neu_metric_entry_t *e = NULL;
            if (NULL != el->adapter->metrics) {
                HASH_FIND_STR(el->adapter->metrics->entries,
                              NEU_METRIC_LAST_RTT_MS, e);
            }
            state.rtt = NULL != e ? e->value : 0;

            utarray_push_back(states, &state);
        }
    }

    return states;
}
