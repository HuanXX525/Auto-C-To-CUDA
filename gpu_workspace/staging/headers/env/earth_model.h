/** @file earth_model.h
 *  @brief 地球几何模型参数。
 *
 *  当前工程以球形或近似椭球模型作为环境基础，用于 LLA/ECEF 变换和高度
 *  计算。该结构保留地球几何常量，便于后续切换更精细模型。
 */
#ifndef ENV_EARTH_MODEL_H
#define ENV_EARTH_MODEL_H

#include "common/status.h"
#include "common/device_qualifiers.h"

/** @brief 地球几何参数。 */
typedef struct EarthModel {
    /** @brief 长半轴，单位米。 */
    double semi_major_axis_m;
    /** @brief 扁率。 */
    double flattening;
} EarthModel;

/** @brief 返回标准 WGS-84 椭球模型。 */
C2CUDA_HOST_DEVICE EarthModel earth_model_wgs84(void);
/** @brief 返回由长半轴和扁率定义的地球模型。 */
C2CUDA_HOST_DEVICE SimStatus earth_model_make(double semi_major_axis_m, double flattening, EarthModel *out);
/** @brief 计算第一偏心率平方。 */
C2CUDA_HOST_DEVICE double earth_model_eccentricity_squared(const EarthModel *model);
/** @brief 计算短半轴。 */
C2CUDA_HOST_DEVICE double earth_model_semi_minor_axis(const EarthModel *model);
/** @brief 校验地球模型参数。 */
C2CUDA_HOST_DEVICE SimStatus earth_model_validate(const EarthModel *model);

#endif
