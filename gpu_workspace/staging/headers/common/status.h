/** @file status.h
 *  @brief 全工程通用的状态码定义。
 *
 *  该头文件定义仿真系统内部统一使用的返回码。所有公共接口都应返回
 *  @c SimStatus，从而让飞控、环境和工具层使用相同的错误语义。
 */
#ifndef COMMON_STATUS_H
#define COMMON_STATUS_H

/** @brief 仿真系统统一状态码。
 *
 *  约定：
 *  - @c SIM_OK 表示操作成功。
 *  - 其余状态表示不同类别的失败，调用方应按错误类型处理。
 */
typedef enum SimStatus {
    SIM_OK = 0,
    SIM_ERR_INVALID_ARG,
    SIM_ERR_OUT_OF_RANGE,
    SIM_ERR_BAD_PACKET,
    SIM_ERR_TIMEOUT,
    SIM_ERR_CONFIG,
    SIM_ERR_NUMERIC,
    SIM_ERR_IO,
    SIM_ERR_INTERNAL
} SimStatus;

/** @brief 将状态码转换为可读字符串。
 *
 *  @param status 需要转换的状态码。
 *  @return 对应的静态字符串，调用方无需释放。
 */
const char *sim_status_to_string(SimStatus status);

#endif
