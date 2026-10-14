/** @file estimator.h
 *  @brief 飞控状态估计器接口。
 *
 *  当前估计器实现为传感器测量一致化层：它把速度计、加速度计、陀螺仪、
 *  大地坐标和导引头测量转换为统一 @c NavState。后续可以在该接口下替换
 *  为卡尔曼滤波或组合导航，不影响主循环和制导模块。
 */
#ifndef FC_ESTIMATOR_H
#define FC_ESTIMATOR_H

#include "common/protocol.h"
#include "common/status.h"
#include "fc/navigation.h"
#include "common/device_qualifiers.h"

#include <stdint.h>

/** @brief 状态估计器对象。
 *
 *  @var nav
 *  最近一次被接受传感器帧生成的导航状态。
 */
typedef struct Estimator {
    NavState nav;
    uint32_t accepted_frames;
    uint32_t rejected_frames;
} Estimator;

/** @brief 初始化估计器状态。 */
C2CUDA_HOST_DEVICE SimStatus estimator_init(Estimator *estimator);

/** @brief 使用一帧传感器数据更新导航估计。 */
C2CUDA_HOST_DEVICE SimStatus estimator_update(Estimator *estimator, const SensorFrame *sensor, NavState *out);

#endif
