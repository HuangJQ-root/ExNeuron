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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "errcodes.h"
#include "utils/asprintf.h"
#include "utils/log.h"
#include "utils/time.h"

#include "argparse.h"
#include "persist/json/persist_json_plugin.h"
#include "persist/persist.h"
#include "persist/persist_impl.h"
#include "persist/sqlite.h"

#include "json/neu_json_fn.h"

#if defined _WIN32 || defined __CYGWIN__
#define PATH_SEP_CHAR '\\'
#else
#define PATH_SEP_CHAR '/'
#endif

#define PATH_MAX_SIZE 128

static const char *     plugin_file = "persistence/plugins.json";
static const char *     tmp_path    = "tmp";
static neu_persister_t *g_impl      = NULL; // 全局的 SQLite持久化器实例指针

static int write_file_string(const char *fn, const char *s)
{
    char *tmp = NULL;
    if (0 > neu_asprintf(&tmp, "%s.tmp", fn)) {
        nlog_error("persister too long file name:%s", fn);
        return -1;
    }

    FILE *f = fopen(tmp, "w+");
    if (NULL == f) {
        nlog_error("persister failed to open file:%s", fn);
        free(tmp);
        return -1;
    }

    // write to a temporary file first
    int n = strlen(s);
    if (((size_t) n) != fwrite(s, 1, n, f)) {
        nlog_error("persister failed to write file:%s", fn);
        fclose(f);
        free(tmp);
        return -1;
    }

    fclose(f);

    nlog_debug("persister write %s to %s", s, tmp);

    // rename the temporary file to the destination file
    if (0 != rename(tmp, fn)) {
        nlog_error("persister failed rename %s to %s", tmp, fn);
        free(tmp);
        return -1;
    }

    free(tmp);
    return 0;
}

int read_file_string(const char *fn, char **out)
{
    int rv = 0;
    int fd = open(fn, O_RDONLY);
    if (-1 == fd) {
        rv = -1;
        goto error_open;
    }

    // get file size
    struct stat statbuf;
    if (-1 == fstat(fd, &statbuf)) {
        goto error_fstat;
    }
    off_t fsize = statbuf.st_size;

    char *buf = malloc(fsize + 1);
    if (NULL == buf) {
        rv = -1;
        goto error_buf;
    }

    // read all file content
    ssize_t n = 0;
    while ((n = read(fd, buf + n, fsize))) {
        if (fsize == n) {
            break;
        } else if (n < fsize && EINTR == errno) {
            continue;
        } else {
            rv = -1;
            goto error_read;
        }
    }

    buf[fsize] = 0;
    *out       = buf;
    close(fd);
    return rv;

error_read:
    free(buf);
error_buf:
error_fstat:
    close(fd);
error_open:
    nlog_warn("persister fail to read %s, reason: %s", fn, strerror(errno));
    return rv;
}

int neu_persister_store_plugins(UT_array *plugin_infos)
{
    int                    index       = 0;
    neu_json_plugin_resp_t plugin_resp = {
        .n_plugin = utarray_len(plugin_infos),
    };

    plugin_resp.plugins = calloc(utarray_len(plugin_infos), sizeof(char *));

    utarray_foreach(plugin_infos, neu_resp_plugin_info_t *, plugin)
    {
        // if (NEU_PLUGIN_KIND_SYSTEM == plugin->kind) {
        //     // filter out system plugins
        //     continue;
        // }
        plugin_resp.plugins[index] = plugin->library;
        index++;
    }

    char *result = NULL;
    int   rv = neu_json_encode_by_fn(&plugin_resp, neu_json_encode_plugin_resp,
                                   &result);

    free(plugin_resp.plugins);
    if (rv != 0) {
        return rv;
    }

    rv = write_file_string(plugin_file, result);

    free(result);
    return rv;
}

/**
 * @brief 从指定的 JSON 文件中加载插件信息，并将其添加到插件信息数组中。
 *
 * 该函数会读取指定文件名的 JSON 文件内容，将其解析为插件请求结构体，
 * 然后把解析得到的插件名称添加到传入的插件信息数组中。最后，释放解析过程中分配的内存。
 *
 * @param fname 指向要读取的 JSON 文件名称的常量字符指针。
 * @param plugin_infos 指向存储插件信息的 UT_array 数组的指针。
 * @return 如果文件读取或 JSON 解析过程中出现错误，返回相应的错误码；如果一切正常，返回 0。
 */
static int load_plugins_file(const char *fname, UT_array *plugin_infos)
{
    // 用于存储从文件中读取的 JSON 字符串
    char *json_str = NULL;
    int   rv       = read_file_string(fname, &json_str);
    if (rv != 0) {
        return rv;
    }

    // 用于存储解析后的插件请求结构体
    neu_json_plugin_req_t *plugin_req = NULL;

    // 对 JSON 字符串进行解析
    rv = neu_json_decode_plugin_req(json_str, &plugin_req);
    if (rv != 0) {
        free(json_str);
        return rv;
    }

    for (int i = 0; i < plugin_req->n_plugin; i++) {
        char *name = plugin_req->plugins[i];

        // 将插件名称添加到插件信息数组中
        utarray_push_back(plugin_infos, &name);
    }

    // 释放读取的 JSON 字符串内存
    free(json_str);
    // 释放插件请求结构体中插件列表的内存
    free(plugin_req->plugins);
    // 释放插件请求结构体的内存
    free(plugin_req);
    return 0;
}

static int ut_str_cmp(const void *a, const void *b)
{
    return strcmp(*(char **) a, *(char **) b);
}

/**
 * @brief 从默认配置文件和用户配置文件中加载插件信息，并合并去重。
 *
 * 该函数的主要功能是从默认插件配置文件和用户自定义插件配置文件中加载插件信息。
 * 它会分别加载这两个文件中的插件列表，对默认插件列表进行排序，然后将用户插件列表中的
 * 非重复插件添加到默认插件列表中，最后释放用户插件列表占用的内存，并将合并后的插件列表
 * 通过指针返回给调用者。
 *
 * @param plugin_infos 指向 UT_array 指针的指针，用于存储合并后的插件信息数组。
 * @return 无论是否成功加载插件文件，最终都会返回 0 表示函数执行完成。
 */
int neu_persister_load_plugins(UT_array **plugin_infos)
{
    // 定义存储默认插件信息的数组
    UT_array *default_plugins = NULL;
    // 定义存储用户自定义插件信息的数组
    UT_array *user_plugins    = NULL;
    // 初始化默认插件数组
    utarray_new(default_plugins, &ut_ptr_icd);
    // 初始化用户插件数组
    utarray_new(user_plugins, &ut_ptr_icd);

    // 从默认插件配置文件中加载插件信息
    // default plugins will always present
    if (0 !=
        load_plugins_file("config/default_plugins.json", default_plugins)) {
        nlog_warn("cannot load default plugins");
    }
    
    // 从用户插件配置文件中加载插件信息
    if (0 != load_plugins_file(plugin_file, user_plugins)) {
        nlog_warn("cannot load user plugins");
    } else {
        // the following operation needs sorting
        utarray_sort(default_plugins, ut_str_cmp);
    }

    utarray_foreach(user_plugins, char **, name)
    {
        // 检查当前用户插件是否已存在于默认插件数组中
        char **find = utarray_find(default_plugins, name, ut_str_cmp);
        if (NULL == find) {
            // 若不存在，则将该用户插件添加到默认插件数组中
            utarray_push_back(default_plugins, name);

            // 将原用户插件指针置空，表示已移动到默认插件数组
            *name = NULL; 
        } else {
            // 若已存在，则释放该用户插件占用的内存
            free(*name);
        }
    }

    // 释放用户插件数组占用的内存
    utarray_free(user_plugins);
    *plugin_infos = default_plugins;
    return 0;
}

char *neu_persister_save_file_tmp(const char *file_data, uint32_t len,
                                  const char *suffix)
{
    struct stat st;

    if (stat(tmp_path, &st) == -1) {
        if (mkdir(tmp_path, 0700) == -1) {
            nlog_error("%s mkdir fail", tmp_path);
            return NULL;
        }
    }

    char *file_name = calloc(1, 128);
    snprintf(file_name, 128, "%s/%" PRId64 ".%s", tmp_path, neu_time_ms(),
             suffix);
    FILE *fp = NULL;
    fp       = fopen(file_name, "wb+");
    if (fp == NULL) {
        nlog_error("not create tmp file: %s, err:%s", file_name,
                   strerror(errno));
        free(file_name);
        return NULL;
    }

    size_t size = fwrite(file_data, 1, len, fp);

    if (size < len) {
        fclose(fp);
        free(file_name);
        return NULL;
    }

    fclose(fp);

    return file_name;
}

bool neu_persister_library_exists(const char *library)
{
    bool      ret          = false;
    UT_array *plugin_infos = NULL;

    int rv = neu_persister_load_plugins(&plugin_infos);
    if (rv != 0) {
        return ret;
    }

    utarray_foreach(plugin_infos, char **, name)
    {
        if (strcmp(library, *name) == 0) {
            ret = true;
            break;
        }
    }

    utarray_foreach(plugin_infos, char **, name) { free(*name); }
    utarray_free(plugin_infos);

    return ret;
}

/**
 * @brief 创建并初始化一个 SQLite 持久化实例。
 *
 * 该函数接收一个指向包含数据库模式文件目录路径的字符串，并使用该目录中的模式文件
 * 创建和初始化一个 SQLite 持久化实例。如果成功创建持久化实例，则返回 0；
 * 如果创建失败（例如内存分配失败或数据库打开失败），则返回 -1。
 *
 * @param schema_dir 指向包含数据库模式文件的目录路径的字符串。\n
 *                   默认值为 "./config"。
 *
 * @return int 成功时返回 0，失败时返回 -1。
 *
 * @note 
 * - `g_impl` 是一个全局变量，用于存储持久化实例的引用。
 * - 确保 `schema_dir` 指向有效的目录路径，且该目录中包含所需的 SQL 模式文件。
 */
int neu_persister_create(const char *schema_dir)
{
    g_impl = neu_sqlite_persister_create(schema_dir);
    if (NULL == g_impl) {
        return -1;
    }
    return 0;
}

sqlite3 *neu_persister_get_db()
{
    return g_impl->vtbl->native_handle(g_impl);
}

void neu_persister_destroy()
{
    g_impl->vtbl->destroy(g_impl);
}

int neu_persister_store_node(neu_persist_node_info_t *info)
{
    return g_impl->vtbl->store_node(g_impl, info);
}

int neu_persister_load_nodes(UT_array **node_infos)
{
    return g_impl->vtbl->load_nodes(g_impl, node_infos);
}

int neu_persister_delete_node(const char *node_name)
{
    return g_impl->vtbl->delete_node(g_impl, node_name);
}

int neu_persister_update_node(const char *node_name, const char *new_name)
{
    return g_impl->vtbl->update_node(g_impl, node_name, new_name);
}

int neu_persister_update_node_state(const char *node_name, int state)
{
    return g_impl->vtbl->update_node_state(g_impl, node_name, state);
}

int neu_persister_store_tag(const char *driver_name, const char *group_name,
                            const neu_datatag_t *tag)
{
    return g_impl->vtbl->store_tag(g_impl, driver_name, group_name, tag);
}

int neu_persister_store_tags(const char *driver_name, const char *group_name,
                             const neu_datatag_t *tags, size_t n)
{
    return g_impl->vtbl->store_tags(g_impl, driver_name, group_name, tags, n);
}

int neu_persister_load_tags(const char *driver_name, const char *group_name,
                            UT_array **tags)
{
    return g_impl->vtbl->load_tags(g_impl, driver_name, group_name, tags);
}

int neu_persister_update_tag(const char *driver_name, const char *group_name,
                             const neu_datatag_t *tag)
{
    return g_impl->vtbl->update_tag(g_impl, driver_name, group_name, tag);
}

int neu_persister_update_tag_value(const char *         driver_name,
                                   const char *         group_name,
                                   const neu_datatag_t *tag)
{
    return g_impl->vtbl->update_tag_value(g_impl, driver_name, group_name, tag);
}

int neu_persister_delete_tag(const char *driver_name, const char *group_name,
                             const char *tag_name)
{
    return g_impl->vtbl->delete_tag(g_impl, driver_name, group_name, tag_name);
}

int neu_persister_store_subscription(const char *app_name,
                                     const char *driver_name,
                                     const char *group_name, const char *params,
                                     const char *static_tags)
{
    return g_impl->vtbl->store_subscription(g_impl, app_name, driver_name,
                                            group_name, params, static_tags);
}

int neu_persister_update_subscription(const char *app_name,
                                      const char *driver_name,
                                      const char *group_name,
                                      const char *params,
                                      const char *static_tags)
{
    return g_impl->vtbl->update_subscription(g_impl, app_name, driver_name,
                                             group_name, params, static_tags);
}

int neu_persister_load_subscriptions(const char *app_name,
                                     UT_array ** subscription_infos)
{
    return g_impl->vtbl->load_subscriptions(g_impl, app_name,
                                            subscription_infos);
}

int neu_persister_delete_subscription(const char *app_name,
                                      const char *driver_name,
                                      const char *group_name)
{
    return g_impl->vtbl->delete_subscription(g_impl, app_name, driver_name,
                                             group_name);
}

int neu_persister_store_group(const char *              driver_name,
                              neu_persist_group_info_t *group_info,
                              const char *              context)
{
    return g_impl->vtbl->store_group(g_impl, driver_name, group_info, context);
}

int neu_persister_update_group(const char *driver_name, const char *group_name,
                               neu_persist_group_info_t *group_info)
{
    return g_impl->vtbl->update_group(g_impl, driver_name, group_name,
                                      group_info);
}

int neu_persister_load_groups(const char *driver_name, UT_array **group_infos)
{
    return g_impl->vtbl->load_groups(g_impl, driver_name, group_infos);
}

int neu_persister_delete_group(const char *driver_name, const char *group_name)
{
    return g_impl->vtbl->delete_group(g_impl, driver_name, group_name);
}

int neu_persister_store_node_setting(const char *node_name, const char *setting)
{
    return g_impl->vtbl->store_node_setting(g_impl, node_name, setting);
}

/**
 * @brief 从持久化存储中加载指定节点的设置信息。
 *
 * 该函数通过调用由全局变量 `g_impl` 所指向的结构体中 `vtbl` 成员（函数指针表）
 * 里的 `load_node_setting` 函数，来实现从持久化存储中读取指定节点的设置信息的功能。
 *
 * @param node_name 指向一个表示节点名称的字符串的指针，用于唯一标识要加载设置信息的节点。
 * @param setting 指向一个常量字符指针的常量指针。函数会将从持久化存储中读取到的节点设置
 *                信息的指针存储在这里。注意，该指针所指向的内容是常量，不能在函数内部被修改。
 * @return 函数的返回值表示加载操作的结果。
 *         具体的返回值含义取决于 `g_impl->vtbl->load_node_setting` 函数的实现，
 *         通常返回 0 表示加载成功，非 0 值表示加载失败。
 */
int neu_persister_load_node_setting(const char *       node_name,
                                    const char **const setting)
{
    return g_impl->vtbl->load_node_setting(g_impl, node_name, setting);
}

int neu_persister_delete_node_setting(const char *node_name)
{
    return g_impl->vtbl->delete_node_setting(g_impl, node_name);
}

int neu_persister_store_user(const neu_persist_user_info_t *user)
{
    return g_impl->vtbl->store_user(g_impl, user);
}

int neu_persister_update_user(const neu_persist_user_info_t *user)
{
    return g_impl->vtbl->update_user(g_impl, user);
}

int neu_persister_load_user(const char *              user_name,
                            neu_persist_user_info_t **user_p)
{
    return g_impl->vtbl->load_user(g_impl, user_name, user_p);
}

int neu_persister_delete_user(const char *user_name)
{
    return g_impl->vtbl->delete_user(g_impl, user_name);
}

int neu_persister_load_users(UT_array **user_infos)
{
    return g_impl->vtbl->load_users(g_impl, user_infos);
}
