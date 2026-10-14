/** @file sensor_noise.h
 *  @brief 传感器噪声与量化配置。
 *
 *  该结构用于统一描述各类传感器的偏置、白噪声、随机游走、限幅、分辨率和
 *  丢包概率。
 */
#ifndef ENV_SENSOR_NOISE_H
#define ENV_SENSOR_NOISE_H

#include "common/random.h"
#include "common/status.h"
#include "common/device_qualifiers.h"

/** @brief 传感器噪声模型参数。 */
typedef struct SensorNoiseConfig {
    /** @brief 固定偏置。 */
    double bias;
    /** @brief 白噪声标准差。 */
    double white_noise_std;
    /** @brief 随机游走标准差。 */
    double random_walk_std;
    /** @brief 传感器输出下限。 */
    double min_value;
    /** @brief 传感器输出上限。 */
    double max_value;
    /** @brief 量化分辨率。 */
    double resolution;
    /** @brief 丢包概率。 */
    double dropout_probability;
} SensorNoiseConfig;

/** @brief 传感器误差模型的动态随机游走状态。 */
typedef struct SensorNoiseState {
    double random_walk_value;
} SensorNoiseState;

/** @brief 校验噪声、量化、范围和丢包参数。 */
C2CUDA_HOST_DEVICE SimStatus sensor_noise_validate(const SensorNoiseConfig *config);
/** @brief 对标量真值应用偏置、随机游走、白噪声、量化和限幅。 */
C2CUDA_HOST_DEVICE SimStatus sensor_noise_apply(
    const SensorNoiseConfig *config,
    SensorNoiseState *state,
    SimRandom *random,
    double truth,
    double dt,
    double *measurement,
    int *dropped);

#endif
