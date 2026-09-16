/** @file logger.h
 *  @brief 最小日志输出封装。
 *
 *  日志模块采用轻量级设计，仅封装标准输出流和最基础的信息级日志，
 *  适合嵌入式风格仿真程序的启动与诊断路径。
 */
#ifndef COMMON_LOGGER_H
#define COMMON_LOGGER_H

#include "common/status.h"

#include <stdio.h>

/** @brief 日志句柄。 */
typedef struct Logger {
    FILE *stream;
} Logger;

/** @brief 将日志器绑定到标准输出。 */
SimStatus logger_open_stdout(Logger *logger);
/** @brief 输出一条信息级日志。 */
SimStatus logger_info(Logger *logger, const char *message);

#endif
