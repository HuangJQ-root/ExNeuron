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

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "core/manager.h"
#include "utils/log.h"
#include "utils/time.h"

#include "argparse.h"
#include "daemon.h"
#include "version.h"

static bool           exit_flag         = false;
static neu_manager_t *g_manager         = NULL;
zlog_category_t *     neuron            = NULL;
bool                  disable_jwt       = false;
bool                  sub_filter_err    = false;
int                   default_log_level = ZLOG_LEVEL_NOTICE;
char                  host_port[32]     = { 0 };
char                  g_status[32]      = { 0 };
static bool           sig_trigger       = false;

int64_t global_timestamp = 0;

struct {
    struct sockaddr_in addr;
    int                fd;
} g_remote_syslog_ctx;

static void sig_handler(int sig)
{
    nlog_warn("recv sig: %d", sig);

    if (!sig_trigger) { 
        if (sig == SIGINT || sig == SIGTERM) {
            neu_manager_destroy(g_manager);
            neu_persister_destroy();
            zlog_fini();
        }
        sig_trigger = true;
    }
    exit_flag = true;
    exit(-1);
}

static inline char syslog_priority(const char *level)
{
    switch (level[0]) {
    case 'D': // DEBUG
        return '7';
    case 'I': // INFO
        return '6';
    case 'N': // NOTICE
        return '5';
    case 'W': // WARN
        return '4';
    case 'E': // ERROR
        return '3';
    case 'F': // FATAL
        return '2';
    default: // UNKNOWN
        return '1';
    }
}

/**
 * @brief 发送日志消息到远程系统日志服务器。
 *
 * @details
 * 该函数接收一个日志消息结构体，并将其发送到预先配置好的远程系统日志服务器。
 * 在发送之前，它会调整消息的优先级字段以匹配syslog协议的要求。
 *
 * @param msg 指向包含日志消息信息的 zlog_msg_t 结构体的指针。
 *
 * @return int 成功时返回0；理论上此函数不处理任何错误情况，总是返回0。
 *
 * @note 
 * - 确保在调用此函数之前已经通过 config_remote_syslog 正确配置了 g_remote_syslog_ctx。
 * - 此函数依赖于 sendto 函数来发送UDP数据报。
 * - 日志消息的优先级字段由 syslog_priority 函数根据 msg->path 计算得出。
 */
static int remote_syslog(zlog_msg_t *msg)
{
    // 调整日志消息的优先级字段
    msg->buf[1] = syslog_priority(msg->path);

    sendto(g_remote_syslog_ctx.fd, msg->buf, msg->len, 0,
           (const struct sockaddr *) &g_remote_syslog_ctx.addr,
           sizeof(g_remote_syslog_ctx.addr));
    return 0;
}

/**
 * @brief 配置远程系统日志。
 * 
 * @details
 * 该函数用于配置远程系统日志功能，通过指定的主机名或IP地址和端口号建立UDP套接字连接。
 * 如果提供的主机名为无效IP地址，则尝试将其解析为域名。
 * 
 * @param host 远程系统日志服务器的主机名或IP地址。
 * @param port 远程系统日志服务器的端口号。
 * 
 * @return int 成功时返回0；如果创建套接字失败或无法解析主机名，则返回-1。
 * 
 * @note 在使用此函数之前，请确保已正确初始化了 g_remote_syslog_ctx 结构体。
 *       此函数依赖于标准C库中的 socket, inet_addr, inet_pton, gethostbyname 等函数。
 */
static int config_remote_syslog(const char *host, uint16_t port)
{
    // 创建一个用于UDP通信的套接字
    g_remote_syslog_ctx.fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_remote_syslog_ctx.fd < 0) {
        return -1;
    }

    // 设置目标服务器的地址信息
    g_remote_syslog_ctx.addr.sin_family      = AF_INET;           ///< 地址族（IPv4）
    g_remote_syslog_ctx.addr.sin_port        = htons(port);       ///< 端口号，转换为网络字节序
    g_remote_syslog_ctx.addr.sin_addr.s_addr = inet_addr(host);   ///< IP地址

    // 检查是否是有效的IP地址格式，如果不是，则尝试作为主机名解析
    if (0 == inet_pton(AF_INET, host, &g_remote_syslog_ctx.addr.sin_addr)) {
        // 不是一个有效的IP地址，尝试解析为主机名
        struct hostent *he = gethostbyname(host);
        if (NULL == he) {
            return -1;
        }

        // 使用解析得到的第一个IP地址填充结构体
        memcpy(&g_remote_syslog_ctx.addr.sin_addr, he->h_addr_list[0],
               he->h_length);
    }

    // 设置zlog的日志记录回调函数为 remote_syslog
    zlog_set_record("remote_syslog", remote_syslog);
    return 0;
}

static int neuron_run(const neu_cli_args_t *args)
{
    struct rlimit rl = { 0 };
    int           rv = 0;

    // try to enable core dump
    rl.rlim_cur = rl.rlim_max = RLIM_INFINITY;
    if (setrlimit(RLIMIT_CORE, &rl) < 0) {
        nlog_warn("neuron process failed enable core dump, ignore");
    }

    //创建一个 SQLite 持久化实例
    rv = neu_persister_create(args->config_dir);
    assert(rv == 0);

    zlog_notice(neuron, "neuron start, daemon: %d, version: %s (%s %s)",
                args->daemonized, NEURON_VERSION,
                NEURON_GIT_REV NEURON_GIT_DIFF, NEURON_BUILD_DATE);
    
    //创建并初始化一个 neu_manager 实例
    g_manager = neu_manager_create();
    if (g_manager == NULL) {
        nlog_fatal("neuron process failed to create neuron manager, exit!");
        return -1;
    }

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGKILL, sig_handler);

    while (!exit_flag) {
        sleep(1);
    }

    return 0;
}

/**
 * @brief 程序主入口点。
 *
 * 该函数负责初始化应用程序，并根据命令行参数执行不同的操作，如启动或停止守护进程、配置日志等。
 * 它还处理了程序的多实例运行检查、子进程重启逻辑以及错误处理。
 *
 * @param argc 命令行参数的数量。
 * @param argv 命令行参数的数组。
 *
 * @return int 返回程序的退出状态：
 *         - 0 表示成功；
 *         - 非零值表示失败。
 *
 * @note 
 * - 在调用 `neuron_run` 之前，必须确保已经正确初始化了所有必要的组件（如日志、远程syslog）。
 * - 如果启用了守护进程模式 (`daemonized`)，则在初始化之前调用 `daemonize()` 函数。
 * - 通过 `args.stop` 参数可以控制是否尝试停止正在运行的 neuron 进程。
 * - 使用 `fork()` 和 `waitpid()` 实现了子进程的创建和等待机制。
 */
int main(int argc, char *argv[])
{
    int            rv     = 0;
    int            status = 0;
    int            signum = 0;
    pid_t          pid    = 0;
    neu_cli_args_t args   = { 0 };

    global_timestamp = neu_time_ms();
    neu_cli_args_init(&args, argc, argv);

    disable_jwt    = args.disable_auth;
    sub_filter_err = args.sub_filter_err;
    snprintf(host_port, sizeof(host_port), "http://%s:%d", args.ip, args.port);

    if (args.daemonized) {
        // become a daemon, this should be before calling `init`
        daemonize();
    }

    /**
     * @brief 日志系统初始化
     *  log_init_file配置文件定义了日志系统的各种设置，如输出格式、日志级别、输出目标（例如控制台、文件等）以及具体的日志类别规则等信息。
     * 
     * @name 全局设置 [global]
     *  设置所有日志文件的权限。
     * 
     * @name 日志格式 [formats]
     *  simple: 定义一个简单的日志格式
     *      包含日期时间(`%d`)、毫秒(`%ms`)、日志级别(`%V`)、源文件名(`%f`)、行号(`%L`) 和消息(`%m`)，最后换行(`%n`)。
     *  syslog: 定义一个用于远程 syslog 的特殊格式
     *      包含更多的信息如优先级(`<P>`)、日期时间(`%d`)、主机名(`%H`)、进程ID(`%p`)、日志级别(`%v`)、类别(`%c`)、消息(`%m`)
     *      以及源文件名和行号(`%f:%L`)。
     * 
     * @name 日志规则 [rules]
     *  
     */ 
    zlog_init(args.log_init_file);

    if (args.syslog_host && strlen(args.syslog_host) > 0 &&
        0 != config_remote_syslog(args.syslog_host, args.syslog_port)) {
        nlog_fatal("neuron setup remote syslog fail, exit.");
        goto main_end;
    }

    neuron = zlog_get_category("neuron");
    zlog_level_switch(neuron, default_log_level);

    /**
     * @brief 主函数中处理 neuron 守护进程的启动、停止及重启逻辑。
     *
     * 该部分代码首先检查是否有其他 neuron 实例在运行。如果有，并且用户请求停止，则尝试停止该实例；
     * 如果没有其他实例在运行或者用户未请求停止，则根据命令行参数指定的重启次数循环创建子进程。
     * 父进程等待子进程结束，并根据子进程的退出状态决定是否需要重新启动新的子进程。
     */
    if (neuron_already_running()) {
        // 如果命令行参数指定了停止操作
        if (args.stop) {
            // 调用 neuron_stop 函数尝试停止 neuron 进程
            rv = neuron_stop();
            nlog_notice("neuron stop ret=%d", rv);
            if (rv == 0) {
                printf("neuron stop successfully.\n");
            } else {
                printf("neuron stop failed.\n");
            }
        } else {
            nlog_fatal("neuron process already running, exit.");
            rv = -1;
        }
        goto main_end;
    } else {
        // 如果命令行参数指定了停止操作
        if (args.stop) {
            // 设置返回值为 0 表示正常
            rv = 0;
            printf("neuron no running.\n");
            goto main_end;
        }
    }

    /**
     * @brief 循环重启逻辑
     * `fork()` 创建子进程，并根据子进程的退出状态决定是否需要重新启动新的子进程。
     * 父进程会等待每个子进程结束，并根据其退出状态（正常退出或被信号终止）记录相应的日志信息。
     * 根据重启策略和退出状态决定是否需要重新启动新的子进程。
     */
    // for 循环基于用户指定的重启次数来控制
    // 使用 `()` 创建一个新的子进程
    // 如果 `fork()` 返回值小于0，表示创建子进程失败，记录错误日志并退出
    // 如果 `fork()` 返回值等于0，表示当前代码正在子进程中运行，这时使用 `break` 跳出循环，继续执行后续的主逻辑
    // 如果 `fork()` 返回值大于0，表示当前代码仍在父进程中运行，返回值就是新创建的子进程ID

    /**
     * @brief 父进程等待子进程结束，并根据子进程的退出状态决定是否需要重新启动新的子进程。
     *
     * 使用 `waitpid(pid, &status, 0)` 阻塞父进程，直到子进程结束。
     * 如果 `waitpid()` 返回值与 `pid` 不匹配，表示等待子进程失败，记录错误日志并退出。
     */
    // 使用 `waitpid()` 等待子进程结束，并获取状态
    // 使用宏 `WIFEXITED(status)` 检查子进程是否正常退出
    // 如果子进程正常退出，使用 `WEXITSTATUS(status)` 获取子进程的退出状态码
    // 使用宏 `WIFSIGNALED(status)` 检查子进程是否被信号终止
    // 根据子进程的退出状态或终止信号记录相应的日志信息


    // 根据命令行参数指定的重启次数循环
    for (size_t i = 0; i < args.restart; ++i) {
        // 创建子进程
        if ((pid = fork()) < 0) { //失败
            nlog_error("cannot fork neuron daemon");
            goto main_end;
        } else if (pid == 0) {  // 如果是在子进程中
            break;              // 子进程跳出循环，继续执行主逻辑
        }

        // 父进程阻塞等待子进程结束
        if (pid != waitpid(pid, &status, 0)) {
            nlog_error("cannot wait for neuron daemon");
            goto main_end;
        }

        // 检查子进程是否正常退出
        if (WIFEXITED(status)) {
            status = WEXITSTATUS(status);
            nlog_error("detect neuron daemon exit with status:%d", status);
            // 根据重启策略和退出状态决定是否继续
            if (NEU_RESTART_ONFAILURE == args.restart && 0 == status) {
                goto main_end;
            }
        } else if (WIFSIGNALED(status)) {
            signum = WTERMSIG(status);
            nlog_notice("detect neuron daemon term with signal:%d", signum);
        }
    }

    //启动
    rv = neuron_run(&args);

main_end:
    neu_cli_args_fini(&args);
    zlog_fini();
    return rv;
}
