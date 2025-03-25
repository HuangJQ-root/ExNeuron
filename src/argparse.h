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

#ifndef NEURON_ARGPARSE_H
#define NEURON_ARGPARSE_H

#define NEU_LOG_STDOUT_FNAME "/dev/stdout"

#define NEU_RESTART_NEVER 0
#define NEU_RESTART_ALWAYS ((size_t) -2)
#define NEU_RESTART_ONFAILURE ((size_t) -1)

#define NEU_ENV_DAEMON "NEURON_DAEMON"
#define NEU_ENV_LOG "NEURON_LOG"
#define NEU_ENV_LOG_LEVEL "NEURON_LOG_LEVEL"
#define NEU_ENV_RESTART "NEURON_RESTART"
#define NEU_ENV_DISABLE_AUTH "NEURON_DISABLE_AUTH"
#define NEU_ENV_CONFIG_DIR "NEURON_CONFIG_DIR"
#define NEU_ENV_PLUGIN_DIR "NEURON_PLUGIN_DIR"
#define NEU_ENV_SYSLOG_HOST "NEURON_SYSLOG_HOST"
#define NEU_ENV_SYSLOG_PORT "NEURON_SYSLOG_PORT"
#define NEU_ENV_SUB_FILTER_ERROR "NEURON_SUB_FILTER_ERROR"

#define NEURON_CONFIG_FNAME "./config/neuron.json"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

extern const char *g_config_dir;
extern const char *g_plugin_dir;

/** Neuron command line arguments.
 */
/**
 * @brief 命令行参数结构体，用于存储从命令行解析出的各种参数。
 *
 * 该结构体包含了应用程序运行时所需的配置信息，如是否以守护进程模式运行、日志级别设置、重启策略等。
 */
typedef struct {
    /**
     * @brief 标志位，指示是否以守护进程模式运行。
     *
     * 当程序启动时，如果用户指定了 `-d` 或 `--daemon` 参数，则此标志将被设置为 true。
     */
    bool daemonized;

    /**
     * @brief 开发日志标志，如果设置为 true，则启用开发环境下的日志记录。
     *
     * 此标志可以通过命令行选项 `-l` 来激活。
     */
    bool dev_log;

    /**
     * @brief 重启策略，定义了应用在何种情况下应该自动重启以及如何重启。
     *
     * 这个值可以通过解析 `--restart` 参数得到，并且只有当 `daemonized` 标志也被设置时才有效。
     */
    size_t restart;

    /**
     * @brief 禁用认证标志，如果设置为 true，则禁用应用程序中的认证机制。
     *
     * 此标志可以通过命令行选项 `-a` 或 `--disable_auth` 来激活。
     */
    bool disable_auth;

    /**
     * @brief 指向初始化日志配置文件路径的指针。
     *
     * 根据是否启用了开发日志 (`dev_log`)，选择不同的日志配置文件路径。
     */
    char *log_init_file;

    /**
     * @brief 配置目录路径，指向存放应用程序配置文件的目录。
     *
     * 可以通过命令行选项 `--config_dir` 来指定。
     */
    char *config_dir;

    /**
     * @brief 插件目录路径，指向存放插件的目录。
     *
     * 可以通过命令行选项 `--plugin_dir` 来指定。
     */
    char *plugin_dir;

    /**
     * @brief 停止标志，可能用于指示程序应停止运行。
     *
     * 支持优雅关闭或重启功能。
     */
    bool stop;

    /**
     * @brief IP 地址字符串，可能用于网络通信相关的配置。
     *
     * 此字段没有在提供的代码片段中直接使用，但可以推测它与服务监听地址有关。
     */
    char *ip;

    /**
     * @brief 端口号，用于指定服务监听的端口。
     *
     * 同样，这个字段的具体使用情况未在代码中明确给出。
     */
    int port;

    /**
     * @brief Syslog 主机名或IP地址，用于远程Syslog日志记录。
     *
     * 可以通过 `--syslog_host` 命令行选项来设置。
     */
    char *syslog_host;

    /**
     * @brief Syslog 服务端口号，用于远程Syslog日志记录。
     *
     * 可以通过 `--syslog_port` 命令行选项来设置。
     */
    uint16_t syslog_port;

    /**
     * @brief 子过滤器错误标志，可能用于控制子过滤器错误的行为。
     *
     * 当解析到 `-f` 或 `--sub_filter_error` 命令行选项时，此标志被设置为 true。
     */
    bool sub_filter_err;
} neu_cli_args_t;

/** Parse command line arguments.
 */
void neu_cli_args_init(neu_cli_args_t *args, int argc, char *argv[]);

/** Clean up the command line arguments.
 */
void neu_cli_args_fini(neu_cli_args_t *args);

#ifdef __cplusplus
}
#endif

#endif
