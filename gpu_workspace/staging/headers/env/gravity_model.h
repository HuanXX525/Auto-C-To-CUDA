/** @file gravity_model.h
 *  @brief ECEF 中心引力模型。
 */
#ifndef ENV_GRAVITY_MODEL_H
#define ENV_GRAVITY_MODEL_H

#include "common/status.h"
#include "common/vec3.h"

/** @brief 重力模型配置。 */
typedef struct GravityModel {
    int enabled;
    /** @brief 地球标准引力参数，单位 m^3/s^2。 */
    double gravitational_parameter;
} GravityModel;

/** @brief 返回 WGS-84 常用标准引力参数配置。 */
GravityModel gravity_model_wgs84(void);
/** @brief 计算指定 ECEF 位置的中心引力加速度。 */
SimStatus gravity_model_acceleration(
    const GravityModel *model,
    Vec3 position_ecef_m,
    Vec3 *acceleration_ecef_mps2);

#endif
