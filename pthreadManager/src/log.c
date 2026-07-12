#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

static FILE* g_log = NULL;
static log_level_t g_level = LOG_TRACE;

/**
 * @brief 打开日志输出
 *
 * 根据传入的路径决定输出目标：
 *   - path 为 NULL → 输出到控制台（stdout / stderr）
 *   - path 不为 NULL → 写入指定文件
 *
 * @param path  日志文件路径，传 NULL 表示控制台输出
 * @param level 日志过滤级别
 */
void log_open(const char* path, log_level_t level) {
    g_level = level;
    if (path)
        g_log = fopen(path, "w");
}

/**
 * @brief 关闭日志文件（若已打开），恢复静默状态
 */
void log_close(void) {
    if (g_log) {
        fclose(g_log);
        g_log = NULL;
    }
}

/**
 * @brief 写入一条格式化日志
 *
 * 格式：时间 [级别标签] 消息
 * 控制台模式下：
 *   - LOG_ERROR → stderr
 *   - 其他级别 → stdout
 * 文件模式下全部写入文件。
 *
 * @param level 日志级别，高于 g_level 的消息被忽略
 * @param fmt    printf 风格格式化字符串
 * @param ...    可变参数
 */
void log_write(log_level_t level, const char* fmt, ...) {
    if (level > g_level) return;

    FILE* out = g_log ? g_log : (level == LOG_ERROR ? stderr : stdout);

    time_t t = time(NULL);
    struct tm* tm = localtime(&t);
    char buf[20];
    strftime(buf, sizeof(buf), "%H:%M:%S", tm);

    const char* tag = "";
    switch (level) {
        case LOG_ERROR: tag = "[E]"; break;
        case LOG_INFO:  tag = "[I]"; break;
        case LOG_TRACE: tag = "[T]"; break;
    }

    fprintf(out, "%s %s ", buf, tag);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);
    fputc('\n', out);
}
