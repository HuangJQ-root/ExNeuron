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

#include <dirent.h>
#include <pthread.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <unistd.h>

#ifndef NEU_CLIB
#include <gnu/libc-version.h>
#endif

#include "adapter.h"
#include "adapter/adapter_internal.h"
#include "metrics.h"
#include "utils/log.h"
#include "utils/time.h"

/**
 * @brief 全局读写锁，用于保护度量集合的并发访问。
 *
 * 此读写锁用于确保在多线程环境中对全局度量集合进行安全的读写操作。
 * 通过使用读写锁，可以允许多个读者同时访问数据，但写者需要独占访问权限。
 */
pthread_rwlock_t g_metrics_mtx_ = PTHREAD_RWLOCK_INITIALIZER;

/**
 * @brief 全局度量对象，用于存储和管理所有适配器和节点的度量信息。
 *
 * 此全局变量包含整个应用程序中所有适配器和节点的度量信息。
 */
neu_metrics_t    g_metrics_;

/**
 * @brief 全局起始时间戳，记录程序或特定操作的开始时间。
 *
 * 此变量用于存储某个关键点的时间戳（通常以毫秒为单位），
 * 例如程序启动时间或某项操作的开始时间。它常被用于计算经过的时间或进行性能监控。
 * 
 * @note 静态全局变量的作用域仅限于声明它的文件
 */
static uint64_t  g_start_ts_;

/**
 * @brief 查找并填充操作系统的相关信息到全局度量结构体中。
 *
 * 此函数执行一系列命令来获取操作系统发行版名称、内核版本和机器类型，并将这些信息
 * 存储到全局度量结构体 `g_metrics_` 中。如果在执行过程中遇到任何错误（例如命
 * 令执行失败或无输出），则会记录错误日志并返回。
 */
static void find_os_info()
{
    // 定义用于获取操作系统信息的shell命令
    const char *cmd =
        "if [ -f /etc/os-release ]; then . /etc/os-release;"
        "echo $NAME $VERSION_ID; else uname -s; fi; uname -r; uname -m";
    
    // 执行命令并获取文件指针
    FILE *f = popen(cmd, "r");

    if (NULL == f) {
        nlog_error("popen command fail");
        return;
    }

    char buf[64] = {};

    // 读取第一行输出作为发行版名称
    if (NULL == fgets(buf, sizeof(buf), f)) {
        nlog_error("no command output");
        pclose(f);
        return;
    }
    buf[strcspn(buf, "\n")] = 0;
    strncpy(g_metrics_.distro, buf, sizeof(g_metrics_.distro));
    g_metrics_.distro[sizeof(g_metrics_.distro) - 1] = 0;

    // 读取第二行输出作为内核版本
    if (NULL == fgets(buf, sizeof(buf), f)) {
        nlog_error("no command output");
        pclose(f);
        return;
    }
    buf[strcspn(buf, "\n")] = 0;
    strncpy(g_metrics_.kernel, buf, sizeof(g_metrics_.kernel));
    g_metrics_.kernel[sizeof(g_metrics_.kernel) - 1] = 0;

    // 读取第三行输出作为机器类型
    if (NULL == fgets(buf, sizeof(buf), f)) {
        nlog_error("no command output");
        pclose(f);
        return;
    }
    buf[strcspn(buf, "\n")] = 0;
    strncpy(g_metrics_.machine, buf, sizeof(g_metrics_.machine));
    g_metrics_.kernel[sizeof(g_metrics_.machine) - 1] = 0;

    // 关闭管道
    pclose(f);

#ifdef NEU_CLIB
    // 如果定义了NEU_CLIB宏，则使用预定义的C库信息
    strncpy(g_metrics_.clib, NEU_CLIB, sizeof(g_metrics_.clib));
    strncpy(g_metrics_.clib_version, "unknow", sizeof(g_metrics_.clib_version));
#else
    // 否则，默认使用glibc及其版本信息
    strncpy(g_metrics_.clib, "glibc", sizeof(g_metrics_.clib));
    strncpy(g_metrics_.clib_version, gnu_get_libc_version(),
            sizeof(g_metrics_.clib_version));
#endif
}

/**
 * @brief 解析并返回指定列的内存信息（以字节为单位）。
 *
 * 此函数通过执行 shell 命令 `free -b | awk 'NR==2 {print $<col>}'` 来获取系统内存信息的特定列，
 * 并将其转换为字节大小返回。如果命令执行失败或没有输出，则会记录错误日志并返回 0。
 *
 * @param col 要解析的列号（从1开始），例如在 `free` 命令输出中，总内存位于第2列，已用内存位于第3列等。
 * @return 返回指定列的内存大小（以字节为单位）。如果命令执行失败或无输出，则返回 0。
 */
static size_t parse_memory_fields(int col)
{
    FILE * f       = NULL;
    char   buf[64] = {};
    size_t val     = 0;

    sprintf(buf, "free -b | awk 'NR==2 {print $%i}'", col);

    f = popen(buf, "r");
    if (NULL == f) {
        nlog_error("popen command fail");
        return 0;
    }

    if (NULL != fgets(buf, sizeof(buf), f)) {
        val = atoll(buf);
    } else {
        nlog_error("no command output");
    }

    pclose(f);
    return val;
}

static inline size_t memory_total()
{
    return parse_memory_fields(2);
}

static inline size_t memory_used()
{
    return parse_memory_fields(3);
}

static inline size_t neuron_memory_used()
{
    FILE * f       = NULL;
    char   buf[32] = {};
    size_t val     = 0;
    pid_t  pid     = getpid();

    sprintf(buf, "ps -o rss= %ld", (long) pid);

    f = popen(buf, "r");
    if (NULL == f) {
        nlog_error("popen command fail");
        return 0;
    }

    if (NULL != fgets(buf, sizeof(buf), f)) {
        val = atoll(buf);
    } else {
        nlog_error("no command output");
    }

    pclose(f);
    return val * 1024;
}

static inline size_t memory_cache()
{
    return parse_memory_fields(6);
}

static inline int disk_usage(size_t *size_p, size_t *used_p, size_t *avail_p)
{
    struct statvfs buf = {};
    if (0 != statvfs(".", &buf)) {
        return -1;
    }

    *size_p  = (double) buf.f_frsize * buf.f_blocks / (1 << 30);
    *used_p  = (double) buf.f_frsize * (buf.f_blocks - buf.f_bfree) / (1 << 30);
    *avail_p = (double) buf.f_frsize * buf.f_bavail / (1 << 30);
    return 0;
}

static unsigned cpu_usage()
{
    int                ret   = 0;
    unsigned long long user1 = 0, nice1 = 0, sys1 = 0, idle1 = 0, iowait1 = 0,
                       irq1 = 0, softirq1 = 0;
    unsigned long long user2 = 0, nice2 = 0, sys2 = 0, idle2 = 0, iowait2 = 0,
                       irq2 = 0, softirq2 = 0;
    unsigned long long work = 0, total = 0;
    struct timespec    tv = {
        .tv_sec  = 0,
        .tv_nsec = 50000000,
    };
    FILE *f = NULL;

    f = fopen("/proc/stat", "r");
    if (NULL == f) {
        nlog_error("open /proc/stat fail");
        return 0;
    }

    ret = fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu", &user1, &nice1,
                 &sys1, &idle1, &iowait1, &irq1, &softirq1);
    if (7 != ret) {
        fclose(f);
        return 0;
    }

    nanosleep(&tv, NULL);

    rewind(f);
    ret = fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu", &user2, &nice2,
                 &sys2, &idle2, &iowait2, &irq2, &softirq2);
    if (7 != ret) {
        fclose(f);
        return 0;
    }
    fclose(f);

    work  = (user2 - user1) + (nice2 - nice1) + (sys2 - sys1);
    total = work + (idle2 - idle1) + (iowait2 - iowait1) + (irq2 - irq1) +
        (softirq2 - softirq1);

    return (double) work / total * 100.0 * sysconf(_SC_NPROCESSORS_CONF);
}

static bool has_core_dump_in_dir(const char *dir, const char *prefix)
{
    DIR *dp = opendir(dir);
    if (dp == NULL) {
        return false;
    }

    struct dirent *de;
    bool           found = false;
    while ((de = readdir(dp)) != NULL) {
        if (strncmp(prefix, de->d_name, strlen(prefix)) == 0) {
            found = true;
            break;
        }
    }

    closedir(dp);
    return found;
}

static char *get_core_dir(const char *core_pattern)
{
    char *last_slash = strrchr(core_pattern, '/');
    if (last_slash != NULL) {
        static char core_dir[256];
        ptrdiff_t   path_length = last_slash - core_pattern + 1;
        strncpy(core_dir, core_pattern, path_length);
        core_dir[path_length] = '\0';
        return core_dir;
    }
    return NULL;
}

static bool has_core_dumps()
{
    if (has_core_dump_in_dir("core", "core-neuron")) {
        return true;
    }

    FILE *fp = fopen("/proc/sys/kernel/core_pattern", "r");
    if (fp == NULL) {
        return false;
    }

    char core_pattern[256];
    if (fgets(core_pattern, sizeof(core_pattern), fp) == NULL) {
        fclose(fp);
        return false;
    }
    fclose(fp);

    char *core_dir =
        (core_pattern[0] == '|') ? "/var/crash/" : get_core_dir(core_pattern);
    if (core_dir == NULL) {
        return false;
    }

    return has_core_dump_in_dir(core_dir, "core-neuron");
}

static inline void metrics_unregister_entry(const char *name)
{
    neu_metric_entry_t *e = NULL;
    HASH_FIND_STR(g_metrics_.registered_metrics, name, e);
    if (0 == --e->value) {
        HASH_DEL(g_metrics_.registered_metrics, e);
        nlog_notice("del entry:%s", e->name);
        neu_metric_entry_free(e);
    }
}

/**
 * @brief 向度量项列表中添加一个新的度量项。
 *
 * 此函数用于向指定的度量项列表中添加一个新的度量项。如果度量项已存在，则检查其类型和帮助信息是否匹配，若不匹配则记录错误并返回 -1。
 * 如果需要并且初始化值大于 0，则会为滚动计数器类型的度量项分配新的滚动计数器实例。
 * 在操作过程中可能会分配内存，如果内存分配失败，则返回 -1。
 *
 * @param entries 指向度量项列表指针的指针。
 * @param name 度量项的名称。
 * @param help 度量项的帮助信息或描述。
 * @param type 度量项的类型（如计数器、仪表等）。
 * @param init 度量项的初始值。
 * @return 成功时返回 0；如果度量项已存在且类型或帮助信息不匹配，返回 -1；如果内存分配失败，也返回 -1；如果度量项已存在但匹配成功，返回 1。
 */
int neu_metric_entries_add(neu_metric_entry_t **entries, const char *name,
                           const char *help, neu_metric_type_e type,
                           uint64_t init)
{
    neu_metric_entry_t *entry = NULL;

    // 查找是否存在同名的度量项
    HASH_FIND_STR(*entries, name, entry);
    if (NULL != entry) {
        // 如果类型或帮助信息不匹配，则记录错误并返回 -1
        if (entry->type != type || (0 != strcmp(entry->help, help))) {
            nlog_error("metric entry %s(%d, %s) conflicts with (%d, %s)", name,
                       entry->type, entry->help, type, help);
            return -1;
        }

        // 类型和帮助信息匹配，返回 1 表示度量项已存在
        return 1;
    }

    // 分配新的度量项
    entry = calloc(1, sizeof(*entry));
    if (NULL == entry) {
        return -1;
    }

    // 根据度量项类型进行不同的初始化
    if (NEU_METRIC_TYPE_ROLLING_COUNTER == type) {
        // 仅在初始化值大于 0 时为滚动计数器分配内存
        if (init > 0 && NULL == (entry->rcnt = neu_rolling_counter_new(init))) {
            free(entry);
            return -1;
        }
    } else {
        entry->value = init;
    }

    // 初始化度量项的基本属性
    entry->init = init;
    entry->name = name;
    entry->type = type;
    entry->help = help;

    // 将新的度量项添加到列表中
    HASH_ADD_STR(*entries, name, entry);
    return 0;
}

/**
 * @brief 初始化度量指标。
 *
 * 此函数负责初始化全局度量指标系统，包括设置启动时间戳、获取操作系统信息和总内存大小。
 * 使用写锁保护对全局变量的访问，以确保线程安全。
 *
 * 在首次调用时，此函数会：
 * - 设置启动时间戳（`g_start_ts_`）为当前时间（毫秒）。
 * - 获取并记录操作系统信息。
 * - 获取并记录系统的总内存大小（字节）到 `g_metrics_.mem_total_bytes`。
 *
 * 后续调用不会重复执行上述初始化步骤，因为它们依赖于 `g_start_ts_` 是否已设置。
 */
void neu_metrics_init()
{
    pthread_rwlock_wrlock(&g_metrics_mtx_);
    if (0 == g_start_ts_) {
        g_start_ts_ = neu_time_ms();
        find_os_info();
        g_metrics_.mem_total_bytes = memory_total();
    }
    pthread_rwlock_unlock(&g_metrics_mtx_);
}

/**
 * @brief 添加一个节点度量到全局度量集合中。
 *
 * 此函数用于将指定适配器的节点度量信息添加到全局度量集合中。
 * 在进行添加操作时，会使用写锁保护全局度量集合以确保线程安全。
 *
 * @param adapter 指向适配器对象的常量指针，包含要添加的节点度量信息。
 */
void neu_metrics_add_node(const neu_adapter_t *adapter)
{
    pthread_rwlock_wrlock(&g_metrics_mtx_);
    HASH_ADD_STR(g_metrics_.node_metrics, name, adapter->metrics);
    pthread_rwlock_unlock(&g_metrics_mtx_);
}

void neu_metrics_del_node(const neu_adapter_t *adapter)
{
    pthread_rwlock_wrlock(&g_metrics_mtx_);
    HASH_DEL(g_metrics_.node_metrics, adapter->metrics);
    pthread_rwlock_unlock(&g_metrics_mtx_);
}

/**
 * @brief 注册一个新的度量项。
 *
 * 此函数用于注册一个新的度量项，并将其添加到全局注册度量列表中。如果度量项已存在，则增加其引用计数。
 * 在操作过程中使用读写锁 `g_metrics_mtx_` 以确保线程安全。
 *
 * @param name 度量项的名称。
 * @param help 度量项的帮助信息或描述。
 * @param type 度量项的类型（如计数器、仪表等）。
 * @return 成功时返回 0；如果发生错误（如添加失败），则返回 -1。
 */
int neu_metrics_register_entry(const char *name, const char *help,
                               neu_metric_type_e type)
{
    int                 rv = 0;
    neu_metric_entry_t *e  = NULL;

    pthread_rwlock_wrlock(&g_metrics_mtx_);
    // use `value` field as reference counter, initialize to zero
    // and we don't need to allocate rolling counter for register entries
    rv = neu_metric_entries_add(&g_metrics_.registered_metrics, name, help,
                                type, 0);
    if (-1 != rv) {
        HASH_FIND_STR(g_metrics_.registered_metrics, name, e);
        ++e->value;
        rv = 0;
    }
    pthread_rwlock_unlock(&g_metrics_mtx_);
    return rv;
}

void neu_metrics_unregister_entry(const char *name)
{
    pthread_rwlock_wrlock(&g_metrics_mtx_);
    metrics_unregister_entry(name);
    pthread_rwlock_unlock(&g_metrics_mtx_);
}

void neu_metrics_visist(neu_metrics_cb_t cb, void *data)
{
    unsigned cpu       = cpu_usage();
    size_t   mem_used  = neuron_memory_used();
    size_t   mem_cache = memory_cache();
    size_t   disk_size = 0, disk_used = 0, disk_avail = 0;
    disk_usage(&disk_size, &disk_used, &disk_avail);
    bool     core_dumped    = has_core_dumps();
    uint64_t uptime_seconds = (neu_time_ms() - g_start_ts_) / 1000;
    pthread_rwlock_rdlock(&g_metrics_mtx_);
    g_metrics_.cpu_percent          = cpu;
    g_metrics_.cpu_cores            = get_nprocs();
    g_metrics_.mem_used_bytes       = mem_used;
    g_metrics_.mem_cache_bytes      = mem_cache;
    g_metrics_.disk_size_gibibytes  = disk_size;
    g_metrics_.disk_used_gibibytes  = disk_used;
    g_metrics_.disk_avail_gibibytes = disk_avail;
    g_metrics_.core_dumped          = core_dumped;
    g_metrics_.uptime_seconds       = uptime_seconds;

    g_metrics_.north_nodes              = 0;
    g_metrics_.north_running_nodes      = 0;
    g_metrics_.north_disconnected_nodes = 0;
    g_metrics_.south_nodes              = 0;
    g_metrics_.south_running_nodes      = 0;
    g_metrics_.south_disconnected_nodes = 0;

    neu_node_metrics_t *n;
    HASH_LOOP(hh, g_metrics_.node_metrics, n)
    {
        neu_plugin_common_t *common =
            neu_plugin_to_plugin_common(n->adapter->plugin);

        n->adapter->cb_funs.update_metric(n->adapter, NEU_METRIC_RUNNING_STATE,
                                          n->adapter->state, NULL);
        n->adapter->cb_funs.update_metric(n->adapter, NEU_METRIC_LINK_STATE,
                                          common->link_state, NULL);

        if (NEU_NA_TYPE_DRIVER == n->adapter->module->type) {
            ++g_metrics_.south_nodes;
            if (NEU_NODE_RUNNING_STATE_RUNNING == n->adapter->state) {
                ++g_metrics_.south_running_nodes;
            }
            if (NEU_NODE_LINK_STATE_DISCONNECTED == common->link_state) {
                ++g_metrics_.south_disconnected_nodes;
            }
        } else if (NEU_NA_TYPE_APP == n->adapter->module->type) {
            ++g_metrics_.north_nodes;
            if (NEU_NODE_RUNNING_STATE_RUNNING == n->adapter->state) {
                ++g_metrics_.north_running_nodes;
            }
            if (NEU_NODE_LINK_STATE_DISCONNECTED == common->link_state) {
                ++g_metrics_.north_disconnected_nodes;
            }
        }
    }

    cb(&g_metrics_, data);
    pthread_rwlock_unlock(&g_metrics_mtx_);
}

neu_metrics_t *neu_get_global_metrics()
{
    return &g_metrics_;
}