#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>

// ANSI 颜色转义字符定义
#define ANSI_COLOR_RED "\x1b[31m"
#define ANSI_COLOR_GREEN "\x1b[32m"
#define ANSI_COLOR_YELLOW "\x1b[33m"
#define ANSI_COLOR_BLUE "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN "\x1b[36m"
#define ANSI_COLOR_RESET "\x1b[0m"

// 只有定义了 C2CUDEBUG 宏，下面的日志函数才会生效
#ifdef C2CUDEBUG
#define log_debug(fmt, ...) \
    fprintf(stdout, "[DEBUG] [%s:%d] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)

// INFO 使用蓝色
#define log_info(fmt, ...)                                                        \
    fprintf(stdout, ANSI_COLOR_BLUE "[INFO ] [%s:%d] " fmt ANSI_COLOR_RESET "\n", \
            __FILE__, __LINE__, ##__VA_ARGS__)
#else
#define log_debug(fmt, ...) ((void)0)
#define log_info(fmt, ...) ((void)0)
#endif

// ERROR 使用红色 (始终保留)
#define log_error(fmt, ...)                                                      \
    fprintf(stderr, ANSI_COLOR_RED "[ERROR] [%s:%d] " fmt ANSI_COLOR_RESET "\n", \
            __FILE__, __LINE__, ##__VA_ARGS__)

#endif