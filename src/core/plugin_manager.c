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
#include <dlfcn.h>
#include <stdlib.h>
#include <unistd.h>

#include "errcodes.h"
#include "utils/log.h"
#include "utils/utextend.h"

#include "adapter.h"
#include "plugin.h"

#include "restful/rest.h"

#include "argparse.h"
#include "plugin_manager.h"

/**
 * @brief 存储插件实体的相关信息。
 *
 * 描述插件的各种元信息，这些信息有助于系统对插件进行
 * 管理、加载、展示和配置
 */
typedef struct plugin_entity {
    /**
     * @brief 插件对应的 schema 名称。
     *
     * 通常用于标识插件支持的数据模型或协议。
     */
    char *schema;

    /**
     * @brief 插件的名称。
     *
     * 唯一标识一个插件的短名称，通常用于用户界面展示或配置文件中。
     */
    char *name;

    /**
     * @brief 插件的库文件名称。
     *
     * 包含插件实现的共享库文件名（例如 .so 文件），用于动态加载插件。
     */
    char *lib_name;

    /**
     * @brief 插件的英文描述。
     *
     * 提供关于插件功能和用途的简要说明。
     */
    char *description;

    /**
     * @brief 插件的中文描述。
     *
     * 提供关于插件功能和用途的简要说明，适用于中文环境。
     */
    char *description_zh;

    /**
     * @brief 插件的种类。
     *
     * 驱动 / APP  
     */
    neu_plugin_kind_e kind;

    /**
     * @brief 插件的节点类型。
     *
     * 驱动 / APP  
     */
    neu_node_type_e type;

    /**
     * @brief 插件的版本号。
     *
     * 表示插件的版本，通常是一个递增的整数值。
     */
    uint32_t version;

    /**
     * @brief 是否在用户界面中显示该插件。
     *
     * 如果设置为 true，则插件将在用户界面中可见；否则不可见。
     */
    bool display;

    /**
     * @brief 是否为单例模式。
     *
     * 如果设置为 true，则表示该插件只能有一个实例存在。
     */
    bool single;

    /**
     * @brief 单例模式下的名称。
     *
     * 如果插件是单例模式，则此字段指定其唯一的名称。
     */
    char *single_name;

    /**
     * @brief 用于哈希表的句柄。
     *
     * 使用 UT_hash_handle 来实现基于哈希表的快速查找功能。
     */
    UT_hash_handle hh;
} plugin_entity_t;

/**
 * @brief 插件管理器结构体，负责管理所有的插件实体。
 *
 * 该结构体作为插件管理的核心，通过一个指向 `plugin_entity_t`
 * 的指针来维护一个插件实体的集合。借助这个集合，系统可以对插件
 * 进行添加、删除、查找等操作，实现对插件的统一管理。
 */
struct neu_plugin_manager {
    plugin_entity_t *plugins;
};

static void entity_free(plugin_entity_t *entity);

neu_plugin_manager_t *neu_plugin_manager_create()
{
    neu_plugin_manager_t *manager = calloc(1, sizeof(neu_plugin_manager_t));

    return manager;
}

void neu_plugin_manager_destroy(neu_plugin_manager_t *mgr)
{
    plugin_entity_t *el = NULL, *tmp = NULL;

    HASH_ITER(hh, mgr->plugins, el, tmp)
    {
        HASH_DEL(mgr->plugins, el);
        entity_free(el);
    }

    free(mgr);
}

/**
 * @brief 向插件管理器中添加一个新的插件。
 *
 * 该函数尝试在指定的插件目录中打开名为 `plugin_lib_name` 的动态链接库文件，并查找其中的 `neu_plugin_module` 符号。
 * 如果找到该符号，并且插件信息有效，该函数将创建一个新的 `plugin_entity_t` 结构并将其添加到插件管理器的哈希表中。
 *
 * @param mgr 指向插件管理器的指针。
 * @param plugin_lib_name 要加载的插件库文件的名称（不包括路径）。
 *
 * @return 返回错误码，如果成功则返回 `NEU_ERR_SUCCESS`。
 *         - `NEU_ERR_LIBRARY_FAILED_TO_OPEN`：无法打开插件库文件。
 *         - `NEU_ERR_LIBRARY_MODULE_INVALID`：插件库文件中没有 `neu_plugin_module` 符号。
 *         - `NEU_ERR_LIBRARY_INFO_INVALID`：插件信息无效。
 *         - `NEU_ERR_LIBRARY_MODULE_VERSION_NOT_MATCH`：插件版本与主程序版本不匹配。
 *         - `NEU_ERR_LIBRARY_NAME_CONFLICT`：插件名称冲突。
 * 
 * @note
 * dlopen：此函数用于在运行时打开一个共享库文件，并且返回一个指向该共享库的句柄。借助这个句柄，程序就能调用共享库中的函数和变量。
 * dlsym(Dynamic Link Symbol)：该函数借助共享库的句柄，在共享库中查找指定名称的符号（函数或者变量），然后返回该符号的地址。
 */
int neu_plugin_manager_add(neu_plugin_manager_t *mgr,
                           const char *          plugin_lib_name)
{
    char                 lib_path[256]    = { 0 }; // 定义动态链接库文件的完整路径
    void *               handle           = NULL;  // 动态链接库文件的句柄
    void *               module           = NULL;  // 指向插件模块的指针
    neu_plugin_module_t *pm               = NULL;  // 指向插件模块结构体的指针
    plugin_entity_t *    plugin           = NULL;  // 指向插件实体的指针
    char                 lib_paths[3][64] = { 0 }; // 存储可能的库文件路径的数组
    snprintf(lib_paths[0], sizeof(lib_paths[0]), "%s", g_plugin_dir);         // 设置第一个可能的库文件路径为全局插件目录
    snprintf(lib_paths[1], sizeof(lib_paths[1]), "%s/system", g_plugin_dir);  // 设置第二个可能的库文件路径为系统插件目录
    snprintf(lib_paths[2], sizeof(lib_paths[2]), "%s/custom", g_plugin_dir);  // 设置第三个可能的库文件路径为自定义插件目录

    // 确保插件库文件名长度不超过限制
    assert(strlen(plugin_lib_name) <= NEU_PLUGIN_LIBRARY_LEN);

    // 遍历所有可能的库文件路径
    for (size_t i = 0; i < sizeof(lib_paths) / sizeof(lib_paths[0]); i++) {
        // 构造完整的库文件路径
        snprintf(lib_path, sizeof(lib_path) - 1, "%s/%s", lib_paths[i],
                 plugin_lib_name);

        // 尝试打开库文件
        handle = dlopen(lib_path, RTLD_NOW);

        if (handle == NULL) {
            nlog_warn("failed to open library %s, error: %s", lib_path,
                      dlerror());
        } else {
            break;
        }
    }

    // 如果无法打开任何库文件，则返回错误码
    if (handle == NULL) {
        return NEU_ERR_LIBRARY_FAILED_TO_OPEN;
    }

    // 尝试从库文件中获取 neu_plugin_module 符号
    module = dlsym(handle, "neu_plugin_module");
    if (module == NULL) {
        dlclose(handle);
        nlog_warn("failed to find neu_plugin_module symbol in %s", lib_path);
        return NEU_ERR_LIBRARY_MODULE_INVALID;
    }

    pm = (neu_plugin_module_t *) module;
    //判断插件模块种类：禁止添加静态插件
    if (pm->kind != NEU_PLUGIN_KIND_CUSTOM &&
        pm->kind != NEU_PLUGIN_KIND_SYSTEM) {
        dlclose(handle);
        nlog_warn("library: %s, kind wrong: %d", lib_path, pm->kind);
        return NEU_ERR_LIBRARY_INFO_INVALID;
    }

    //判断插件类型：禁止非驱动非APP
    if (pm->type != NEU_NA_TYPE_APP && pm->type != NEU_NA_TYPE_DRIVER) {
        dlclose(handle);
        nlog_warn("library: %s, type wrong: %d", lib_path, pm->type);
        return NEU_ERR_LIBRARY_INFO_INVALID;
    }

    uint32_t major = NEU_GET_VERSION_MAJOR(pm->version);
    uint32_t minor = NEU_GET_VERSION_MINOR(pm->version);

    //判断插件的版本号
    if (NEU_VERSION_MAJOR != major || NEU_VERSION_MINOR != minor) {
        nlog_warn("library %s plugin version error, major:%d minor:%d",
                  lib_path, major, minor);
        return NEU_ERR_LIBRARY_MODULE_VERSION_NOT_MATCH;
    }

    //查询是否已经存在同名插件
    HASH_FIND_STR(mgr->plugins, pm->module_name, plugin);
    if (plugin != NULL) {
        dlclose(handle);
        return NEU_ERR_LIBRARY_NAME_CONFLICT;
    }

    // 为插件实体分配内存
    plugin = calloc(1, sizeof(plugin_entity_t));

    plugin->version        = pm->version;
    plugin->display        = pm->display;
    plugin->type           = pm->type;
    plugin->kind           = pm->kind;
    plugin->schema         = strdup(pm->schema);
    plugin->name           = strdup(pm->module_name);
    plugin->lib_name       = strdup(plugin_lib_name);
    plugin->description    = strdup(pm->module_descr);
    plugin->description_zh = strdup(pm->module_descr_zh);
    plugin->single         = pm->single;
    if (plugin->single) {
        plugin->single_name = strdup(pm->single_name);
    }

    //将 plugin 实体添加到 mgr->plugins 哈希表中，使用 plugin 的 name 字段作为键。
    HASH_ADD_STR(mgr->plugins, name, plugin);

    nlog_notice("add plugin, name: %s, library: %s, kind: %d, type: %d",
                plugin->name, plugin->lib_name, plugin->kind, plugin->type);

    dlclose(handle);
    return NEU_ERR_SUCCESS;
}

/**
 * @brief 更新插件管理器中的插件信息。
 *
 * 该函数尝试在指定的插件目录中打开指定的插件库文件，并从中获取插件模块信息。
 * 如果插件已存在于管理器中，则更新其信息；否则，返回错误。
 *
 * @param mgr 指向插件管理器的指针。
 * @param plugin_lib_name 要更新的插件库文件的名称（不带路径）。
 *
 * @return 返回错误代码：
 *         - NEU_ERR_SUCCESS: 成功更新插件。
 *         - NEU_ERR_LIBRARY_FAILED_TO_OPEN: 无法打开插件库文件。
 *         - NEU_ERR_LIBRARY_MODULE_INVALID: 插件库文件中没有找到有效的插件模块符号。
 *         - NEU_ERR_LIBRARY_MODULE_NOT_EXISTS: 插件管理器中没有找到指定的插件模块。
 *
 * @note 该函数假设 `g_plugin_dir` 是一个全局变量，存储了插件目录的路径。
 * @note 该函数使用 `dlopen`、`dlsym` 和 `dlclose` 函数来动态加载和卸载插件库。
 */
int neu_plugin_manager_update(neu_plugin_manager_t *mgr,
                              const char *          plugin_lib_name)
{
    char                 lib_path[256]    = { 0 }; // 用于存储构建的插件库文件路径
    void *               handle           = NULL;  // 动态库加载句柄
    void *               module           = NULL;  // 插件模块指针（从动态库中获取）
    neu_plugin_module_t *pm               = NULL;  // 指向插件模块信息的指针
    plugin_entity_t *    plugin           = NULL;  // 指向插件实体信息的指针（在管理器中查找）
    char                 lib_paths[3][64] = { 0 }; // 存储可能的插件库路径

    // 构建可能的插件库路径
    snprintf(lib_paths[0], sizeof(lib_paths[0]), "%s", g_plugin_dir);
    snprintf(lib_paths[1], sizeof(lib_paths[1]), "%s/system", g_plugin_dir);
    snprintf(lib_paths[2], sizeof(lib_paths[2]), "%s/custom", g_plugin_dir);

    // 确保插件库名称长度合法
    assert(strlen(plugin_lib_name) <= NEU_PLUGIN_LIBRARY_LEN);

    // 遍历可能的插件库路径，尝试打开插件库文件
    for (size_t i = 0; i < sizeof(lib_paths) / sizeof(lib_paths[0]); i++) {

        snprintf(lib_path, sizeof(lib_path) - 1, "%s/%s", lib_paths[i],
                 plugin_lib_name);

        handle = dlopen(lib_path, RTLD_NOW);

        if (handle == NULL) {
            nlog_warn("failed to open library %s, error: %s", lib_path,
                      dlerror());
        } else {
            break;
        }
    }

    // 如果所有路径都尝试失败，则返回错误代码
    if (handle == NULL) {
        return NEU_ERR_LIBRARY_FAILED_TO_OPEN;
    }

    // 从打开的插件库文件中获取插件模块符号
    module = dlsym(handle, "neu_plugin_module");
    if (module == NULL) {
        dlclose(handle);
        nlog_warn("failed to find neu_plugin_module symbol in %s", lib_path);
        return NEU_ERR_LIBRARY_MODULE_INVALID;
    }

    // 将获取的模块符号转换为插件模块信息指针
    pm = (neu_plugin_module_t *) module;

    // 在插件管理器中查找指定的插件模块
    HASH_FIND_STR(mgr->plugins, pm->module_name, plugin);
    if (plugin == NULL) {
        dlclose(handle);
        return NEU_ERR_LIBRARY_MODULE_NOT_EXISTS;
    }

    // 更新插件信息
    plugin->version = pm->version;
    plugin->display = pm->display;
    plugin->type    = pm->type;
    plugin->kind    = pm->kind;

    // 释放并更新旧的 schema、description 和 description_zh 字符串
    free(plugin->schema);
    free(plugin->description);
    free(plugin->description_zh);
    plugin->schema         = strdup(pm->schema);
    plugin->description    = strdup(pm->module_descr);
    plugin->description_zh = strdup(pm->module_descr_zh);

    // 更新 single 和 single_name 信息（如果插件是单例）
    plugin->single         = pm->single;
    if (plugin->single) {
        free(plugin->single_name);
        plugin->single_name = strdup(pm->single_name);
    }

    nlog_notice("update plugin, name: %s, library: %s, kind: %d, type: %d",
                plugin->name, plugin->lib_name, plugin->kind, plugin->type);

    // 关闭插件库文件句柄（因为信息已经更新到管理器中）
    dlclose(handle);

    // 返回成功代码
    return NEU_ERR_SUCCESS;
}

/**
 * @brief 从插件管理器中删除指定名称的插件。
 *
 * 此函数尝试从插件管理器中删除一个插件，并根据插件类型（系统插件或自定义插件）决定是否允许删除。
 * 对于自定义插件，它还会删除与插件相关的共享库文件和JSON模式文件。
 *
 * @param mgr 指向插件管理器实例的指针。
 * @param plugin_name 要删除的插件名称。
 *
 * @return 成功时返回 NEU_ERR_SUCCESS；如果插件是系统插件，则返回 NEU_ERR_LIBRARY_SYSTEM_NOT_ALLOW_DEL；
 *         如果插件不存在或其他错误发生，则返回相应的错误码。
 */
int neu_plugin_manager_del(neu_plugin_manager_t *mgr, const char *plugin_name)
{
    // 初始化插件实体指针和返回值
    plugin_entity_t *plugin = NULL;
    int              ret    = NEU_ERR_LIBRARY_SYSTEM_NOT_ALLOW_DEL;

    // 在插件哈希表中查找指定名称的插件
    HASH_FIND_STR(mgr->plugins, plugin_name, plugin);
    if (plugin != NULL) {

        // 如果插件不是系统插件，则允许删除
        if (plugin->kind != NEU_PLUGIN_KIND_SYSTEM) {

            // 定义存储共享库文件路径和JSON模式文件路径的字符数组
            char so_file_path[128]     = { 0 };
            char schema_file_path[128] = { 0 };

            // 根据全局变量 g_plugin_dir 和插件模式文件名构造JSON模式文件路径
            snprintf(so_file_path, sizeof(so_file_path), "%s/custom/%s",
                     g_plugin_dir, plugin->lib_name);

            // 根据全局变量 g_plugin_dir 和插件模式文件名构造JSON模式文件路径
            snprintf(schema_file_path, sizeof(schema_file_path),
                     "%s/custom/schema/%s.json", g_plugin_dir, plugin->schema);
            
            // 尝试删除共享库文件，如果失败则记录错误日志
            if (remove(so_file_path) != 0) {
                nlog_error("rm %s file fail!", so_file_path);
            }

            // 尝试删除JSON模式文件，如果失败则记录错误日志
            if (remove(schema_file_path) != 0) {
                nlog_error("rm %s file fail!", schema_file_path);
            }

            // 从插件哈希表中删除该插件
            HASH_DEL(mgr->plugins, plugin);

            // 释放插件实体占用的内存
            entity_free(plugin);

            // 设置返回值为成功
            ret = NEU_ERR_SUCCESS;
        }
    }

    return ret;
}

/**
 * @brief 获取插件管理器中的所有非单例插件信息。
 *
 * 此函数遍历插件管理器中所有的插件实体，并将非单例插件的信息收集到一个动态数组中返回。
 * 插件信息包括插件类型、种类、版本、显示名称、模式文件名、名称、库名称以及描述信息等。
 *
 * @param mgr 指向插件管理器实例的指针。
 *
 * @return 返回包含所有非单例插件信息的动态数组；如果没有任何插件或发生错误，则返回空数组。
 *
 * @note 
 * - 动态数组使用 `UT_array` 实现，需要在使用后通过 `utarray_free` 释放。
 * - 所有字符串字段都是通过 `strncpy` 复制，确保不会超出目标缓冲区大小。
 */
UT_array *neu_plugin_manager_get(neu_plugin_manager_t *mgr)
{
    // 定义用于存储插件信息的动态数组和项控制描述符（icd）
    UT_array *       plugins;
    UT_icd           icd = { sizeof(neu_resp_plugin_info_t), NULL, NULL, NULL };

    // 初始化动态数组
    utarray_new(plugins, &icd);

    // 定义用于遍历哈希表的临时变量
    plugin_entity_t *el = NULL, *tmp = NULL;

    // 遍历插件管理器中的所有插件实体
    HASH_ITER(hh, mgr->plugins, el, tmp)
    {
        // 如果当前插件是单例插件，则跳过
        if (el->single) {
            continue;
        }

        // 构造插件信息结构体
        neu_resp_plugin_info_t info = {
            .kind    = el->kind,
            .type    = el->type,
            .version = el->version,
        };

        // 设置插件的显示名称
        info.display = el->display;
        strncpy(info.schema, el->schema, sizeof(info.schema));
        strncpy(info.name, el->name, sizeof(info.name));
        strncpy(info.library, el->lib_name, sizeof(info.library));
        strncpy(info.description, el->description, sizeof(info.description));
        strncpy(info.description_zh, el->description_zh,
                sizeof(info.description_zh));

        // 将插件信息添加到动态数组中
        utarray_push_back(plugins, &info);
    }

    return plugins;
}

/**
 * @brief 从插件管理器中获取所有单例插件的信息。
 *
 * 此函数遍历插件管理器中的所有插件，并收集那些被标记为单例的插件信息到一个UT_array数组中。
 * 插件信息见neu_resp_plugin_info_t类元素
 *
 * @param mgr 指向neu_plugin_manager_t类型的指针，代表插件管理器对象。
 * @return 返回一个包含neu_resp_plugin_info_t结构体的UT_array数组，如果没有任何单例插件，则返回空数组。
 */
UT_array *neu_plugin_manager_get_single(neu_plugin_manager_t *mgr)
{   
    // 用于存储满足条件的插件信息的数组
    UT_array *       plugins;   
    // 定义数组中元素的数据结构
    UT_icd           icd = { sizeof(neu_resp_plugin_info_t), NULL, NULL, NULL }; 
    // 遍历哈希表时使用的临时变量
    plugin_entity_t *el = NULL, *tmp = NULL; 

    // 初始化插件信息数组
    utarray_new(plugins, &icd);

    /**
     * @brief 遍历插件管理器中的所有插件
     * 
     * @param el: 这是一个指向哈希表中元素的指针，在遍历过程中，它会依次指向哈希表中的每个元素。
     *            可以通过 el 来访问当前元素的各个成员
     * @param tmp: 这是一个临时指针，tmp 会在删除元素时保存下一个要遍历的元素的指针。它的作用是
     *             在遍历过程中，当需要删除当前元素时，避免因删除操作破坏遍历的顺序。因为在删除
     *             元素时，哈希表的内部结构会发生变化，如果不使用临时指针，可能会导致遍历出错.
     */
    HASH_ITER(hh, mgr->plugins, el, tmp)
    {
        if (el->single) {
            neu_resp_plugin_info_t info = {
                .kind = el->kind,
                .type = el->type,
            };

            info.display = el->display;
            info.single  = el->single;

            strncpy(info.schema, el->schema, sizeof(info.schema));
            strncpy(info.single_name, el->single_name,
                    sizeof(info.single_name));
            strncpy(info.name, el->name, sizeof(info.name));
            strncpy(info.library, el->lib_name, sizeof(info.library));
            strncpy(info.description, el->description,
                    sizeof(info.description));
            strncpy(info.description_zh, el->description_zh,
                    sizeof(info.description_zh));

            utarray_push_back(plugins, &info);
        }
    }

    return plugins;
}

/**
 * @brief 在插件管理器中查找指定名称的插件，并获取其详细信息。
 *
 * 此函数通过插件名称在插件管理器中查找匹配的插件，并将找到的插件信息复制到提供的neu_resp_plugin_info_t结构体中。
 * 如果找到了匹配的插件，返回0；如果没有找到，则返回-1。
 *
 * @param mgr 指向neu_plugin_manager_t类型的指针，代表插件管理器对象。
 * @param plugin_name 要查找的插件名称。
 * @param info 指向neu_resp_plugin_info_t类型的指针，用于存储找到的插件信息。
 * @return 如果成功找到插件，返回0；如果未找到匹配插件，返回-1。
 */
int neu_plugin_manager_find(neu_plugin_manager_t *mgr, const char *plugin_name,
                            neu_resp_plugin_info_t *info)
{
    plugin_entity_t *plugin = NULL;  // 用于存储找到的插件实体
    int              ret    = -1;    // 默认返回值，表示未找到插件

    HASH_FIND_STR(mgr->plugins, plugin_name, plugin);
    if (plugin != NULL) {
        ret           = 0;
        info->single  = plugin->single;
        info->display = plugin->display;
        info->type    = plugin->type;
        info->kind    = plugin->kind;

        // 如果存在单例名称，则进行复制
        if (plugin->single_name != NULL) {
            strcpy(info->single_name, plugin->single_name);
        }

        // 复制插件名、库文件名、描述信息
        strncpy(info->name, plugin->name, sizeof(info->name));
        strncpy(info->library, plugin->lib_name, sizeof(info->library));
        strncpy(info->description, plugin->description,
                sizeof(info->description));
    }

    return ret;
}

/**
 * @brief 检查指定名称的插件是否存在于插件管理器中。
 *
 * 此函数通过插件名称在插件管理器中查找匹配的插件。如果找到了匹配的插件，则返回true，否则返回false。
 *
 * @param mgr 指向neu_plugin_manager_t类型的指针，代表插件管理器对象。
 * @param plugin_name 要检查的插件名称。
 * @return 如果找到匹配的插件，返回true；如果没有找到，返回false。
 */
bool neu_plugin_manager_exists(neu_plugin_manager_t *mgr,
                               const char *          plugin_name)
{
    // 用于存储找到的插件实体
    plugin_entity_t *plugin = NULL;

    // 使用HASH_FIND_STR宏根据插件名称查找插件
    HASH_FIND_STR(mgr->plugins, plugin_name, plugin);

    // 判断是否找到插件，并返回相应的布尔值
    return NULL != plugin;
}

/**
 * @brief 检查指定名称的插件是否为单例模式。
 *
 * 此函数通过插件名称在插件管理器中查找匹配的插件，并检查该插件是否被标记为单例模式（single）。
 * 如果找到了匹配的插件并且它是单例模式，则返回true；如果未找到匹配的插件或插件不是单例模式，则返回false。
 *
 * @param mgr 指向neu_plugin_manager_t类型的指针，代表插件管理器对象。
 * @param plugin_name 要检查的插件名称。
 * @return 如果找到匹配的插件且其为单例模式，返回true；否则返回false。
 */
bool neu_plugin_manager_is_single(neu_plugin_manager_t *mgr,
                                  const char *          plugin_name)
{
    // 用于存储找到的插件实体
    plugin_entity_t *plugin = NULL;

    // 使用HASH_FIND_STR宏根据插件名称查找插件
    HASH_FIND_STR(mgr->plugins, plugin_name, plugin);

    if (NULL == plugin) {
        return false;
    }

    // 返回插件是否为单例模式的状态
    return plugin->single;
}

/**
 * @brief 根据插件名称在插件管理器中创建插件实例。
 *
 * 此函数尝试根据提供的插件名称在指定的目录路径下查找并加载对应的插件库文件（.so等），
 * 并初始化插件实例。如果成功找到并加载了插件库，返回0；否则返回-1。
 *
 * @param mgr 指向neu_plugin_manager_t类型的指针，代表插件管理器对象。
 * @param plugin_name 要创建实例的插件名称。
 * @param instance 指向neu_plugin_instance_t类型的指针，用于存储创建的插件实例信息。
 * @return 如果成功创建插件实例，返回0；如果失败，返回-1。
 */
int neu_plugin_manager_create_instance(neu_plugin_manager_t * mgr,
                                       const char *           plugin_name,
                                       neu_plugin_instance_t *instance)
{
    // 用于存储找到的插件实体
    plugin_entity_t *plugin           = NULL;

    // 定义可能存放插件库文件的路径数组
    char             lib_paths[3][64] = { 0 };
    snprintf(lib_paths[0], sizeof(lib_paths[0]), "%s", g_plugin_dir);
    snprintf(lib_paths[1], sizeof(lib_paths[1]), "%s/system", g_plugin_dir);
    snprintf(lib_paths[2], sizeof(lib_paths[2]), "%s/custom", g_plugin_dir);

    // 查找插件
    HASH_FIND_STR(mgr->plugins, plugin_name, plugin);
    if (plugin != NULL) {
        // 遍历所有可能的库文件路径
        for (size_t i = 0; i < sizeof(lib_paths) / sizeof(lib_paths[0]); i++) {
            char lib_path[256] = { 0 };

            // 构建完整库文件路径
            snprintf(lib_path, sizeof(lib_path) - 1, "%s/%s", lib_paths[i],
                     plugin->lib_name);
            
            // 尝试加载库文件
            instance->handle = dlopen(lib_path, RTLD_NOW);

            if (instance->handle == NULL) {
                continue;
            }

            /**
             * @brief 获取插件模块入口点
             * 
             * dlsym获取 neu_plugin_module 变量的地址。通过获取到的
             * neu_plugin_module 结构体指针，就可以访问插件的版本信息、
             * 接口函数等内容。
             */
            instance->module = (neu_plugin_module_t *) dlsym(
                instance->handle, "neu_plugin_module");

            // 确保获取到有效的模块入口点
            assert(instance->module != NULL);
            return 0;
        }
    }

    // 如果未能成功加载插件库，确保句柄为空，并返回错误码
    assert(instance->handle != NULL);

    return -1;
}

/**
 * @brief 加载静态插件到插件管理器中。
 *
 * 此函数用于加载预定义的静态插件（例如默认仪表盘插件），并将其模块信息存储在提供的实例中。
 * 与动态加载插件不同，静态插件通常直接编译进应用程序或库中。
 *
 * @param mgr 指向neu_plugin_manager_t类型的指针，表示插件管理器对象。当前实现中并未使用此参数。
 * @param plugin_name 要加载的插件名称。
 * @param instance 指向neu_plugin_instance_t类型的指针，用于存储加载的插件模块信息实例。
 */
void neu_plugin_manager_load_static(neu_plugin_manager_t * mgr,
                                    const char *           plugin_name,
                                    neu_plugin_instance_t *instance)
{
    (void) mgr;
    instance->handle = NULL;

    // 如果请求加载的是默认仪表盘插件
    if (strcmp(DEFAULT_DASHBOARD_PLUGIN_NAME, plugin_name) == 0) {
        instance->module =
            (neu_plugin_module_t *) &default_dashboard_plugin_module;
    }
}

void neu_plugin_manager_destroy_instance(neu_plugin_manager_t * mgr,
                                         neu_plugin_instance_t *instance)
{
    (void) mgr;
    nlog_notice("destroy plugin instance: %s, handle: %p",
                instance->module->module_name, instance->handle);
    if (instance->handle != NULL) {
        dlclose(instance->handle);
    }
}

/**
 * @brief 释放插件实体占用的所有资源。
 *
 * 此函数负责释放与插件实体相关的所有动态分配的内存资源，
 * 包括插件名称、库名称、模式文件名、描述信息等。如果插件是单例类型，
 * 还会释放单例名称所占用的内存。最后，释放插件实体本身。
 *
 * @param entity 指向要释放的插件实体的指针。
 *
 * @note 
 * - 该函数假设所有传入的字符串字段都是通过 `malloc` 或类似函数动态分配的。
 * - 调用此函数后，`entity` 指针将不再有效。
 */
static void entity_free(plugin_entity_t *entity)
{
    // 记录删除插件的日志信息
    nlog_notice("del plugin, name: %s, library: %s, kind: %d, type: %d",
                entity->name, entity->lib_name, entity->kind, entity->type);

    // 如果插件是单例类型，则释放单例名称所占用的内存
    if (entity->single) {
        free(entity->single_name); // 释放单例名称
    }

    // 释放模式文件名所占用的内存
    free(entity->schema); // 释放模式文件名

    // 释放插件名称所占用的内存
    free(entity->name); // 释放插件名称

    // 释放库名称所占用的内存
    free(entity->lib_name); // 释放库名称

    // 释放描述信息所占用的内存
    free(entity->description); // 释放描述信息

    // 释放中文描述信息所占用的内存
    free(entity->description_zh); // 释放中文描述信息

    // 最后释放插件实体本身
    free(entity); // 释放插件实体
}

bool neu_plugin_manager_create_instance_by_path(neu_plugin_manager_t *mgr,
                                                const char *plugin_path,
                                                neu_plugin_instance_t *instance,
                                                int *                  error)
{

    (void) mgr;

    instance->handle = dlopen(plugin_path, RTLD_NOW);

    if (instance->handle == NULL) {
        const char *dl_err = dlerror();
        if (strstr(dl_err, "cannot open shared object file") != NULL) {
            *error = NEU_ERR_LIBRARY_ARCH_NOT_SUPPORT;
        } else if (strstr(dl_err, "GLIBC") != NULL &&
                   strstr(dl_err, "not found") != NULL) {
            *error = NEU_ERR_LIBRARY_CLIB_NOT_MATCH;
        } else {
            *error = NEU_ERR_LIBRARY_MODULE_INVALID;
        }

        nlog_debug("create instance error:%s", dl_err);
        (void) dlerror();
        return false;
    }

    instance->module =
        (neu_plugin_module_t *) dlsym(instance->handle, "neu_plugin_module");

    if (instance->module == NULL) {
        dlclose(instance->handle);
        *error           = NEU_ERR_LIBRARY_MODULE_INVALID;
        instance->handle = NULL;
        return false;
    }

    return true;
}

bool neu_plugin_manager_create_instance_by_lib_name(
    neu_plugin_manager_t *mgr, const char *lib_name,
    neu_plugin_instance_t *instance)
{
    (void) mgr;
    char lib_paths[3][64] = { 0 };
    snprintf(lib_paths[0], sizeof(lib_paths[0]), "%s", g_plugin_dir);
    snprintf(lib_paths[1], sizeof(lib_paths[1]), "%s/system", g_plugin_dir);
    snprintf(lib_paths[2], sizeof(lib_paths[2]), "%s/custom", g_plugin_dir);

    for (size_t i = 0; i < sizeof(lib_paths) / sizeof(lib_paths[0]); i++) {
        char lib_path[256] = { 0 };

        snprintf(lib_path, sizeof(lib_path) - 1, "%s/%s", lib_paths[i],
                 lib_name);
        instance->handle = dlopen(lib_path, RTLD_NOW);

        if (instance->handle == NULL) {
            continue;
        }

        instance->module = (neu_plugin_module_t *) dlsym(instance->handle,
                                                         "neu_plugin_module");
        assert(instance->module != NULL);
        return true;
    }

    assert(instance->handle != NULL);

    return false;
}

bool neu_plugin_manager_remove_library(neu_plugin_manager_t *mgr,
                                       const char *          library)
{
    bool ret = true;
    (void) mgr;
    char lib_paths[3][64] = { 0 };
    snprintf(lib_paths[0], sizeof(lib_paths[0]), "%s", g_plugin_dir);
    snprintf(lib_paths[1], sizeof(lib_paths[1]), "%s/system", g_plugin_dir);
    snprintf(lib_paths[2], sizeof(lib_paths[2]), "%s/custom", g_plugin_dir);
    char lib_path[256] = { 0 };
    for (size_t i = 0; i < sizeof(lib_paths) / sizeof(lib_paths[0]); i++) {
        snprintf(lib_path, sizeof(lib_path), "%s/%s", lib_paths[i], library);
        if (access(lib_path, F_OK) != -1 && remove(lib_path) != 0) {
            nlog_debug("library %s remove fail", lib_path);
            ret = false;
            break;
        }
    }

    return ret;
}

bool neu_plugin_manager_schema_exist(neu_plugin_manager_t *mgr,
                                     const char *          schema)
{
    bool             exist = false;
    plugin_entity_t *el = NULL, *tmp = NULL;

    HASH_ITER(hh, mgr->plugins, el, tmp)
    {
        if (strcmp(el->schema, schema) == 0) {
            exist = true;
            break;
        }
    }

    return exist;
}