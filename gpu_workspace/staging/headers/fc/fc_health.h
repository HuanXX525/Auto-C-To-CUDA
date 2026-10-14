/** @file fc_health.h
 *  @brief 飞控健康状态、保护动作和命令状态位。
 *
 *  飞控侧所有输入拒绝、降级、限幅和故障动作都会折叠到该健康状态中。
 *  @c warning_flags 表示本帧采取过保护但仍能输出受控指令，@c fault_flags
 *  表示控制链路已经不能继续可信工作。两类标志最终会写入
 *  @c ControlCommand.command_status，供环境程序和日志回放工具判读。
 */
#ifndef FC_FC_HEALTH_H
#define FC_FC_HEALTH_H

#include <stdint.h>
#include "common/device_qualifiers.h"

/** @brief 收到旧序号或重复序号的传感器帧。 */
#define FC_HEALTH_WARNING_OLD_FRAME UINT32_C(0x00000001)
/** @brief 传感器帧时间间隔超过安全配置的超时阈值。 */
#define FC_HEALTH_WARNING_SENSOR_TIMEOUT UINT32_C(0x00000002)
/** @brief 目标测量无效，制导律不允许使用本帧导引头数据。 */
#define FC_HEALTH_WARNING_MEASUREMENT_INVALID UINT32_C(0x00000004)
/** @brief 闭合速度、距离或视线状态不满足比例导引输入条件。 */
#define FC_HEALTH_WARNING_CLOSING_VELOCITY UINT32_C(0x00000008)
/** @brief 指令范数被最大加速度约束限幅。 */
#define FC_HEALTH_WARNING_COMMAND_LIMITED UINT32_C(0x00000010)
/** @brief 指令变化率被最大加速度变化率约束限幅。 */
#define FC_HEALTH_WARNING_RATE_LIMITED UINT32_C(0x00000020)
/** @brief 当前输出为保持指令或受控零指令。 */
#define FC_HEALTH_WARNING_COMMAND_HELD UINT32_C(0x00000040)
/** @brief 飞控输入或内部计算出现非有限数。 */
#define FC_HEALTH_FAULT_NUMERIC UINT32_C(0x80000000)
/** @brief 飞控配置不满足运行范围要求。 */
#define FC_HEALTH_FAULT_CONFIG UINT32_C(0x40000000)

/** @brief 命令状态正常，无保护动作。 */
#define FC_COMMAND_STATUS_OK UINT32_C(0x00000000)

/** @brief 飞控健康状态摘要。
 *
 *  @var fault_flags
 *  不可恢复或需要上层处置的故障位，高位布局。
 *  @var warning_flags
 *  本帧采取过的保护动作或降级动作，低位布局。
 */
typedef struct FcHealth {
    /** @brief 故障标志集合。 */
    uint32_t fault_flags;
    /** @brief 告警标志集合。 */
    uint32_t warning_flags;
} FcHealth;

/** @brief 清空健康状态。
 *
 *  @param health 需要重置的健康对象，调用方持有所有权。
 */
C2CUDA_HOST_DEVICE void fc_health_reset(FcHealth *health);

/** @brief 设置一个或多个告警位。
 *
 *  @param health 飞控健康对象。
 *  @param flags 由 @c FC_HEALTH_WARNING_* 组成的位集合。
 */
C2CUDA_HOST_DEVICE void fc_health_set_warning(FcHealth *health, uint32_t flags);

/** @brief 设置一个或多个故障位。
 *
 *  @param health 飞控健康对象。
 *  @param flags 由 @c FC_HEALTH_FAULT_* 组成的位集合。
 */
C2CUDA_HOST_DEVICE void fc_health_set_fault(FcHealth *health, uint32_t flags);

/** @brief 判断当前健康对象是否包含故障位。 */
C2CUDA_HOST_DEVICE int fc_health_has_fault(const FcHealth *health);

/** @brief 将健康状态折叠为 ControlCommand.command_status 位集合。 */
C2CUDA_HOST_DEVICE uint32_t fc_health_command_status(const FcHealth *health);

#endif
