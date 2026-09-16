/** @file actuator_model.h
 *  @brief 执行机构动态模型。
 *
 *  这里描述舵机或推力矢量执行机构的命令、位置、速度和限幅参数。
 *  环境程序根据该状态推进执行机构响应。
 */
#ifndef ENV_ACTUATOR_MODEL_H
#define ENV_ACTUATOR_MODEL_H

#include "common/status.h"

#include <stdint.h>

/** @brief 执行机构卡滞故障。 */
#define ACTUATOR_FAULT_STUCK UINT32_C(0x00000001)

/** @brief 单个执行机构的连续时间状态。 */
typedef struct ActuatorState {
    /** @brief 目标指令值。 */
    double cmd;
    /** @brief 当前输出位置。 */
    double pos;
    /** @brief 当前输出速度。 */
    double rate;
    /** @brief 最小位置限幅。 */
    double pos_min;
    /** @brief 最大位置限幅。 */
    double pos_max;
    /** @brief 最大速度限幅。 */
    double rate_limit;
    /** @brief 一阶响应时间常数。 */
    double time_constant;
    /** @brief 执行机构故障标志。 */
    uint32_t fault_flags;
} ActuatorState;

/** @brief 校验执行机构状态和限幅参数。 */
SimStatus actuator_model_validate(const ActuatorState *actuator);
/** @brief 按一阶动态、位置限幅和速率限幅推进一个执行机构。 */
SimStatus actuator_model_step(ActuatorState *actuator, double command, double dt);

#endif
