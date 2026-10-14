/** @file sensor_seeker.h
 *  @brief 导引头弹目相对测量、误差、采样和延迟模型。
 */
#ifndef ENV_SENSOR_SEEKER_H
#define ENV_SENSOR_SEEKER_H

#include "common/random.h"
#include "common/ring_buffer.h"
#include "common/status.h"
#include "common/vec3.h"
#include "env/sensor_noise.h"
#include "env/sensor_vector3.h"
#include "common/device_qualifiers.h"

#include <stdint.h>

/** @brief 导引头配置。 */
typedef struct SeekerSensorConfig {
    int enabled;
    /** @brief 导引头采样周期，单位 s。 */
    double sample_period_s;
    /** @brief 整帧输出延迟，单位 s。 */
    double delay_s;
    /** @brief 整帧丢包概率。 */
    double dropout_probability;
    /** @brief 距离测量误差。 */
    SensorNoiseConfig range;
    /** @brief LOS 单位向量三轴误差，应用后重新归一化。 */
    SensorNoiseConfig los_unit_axis[3];
    /** @brief LOS 角速度三轴误差。 */
    SensorNoiseConfig los_rate_axis[3];
    /** @brief 闭合速度测量误差。 */
    SensorNoiseConfig closing_velocity;
} SeekerSensorConfig;

/** @brief 导引头观测函数所需的弹目真值。 */
typedef struct SeekerTruth {
    Vec3 missile_position_ecef_m;
    Vec3 missile_velocity_ecef_mps;
    Vec3 target_position_ecef_m;
    Vec3 target_velocity_ecef_mps;
} SeekerTruth;

/** @brief 导引头输出测量。 */
typedef struct SeekerMeasurement {
    double range_m;
    Vec3 los_unit_ecef;
    Vec3 los_rate_ecef_radps;
    double closing_velocity_mps;
} SeekerMeasurement;

/** @brief 延迟线中的一个导引头完整测量帧。 */
typedef struct SeekerDelayedSample {
    SeekerMeasurement measurement;
    int valid;
    int dropped;
} SeekerDelayedSample;

/** @brief 导引头实例私有随机、误差和延迟状态。 */
typedef struct SeekerSensor {
    SeekerSensorConfig config;
    SensorNoiseState range_state;
    SensorNoiseState los_unit_state[3];
    SensorNoiseState los_rate_state[3];
    SensorNoiseState closing_velocity_state;
    SimRandom random;
    RingBufferView delay_buffer;
    SeekerDelayedSample delay_storage[SENSOR_DELAY_MAX_STEPS + 1u];
    SeekerDelayedSample held_sample;
    size_t delay_steps;
    double next_sample_time_s;
    int have_sample;
} SeekerSensor;

/** @brief 校验导引头配置。 */
C2CUDA_HOST_DEVICE SimStatus sensor_seeker_validate(
    const SeekerSensorConfig *config,
    double simulation_dt_s);

/** @brief 使用确定性随机种子初始化导引头模型。 */
C2CUDA_HOST_DEVICE SimStatus sensor_seeker_init(
    SeekerSensor *sensor,
    const SeekerSensorConfig *config,
    uint64_t random_seed,
    double simulation_dt_s);

/** @brief 从弹目真值生成延迟后的导引头测量。 */
C2CUDA_HOST_DEVICE SimStatus sensor_seeker_update(
    SeekerSensor *sensor,
    double sim_time_s,
    double simulation_dt_s,
    const SeekerTruth *truth,
    SeekerMeasurement *measurement,
    SensorSampleStatus *sample_status);

#endif
