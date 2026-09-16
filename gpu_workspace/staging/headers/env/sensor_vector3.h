/** @file sensor_vector3.h
 *  @brief 三轴传感器的共享采样、误差和延迟引擎。
 *
 *  IMU 陀螺仪、加速度计和速度计都使用三轴向量测量。本模块统一实现
 *  固定偏置、白噪声、随机游走、量化、限幅、采样保持、整帧丢包和
 *  固定步数延迟。状态和延迟存储均嵌入实例对象，不分配动态内存。
 */
#ifndef ENV_SENSOR_VECTOR3_H
#define ENV_SENSOR_VECTOR3_H

#include "common/random.h"
#include "common/ring_buffer.h"
#include "common/status.h"
#include "common/vec3.h"
#include "env/sensor_noise.h"
#include "common/device_qualifiers.h"

#include <stdint.h>

/** @brief 单个传感器允许的最大延迟历史帧数。 */
#define SENSOR_DELAY_MAX_STEPS 1024u

/** @brief 传感器输出状态，用于设置协议有效位和故障位。 */
typedef struct SensorSampleStatus {
    /** @brief 当前输出是否包含有效测量。 */
    int valid;
    /** @brief 当前延迟输出对应的采样是否发生丢包。 */
    int dropped;
    /** @brief 延迟线是否尚未积累足够历史。 */
    int delay_warmup;
} SensorSampleStatus;

/** @brief 三轴传感器配置。 */
typedef struct SensorVector3Config {
    /** @brief 是否启用该传感器。 */
    int enabled;
    /** @brief 新测量样本的采样周期，单位 s。 */
    double sample_period_s;
    /** @brief 输出相对采样时刻的固定延迟，单位 s。 */
    double delay_s;
    /** @brief 整个三轴样本的丢包概率。 */
    double dropout_probability;
    /** @brief X/Y/Z 三轴独立误差模型。 */
    SensorNoiseConfig axis[3];
} SensorVector3Config;

/** @brief 延迟线中保存的一个三轴测量样本。 */
typedef struct SensorVector3Sample {
    Vec3 measurement;
    int valid;
    int dropped;
} SensorVector3Sample;

/** @brief 三轴传感器的实例私有运行状态。 */
typedef struct SensorVector3 {
    SensorVector3Config config;
    SensorNoiseState noise_state[3];
    SimRandom random;
    RingBufferView delay_buffer;
    SensorVector3Sample delay_storage[SENSOR_DELAY_MAX_STEPS + 1u];
    SensorVector3Sample held_sample;
    size_t delay_steps;
    double next_sample_time_s;
    int have_sample;
} SensorVector3;

/** @brief 校验三轴传感器配置。 */
C2CUDA_HOST_DEVICE SimStatus sensor_vector3_validate(const SensorVector3Config *config, double simulation_dt_s);

/** @brief 使用确定性随机种子初始化三轴传感器及固定延迟线。 */
C2CUDA_HOST_DEVICE SimStatus sensor_vector3_init(
    SensorVector3 *sensor,
    const SensorVector3Config *config,
    uint64_t random_seed,
    double simulation_dt_s);

/** @brief 根据三轴真值生成当前时刻的延迟测量。 */
C2CUDA_HOST_DEVICE SimStatus sensor_vector3_update(
    SensorVector3 *sensor,
    double sim_time_s,
    double simulation_dt_s,
    Vec3 truth,
    Vec3 *measurement,
    SensorSampleStatus *sample_status);

#endif
