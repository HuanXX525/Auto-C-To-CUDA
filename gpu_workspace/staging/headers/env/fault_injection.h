/** @file fault_injection.h
 *  @brief 环境程序运行期故障注入接口。
 *
 *  故障脚本由 `faults.json` 提供，环境程序在每个固定仿真步按当前仿真时间
 *  计算激活故障，并把结果应用到传感器帧或虚拟执行机构。该模块只保存实例
 *  私有状态，不共享跨实例数据。
 */
#ifndef ENV_FAULT_INJECTION_H
#define ENV_FAULT_INJECTION_H

#include "common/protocol.h"
#include "common/status.h"
#include "common/vec3.h"
#include "common/device_qualifiers.h"
#include "env/actuator_model.h"

#include <stddef.h>
#include <stdint.h>

/* JSON config loading is host-only: the GPU campaign parses faults.json on
   the host and uploads the parsed FaultInjection, so the ConfigTree API is
   excluded from device compilation. The extern "C" guard keeps the C
   linkage when this header is included from C++ translation units (the
   config.c loader is compiled as C). */
#ifndef __CUDACC__
#if defined(__cplusplus)
extern "C" {
#endif
#include "common/config.h"
#if defined(__cplusplus)
}
#endif
#endif

/** @brief 单个实例最多加载的脚本故障数量。 */
#define ENV_MAX_FAULTS 32u
/** @brief 一个仿真步最多报告的故障状态跳变数量。 */
#define ENV_MAX_FAULT_TRANSITIONS ENV_MAX_FAULTS
/** @brief 通信层传感器帧延迟故障允许的最大步数。 */
#define ENV_MAX_COMMUNICATION_DELAY_STEPS 16u

/** @brief 故障作用对象。 */
typedef enum FaultTarget {
    FAULT_TARGET_SENSOR_FRAME = 0,
    FAULT_TARGET_SENSOR_SEEKER = 1,
    FAULT_TARGET_SENSOR_SEEKER_RANGE,
    FAULT_TARGET_SENSOR_SEEKER_LOS_UNIT,
    FAULT_TARGET_SENSOR_SEEKER_LOS_RATE,
    FAULT_TARGET_SENSOR_SEEKER_CLOSING_VELOCITY,
    FAULT_TARGET_SENSOR_IMU_GYRO,
    FAULT_TARGET_SENSOR_ACCEL,
    FAULT_TARGET_SENSOR_SPEED,
    FAULT_TARGET_ACTUATOR_ACCEL_X,
    FAULT_TARGET_ACTUATOR_ACCEL_Y,
    FAULT_TARGET_ACTUATOR_ACCEL_Z
} FaultTarget;

/** @brief 故障动作类型。 */
typedef enum FaultType {
    FAULT_TYPE_BIAS = 1,
    FAULT_TYPE_DROPOUT,
    FAULT_TYPE_FORCE_INVALID,
    FAULT_TYPE_STUCK,
    FAULT_TYPE_SCALE,
    FAULT_TYPE_DRIFT,
    FAULT_TYPE_RAMP_BIAS,
    FAULT_TYPE_HOLD_VALUE,
    FAULT_TYPE_COMMUNICATION_DELAY,
    FAULT_TYPE_COMMUNICATION_REORDER
} FaultType;

/** @brief 单条故障脚本的规范化定义。 */
typedef struct FaultDefinition {
    /** @brief 是否启用该故障。 */
    int enabled;
    /** @brief 故障标识，用于事件日志。 */
    char id[32];
    /** @brief 原始目标字符串，用于诊断输出。 */
    char target_text[64];
    /** @brief 原始类型字符串，用于诊断输出。 */
    char type_text[24];
    /** @brief 解析后的目标枚举。 */
    FaultTarget target;
    /** @brief 解析后的故障类型。 */
    FaultType type;
    /** @brief 故障开始仿真时间，单位秒。 */
    double start_time_s;
    /** @brief 故障持续时间，单位秒。 */
    double duration_s;
    /** @brief 斜坡偏置渐入时间，单位秒；0 表示立即达到全量。 */
    double ramp_in_s;
    /** @brief 斜坡偏置恢复时间，单位秒；0 表示窗口结束后立即恢复。 */
    double recovery_ramp_s;
    /** @brief 通信故障窗口结束后的保持时间，单位秒；0 表示立即恢复。 */
    double recovery_hold_s;
    /** @brief 标量故障参数或漂移率，单位由目标传感器或执行机构决定。 */
    double scalar_value;
    /** @brief 三轴故障参数或漂移率，单位由目标传感器或执行机构决定。 */
    Vec3 vector_value;
    /** @brief 执行机构缩放系数。 */
    double scale;
    /** @brief 通信层帧延迟步数。 */
    unsigned int delay_steps;
    /** @brief 上一仿真步是否处于激活状态。 */
    int was_active;
} FaultDefinition;

/** @brief 一个实例的故障脚本状态。 */
typedef struct FaultInjection {
    /** @brief 是否启用故障注入。 */
    int enabled;
    /** @brief 已加载故障数量。 */
    size_t fault_count;
    /** @brief 固定容量故障定义数组。 */
    FaultDefinition faults[ENV_MAX_FAULTS];
} FaultInjection;

/** @brief 当前仿真步聚合后的故障效果。 */
typedef struct FaultStepEffects {
    /** @brief 需要清除的 SensorFrame 有效位。 */
    uint32_t sensor_valid_clear_mask;
    /** @brief 需要置位的 SensorFrame 故障位。 */
    uint32_t sensor_fault_set_mask;
    /** @brief 导引头距离附加偏置，单位米。 */
    double seeker_range_bias_m;
    /** @brief 非零表示强制导引头距离为卡常值。 */
    int seeker_range_hold_enabled;
    /** @brief 导引头距离卡常值，单位米。 */
    double seeker_range_hold_m;
    /** @brief 导引头 LOS 单位向量附加扰动。 */
    Vec3 seeker_los_unit_bias;
    /** @brief 非零表示强制导引头 LOS 单位向量为卡常值。 */
    int seeker_los_unit_hold_enabled;
    /** @brief 导引头 LOS 单位向量卡常值。 */
    Vec3 seeker_los_unit_hold;
    /** @brief 导引头 LOS 角速度附加偏置，单位 rad/s。 */
    Vec3 seeker_los_rate_bias_radps;
    /** @brief 非零表示强制导引头 LOS 角速度为卡常值。 */
    int seeker_los_rate_hold_enabled;
    /** @brief 导引头 LOS 角速度卡常值，单位 rad/s。 */
    Vec3 seeker_los_rate_hold_radps;
    /** @brief 导引头闭合速度附加偏置，单位 m/s。 */
    double seeker_closing_velocity_bias_mps;
    /** @brief 非零表示强制导引头闭合速度为卡常值。 */
    int seeker_closing_velocity_hold_enabled;
    /** @brief 导引头闭合速度卡常值，单位 m/s。 */
    double seeker_closing_velocity_hold_mps;
    /** @brief IMU 陀螺仪附加偏置，单位 rad/s。 */
    Vec3 gyro_bias_b_radps;
    /** @brief 非零表示强制 IMU 陀螺仪为卡常值。 */
    int gyro_hold_enabled;
    /** @brief IMU 陀螺仪卡常值，单位 rad/s。 */
    Vec3 gyro_hold_b_radps;
    /** @brief 加速度计附加偏置，单位 m/s^2。 */
    Vec3 accel_bias_ecef_mps2;
    /** @brief 非零表示强制加速度计为卡常值。 */
    int accel_hold_enabled;
    /** @brief 加速度计卡常值，单位 m/s^2。 */
    Vec3 accel_hold_ecef_mps2;
    /** @brief 速度计附加偏置，单位 m/s。 */
    Vec3 speed_bias_ecef_mps;
    /** @brief 非零表示强制速度计为卡常值。 */
    int speed_hold_enabled;
    /** @brief 速度计卡常值，单位 m/s。 */
    Vec3 speed_hold_ecef_mps;
    /** @brief 三个虚拟加速度执行机构是否卡滞。 */
    int actuator_stuck[3];
    /** @brief 三个虚拟加速度执行机构命令缩放系数。 */
    double actuator_command_scale[3];
    /** @brief 非零表示本步应通过通信延迟线发送传感器帧。 */
    int communication_delay_enabled;
    /** @brief 通信延迟步数。 */
    unsigned int communication_delay_steps;
    /** @brief 非零表示本步应执行锁步安全的通信乱序/上一帧重放。 */
    int communication_reorder_enabled;
    /** @brief 当前激活故障数量。 */
    size_t active_fault_count;
} FaultStepEffects;

/** @brief 故障开始或结束事件，用于写入事件日志。 */
typedef struct FaultTransition {
    /** @brief 故障标识。 */
    char id[32];
    /** @brief 目标字符串。 */
    char target[64];
    /** @brief 类型字符串。 */
    char type[24];
    /** @brief 非零表示开始，零表示恢复。 */
    int active;
} FaultTransition;

/** @brief 初始化为空故障脚本。 */
C2CUDA_HOST_DEVICE void fault_injection_init_empty(FaultInjection *faults);
#ifndef __CUDACC__
/** @brief 从已加载 JSON 配置解析故障脚本。 */
SimStatus fault_injection_load_config(const ConfigTree *config, FaultInjection *faults);
#endif
/** @brief 计算当前仿真步的故障效果和状态跳变。 */
C2CUDA_HOST_DEVICE SimStatus fault_injection_update(
    FaultInjection *faults,
    double sim_time_s,
    FaultStepEffects *effects,
    FaultTransition *transitions,
    size_t transition_capacity,
    size_t *transition_count);
/** @brief 将故障效果应用到已生成的传感器帧。 */
C2CUDA_HOST_DEVICE void fault_injection_apply_sensor(const FaultStepEffects *effects, SensorFrame *sensor);
/** @brief 将故障效果应用到虚拟执行机构命令和状态。 */
C2CUDA_HOST_DEVICE void fault_injection_apply_actuators(
    const FaultStepEffects *effects,
    ActuatorState actuators[3],
    double commands[3]);

#endif
