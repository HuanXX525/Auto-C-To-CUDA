#ifndef LOG_H
#define LOG_H

/**
 * @brief 日志级别枚举
 *
 * LOG_ERROR - 仅输出错误信息（0）
 * LOG_INFO  - 输出常规信息和错误（1）
 * LOG_TRACE - 输出所有信息，含详细调试（2）
 */
typedef enum {
    LOG_ERROR = 0,
    LOG_INFO  = 1,
    LOG_TRACE = 2,
} log_level_t;

/**
 * @brief 打开日志输出
 * @param path  日志文件路径，传 NULL 则输出到控制台
 * @param level 日志级别，高于该级别的消息将被过滤
 */
void log_open(const char* path, log_level_t level);

/**
 * @brief 关闭日志输出，释放文件资源
 */
void log_close(void);

/**
 * @brief 写入一条日志（内部函数，推荐使用宏）
 * @param level 日志级别
 * @param fmt   格式化字符串
 * @param ...   可变参数
 */
void log_write(log_level_t level, const char* fmt, ...);

#define LOG_ERROR(...) log_write(LOG_ERROR, __VA_ARGS__)
#define LOG_INFO(...)  log_write(LOG_INFO,  __VA_ARGS__)
#define LOG_TRACE(...) log_write(LOG_TRACE, __VA_ARGS__)

#endif
