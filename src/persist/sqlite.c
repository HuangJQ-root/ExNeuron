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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <errno.h>
#include <sys/types.h>

#include <sqlite3.h>

#include "errcodes.h"
#include "utils/log.h"

#include "sqlite.h"

#if defined _WIN32 || defined __CYGWIN__
#define PATH_SEP_CHAR '\\'
#else
#define PATH_SEP_CHAR '/'
#endif

#define PATH_MAX_SIZE 128

#define DB_FILE "persistence/sqlite.db"

static inline bool ends_with(const char *str, const char *suffix)
{
    size_t m = strlen(str);
    size_t n = strlen(suffix);
    return m >= n && !strcmp(str + m - n, suffix);
}

/**
 * Concatenate a path string to another.
 *
 * @param dst   destination path string buffer
 * @param len   destination path string len, not greater than size
 * @param size  destination path buffer size
 * @param src   path string
 *
 * @return length of the result path string excluding the terminating NULL
 *         byte, `size` indicates overflow.
 */
static int path_cat(char *dst, size_t len, size_t size, const char *src)
{
    size_t i = len;

    if (0 < i && i < size && (PATH_SEP_CHAR != dst[i - 1])) {
        dst[i++] = PATH_SEP_CHAR;
    }

    if (*src && PATH_SEP_CHAR == *src) {
        ++src;
    }

    while (i < size && (dst[i] = *src++)) {
        ++i;
    }

    if (i == size && i > 0) {
        dst[i - 1] = '\0';
    }

    return i;
}

/**
 * @brief 执行格式化的 SQLite SQL 语句。
 *
 * 该内联函数用于执行带有可变参数的 SQLite SQL 语句。它接收一个 SQLite 数据库
 * 连接句柄、一个格式化的 SQL 语句模板，以及一系列可变参数，将这些参数填充到 SQL
 * 语句模板中，然后执行生成的 SQL 语句。
 *
 * @param db 指向 SQLite 数据库连接的指针，代表要执行 SQL 语句的数据库实例。
 * @param sql 格式化的 SQL 语句模板，其中可以包含占位符（如 `%Q`、`%i` 等），用于后续填充可变参数。
 * @param ... 可变参数列表，用于填充 `sql` 中的占位符。
 *
 * @return 执行结果的状态码：
 *         - 如果 SQL 语句执行成功，返回 0。
 *         - 如果在分配 SQL 语句内存或执行 SQL 语句过程中出现错误，返回 `NEU_ERR_EINTERNAL`，
 *           表示内部错误。
 *
 * @note 
 * 该函数使用了 `sqlite3_vmprintf` 函数来格式化 SQL 语句，因此需要确保 `sql` 中的占位符与
 * 可变参数的类型和数量匹配。
 *
 * @warning 
 * 如果传入的 `db` 指针为 `NULL`，会导致未定义行为，因为函数会尝试使用该指针执行 SQL 操作。
 */
static inline int execute_sql(sqlite3 *db, const char *sql, ...)
{
    int rv = 0;

    // 初始化可变参数列表,args 指向第一个可变参数
    va_list args;
    va_start(args, sql);
    // 使用可变参数格式化 SQL 语句
    char *query = sqlite3_vmprintf(sql, args);
    va_end(args);

    // 检查 SQL 语句格式化是否成功
    if (NULL == query) {
        nlog_error("allocate SQL `%s` fail", sql);
        return NEU_ERR_EINTERNAL;
    }

    char *err_msg = NULL;
    if (SQLITE_OK != sqlite3_exec(db, query, NULL, NULL, &err_msg)) {
        nlog_error("query `%s` fail: %s", query, err_msg);
        rv = NEU_ERR_EINTERNAL;
    } else {
        nlog_info("query %s success", query);
    }

    // 释放错误消息的内存
    sqlite3_free(err_msg);

    // 释放格式化后的 SQL 语句的内存
    sqlite3_free(query);

    return rv;
}

static int get_schema_version(sqlite3 *db, char **version_p, bool *dirty_p)
{
    sqlite3_stmt *stmt  = NULL;
    const char *  query = "SELECT version, dirty FROM migrations ORDER BY "
                        "version DESC LIMIT 1";

    if (SQLITE_OK != sqlite3_prepare_v2(db, query, -1, &stmt, NULL)) {
        nlog_error("prepare `%s` fail: %s", query, sqlite3_errmsg(db));
        return NEU_ERR_EINTERNAL;
    }

    int step = sqlite3_step(stmt);
    if (SQLITE_ROW == step) {
        char *version = strdup((char *) sqlite3_column_text(stmt, 0));
        if (NULL == version) {
            sqlite3_finalize(stmt);
            return NEU_ERR_EINTERNAL;
        }

        *version_p = version;
        *dirty_p   = sqlite3_column_int(stmt, 1);
    } else if (SQLITE_DONE != step) {
        nlog_warn("query `%s` fail: %s", query, sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);
    return 0;
}

static int schema_version_cmp(const void *a, const void *b)
{
    return strcmp(*(char **) a, *(char **) b);
}

static int extract_schema_info(const char *file, char **version_p,
                               char **description_p)
{
    if (!ends_with(file, ".sql")) {
        return NEU_ERR_EINTERNAL;
    }

    char *sep = strchr(file, '_');
    if (NULL == sep) {
        return NEU_ERR_EINTERNAL;
    }

    size_t n = sep - file;
    if (4 != n) {
        // invalid version
        return NEU_ERR_EINTERNAL;
    }

    if (strcmp(++sep, ".sql") == 0) {
        // no description
        return NEU_ERR_EINTERNAL;
    }

    char *version = calloc(n + 1, sizeof(char));
    if (NULL == version) {
        return NEU_ERR_EINTERNAL;
    }
    strncat(version, file, n);

    n                 = strlen(sep) - 4;
    char *description = calloc(n + 1, sizeof(char));
    if (NULL == description) {
        free(version);
        return NEU_ERR_EINTERNAL;
    }
    strncat(description, sep, n);

    *version_p     = version;
    *description_p = description;

    return 0;
}

static int should_apply(sqlite3 *db, const char *version)
{
    int           rv   = 0;
    sqlite3_stmt *stmt = NULL;
    const char *  sql  = "SELECT count(*) FROM migrations WHERE version=?";

    if (SQLITE_OK != sqlite3_prepare_v2(db, sql, -1, &stmt, NULL)) {
        nlog_error("prepare `%s` fail: %s", sql, sqlite3_errmsg(db));
        rv = -1;
        goto end;
    }

    if (SQLITE_OK != sqlite3_bind_text(stmt, 1, version, -1, NULL)) {
        nlog_error("bind `%s` with version=`%s` fail: %s", sql, version,
                   sqlite3_errmsg(db));
        rv = -1;
        goto end;
    }

    if (SQLITE_ROW == sqlite3_step(stmt)) {
        rv = sqlite3_column_int(stmt, 0) == 0 ? 1 : 0;
    } else {
        nlog_warn("query `%s` fail: %s", sql, sqlite3_errmsg(db));
        rv = -1;
    }

end:
    sqlite3_finalize(stmt);
    return rv;
}

static int apply_schema_file(sqlite3 *db, const char *dir, const char *file)
{
    int   rv          = 0;
    char *version     = NULL;
    char *description = NULL;
    char *sql         = NULL;
    char *path        = NULL;
    int   n           = 0;

    path = calloc(PATH_MAX_SIZE, sizeof(char));
    if (NULL == path) {
        nlog_error("malloc fail");
        return -1;
    }

    if (PATH_MAX_SIZE <= (n = path_cat(path, 0, PATH_MAX_SIZE, dir)) ||
        PATH_MAX_SIZE <= path_cat(path, n, PATH_MAX_SIZE, file)) {
        nlog_error("path too long: %s", path);
        free(path);
        return -1;
    }

    if (0 != extract_schema_info(file, &version, &description)) {
        nlog_warn("extract `%s` schema info fail, ignore", path);
        free(path);
        return 0;
    }

    rv = should_apply(db, version);
    if (rv <= 0) {
        free(version);
        free(description);
        free(path);
        return rv;
    }

    rv = read_file_string(path, &sql);
    if (0 != rv) {
        goto end;
    }

    rv = execute_sql(db,
                     "INSERT INTO migrations (version, description, dirty) "
                     "VALUES (%Q, %Q, 1)",
                     version, description);
    if (0 != rv) {
        goto end;
    }

    char *err_msg = NULL;
    rv            = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    if (SQLITE_OK != rv) {
        nlog_error("execute %s fail: (%d)%s", path, rv, err_msg);
        sqlite3_free(err_msg);
        rv = NEU_ERR_EINTERNAL;
        goto end;
    }

    rv = execute_sql(db, "UPDATE migrations SET dirty = 0 WHERE version=%Q",
                     version);

end:
    if (0 == rv) {
        nlog_notice("success apply schema `%s`, version=`%s` description=`%s`",
                    path, version, description);
    } else {
        nlog_error("fail apply schema `%s`, version=`%s` description=`%s`",
                   path, version, description);
    }

    free(sql);
    free(path);
    free(version);
    free(description);
    return rv;
}

static UT_array *collect_schemas(const char *dir)
{
    DIR *          dirp  = NULL;
    struct dirent *dent  = NULL;
    UT_array *     files = NULL;

    if ((dirp = opendir(dir)) == NULL) {
        nlog_error("fail open dir: %s", dir);
        return NULL;
    }

    utarray_new(files, &ut_str_icd);

    while (NULL != (dent = readdir(dirp))) {
        if (ends_with(dent->d_name, ".sql")) {
            char *file = dent->d_name;
            utarray_push_back(files, &file);
        }
    }

    closedir(dirp);
    return files;
}

static int apply_schemas(sqlite3 *db, const char *dir)
{
    int rv = 0;

    const char *sql = "CREATE TABLE IF NOT EXISTS migrations ( migration_id "
                      "INTEGER PRIMARY KEY, version TEXT NOT NULL UNIQUE, "
                      "description TEXT NOT NULL, dirty INTEGER NOT NULL, "
                      "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP) ";
    if (0 != execute_sql(db, sql)) {
        nlog_error("create migration table fail");
        return NEU_ERR_EINTERNAL;
    }

    bool  dirty   = false;
    char *version = NULL;
    if (0 != get_schema_version(db, &version, &dirty)) {
        nlog_error("find schema version fail");
        return NEU_ERR_EINTERNAL;
    }

    nlog_notice("schema head version=%s", version ? version : "none");

    if (dirty) {
        nlog_error("database is dirty, need manual intervention");
        return NEU_ERR_EINTERNAL;
    }

    UT_array *files = collect_schemas(dir);
    if (NULL == files) {
        free(version);
        return NEU_ERR_EINTERNAL;
    }

    if (0 == utarray_len(files)) {
        nlog_warn("directory `%s` contains no schema files", dir);
    }

    utarray_sort(files, schema_version_cmp);

    utarray_foreach(files, char **, file)
    {
        if (0 != apply_schema_file(db, dir, *file)) {
            rv = NEU_ERR_EINTERNAL;
            break;
        }
    }

    free(version);
    utarray_free(files);

    return rv;
}

/**
 * @brief 打开或创建一个 SQLite 数据库，并应用模式文件。
 *
 * 该函数尝试打开或创建一个位于指定路径（通过宏定义 DB_FILE 指定）的 SQLite 数据库。
 * 如果数据库成功打开，则会设置一些配置选项（如启用外键支持和设置日志模式为 WAL），
 * 并应用提供的模式文件目录中的 SQL 脚本。如果任何步骤失败，将记录错误信息并关闭数据库
 * 连接，最终返回 -1 表示失败；如果所有操作都成功，则返回 0。
 *
 * @param schema_dir 指向包含数据库模式文件的目录路径的字符串。
 * @param db_p       指向 sqlite3* 类型指针的地址，用于存储打开或创建的数据库连接。
 *
 * @return int 返回 0 表示成功，返回 -1 表示失败。
 *
 * @note 
 * - 确保 schema_dir 指向有效的目录路径，且该目录中包含所需的 SQL 模式文件。
 * - 宏定义 DB_FILE 应指向正确的数据库文件路径，确保数据库文件所在的目录存在。
 */
static inline int open_db(const char *schema_dir, sqlite3 **db_p)
{
    sqlite3 *db = NULL;

    // 尝试打开或创建数据库
    int      rv = sqlite3_open(DB_FILE, &db);
    if (SQLITE_OK != rv) {
        nlog_fatal("db `%s` fail: %s", DB_FILE, sqlite3_errstr(rv));
        return -1;
    }

    // 设置数据库忙等待超时时间为 100 秒
    sqlite3_busy_timeout(db, 100 * 1000);

    // 启用外键支持: 数据一致性
    rv = sqlite3_exec(db, "PRAGMA foreign_keys=ON", NULL, NULL, NULL);
    if (rv != SQLITE_OK) {
        nlog_fatal("db foreign key support fail: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return -1;
    }

    // 设置日志模式为 Write-Ahead Logging (WAL):1.崩溃恢复能力增强 2.减少磁盘 I/O 3.并发性能提升
    rv = sqlite3_exec(db, "PRAGMA journal_mode=WAL", NULL, NULL, NULL);
    if (rv != SQLITE_OK) {
        nlog_fatal("db journal_mode WAL fail: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return -1;
    }

    // 应用 schema_dir 中的模式文件到数据库
    rv = apply_schemas(db, schema_dir);
    if (rv != 0) {
        nlog_fatal("db apply schemas fail");
        sqlite3_close(db);
        return -1;
    }

    // 将打开的数据库连接赋值给 db_p 指向的变量
    *db_p = db;
    return 0;
}

/**
 * @brief 全局的 SQLite 持久化器虚函数表实例。
 *
 * 该虚函数表包含了一系列函数指针，这些指针指向了针对 SQLite 数据库实现的
 * 持久化操作函数。通过这个虚函数表，可以在运行时根据实际的持久化对象类型调
 * 用相应的 SQLite 操作函数，从而实现对 SQLite 数据库的各种持久化操作，
 * 如节点信息、标签信息、订阅信息等的存储、加载、更新和删除。
 */
static struct neu_persister_vtbl_s g_sqlite_persister_vtbl = {
    .destroy             = neu_sqlite_persister_destroy,
    .native_handle       = neu_sqlite_persister_native_handle,
    .store_node          = neu_sqlite_persister_store_node,
    .load_nodes          = neu_sqlite_persister_load_nodes,
    .delete_node         = neu_sqlite_persister_delete_node,
    .update_node         = neu_sqlite_persister_update_node,
    .update_node_state   = neu_sqlite_persister_update_node_state,
    .store_tag           = neu_sqlite_persister_store_tag,
    .store_tags          = neu_sqlite_persister_store_tags,
    .load_tags           = neu_sqlite_persister_load_tags,
    .update_tag          = neu_sqlite_persister_update_tag,
    .update_tag_value    = neu_sqlite_persister_update_tag_value,
    .delete_tag          = neu_sqlite_persister_delete_tag,
    .store_subscription  = neu_sqlite_persister_store_subscription,
    .update_subscription = neu_sqlite_persister_update_subscription,
    .load_subscriptions  = neu_sqlite_persister_load_subscriptions,
    .delete_subscription = neu_sqlite_persister_delete_subscription,
    .store_group         = neu_sqlite_persister_store_group,
    .update_group        = neu_sqlite_persister_update_group,
    .load_groups         = neu_sqlite_persister_load_groups,
    .delete_group        = neu_sqlite_persister_delete_group,
    .store_node_setting  = neu_sqlite_persister_store_node_setting,
    .load_node_setting   = neu_sqlite_persister_load_node_setting,
    .delete_node_setting = neu_sqlite_persister_delete_node_setting,
    .load_users          = neu_sqlite_persister_load_users,
    .store_user          = neu_sqlite_persister_store_user,
    .update_user         = neu_sqlite_persister_update_user,
    .load_user           = neu_sqlite_persister_load_user,
    .delete_user         = neu_sqlite_persister_delete_user,
};

/**
 * @brief 创建并初始化一个 SQLite 持久化实例。
 *
 * 该函数接收一个指向包含数据库模式文件目录路径的字符串，并使用该目录中的
 * 模式文件创建和初始化一个 SQLite 持久化实例。如果成功创建持久化实例，
 * 则返回指向该实例的指针；如果创建失败（例如内存分配失败或数据库打开失败），则返回 NULL。
 *
 * @param schema_dir 指向包含数据库模式文件的目录路径的字符串。
 *
 * @return neu_persister_t* 成功时返回指向新创建的持久化实例的指针，失败时返回 NULL。
 *
 * @note 
 * - 确保 schema_dir 指向有效的目录路径，且该目录中包含所需的 SQL 模式文件。
 * - 内部调用 open_db 函数来打开或创建数据库，并应用 schema_dir 中的模式文件。
 */
neu_persister_t *neu_sqlite_persister_create(const char *schema_dir)
{
    neu_sqlite_persister_t *persister = calloc(1, sizeof(*persister));
    if (NULL == persister) {
        return NULL;
    }

    //定义sqlite实例能执行的sql相关的功能
    persister->vtbl = &g_sqlite_persister_vtbl;

    /**
     * @note
     * C 语言中函数参数传递是值传递, persister->db 就是这个指针的值，也就是它所指向的地址
     * 传递 persister->db 而不是 &persister->db，open_db 函数只能修改传入的指针的副本，
     * 而无法修改 persister->db 本身，这样就无法将打开的数据库连接句柄正确返回给调用者。
     */
    if (0 != open_db(schema_dir, &persister->db)) {
        free(persister);
        return NULL;
    }

    return (neu_persister_t *) persister;
}

/**
 * @brief 获取 SQLite 持久化器的底层数据库句柄。
 *
 * 该函数用于返回 SQLite 持久化器所使用的底层 SQLite 数据库连接句柄。
 * 通过这个句柄，调用者可以直接访问和操作 SQLite 数据库，例如执行自定
 * 义的 SQL 语句等。它提供了一种方式，让调用者在需要时能够绕过持久化器
 * 提供的高级接口，直接与底层数据库进行交互。
 *
 * @param self 指向 `neu_persister_t` 类型的指针，代表当前的持久化器对象。
 *             实际上，该指针指向的是 `neu_sqlite_persister_t` 类型的对象，
 *             函数内部会将其进行类型转换以获取底层数据库句柄。
 *
 * @return 
 * 返回一个 `void *` 类型的指针，该指针指向底层的 SQLite 数据库连接句柄（`sqlite3 *`）。
 *         
 * @warning 
 * -调用者有责任管理通过该句柄进行的数据库操作，包括资源的释放和错误处理。
 * -如果传入的 `self` 指针为 `NULL`，可能会导致未定义行为，因此调用者
 *  需要确保传入有效的指针。
 */
void *neu_sqlite_persister_native_handle(neu_persister_t *self)
{
    return ((neu_sqlite_persister_t *) self)->db;
}

/**
 * @brief 销毁 SQLite 持久化器对象。
 *
 * 该函数用于释放 SQLite 持久化器对象所占用的资源，包括关闭与之关联的 SQLite 
 * 数据库连接，并释放持久化器对象本身占用的内存。通常在不再需要使用该持久化器时
 * 调用此函数，以避免资源泄漏。
 *
 * @param self 指向 `neu_persister_t` 类型的指针，代表要销毁的持久化器对象。
 *             该指针实际上指向的是 `neu_sqlite_persister_t` 类型的对象，
 *             函数内部会进行类型转换以访问其成员。
 *
 * @note 调用此函数后，传入的 `self` 指针将不再有效，不应该再对其进行操作。
 * @warning 如果传入的 `self` 指针为 `NULL`，函数将不执行任何操作。
 */
void neu_sqlite_persister_destroy(neu_persister_t *self)
{
    neu_sqlite_persister_t *persister = (neu_sqlite_persister_t *) self;
    if (persister) {
        // 关闭 SQLite 数据库连接
        sqlite3_close(persister->db);

        // 释放持久化器对象占用的内存
        free(persister);
    }
}

/**
 * @brief 将节点信息持久化到 SQLite 数据库。
 *
 * 此函数用于将给定的节点信息存储到 SQLite 数据库的 `nodes` 表中。
 * 它会执行一条 SQL 插入语句，将节点的名称、类型、状态和插件名称插入到相应的表字段中。
 *
 * @param self 指向 `neu_persister_t` 类型的指针，代表当前的持久化器对象。
 *             实际上，该指针指向的是 `neu_sqlite_persister_t` 类型的对象，
 *             函数内部会将其进行类型转换以获取底层的 SQLite 数据库连接。
 * @param info 指向 `neu_persist_node_info_t` 类型的指针，包含要持久化的节点信息，
 *             如节点名称、类型、状态和插件名称等。
 *
 * @return 执行 SQL 插入操作的返回值。如果操作成功，通常返回 0；
 *         如果出现错误，返回一个非零值，表示操作失败。具体的错误信息可以通过其他方式（如日志）获取。
 *
 * @warning 
 * 如果传入的 `self` 指针为 `NULL`，可能会导致未定义行为，因为函数会尝试访问其指向的对象。
 * 同样，如果 `info` 指针为 `NULL`，SQL 插入语句可能会使用无效的数据，从而导致错误。
 */
int neu_sqlite_persister_store_node(neu_persister_t *        self,
                                    neu_persist_node_info_t *info)
{
    int rv = 0;
    rv     = execute_sql(((neu_sqlite_persister_t *) self)->db,
                     "INSERT INTO nodes (name, type, state, plugin_name) "
                     "VALUES (%Q, %i, %i, %Q)",
                     info->name, info->type, info->state, info->plugin_name);
    return rv;
}

static UT_icd node_info_icd = {
    sizeof(neu_persist_node_info_t),
    NULL,
    NULL,
    (dtor_f *) neu_persist_node_info_fini,
};

/**
 * @brief 从 SQLite 数据库中加载节点信息。
 *
 * 该函数用于从 SQLite 数据库的 `nodes` 表中查询所有节点的信息，并将查询结果
 * 存储到一个 `UT_array` 数组中。每个节点信息由 `neu_persist_node_info_t` 结构体表示，
 * 包含节点的名称、类型、状态和插件名称。
 *
 * @param self 指向 `neu_persister_t` 类型的指针，实际上指向 `neu_sqlite_persister_t` 类型的对象，
 *             用于获取底层的 SQLite 数据库连接。
 * @param node_infos 指向 `UT_array` 指针的指针，用于存储从数据库中加载的节点信息。
 *                   函数会为该 `UT_array` 分配内存，并将查询到的节点信息依次添加到数组中。
 *
 * @return 执行结果的状态码：
 *         - 如果 SQL 语句准备失败，返回 `NEU_ERR_EINTERNAL`，表示内部错误。
 *         - 其他情况下返回 0，即使查询过程中出现警告，也会返回部分或空的结果。
 *
 * @note 该函数会为每个节点的名称和插件名称分配新的内存（使用 `strdup` 函数），
 *       调用者在使用完 `node_infos` 数组后，需要负责释放这些内存。
 * @note 函数内部会自动释放 `sqlite3_stmt` 对象的资源，调用者无需手动释放。
 *
 * @warning 如果传入的 `self` 指针为 `NULL`，会导致未定义行为，因为函数会尝试访问其指向的对象。
 * @warning 如果传入的 `node_infos` 指针为 `NULL`，会导致程序崩溃，因为函数会尝试对其进行解引用操作。
 */
int neu_sqlite_persister_load_nodes(neu_persister_t *self,
                                    UT_array **      node_infos)
{
    neu_sqlite_persister_t *persister = (neu_sqlite_persister_t *) self;

    int           rv    = 0;
    sqlite3_stmt *stmt  = NULL;
    const char *  query = "SELECT name, type, state, plugin_name FROM nodes;";

    utarray_new(*node_infos, &node_info_icd);

    if (SQLITE_OK !=
        sqlite3_prepare_v2(persister->db, query, -1, &stmt, NULL)) {
        nlog_error("prepare `%s` fail: %s", query,
                   sqlite3_errmsg(persister->db));
        utarray_free(*node_infos);
        *node_infos = NULL;
        return NEU_ERR_EINTERNAL;
    }

    int step = sqlite3_step(stmt);
    while (SQLITE_ROW == step) {
        neu_persist_node_info_t info = {};
        char *name = strdup((char *) sqlite3_column_text(stmt, 0));
        if (NULL == name) {
            break;
        }

        char *plugin_name = strdup((char *) sqlite3_column_text(stmt, 3));
        if (NULL == plugin_name) {
            free(name);
            break;
        }

        info.name        = name;
        info.type        = sqlite3_column_int(stmt, 1);
        info.state       = sqlite3_column_int(stmt, 2);
        info.plugin_name = plugin_name;
        utarray_push_back(*node_infos, &info);

        step = sqlite3_step(stmt);
    }

    if (SQLITE_DONE != step) {
        nlog_warn("query `%s` fail: %s", query, sqlite3_errmsg(persister->db));
        // do not set return code, return partial or empty result
    }

    sqlite3_finalize(stmt);
    return rv;
}

int neu_sqlite_persister_delete_node(neu_persister_t *self,
                                     const char *     node_name)
{
    // rely on foreign key constraints to remove settings, groups, tags and
    // subscriptions
    int rv = execute_sql(((neu_sqlite_persister_t *) self)->db,
                         "DELETE FROM nodes WHERE name=%Q;", node_name);
    return rv;
}

int neu_sqlite_persister_update_node(neu_persister_t *self,
                                     const char *     node_name,
                                     const char *     new_name)
{
    return execute_sql(((neu_sqlite_persister_t *) self)->db,
                       "UPDATE nodes SET name=%Q WHERE name=%Q;", new_name,
                       node_name);
}

int neu_sqlite_persister_update_node_state(neu_persister_t *self,
                                           const char *node_name, int state)
{
    return execute_sql(((neu_sqlite_persister_t *) self)->db,
                       "UPDATE nodes SET state=%i WHERE name=%Q;", state,
                       node_name);
}

int neu_sqlite_persister_store_tag(neu_persister_t *    self,
                                   const char *         driver_name,
                                   const char *         group_name,
                                   const neu_datatag_t *tag)
{
    char format_buf[128] = { 0 };
    if (tag->n_format > 0) {
        neu_tag_format_str(tag, format_buf, sizeof(format_buf));
    }

    int rv = execute_sql(
        ((neu_sqlite_persister_t *) self)->db,
        "INSERT INTO tags ("
        " driver_name, group_name, name, address, attribute,"
        " precision, type, decimal, bias, description, value, format"
        ") VALUES (%Q, %Q, %Q, %Q, %i, %i, %i, %lf, %lf, %Q, %Q, %Q)",
        driver_name, group_name, tag->name, tag->address, tag->attribute,
        tag->precision, tag->type, tag->decimal, tag->bias, tag->description,
        "", format_buf);

    return rv;
}

static int put_tags(sqlite3 *db, const char *query, sqlite3_stmt *stmt,
                    const neu_datatag_t *tags, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        const neu_datatag_t *tag             = &tags[i];
        char                 format_buf[128] = { 0 };

        if (tag->n_format > 0) {
            neu_tag_format_str(tag, format_buf, sizeof(format_buf));
        }

        sqlite3_reset(stmt);

        if (SQLITE_OK != sqlite3_bind_text(stmt, 3, tag->name, -1, NULL)) {
            nlog_error("bind `%s` with name=`%s` fail: %s", query, tag->name,
                       sqlite3_errmsg(db));
            return -1;
        }

        if (SQLITE_OK != sqlite3_bind_text(stmt, 4, tag->address, -1, NULL)) {
            nlog_error("bind `%s` with address=`%s` fail: %s", query,
                       tag->address, sqlite3_errmsg(db));
            return -1;
        }

        if (SQLITE_OK != sqlite3_bind_int(stmt, 5, tag->attribute)) {
            nlog_error("bind `%s` with attribute=`%i` fail: %s", query,
                       tag->attribute, sqlite3_errmsg(db));
            return -1;
        }

        if (SQLITE_OK != sqlite3_bind_int(stmt, 6, tag->precision)) {
            nlog_error("bind `%s` with precision=`%i` fail: %s", query,
                       tag->precision, sqlite3_errmsg(db));
            return -1;
        }

        if (SQLITE_OK != sqlite3_bind_int(stmt, 7, tag->type)) {
            nlog_error("bind `%s` with type=`%i` fail: %s", query, tag->type,
                       sqlite3_errmsg(db));
            return -1;
        }

        if (SQLITE_OK != sqlite3_bind_double(stmt, 8, tag->decimal)) {
            nlog_error("bind `%s` with decimal=`%f` fail: %s", query,
                       tag->decimal, sqlite3_errmsg(db));
            return -1;
        }

        if (SQLITE_OK != sqlite3_bind_double(stmt, 9, tag->bias)) {
            nlog_error("bind `%s` with bias=`%f` fail: %s", query, tag->bias,
                       sqlite3_errmsg(db));
            return -1;
        }

        if (SQLITE_OK !=
            sqlite3_bind_text(stmt, 10, tag->description, -1, NULL)) {
            nlog_error("bind `%s` with description=`%s` fail: %s", query,
                       tag->description, sqlite3_errmsg(db));
            return -1;
        }

        if (SQLITE_OK != sqlite3_bind_null(stmt, 11)) {
            nlog_error("bind `%s` with value=null fail: %s", query,
                       sqlite3_errmsg(db));
            return -1;
        }

        if (SQLITE_OK != sqlite3_bind_text(stmt, 12, format_buf, -1, NULL)) {
            nlog_error("bind `%s` with format=`%s` fail: %s", query, format_buf,
                       sqlite3_errmsg(db));
            return -1;
        }

        if (SQLITE_DONE != sqlite3_step(stmt)) {
            nlog_error("sqlite3_step fail: %s", sqlite3_errmsg(db));
            return -1;
        }
    }

    return 0;
}

int neu_sqlite_persister_store_tags(neu_persister_t *    self,
                                    const char *         driver_name,
                                    const char *         group_name,
                                    const neu_datatag_t *tags, size_t n)
{
    neu_sqlite_persister_t *persister = (neu_sqlite_persister_t *) self;

    sqlite3_stmt *stmt = NULL;
    const char *  query =
        "INSERT INTO tags ("
        " driver_name, group_name, name, address, attribute,"
        " precision, type, decimal, bias, description, value, format"
        ") VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12)";

    if (SQLITE_OK != sqlite3_exec(persister->db, "BEGIN", NULL, NULL, NULL)) {
        nlog_error("begin transaction fail: %s", sqlite3_errmsg(persister->db));
        return NEU_ERR_EINTERNAL;
    }

    if (SQLITE_OK !=
        sqlite3_prepare_v2(persister->db, query, -1, &stmt, NULL)) {
        nlog_error("prepare `%s` fail: %s", query,
                   sqlite3_errmsg(persister->db));
        goto error;
    }

    if (SQLITE_OK != sqlite3_bind_text(stmt, 1, driver_name, -1, NULL)) {
        nlog_error("bind `%s` with driver_name=`%s` fail: %s", query,
                   driver_name, sqlite3_errmsg(persister->db));
        goto error;
    }

    if (SQLITE_OK != sqlite3_bind_text(stmt, 2, group_name, -1, NULL)) {
        nlog_error("bind `%s` with group_name=`%s` fail: %s", query, group_name,
                   sqlite3_errmsg(persister->db));
        goto error;
    }

    if (0 != put_tags(persister->db, query, stmt, tags, n)) {
        goto error;
    }

    if (SQLITE_OK != sqlite3_exec(persister->db, "COMMIT", NULL, NULL, NULL)) {
        nlog_error("commit transaction fail: %s",
                   sqlite3_errmsg(persister->db));
        goto error;
    }

    sqlite3_finalize(stmt);
    return 0;

error:
    nlog_warn("rollback transaction");
    sqlite3_exec(persister->db, "ROLLBACK", NULL, NULL, NULL);
    sqlite3_finalize(stmt);
    return NEU_ERR_EINTERNAL;
}

static int collect_tag_info(sqlite3_stmt *stmt, UT_array **tags)
{
    int step = sqlite3_step(stmt);
    while (SQLITE_ROW == step) {
        const char *format = (const char *) sqlite3_column_text(stmt, 9);

        neu_datatag_t tag = {
            .name        = (char *) sqlite3_column_text(stmt, 0),
            .address     = (char *) sqlite3_column_text(stmt, 1),
            .attribute   = sqlite3_column_int(stmt, 2),
            .precision   = sqlite3_column_int64(stmt, 3),
            .type        = sqlite3_column_int(stmt, 4),
            .decimal     = sqlite3_column_double(stmt, 5),
            .bias        = sqlite3_column_double(stmt, 6),
            .description = (char *) sqlite3_column_text(stmt, 7),
        };

        tag.n_format = neu_format_from_str(format, tag.format);

        utarray_push_back(*tags, &tag);

        step = sqlite3_step(stmt);
    }

    if (SQLITE_DONE != step) {
        return -1;
    }

    return 0;
}

int neu_sqlite_persister_load_tags(neu_persister_t *self,
                                   const char *     driver_name,
                                   const char *group_name, UT_array **tags)
{
    neu_sqlite_persister_t *persister = (neu_sqlite_persister_t *) self;

    sqlite3_stmt *stmt  = NULL;
    const char *  query = "SELECT name, address, attribute, precision, type, "
                        "decimal, bias, description, value, format "
                        "FROM tags WHERE driver_name=? AND group_name=? "
                        "ORDER BY rowid ASC";

    utarray_new(*tags, neu_tag_get_icd());

    if (SQLITE_OK !=
        sqlite3_prepare_v2(persister->db, query, -1, &stmt, NULL)) {
        nlog_error("prepare `%s` fail: %s", query,
                   sqlite3_errmsg(persister->db));
        goto error;
    }

    if (SQLITE_OK != sqlite3_bind_text(stmt, 1, driver_name, -1, NULL)) {
        nlog_error("bind `%s` with `%s` fail: %s", query, driver_name,
                   sqlite3_errmsg(persister->db));
        goto error;
    }

    if (SQLITE_OK != sqlite3_bind_text(stmt, 2, group_name, -1, NULL)) {
        nlog_error("bind `%s` with `%s` fail: %s", query, group_name,
                   sqlite3_errmsg(persister->db));
        goto error;
    }

    if (0 != collect_tag_info(stmt, tags)) {
        nlog_warn("query `%s` fail: %s", query, sqlite3_errmsg(persister->db));
        // do not set return code, return partial or empty result
    }

    sqlite3_finalize(stmt);
    return 0;

error:
    utarray_free(*tags);
    *tags = NULL;
    return NEU_ERR_EINTERNAL;
}

int neu_sqlite_persister_update_tag(neu_persister_t *    self,
                                    const char *         driver_name,
                                    const char *         group_name,
                                    const neu_datatag_t *tag)
{
    int rv = execute_sql(((neu_sqlite_persister_t *) self)->db,
                         "UPDATE tags SET"
                         " address=%Q, attribute=%i, precision=%i, type=%i,"
                         " decimal=%lf, bias=%lf, description=%Q, value=%Q "
                         "WHERE driver_name=%Q AND group_name=%Q AND name=%Q",
                         tag->address, tag->attribute, tag->precision,
                         tag->type, tag->decimal, tag->bias, tag->description,
                         "", driver_name, group_name, tag->name);
    return rv;
}

int neu_sqlite_persister_update_tag_value(neu_persister_t *    self,
                                          const char *         driver_name,
                                          const char *         group_name,
                                          const neu_datatag_t *tag)
{
    int rv = execute_sql(((neu_sqlite_persister_t *) self)->db,
                         "UPDATE tags SET value=%Q "
                         "WHERE driver_name=%Q AND group_name=%Q AND name=%Q",
                         "", driver_name, group_name, tag->name);
    return rv;
}

int neu_sqlite_persister_delete_tag(neu_persister_t *self,
                                    const char *     driver_name,
                                    const char *     group_name,
                                    const char *     tag_name)
{
    int rv = execute_sql(
        ((neu_sqlite_persister_t *) self)->db,
        "DELETE FROM tags WHERE driver_name=%Q AND group_name=%Q AND name=%Q",
        driver_name, group_name, tag_name);
    return rv;
}

int neu_sqlite_persister_store_subscription(
    neu_persister_t *self, const char *app_name, const char *driver_name,
    const char *group_name, const char *params, const char *static_tags)
{
    return execute_sql(((neu_sqlite_persister_t *) self)->db,
                       "INSERT INTO subscriptions (app_name, driver_name, "
                       "group_name, params, static_tags) "
                       "VALUES (%Q, %Q, %Q, %Q, %Q)",
                       app_name, driver_name, group_name, params, static_tags);
}

int neu_sqlite_persister_update_subscription(
    neu_persister_t *self, const char *app_name, const char *driver_name,
    const char *group_name, const char *params, const char *static_tags)
{
    return execute_sql(((neu_sqlite_persister_t *) self)->db,
                       "UPDATE subscriptions SET params=%Q, static_tags=%Q "
                       "WHERE app_name=%Q AND driver_name=%Q AND group_name=%Q",
                       params, static_tags, app_name, driver_name, group_name);
}

static UT_icd subscription_info_icd = {
    sizeof(neu_persist_subscription_info_t),
    NULL,
    NULL,
    (dtor_f *) neu_persist_subscription_info_fini,
};

int neu_sqlite_persister_load_subscriptions(neu_persister_t *self,
                                            const char *     app_name,
                                            UT_array **      subscription_infos)
{
    neu_sqlite_persister_t *persister = (neu_sqlite_persister_t *) self;

    sqlite3_stmt *stmt  = NULL;
    const char *  query = "SELECT driver_name, group_name, params, static_tags "
                        "FROM subscriptions WHERE app_name=?";

    utarray_new(*subscription_infos, &subscription_info_icd);

    if (SQLITE_OK !=
        sqlite3_prepare_v2(persister->db, query, -1, &stmt, NULL)) {
        nlog_error("prepare `%s` fail: %s", query,
                   sqlite3_errmsg(persister->db));
        goto error;
    }

    if (SQLITE_OK != sqlite3_bind_text(stmt, 1, app_name, -1, NULL)) {
        nlog_error("bind `%s` with `%s` fail: %s", query, app_name,
                   sqlite3_errmsg(persister->db));
        goto error;
    }

    int step = sqlite3_step(stmt);
    while (SQLITE_ROW == step) {
        char *driver_name = strdup((char *) sqlite3_column_text(stmt, 0));
        if (NULL == driver_name) {
            break;
        }

        char *group_name = strdup((char *) sqlite3_column_text(stmt, 1));
        if (NULL == group_name) {
            free(driver_name);
            break;
        }

        char *params = (char *) sqlite3_column_text(stmt, 2);
        // copy if params not NULL
        if (NULL != params && NULL == (params = strdup(params))) {
            free(group_name);
            free(driver_name);
            break;
        }

        char *static_tags = (char *) sqlite3_column_text(stmt, 3);
        // copy if params not NULL
        if (NULL != static_tags &&
            NULL == (static_tags = strdup(static_tags))) {
            free(group_name);
            free(driver_name);
            break;
        }

        neu_persist_subscription_info_t info = {
            .driver_name = driver_name,
            .group_name  = group_name,
            .params      = params,
            .static_tags = static_tags,
        };
        utarray_push_back(*subscription_infos, &info);

        step = sqlite3_step(stmt);
    }

    if (SQLITE_DONE != step) {
        nlog_warn("query `%s` fail: %s", query, sqlite3_errmsg(persister->db));
        // do not set return code, return partial or empty result
    }

    sqlite3_finalize(stmt);
    return 0;

error:
    utarray_free(*subscription_infos);
    *subscription_infos = NULL;
    return NEU_ERR_EINTERNAL;
}

int neu_sqlite_persister_delete_subscription(neu_persister_t *self,
                                             const char *     app_name,
                                             const char *     driver_name,
                                             const char *     group_name)
{
    return execute_sql(((neu_sqlite_persister_t *) self)->db,
                       "DELETE FROM subscriptions WHERE app_name=%Q AND "
                       "driver_name=%Q AND group_name=%Q",
                       app_name, driver_name, group_name);
}

int neu_sqlite_persister_store_group(neu_persister_t *         self,
                                     const char *              driver_name,
                                     neu_persist_group_info_t *group_info,
                                     const char *              context)
{
    return execute_sql(((neu_sqlite_persister_t *) self)->db,
                       "INSERT INTO groups (driver_name, name, interval, "
                       "context) VALUES (%Q, %Q, %u, %Q)",
                       driver_name, group_info->name,
                       (unsigned) group_info->interval, context);
}

int neu_sqlite_persister_update_group(neu_persister_t *         self,
                                      const char *              driver_name,
                                      const char *              group_name,
                                      neu_persist_group_info_t *group_info)
{
    neu_sqlite_persister_t *persister = (neu_sqlite_persister_t *) self;

    int  ret             = -1;
    bool update_name     = (0 != strcmp(group_name, group_info->name));
    bool update_interval = (NEU_GROUP_INTERVAL_LIMIT <= group_info->interval);

    if (update_name && update_interval) {
        ret = execute_sql(persister->db,
                          "UPDATE groups SET name=%Q, interval=%i "
                          "WHERE driver_name=%Q AND name=%Q",
                          group_info->name, group_info->interval, driver_name,
                          group_name);
    } else if (update_name) {
        ret = execute_sql(persister->db,
                          "UPDATE groups SET name=%Q "
                          "WHERE driver_name=%Q AND name=%Q",
                          group_info->name, driver_name, group_name);
    } else if (update_interval) {
        ret = execute_sql(persister->db,
                          "UPDATE groups SET interval=%i "
                          "WHERE driver_name=%Q AND name=%Q",
                          group_info->interval, driver_name, group_name);
    }

    return ret;
}

static UT_icd group_info_icd = {
    sizeof(neu_persist_group_info_t),
    NULL,
    NULL,
    (dtor_f *) neu_persist_group_info_fini,
};

/**
 * @brief 从 SQLite 语句句柄中收集组信息并存储到 UT_array 中。
 *
 * 此函数通过遍历 SQLite 语句执行结果的每一行，将每行中的组信息提取出来，
 * 填充到 `neu_persist_group_info_t` 结构体中，然后将该结构体添加到 `UT_array` 中。
 * 如果在内存分配过程中出现错误（如 `strdup` 失败），则会提前终止遍历。
 * 若最终的执行状态不是 `SQLITE_DONE`，表示执行过程中出现异常，函数将返回 -1。
 *
 * @param stmt 指向 SQLite 语句句柄的指针，用于执行 SQL 查询并获取结果。
 * @param group_infos 指向 `UT_array` 指针的指针，用于存储收集到的组信息。
 * @return 成功收集信息并执行完毕返回 0，否则返回 -1。
 */
static int collect_group_info(sqlite3_stmt *stmt, UT_array **group_infos)
{
    // 获取当前行的状态
    int step = sqlite3_step(stmt);

    // 当前行状态为 SQLITE_ROW 时，表示有数据行
    while (SQLITE_ROW == step) {
        // 存储当前组的信息
        neu_persist_group_info_t info = {};

        // 复制 SQL 结果集中第 0 列（name 字段）的文本数据
        char *name = strdup((char *) sqlite3_column_text(stmt, 0));
        if (NULL == name) {
            break;
        }

        info.name     = name;
        info.interval = sqlite3_column_int(stmt, 1);
        char *context = (char *) sqlite3_column_text(stmt, 2);
        if (NULL != context) {
            info.context = strdup(context);
        } else {
            info.context = NULL;
        }
        utarray_push_back(*group_infos, &info);

        // 获取下一行的状态
        step = sqlite3_step(stmt);
    }

    // 如果最终状态不是 SQLITE_DONE,表示 SQL 执行未正常结束
    if (SQLITE_DONE != step) {
        return -1;
    }

    return 0;
}

/**
 * @brief 从 SQLite 数据库中加载指定驱动程序的组信息。
 *
 * 此函数会根据给定的驱动程序名称，从 SQLite 数据库中查询相关组的信息，
 * 并将结果存储在 `group_infos` 指向的 UT_array 中。
 *
 * @param self 持久化器对象指针，包含数据库连接等信息。
 * @param driver_name 要查询的驱动程序名称。
 * @param group_infos 用于存储查询结果的 UT_array 指针的指针。
 * @return 成功返回 0，失败返回 NEU_ERR_EINTERNAL。
 */
int neu_sqlite_persister_load_groups(neu_persister_t *self,
                                     const char *     driver_name,
                                     UT_array **      group_infos)
{
    neu_sqlite_persister_t *persister = (neu_sqlite_persister_t *) self;

    // 用于存储 SQLite 语句句柄，初始化为 NULL
    sqlite3_stmt *stmt = NULL;
    const char *  query =
        "SELECT name, interval, context FROM groups WHERE driver_name=?";

    /**
     * @brief
     * 
     * 创建一个新的 UT_array 来存储组信息，使用 group_info_icd 作为元素初始化信息
     * 
     * @note
     * - 修改了group_infos指针本身的值，所以传入UT_array **  group_infos
     * - 使用 UT_array *group_infos，因为函数只能修改 group_infos 所指向的
     *   UT_array 的内容
     */
    utarray_new(*group_infos, &group_info_icd);

    // 准备 SQL 语句
    if (SQLITE_OK !=
        sqlite3_prepare_v2(persister->db, query, -1, &stmt, NULL)) {
        nlog_error("prepare `%s` fail: %s", query,
                   sqlite3_errmsg(persister->db));
        goto error;
    }

    // 绑定 driver_name 参数到 SQL 语句的第一个占位符，
    if (SQLITE_OK != sqlite3_bind_text(stmt, 1, driver_name, -1, NULL)) {
        nlog_error("bind `%s` with `%s` fail: %s", query, driver_name,
                   sqlite3_errmsg(persister->db));
        goto error;
    }

    if (0 != collect_group_info(stmt, group_infos)) {
        nlog_warn("query `%s` fail: %s", query, sqlite3_errmsg(persister->db));
        // do not set return code, return partial or empty result
    }

    sqlite3_finalize(stmt);
    return 0;

error:
    utarray_free(*group_infos);
    *group_infos = NULL;
    return NEU_ERR_EINTERNAL;
}

int neu_sqlite_persister_delete_group(neu_persister_t *self,
                                      const char *     driver_name,
                                      const char *     group_name)
{
    // rely on foreign key constraints to delete tags and subscriptions
    int rv = execute_sql(((neu_sqlite_persister_t *) self)->db,
                         "DELETE FROM groups WHERE driver_name=%Q AND name=%Q",
                         driver_name, group_name);
    return rv;
}

int neu_sqlite_persister_store_node_setting(neu_persister_t *self,
                                            const char *     node_name,
                                            const char *     setting)
{
    return execute_sql(
        ((neu_sqlite_persister_t *) self)->db,
        "INSERT OR REPLACE INTO settings (node_name, setting) VALUES (%Q, %Q)",
        node_name, setting);
}

/**
 * @brief 从 SQLite 数据库中加载指定节点的设置信息。
 *
 * 该函数用于从 SQLite 数据库中检索指定节点的设置信息。它首先准备一个 SQL 查询语句，
 * 然后绑定节点名称参数，执行查询，并处理查询结果。如果查询成功且有匹配的记录，
 * 则将设置信息复制到传入的指针所指向的位置。
 *
 * @param self 指向 neu_persister_t 类型的指针，实际指向 neu_sqlite_persister_t 结构体，
 *             该结构体包含了与 SQLite 数据库相关的信息，如数据库连接指针等。
 * @param node_name 指向表示节点名称的字符串的指针，用于在数据库中定位要加载设置信息的节点。
 * @param setting 指向常量字符指针的常量指针，函数会将从数据库中读取到的设置信息的指针存储在这里。
 *                调用者需要确保该指针指向的内存可以存储返回的设置信息，或者在不再需要时进行适当的内存释放。
 * @return 函数的返回值表示操作的结果：
 *         - 如果操作成功，即从数据库中成功读取并设置了设置信息，返回 0。
 *         - 如果在准备 SQL 语句、绑定参数、执行查询或内存分配等过程中出现错误，
 *           则返回 NEU_ERR_EINTERNAL（表示内部错误），并记录相应的错误日志。
 */
int neu_sqlite_persister_load_node_setting(neu_persister_t *  self,
                                           const char *       node_name,
                                           const char **const setting)
{
    neu_sqlite_persister_t *persister = (neu_sqlite_persister_t *) self;

    int           rv    = 0;
    sqlite3_stmt *stmt  = NULL; // 表示 SQLite 数据库的预编译语句对象
    const char *  query = "SELECT setting FROM settings WHERE node_name=?";

    // 预编译 SQL 语句, &stmt 用于接收预编译后的语句对象指针
    if (SQLITE_OK !=
        sqlite3_prepare_v2(persister->db, query, -1, &stmt, NULL)) {
        nlog_error("prepare `%s` with `%s` fail: %s", query, node_name,
                   sqlite3_errmsg(persister->db));
        return NEU_ERR_EINTERNAL;
    }

    // 绑定参数: node_name
    if (SQLITE_OK != sqlite3_bind_text(stmt, 1, node_name, -1, NULL)) {
        nlog_error("bind `%s` with `%s` fail: %s", query, node_name,
                   sqlite3_errmsg(persister->db));
        rv = NEU_ERR_EINTERNAL;
        goto end;
    }
    
    // 执行 SQL 语句
    if (SQLITE_ROW != sqlite3_step(stmt)) {
        nlog_warn("SQL `%s` with `%s` fail: %s", query, node_name,
                  sqlite3_errmsg(persister->db));
        rv = NEU_ERR_EINTERNAL;
        goto end;
    }

    // 提取查询结果
    char *s = strdup((char *) sqlite3_column_text(stmt, 0));
    if (NULL == s) {
        nlog_error("strdup fail");
        rv = NEU_ERR_EINTERNAL;
        goto end;
    }

    *setting = s;

end:
    sqlite3_finalize(stmt);
    return rv;
}

int neu_sqlite_persister_delete_node_setting(neu_persister_t *self,
                                             const char *     node_name)
{
    return execute_sql(((neu_sqlite_persister_t *) self)->db,
                       "DELETE FROM settings WHERE node_name=%Q", node_name);
}

static UT_icd user_info_icd = {
    sizeof(neu_persist_user_info_t),
    NULL,
    NULL,
    (dtor_f *) neu_persist_user_info_fini,
};

static int collect_user_info(sqlite3_stmt *stmt, UT_array **user_infos)
{
    int step = sqlite3_step(stmt);
    while (SQLITE_ROW == step) {
        neu_persist_user_info_t info = {};
        char *name = strdup((char *) sqlite3_column_text(stmt, 0));
        if (NULL == name) {
            break;
        }

        info.name = name;
        info.hash = strdup((char *) sqlite3_column_text(stmt, 1));
        if (NULL == info.hash) {
            break;
        }
        utarray_push_back(*user_infos, &info);

        step = sqlite3_step(stmt);
    }

    if (SQLITE_DONE != step) {
        return -1;
    }

    return 0;
}

int neu_sqlite_persister_load_users(neu_persister_t *self,
                                    UT_array **      user_infos)
{
    neu_sqlite_persister_t *persister = (neu_sqlite_persister_t *) self;

    sqlite3_stmt *stmt  = NULL;
    const char *  query = "SELECT name, password FROM users";

    utarray_new(*user_infos, &user_info_icd);

    if (SQLITE_OK !=
        sqlite3_prepare_v2(persister->db, query, -1, &stmt, NULL)) {
        nlog_error("prepare `%s` fail: %s", query,
                   sqlite3_errmsg(persister->db));
        goto error;
    }

    if (0 != collect_user_info(stmt, user_infos)) {
        nlog_warn("query `%s` fail: %s", query, sqlite3_errmsg(persister->db));
        // do not set return code, return partial or empty result
    }

    sqlite3_finalize(stmt);
    return 0;

error:
    utarray_free(*user_infos);
    *user_infos = NULL;
    return NEU_ERR_EINTERNAL;
}

int neu_sqlite_persister_store_user(neu_persister_t *              self,
                                    const neu_persist_user_info_t *user)
{
    return execute_sql(((neu_sqlite_persister_t *) self)->db,
                       "INSERT INTO users (name, password) VALUES (%Q, %Q)",
                       user->name, user->hash);
}

int neu_sqlite_persister_update_user(neu_persister_t *              self,
                                     const neu_persist_user_info_t *user)
{
    return execute_sql(((neu_sqlite_persister_t *) self)->db,
                       "UPDATE users SET password=%Q WHERE name=%Q", user->hash,
                       user->name);
}

int neu_sqlite_persister_load_user(neu_persister_t *self, const char *user_name,
                                   neu_persist_user_info_t **user_p)
{
    neu_sqlite_persister_t *persister = (neu_sqlite_persister_t *) self;

    neu_persist_user_info_t *user  = NULL;
    sqlite3_stmt *           stmt  = NULL;
    const char *             query = "SELECT password FROM users WHERE name=?";

    if (SQLITE_OK !=
        sqlite3_prepare_v2(persister->db, query, -1, &stmt, NULL)) {
        nlog_error("prepare `%s` with `%s` fail: %s", query, user_name,
                   sqlite3_errmsg(persister->db));
        return NEU_ERR_EINTERNAL;
    }

    if (SQLITE_OK != sqlite3_bind_text(stmt, 1, user_name, -1, NULL)) {
        nlog_error("bind `%s` with `%s` fail: %s", query, user_name,
                   sqlite3_errmsg(persister->db));
        goto error;
    }

    if (SQLITE_ROW != sqlite3_step(stmt)) {
        nlog_warn("SQL `%s` with `%s` fail: %s", query, user_name,
                  sqlite3_errmsg(persister->db));
        goto error;
    }

    user = calloc(1, sizeof(*user));
    if (NULL == user) {
        goto error;
    }

    user->hash = strdup((char *) sqlite3_column_text(stmt, 0));
    if (NULL == user->hash) {
        nlog_error("strdup fail");
        goto error;
    }

    user->name = strdup(user_name);
    if (NULL == user->name) {
        nlog_error("strdup fail");
        goto error;
    }

    *user_p = user;
    sqlite3_finalize(stmt);
    return 0;

error:
    if (user) {
        neu_persist_user_info_fini(user);
        free(user);
    }
    sqlite3_finalize(stmt);
    return NEU_ERR_EINTERNAL;
}

int neu_sqlite_persister_delete_user(neu_persister_t *self,
                                     const char *     user_name)
{
    return execute_sql(((neu_sqlite_persister_t *) self)->db,
                       "DELETE FROM users WHERE name=%Q", user_name);
}
