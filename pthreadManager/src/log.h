#ifndef __PM__LOG_H
#define __PM__LOG_H

/**
 * @brief 日志级别枚举
 *
 * __Pm__LOG_ERROR - 仅输出错误信息（0）
 * __Pm__LOG_INFO  - 输出常规信息和错误（1）
 * __Pm__LOG_TRACE - 输出所有信息，含详细调试（2）
 */
typedef enum {
    __Pm__LOG_ERROR = 0,
    __Pm__LOG_INFO  = 1,
    __Pm__LOG_TRACE = 2,
} __Pm__log_level_t;

/**
 * @brief 打开日志输出
 * @param path  日志文件路径，传 NULL 则输出到控制台
 * @param level 日志级别，高于该级别的消息将被过滤
 */
void __Pm__log_open(const char* path, __Pm__log_level_t level);

/**
 * @brief 关闭日志输出，释放文件资源
 */
void __Pm__log_close(void);

/**
 * @brief 写入一条日志（内部函数，推荐使用宏）
 * @param level 日志级别
 * @param fmt   格式化字符串
 * @param ...   可变参数
 */
void __Pm__log_write(__Pm__log_level_t level, const char* fmt, ...);

#define __Pm__LOG_ERROR(...) __Pm__log_write(__Pm__LOG_ERROR, __VA_ARGS__)
#define __Pm__LOG_INFO(...)  __Pm__log_write(__Pm__LOG_INFO,  __VA_ARGS__)
#define __Pm__LOG_TRACE(...) __Pm__log_write(__Pm__LOG_TRACE, __VA_ARGS__)

#endif /* __PM__LOG_H */
