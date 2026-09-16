/** @file actuator_model.c
 *  @brief 执行机构一阶动态、位置限幅和速率限幅实现。
 */
#include "env/actuator_model.h"

#include <math.h>

static double clamp_value(double value, double minimum, double maximum)
{
    return value < minimum ? minimum : (value > maximum ? maximum : value);
}

/** @brief 检查执行机构参数和当前状态是否可用于数值推进。 */
SimStatus actuator_model_validate(const ActuatorState *actuator)
{
    if (actuator == 0) {
        return SIM_ERR_INVALID_ARG;
    }
    if (!isfinite(actuator->cmd) || !isfinite(actuator->pos) ||
        !isfinite(actuator->rate) || !isfinite(actuator->pos_min) ||
        !isfinite(actuator->pos_max) || !isfinite(actuator->rate_limit) ||
        !isfinite(actuator->time_constant) ||
        actuator->pos_min > actuator->pos_max ||
        actuator->rate_limit < 0.0 || actuator->time_constant <= 0.0) {
        return SIM_ERR_OUT_OF_RANGE;
    }
    return SIM_OK;
}

/** @brief 执行一次带位置和速率限幅的一阶离散更新。 */
SimStatus actuator_model_step(ActuatorState *actuator, double command, double dt)
{
    double requested_rate;
    double next_position;
    SimStatus status;

    if (actuator == 0 || !isfinite(command) || !isfinite(dt)) {
        return SIM_ERR_INVALID_ARG;
    }
    status = actuator_model_validate(actuator);
    if (status != SIM_OK) {
        return status;
    }
    if (dt <= 0.0) {
        return SIM_ERR_OUT_OF_RANGE;
    }
    actuator->cmd = clamp_value(command, actuator->pos_min, actuator->pos_max);
    if ((actuator->fault_flags & ACTUATOR_FAULT_STUCK) != 0u) {
        actuator->rate = 0.0;
        return SIM_OK;
    }
    requested_rate = (actuator->cmd - actuator->pos) / actuator->time_constant;
    requested_rate = clamp_value(
        requested_rate,
        -actuator->rate_limit,
        actuator->rate_limit);
    next_position = clamp_value(
        actuator->pos + (requested_rate * dt),
        actuator->pos_min,
        actuator->pos_max);
    actuator->rate = (next_position - actuator->pos) / dt;
    actuator->pos = next_position;
    return SIM_OK;
}
