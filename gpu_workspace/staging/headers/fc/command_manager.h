/** @file command_manager.h
 *  @brief 控制指令管理器。
 *
 *  指令管理器是飞控输出进入协议层前的最后一道约束。它负责命令保持、
 *  加速度范数限幅、加速度变化率限幅，以及把飞控模式和保护状态写入
 *  @c ControlCommand。
 */
#ifndef FC_COMMAND_MANAGER_H
#define FC_COMMAND_MANAGER_H

#include "common/protocol.h"
#include "common/status.h"
#include "fc/autopilot.h"
#include "fc/fc_modes.h"
#include "common/device_qualifiers.h"

/** @brief 命令管理器缓存。 */
typedef struct CommandManager {
    /** @brief 最近一次输出的控制命令。 */
    ControlCommand last_command;
    /** @brief 最近一次输出是否有效。 */
    int has_last_command;
    /** @brief 最大加速度指令范数，单位 m/s^2。 */
    double max_accel_mps2;
    /** @brief 最大加速度变化率，单位 m/s^3。 */
    double max_accel_rate_mps3;
    /** @brief 测量临时失效时允许保持上一命令的时长，单位秒。 */
    double command_hold_s;
} CommandManager;

/** @brief 命令管理器配置。 */
typedef struct CommandManagerConfig {
    double max_accel_mps2;
    double max_accel_rate_mps3;
    double command_hold_s;
} CommandManagerConfig;

/** @brief 初始化命令管理器。 */
C2CUDA_HOST_DEVICE SimStatus command_manager_init(CommandManager *manager, const CommandManagerConfig *config);

/** @brief 构造一条标准控制命令并执行所有输出限制。
 *
 *  @param manager 命令管理器状态，保存上一帧输出。
 *  @param seq 本帧传感器序号。
 *  @param sim_time 本帧仿真时间，单位秒。
 *  @param dt 本帧时间步长，单位秒。
 *  @param mode 当前飞控模式。
 *  @param status_flags 调用方已经产生的保护状态位。
 *  @param request_hold 非零表示输出保持/受控零命令。
 *  @param autopilot_cmd 自动驾驶仪期望命令；保持时可为空。
 *  @param out 输出的协议控制命令。
 *  @param out_status_flags 返回追加后的保护状态位。
 */
C2CUDA_HOST_DEVICE SimStatus command_manager_build(
    CommandManager *manager,
    uint32_t seq,
    double sim_time,
    double dt,
    FcMode mode,
    uint32_t status_flags,
    int request_hold,
    const AutopilotCommand *autopilot_cmd,
    ControlCommand *out,
    uint32_t *out_status_flags);

#endif
