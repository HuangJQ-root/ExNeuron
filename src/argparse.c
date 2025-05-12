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

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <zlog.h>

#include "argparse.h"
#include "persist/persist.h"
#include "utils/log.h"
#include "version.h"
#include "json/json.h"
#include "json/neu_json_param.h"

#define OPTIONAL_ARGUMENT_IS_PRESENT                             \
    ((optarg == NULL && optind < argc && argv[optind][0] != '-') \
         ? (bool) (optarg = argv[optind++])                      \
         : (optarg != NULL))

#define STRDUP(var, str)                                                       \
    do {                                                                       \
        (var) = strdup(str);                                                   \
        if (NULL == (var)) {                                                   \
            fprintf(                                                           \
                stderr,                                                        \
                "Neuron argument parser fail strdup string: %s, reason: %s\n", \
                (str), strerror(errno));                                       \
            goto quit;                                                         \
        }                                                                      \
    } while (0)

const char *g_config_dir = NULL;
const char *g_plugin_dir = NULL;

// clang-format off
const char *usage_text =
"USAGE:\n"
"    neuron [OPTIONS]\n\n"
"OPTIONS:\n"
"    -d, --daemon         run as daemon process\n"
"    -h, --help           show this help message\n"
"    stop                 stop running neuron\n"
"    --log                log to the stdout\n"
"    --log_level <LEVEL>  default log level(DEBUG,NOTICE)\n"
"    --reset-password     reset dashboard to use default password\n"
"    --restart <POLICY>   restart policy to apply when neuron daemon terminates,\n"
"                           - never,      never restart (default)\n"
"                           - always,     always restart\n"
"                           - on-failure, restart only if failure\n"
"                           - NUMBER,     restart max NUMBER of times\n"
"    --version            print version information\n"
"    --disable_auth       disable http api auth\n"
"    --config_file <PATH> startup parameter configuration file\n"
"    --config_dir <DIR>   directory from which neuron reads configuration\n"
"    --plugin_dir <DIR>   directory from which neuron loads plugin lib files\n"
"    --syslog_host <HOST> syslog server host to which neuron will send logs\n"
"    --syslog_port <PORT> syslog server port (default 541 if not provided)\n"
"    --sub_filter_error The subscribe attribute only detects the last read value and does not report any error tags\n"
"\n";
// clang-format on

static inline void usage()
{
    fprintf(stderr, "%s", usage_text);
}

static inline void version()
{
    printf("Neuron %s (%s %s)\n", NEURON_VERSION,
           NEURON_GIT_REV NEURON_GIT_DIFF, NEURON_BUILD_DATE);
}

static inline int reset_password()
{
    neu_persist_user_info_t info = {
        .name = "admin",
        .hash =
            "$5$PwFeXpBBIBZuZdZl$fP8fFPWCLoaWcnVXVSR.3Xi8TEqCvX92gjhowNNn6S4",
    };

    int rv = neu_persister_create(g_config_dir);
    if (0 != rv) {
        return rv;
    }

    rv = neu_persister_update_user(&info);
    return rv;
}

static inline size_t parse_restart_policy(const char *s, size_t *out)
{
    if (0 == strcmp(s, "always")) {
        *out = NEU_RESTART_ALWAYS;
    } else if (0 == strcmp(s, "never")) {
        *out = NEU_RESTART_NEVER;
    } else if (0 == strcmp(s, "on-failure")) {
        *out = NEU_RESTART_ONFAILURE;
    } else {
        errno         = 0;
        char *    end = NULL;
        uintmax_t n   = strtoumax(s, &end, 0);
        // the entire string should be a number within range
        if (0 != errno || '\0' == *s || '\0' != *end ||
            n > NEU_RESTART_ALWAYS) {
            return -1;
        }
        *out = n;
    }

    return 0;
}

static inline bool file_exists(const char *const path)
{
    struct stat buf = { 0 };
    return -1 != stat(path, &buf);
}

/**
 * @brief 加载特定的命令行参数。
 *
 * 如果第一个命令行参数是 "stop"，则将 args->stop 设置为 true。
 * 这个函数通常用于快速响应停止请求而不进行完整的参数解析。
 *
 * @param argc 命令行参数的数量（包括程序名）。
 * @param argv 命令行参数数组。
 * @param args 指向 neu_cli_args_t 结构体的指针，用于存储解析后的参数。
 * @return 返回 0 表示成功，非零值表示错误。
 */
static inline int load_spec_arg(int argc, char *argv[], neu_cli_args_t *args)
{
    int ret = 0;
    if (argc > 1 && strcmp(argv[1], "stop") == 0) {
        args->stop = true;
    }
    return ret;
}

/**
 * @brief      加载环境配置参数到命令行参数结构体
 * @details    该函数从系统环境变量中读取配置参数，用于初始化 `neu_cli_args_t` 结构体及输出路径指针。
 *             支持解析环境变量以设置守护进程模式、日志级别、配置目录等参数，具备错误处理和内存管理功能。
 * @param[in,out] args          指向 neu_cli_args_t 结构体的指针
 *                             - [输入]：结构体初始值（如默认日志级别、默认目录路径）
 *                             - [输出]：填充从环境变量解析后的配置参数
 * @param[out]    log_level_out 指向 char* 的指针，存储解析出的日志级别字符串
 *                             - 若环境变量存在，通过 strdup 分配内存，需调用者释放
 *                             - 若环境变量不存在，保持原有值（需确保调用前已正确初始化）
 * @param[out]    config_dir_out 指向 char* 的指针，存储解析出的配置目录路径
 *                             - 内存管理规则与 log_level_out 一致
 * @param[out]    plugin_dir_out 指向 char* 的指针，存储解析出的插件目录路径
 *                             - 内存管理规则与 log_level_out 一致
 * @return       0  成功解析所有有效环境变量
 *              -1  解析过程中出现错误（如环境变量格式非法、内存分配失败）
 * @note         1. 环境变量优先级高于默认配置，未设置的变量将保留结构体默认值
 *               2. 支持的环境变量列表：
 *                  - NEU_ENV_DAEMON: 守护进程模式（"1"启用，"0"禁用）
 *                  - NEU_ENV_LOG: 开发日志功能（"1"启用，"0"禁用）
 *                  - NEU_ENV_LOG_LEVEL: 日志级别字符串（如"debug"|"info"）
 *                  - NEU_ENV_CONFIG_DIR: 配置文件目录路径
 *               3. 如何设置环境变量
 *                  - 通过 shell 设置环境变量: export NEURON_DAEMON=1
 *               4. 错误处理：
 *                  - 非法环境变量值将输出错误信息到标准输出
 *                  - 内存分配失败时（如 strdup 失败）将提前返回 -1
 */
static inline int load_env(neu_cli_args_t *args, char **log_level_out,
                           char **config_dir_out, char **plugin_dir_out)
{

    int ret = 0;
    do {
        // -------------------- 解析守护进程模式 --------------------
        char *daemon = getenv(NEU_ENV_DAEMON); 
        if (daemon != NULL) {
            if (strcmp(daemon, "1") == 0) {
                args->daemonized = true;
            } else if (strcmp(daemon, "0") == 0) {
                args->daemonized = false;
            } else {
                printf("neuron NEURON_DAEMON setting error!\n");
                ret = -1;
                break;
            }
        }

        // -------------------- 解析开发日志配置 --------------------
        char *log = getenv(NEU_ENV_LOG);
        if (log != NULL) {
            if (strcmp(log, "1") == 0) {
                args->dev_log = true;
            } else if (strcmp(log, "0") == 0) {
                args->dev_log = false;
            } else {
                printf("neuron NEURON_LOG setting error!\n");
                ret = -1;
                break;
            }
        }

        // -------------------- 解析订阅过滤器错误配置 --------------------
        char *sub_filter_e = getenv(NEU_ENV_SUB_FILTER_ERROR);
        if (sub_filter_e != NULL) {
            if (strcmp(sub_filter_e, "1") == 0) {
                args->sub_filter_err = true;
            } else if (strcmp(sub_filter_e, "0") == 0) {
                args->sub_filter_err = false;
            } else {
                printf("neuron NEURON_SUB_FILTER_ERROR setting error!\n");
                ret = -1;
                break;
            }
        }

        // -------------------- 解析日志级别（字符串） --------------------
        char *log_level = getenv(NEU_ENV_LOG_LEVEL);
        if (log_level != NULL) {
            if (*log_level_out != NULL) {
                free(*log_level_out);
            }
            *log_level_out = strdup(log_level);
        }

        // -------------------- 解析重启策略 --------------------
        char *restart = getenv(NEU_ENV_RESTART);
        if (restart != NULL) {
            int t = parse_restart_policy(restart, &args->restart);
            if (t < 0) {
                printf("neuron NEURON_RESTART setting error!\n");
                ret = -1;
                break;
            }
        }

        // -------------------- 解析禁用认证配置 --------------------
        char *disable_auth = getenv(NEU_ENV_DISABLE_AUTH);
        if (disable_auth != NULL) {
            if (strcmp(disable_auth, "1") == 0) {
                args->disable_auth = true;
            } else if (strcmp(disable_auth, "0") == 0) {
                args->disable_auth = false;
            } else {
                printf("neuron NEURON_DISABLE_AUTH setting error!\n");
                ret = -1;
                break;
            }
        }

        // -------------------- 解析配置目录路径 --------------------
        char *config_dir = getenv(NEU_ENV_CONFIG_DIR);
        if (config_dir != NULL) {
            if (*config_dir_out != NULL) {
                free(*config_dir_out);
            }
            *config_dir_out = strdup(config_dir);
        }

        // -------------------- 解析插件目录路径 --------------------
        char *plugin_dir = getenv(NEU_ENV_PLUGIN_DIR);
        if (plugin_dir != NULL) {
            if (*plugin_dir_out != NULL) {
                free(*plugin_dir_out);
            }
            *plugin_dir_out = strdup(plugin_dir);
        }

        // -------------------- 解析系统日志主机 --------------------
        char *syslog_host = getenv(NEU_ENV_SYSLOG_HOST);
        if (NULL != syslog_host) {
            if (NULL != args->syslog_host) {
                free(args->syslog_host);
            }
            args->syslog_host = strdup(syslog_host);
        }

        // -------------------- 解析系统日志端口 --------------------
        char *syslog_port = getenv(NEU_ENV_SYSLOG_PORT);
        if (NULL != syslog_port) {
            int port = atoi(syslog_port);
            if (port < 1 || 65535 < port) {
                printf("neuron %s setting invalid!\n", NEU_ENV_SYSLOG_PORT);
                ret = -1;
                break;
            }
            args->syslog_port = port;
        }
    } while (0);

    return ret;
}

/**
 * @brief 解析命令行参数以确定配置文件路径。
 *
 * @details
 * 该函数通过getopt_long解析命令行参数，查找指定的配置文件选项（如-c或--config_file），
 * 并将找到的配置文件路径存储在传入的指针中。如果未找到相关选项，则不会修改传入的指针。
 *
 * @param argc 命令行参数的数量（包括程序名）。
 * @param argv 命令行参数数组。
 * @param long_options 长选项列表，用于解析配置文件路径。
 * @param opts 短选项字符串，用于解析配置文件路径。
 * @param config_file 指向char*的指针，用于存储解析后的配置文件路径。
 * 
 * @note
 * -optind:指示下一个要处理的命令行参数在 argv 数组中的索引位置。
 *  随着 getopt_long 对命令行参数的解析逐步增加其值，直到所有选项都被处理完毕。
 *  在某些情况下，你可能需要多次解析相同的命令行参数数组，或者你想确保在下一次调
 *  用 getopt_long 时从头开始解析。这时，将 optind 设为0可以达到这个目的
 * 
 * -此处非必要：除非计划在同一个执行流程中再次调用 getopt_long 来解析相同的参数列表。
 *  更常见的做法是让 getopt_long 自动管理 optind 的值，而不需要手动干预。
 */
static inline void resolve_config_file_path(int argc, char *argv[],
                                            struct option *long_options,
                                            char *opts, char **config_file)
{
    int c            = 0;
    int option_index = 0;

    // 遍历所有命令行选项
    while ((c = getopt_long(argc, argv, opts, long_options, &option_index)) !=
           -1) {
        switch (c) {
        case 'c': ///< 如果是短选项'-c'
            if (0 == strcmp("config_file", long_options[option_index].name)) {  ///< 检查是否为长选项'--config_file'
                *config_file = strdup(optarg);
                goto end;
            }
            break;
        default: ///< 对于未知选项，忽略处理
            break;
        }
    }
end:
    optind = 0; ///< 重置getopt库的状态变量
}


/**
 * @brief 加载配置文件并更新CLI参数结构体。
 *
 * @details
 * 该函数尝试根据命令行参数找到配置文件的位置，
 * 如果找不到则使用默认配置文件名。接着读取文件内容，
 * 解析其中的JSON数据，并更新提供的neu_cli_args_t结构体。
 *
 * @param argc 命令行参数的数量（包括程序名）。
 * @param argv 命令行参数数组。
 * @param long_options 长选项列表，用于解析配置文件路径。
 * @param opts 短选项字符串，用于解析配置文件路径。
 * @param args 指向neu_cli_args_t结构体的指针，用于存储解析后的参数。
 * @return 返回0表示成功，非零值表示错误。
 * 
 * @note
 * -加载配置元素有：
 *  disable_auth ip port syslog_host syslog_port sub_filter_error
 */
static inline int load_config_file(int argc, char *argv[],
                                   struct option *long_options, char *opts,
                                   neu_cli_args_t *args)
{
    char *config_file = NULL; // 配置文件路径
    int   ret         = -1;   // 返回状态码
    int   fd          = -1;   // 文件描述符
    void *root        = NULL; // JSON根节点

    // 定义要解析的JSON元素
    neu_json_elem_t elems[] = {
        { .name = "disable_auth", .t = NEU_JSON_INT },
        { .name = "ip", .t = NEU_JSON_STR },
        { .name = "port", .t = NEU_JSON_INT },
        { .name = "syslog_host", .t = NEU_JSON_STR },
        { .name = "syslog_port", .t = NEU_JSON_INT },
        { .name = "sub_filter_error", .t = NEU_JSON_INT },
    };

    // 解析命令行参数获取配置文件路径
    resolve_config_file_path(argc, argv, long_options, opts, &config_file);

    do {
        if (!config_file) {
            config_file = strdup(NEURON_CONFIG_FNAME);  // 使用默认配置文件名
        }

        if (!file_exists(config_file)) {
            printf("configuration file `%s` not exists\n", config_file);
            nlog_error("configuration file `%s` not exists\n", config_file);
            break;
        }

        char buf[512] = { 0 };
        fd            = open(config_file, O_RDONLY);
        if (fd < 0) {
            printf("cannot open %s reason: %s\n", config_file, strerror(errno));
            break;
        }
        int size = read(fd, buf, sizeof(buf) - 1);
        if (size <= 0) {
            printf("cannot read %s reason: %s\n", config_file, strerror(errno));
            break;
        }

        root = neu_json_decode_new(buf); // 解析JSON数据
        if (root == NULL) {
            printf("config file %s foramt error!\n", config_file);
            nlog_error("config file %s foramt error!\n", config_file);
            break;
        }

        // 解析具体的JSON元素并更新参数
        if (neu_json_decode_by_json(root, NEU_JSON_ELEM_SIZE(elems), elems) ==
            0) {
            if (elems[0].v.val_int == 1) { //解析 disable_auth字段值
                args->disable_auth = true;
            } else if (elems[0].v.val_int == 0) {
                args->disable_auth = false;
            } else {
                printf("config file %s disable_auth setting error!\n",
                       config_file);
                break;
            }

            if (elems[1].v.val_str != NULL) { //解析ip字段值
                args->ip = strdup(elems[1].v.val_str);
            } else {
                printf("config file %s ip setting error!\n", config_file);
                break;
            }

            if (elems[2].v.val_int > 0) { //解析port字段值
                args->port = elems[2].v.val_int;
            } else {
                printf("config file %s port setting error! must greater than "
                       "0\n",
                       config_file);
                break;
            }

            if (elems[3].v.val_str != NULL) {
                if (strlen(elems[3].v.val_str) > 0) {
                    args->syslog_host = strdup(elems[3].v.val_str);
                }
            } else {
                printf("config file %s syslog_host setting error!\n",
                       config_file);
                break;
            }

            if (0 < elems[4].v.val_int && elems[4].v.val_int < 65536) {
                args->syslog_port = elems[4].v.val_int;
            } else {
                printf("config file %s syslog_port setting invalid!\n",
                       config_file);
                break;
            }

            if (elems[5].v.val_int == 1) {
                args->sub_filter_err = true;
            } else if (elems[5].v.val_int == 0) {
                args->sub_filter_err = false;
            } else {
                printf("config file %s sub_filter_error setting error!\n",
                       config_file);
                break;
            }

            ret = 0;
        } else {
            printf("config file %s elems error! must had ip, port and "
                   "disable_auth.\n",
                   config_file);
        }

    } while (0);

    if (config_file) {
        free(config_file);
    }

    if (fd) {
        close(fd);
    }

    if (root) {
        neu_json_decode_free(root);
    }

    /**
     * @details
     * 由strdup() 分配内存，必须在使用结束后释放。
     * strdup 返回的指针是动态分配的内存，因此需要调用 free() 释放它。
     * 
     */
    if (elems[1].v.val_str != NULL) {
        free(elems[1].v.val_str); 
    }

    if (elems[3].v.val_str != NULL) {
        free(elems[3].v.val_str);
    }

    return ret;
}

/**
 * @brief 初始化命令行参数结构体。
 *
 * @details
 * 该函数解析命令行参数，并将它们存储到 neu_cli_args_t 结构体中。它支持短选项和长选项，
 * 并处理特定的配置项如守护进程模式、日志级别、重启策略等。如果遇到未知选项或错误，
 * 它会打印帮助信息并退出程序。
 *
 * @param args 指向 neu_cli_args_t 结构体的指针，用于存储解析后的参数。
 * @param argc 命令行参数的数量。
 * @param argv 命令行参数数组。
 * 
 * @note
 * -opts: 短选项字符串，每个字符代表一个可接受的短选项，后面跟冒号表示需要参数。
 * -long_options: 长选项的描述数组，每个元素是一个 option 结构体，包含名称、是否需要参数、标志和对应的短选项字符
 *  最后一个元素必须是全零的 struct option，以表示数组的结束。
 * 
 * @example
 * 启用守护进程模式:./neuron -d
 * 显示帮助信息: ./neuron -h / ./neuron --help
 * 设置日志级别: ./neuron -o DEBUG / ./neuron --log_level=DEBUG
 * 重置密码: ./neuron --reset-password  注：这里不能用-r 
 * 设置重启次数: ./neuron --restart=3
 * 同时使用多个选项： ./neuron -d --log_level=INFO --syslog_host=localhost --syslog_port=514
 */
void neu_cli_args_init(neu_cli_args_t *args, int argc, char *argv[])
{
    int           ret                 = 0;
    bool          reset_password_flag = false; ///< 标记是否需要重置密码
    char *        config_dir          = NULL;  ///< 配置目录路径
    char *        plugin_dir          = NULL;  ///< 插件目录路径
    char *        log_level           = NULL;  ///< 日志级别字符串

    char *        opts                = "dh";
    struct option long_options[]      = {
        { "help", no_argument, NULL, 'h' },
        { "daemon", no_argument, NULL, 'd' },
        { "log", no_argument, NULL, 'l' },
        { "log_level", required_argument, NULL, 'o' },
        { "reset-password", no_argument, NULL, 'r' },
        { "restart", required_argument, NULL, 'r' },
        { "version", no_argument, NULL, 'v' },
        { "disable_auth", no_argument, NULL, 'a' },
        { "config_file", required_argument, NULL, 'c' },
        { "config_dir", required_argument, NULL, 'c' },
        { "plugin_dir", required_argument, NULL, 'p' },
        { "stop", no_argument, NULL, 's' },
        { "syslog_host", required_argument, NULL, 'S' },
        { "syslog_port", required_argument, NULL, 'P' },
        { "sub_filter_error", no_argument, NULL, 'f' },
        { NULL, 0, NULL, 0 }, 
    };

    memset(args, 0, sizeof(*args));

    int c            = 0;
    int option_index = 0;

    // 加载特定参数 --实际不执行代码快
    if (load_spec_arg(argc, argv, args) < 0) {
        ret = 1;
        goto quit;
    }

    // 加载配置文件
    if (load_config_file(argc, argv, long_options, opts, args) < 0) {
        ret = 1;
        goto quit;
    }

    // 加载环境变量
    if (load_env(args, &log_level, &config_dir, &plugin_dir) < 0) {
        ret = 1;
        goto quit;
    }

    // 解析命令行选项
    while ((c = getopt_long(argc, argv, opts, long_options, &option_index)) !=
           -1) {
        switch (c) {
        case 'h':
            usage(); ///< 打印使用说明
            goto quit;
        case 'd':
            args->daemonized = true; ///< 设置为守护进程模式
            break;
        case ':':
            usage();
            goto quit;
        case 'l':
            args->dev_log = true; // 启用开发日志
            break;
        case 'o':
            if (log_level != NULL) {
                free(log_level);
            }
            log_level = strdup(optarg); //设置日至级别
            break;
        case 'r':
            if (0 ==
                strcmp("reset-password", long_options[option_index].name)) {
                reset_password_flag = true; // 标记需要重置密码
            } else if (0 != parse_restart_policy(optarg, &args->restart)) {
                fprintf(stderr, "%s: option '--restart' invalid policy: `%s`\n",
                        argv[0], optarg);
                ret = 1;
                goto quit;
            }
            break;
        case 'v':
            version(); // 打印版本信息
            goto quit;
        case 'a':
            args->disable_auth = true;
            break;
        case 'c':
            if (0 == strcmp("config_dir", long_options[option_index].name)) {
                if (config_dir != NULL) {
                    free(config_dir);
                }
                config_dir = strdup(optarg); // 设置配置目录路径
            }
            break;
        case 'p':
            if (plugin_dir != NULL) {
                free(plugin_dir);
            }
            plugin_dir = strdup(optarg);  // 设置插件目录路径
            break;
        case 'P': {
            int syslog_port = atoi(optarg);
            if (syslog_port < 1 || 65535 < syslog_port) {
                fprintf(stderr, "%s: option '--syslog_port' invalid : `%s`\n",
                        argv[0], optarg);
                goto quit;
            }
            args->syslog_port = syslog_port;  // 设置 Syslog 端口号
            break;
        }
        case 'S':
            if (args->syslog_host) {
                free(args->syslog_host);
            }
            args->syslog_host = strdup(optarg); // 设置 Syslog 主机名
            break;
        case 'f':
            args->sub_filter_err = true;  // 设置子过滤器错误标志
            break;
        case '?':
        default:
            usage(); // 打印使用说明
            ret = 1;
            goto quit;
        }
    }

    // 如果没有启用守护进程模式，则忽略重启策略
    if (!args->daemonized && args->restart != NEU_RESTART_NEVER) {
        fprintf(stderr,
                "%s: option '--restart' has no effects without '--daemon'\n",
                argv[0]);
        args->restart = NEU_RESTART_NEVER;
    }

    // 设置默认配置目录
    args->config_dir = config_dir ? config_dir : strdup("./config");
    if (!file_exists(args->config_dir)) {
        fprintf(stderr, "configuration directory `%s` not exists\n",
                args->config_dir);
        ret = 1;
        goto quit;
    }

    // 设置默认插件目录
    args->plugin_dir = plugin_dir ? plugin_dir : strdup("./plugins");
    if (!file_exists(args->plugin_dir)) {
        fprintf(stderr, "plugin directory `%s` not exists\n", args->plugin_dir);
        ret = 1;
        goto quit;
    }

    const char *zlog_conf = args->dev_log ? "dev.conf" : "zlog.conf";
    int         n = 1 + snprintf(NULL, 0, "%s/%s", args->config_dir, zlog_conf);
    char *      log_init_file = malloc(n);
    snprintf(log_init_file, n, "%s/%s", args->config_dir, zlog_conf);
    args->log_init_file = log_init_file;
    if (!file_exists(args->log_init_file)) {
        fprintf(stderr, "log init file `%s` not exists\n", args->log_init_file);
        ret = 1;
        goto quit;
    }

     // 全局变量赋值（非推荐做法）
    g_config_dir = args->config_dir;
    g_plugin_dir = args->plugin_dir;

    if (reset_password_flag) {
        ret = reset_password();
        goto quit;
    }

    if (log_level != NULL) {
        if (strcmp(log_level, "DEBUG") == 0) {
            default_log_level = ZLOG_LEVEL_DEBUG;
        }
        if (strcmp(log_level, "NOTICE") == 0) {
            default_log_level = ZLOG_LEVEL_NOTICE;
        }
        free(log_level);
    }

    return;

quit:
    neu_cli_args_fini(args); // 清理资源
    exit(ret);
}

void neu_cli_args_fini(neu_cli_args_t *args)
{
    if (args) {
        free(args->log_init_file);
        free(args->config_dir);
        free(args->plugin_dir);
        free(args->ip);
        free(args->syslog_host);
    }
}
