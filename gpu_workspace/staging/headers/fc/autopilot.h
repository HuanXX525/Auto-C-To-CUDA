/** @file autopilot.h
 *  @brief 自动驾驶仪和指令层转换接口。
 *
 *  环境程序当前仍保留加速度级虚拟执行机构指令，以保证闭环稳定回归；
 *  自动驾驶仪模块同时把制导期望加速度转换为姿态、角速度和舵面执行机构
 *  通道。姿态环使用速度坐标系近似，不依赖环境真值姿态，适合作为纯软件
 *  飞控模拟件的第一版可验证控制链路。
 */
#ifndef FC_AUTOPILOT_H
#define FC_AUTOPILOT_H

#include "common/protocol.h"
#include "common/status.h"
#include "fc/guidance_png.h"
#include "fc/navigation.h"
#include "common/device_qualifiers.h"

#include <stdint.h>

/** @brief 自动驾驶仪输出的标准化命令视图。 */
typedef struct AutopilotCommand {
    /** @brief ECEF 坐标系加速度期望，单位 m/s^2。 */
    Vec3 accel_cmd_ecef;
    /** @brief 预留姿态期望，当前为零向量。 */
    Vec3 attitude_cmd;
    /** @brief 预留机体系角速度期望，当前为零向量。 */
    Vec3 body_rate_cmd;
    /** @brief 预留执行机构通道，当前全部置零。 */
    double actuator_cmd[SIM_MAX_ACTUATORS];
} AutopilotCommand;

/** @brief 自动驾驶仪参数。
 *
 *  坐标约定：
 *  - 输入加速度为 ECEF，单位 m/s^2。
 *  - 姿态命令按 roll/pitch/yaw 小角度近似存入 @c attitude_cmd，单位 rad。
 *  - 角速度命令按机体系 p/q/r 存入 @c body_rate_cmd，单位 rad/s。
 *  - @c actuator_cmd[0] 为俯仰舵等效偏角，@c actuator_cmd[1] 为偏航舵等效偏角，
 *    单位 rad；其余通道保留。
 */
typedef struct AutopilotConfig {
    /** @brief 是否生成姿态和角速度命令。 */
    int enable_attitude_loop;
    /** @brief 是否生成执行机构分配命令。 */
    int enable_control_allocation;
    /** @brief 最大姿态命令绝对值，单位 rad。 */
    double max_attitude_cmd_rad;
    /** @brief 最大机体系角速度命令绝对值，单位 rad/s。 */
    double max_body_rate_cmd_radps;
    /** @brief 姿态命令到角速度命令的一阶时间常数，单位 s。 */
    double attitude_time_constant_s;
    /** @brief 陀螺角速度反馈阻尼增益，无量纲。 */
    double gyro_damping_gain;
    /** @brief 单位舵偏可产生的等效侧向加速度，单位 (m/s^2)/rad。 */
    double fin_accel_effectiveness_mps2_per_rad;
    /** @brief 最大舵偏绝对值，单位 rad。 */
    double max_fin_deflection_rad;
} AutopilotConfig;

/** @brief 自动驾驶仪状态。 */
typedef struct Autopilot {
    /** @brief 当前控制参数快照。 */
    AutopilotConfig config;
    uint32_t accepted_count;
    uint32_t rejected_count;
} Autopilot;

/** @brief 初始化自动驾驶仪状态。 */
C2CUDA_HOST_DEVICE SimStatus autopilot_init(Autopilot *autopilot, const AutopilotConfig *config);

/** @brief 将制导输出转换为自动驾驶仪命令。 */
C2CUDA_HOST_DEVICE SimStatus autopilot_update(
    Autopilot *autopilot,
    const NavState *nav,
    const GuidancePngOutput *guidance,
    AutopilotCommand *out);

/** @brief 生成受控零自动驾驶仪命令。 */
C2CUDA_HOST_DEVICE void autopilot_zero_command(AutopilotCommand *out);

#endif
