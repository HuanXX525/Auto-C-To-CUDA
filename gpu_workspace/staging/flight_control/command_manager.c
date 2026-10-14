/** @file command_manager.c
 *  @brief 飞控输出命令管理器实现。
 */
#include "fc/command_manager.h"

#include "fc/fc_health.h"

#include <math.h>
#include <string.h>

/** @brief 对向量范数执行上限约束。 */
C2CUDA_HOST_DEVICE static Vec3 limit_norm(Vec3 value, double max_norm, uint32_t *status_flags)
{
    const double norm = vec3_norm(value);

    if (isfinite(max_norm) && max_norm >= 0.0 && norm > max_norm && norm > 0.0) {
        if (status_flags != 0) {
            *status_flags |= FC_HEALTH_WARNING_COMMAND_LIMITED;
        }
        return vec3_scale(value, max_norm / norm);
    }
    return value;
}

/** @brief 根据上一条命令执行矢量变化率限制。 */
C2CUDA_HOST_DEVICE static Vec3 limit_rate(
    const CommandManager *manager,
    Vec3 requested,
    double dt,
    uint32_t *status_flags)
{
    Vec3 previous = vec3_make(0.0, 0.0, 0.0);
    Vec3 delta;
    double delta_norm;
    double max_delta;

    if (manager == 0 ||
        !isfinite(manager->max_accel_rate_mps3) ||
        manager->max_accel_rate_mps3 < 0.0 ||
        !isfinite(dt) ||
        dt <= 0.0) {
        return requested;
    }
    if (manager->has_last_command != 0) {
        previous = manager->last_command.accel_cmd_ecef;
    }
    delta = vec3_sub(requested, previous);
    delta_norm = vec3_norm(delta);
    max_delta = manager->max_accel_rate_mps3 * dt;
    if (delta_norm > max_delta && delta_norm > 0.0) {
        if (status_flags != 0) {
            *status_flags |= FC_HEALTH_WARNING_RATE_LIMITED;
        }
        return vec3_add(previous, vec3_scale(delta, max_delta / delta_norm));
    }
    return requested;
}

SimStatus command_manager_init(CommandManager *manager, const CommandManagerConfig *config)
{
    if (manager == 0 || config == 0) {
        return SIM_ERR_INVALID_ARG;
    }
    if (!isfinite(config->max_accel_mps2) ||
        !isfinite(config->max_accel_rate_mps3) ||
        !isfinite(config->command_hold_s) ||
        config->max_accel_mps2 < 0.0 ||
        config->max_accel_rate_mps3 < 0.0 ||
        config->command_hold_s < 0.0) {
        return SIM_ERR_OUT_OF_RANGE;
    }
    (void)memset(manager, 0, sizeof(*manager));
    manager->max_accel_mps2 = config->max_accel_mps2;
    manager->max_accel_rate_mps3 = config->max_accel_rate_mps3;
    manager->command_hold_s = config->command_hold_s;
    manager->last_command.accel_cmd_ecef = vec3_make(0.0, 0.0, 0.0);
    manager->has_last_command = 1;
    return SIM_OK;
}

SimStatus command_manager_build(
    CommandManager *manager,
    uint32_t seq,
    double sim_time,
    double dt,
    FcMode mode,
    uint32_t status_flags,
    int request_hold,
    const AutopilotCommand *autopilot_cmd,
    ControlCommand *out,
    uint32_t *out_status_flags)
{
    ControlCommand command;
    Vec3 requested_accel;
    size_t index;

    if (manager == 0 || out == 0 || out_status_flags == 0) {
        return SIM_ERR_INVALID_ARG;
    }
    if (!isfinite(sim_time) || !isfinite(dt)) {
        return SIM_ERR_NUMERIC;
    }
    (void)memset(&command, 0, sizeof(command));
    command.seq = seq;
    command.sim_time = sim_time;
    command.command_mode = (uint32_t)mode;

    if (request_hold != 0) {
        status_flags |= FC_HEALTH_WARNING_COMMAND_HELD;
        requested_accel = manager->last_command.accel_cmd_ecef;
        if (manager->has_last_command == 0 ||
            sim_time - manager->last_command.sim_time > manager->command_hold_s ||
            sim_time < manager->last_command.sim_time) {
            requested_accel = vec3_make(0.0, 0.0, 0.0);
        }
    } else {
        if (autopilot_cmd == 0 || !vec3_isfinite(autopilot_cmd->accel_cmd_ecef)) {
            return SIM_ERR_NUMERIC;
        }
        if (!vec3_isfinite(autopilot_cmd->attitude_cmd) ||
            !vec3_isfinite(autopilot_cmd->body_rate_cmd)) {
            return SIM_ERR_NUMERIC;
        }
        requested_accel = autopilot_cmd->accel_cmd_ecef;
        command.attitude_cmd = autopilot_cmd->attitude_cmd;
        command.body_rate_cmd = autopilot_cmd->body_rate_cmd;
        for (index = 0u; index < SIM_MAX_ACTUATORS; ++index) {
            command.actuator_cmd[index] = autopilot_cmd->actuator_cmd[index];
            if (!isfinite(command.actuator_cmd[index])) {
                return SIM_ERR_NUMERIC;
            }
        }
    }

    if (!vec3_isfinite(requested_accel)) {
        return SIM_ERR_NUMERIC;
    }
    requested_accel = limit_norm(requested_accel, manager->max_accel_mps2, &status_flags);
    requested_accel = limit_rate(manager, requested_accel, dt, &status_flags);
    command.accel_cmd_ecef = requested_accel;
    command.command_status = status_flags;

    manager->last_command = command;
    manager->has_last_command = 1;
    *out = command;
    *out_status_flags = status_flags;
    return SIM_OK;
}
