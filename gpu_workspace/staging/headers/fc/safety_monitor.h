/** @file safety_monitor.h
 *  @brief 飞控安全监视状态与单帧输入校验。
 *
 *  安全监视器处理配置阈值、旧帧拒绝、传感器超时、NaN/Inf 拒绝和连续
 *  异常计数。它不直接发 UDP，而是给控制器返回本帧保护动作。
 */
#ifndef FC_SAFETY_MONITOR_H
#define FC_SAFETY_MONITOR_H

#include "common/protocol.h"
#include "common/status.h"
#include "common/device_qualifiers.h"

#include <stdint.h>

/** @brief 安全监视器配置。
 *
 *  @var sensor_timeout_s
 *  允许的最大传感器时间间隔，单位秒。
 *  @var command_hold_s
 *  导引头临时无效时保持上一条命令的最大时长，单位秒。
 */
typedef struct FcSafetyConfig {
    double sensor_timeout_s;
    double command_hold_s;
    int reject_nan;
    int reject_old_seq;
    uint32_t max_consecutive_bad_frames;
} FcSafetyConfig;

/** @brief 安全监视器状态。 */
typedef struct SafetyMonitor {
    /** @brief 当前健康/故障标志。 */
    uint32_t status_flags;
    /** @brief 连续异常帧数量。 */
    uint32_t consecutive_bad_frames;
    /** @brief 运行时安全配置。 */
    FcSafetyConfig config;
} SafetyMonitor;

/** @brief 安全监视器对单个传感器帧的判定结果。 */
typedef struct SafetyAssessment {
    uint32_t status_flags;
    int accept_frame;
    int request_hold;
    int degraded;
    int fault;
} SafetyAssessment;

/** @brief 初始化安全监视器。 */
C2CUDA_HOST_DEVICE SimStatus safety_monitor_init(SafetyMonitor *monitor, const FcSafetyConfig *config);

/** @brief 校验一帧传感器数据并给出保护动作。 */
C2CUDA_HOST_DEVICE SimStatus safety_monitor_check_sensor(
    SafetyMonitor *monitor,
    const SensorFrame *sensor,
    int have_last_seq,
    uint32_t last_seq,
    int have_last_sensor_time,
    double last_sensor_time,
    SafetyAssessment *out);

#endif
