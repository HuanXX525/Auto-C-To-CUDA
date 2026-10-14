/** @file gravity_model.c
 *  @brief ECEF 中心引力模型实现。
 */
#include "env/gravity_model.h"

#include <math.h>

/** @brief 构造使用标准地球引力参数的模型。 */
GravityModel gravity_model_wgs84(void)
{
    GravityModel model = { 1, 3.986004418e14 };
    return model;
}

/** @brief 按 -mu*r/|r|^3 计算中心引力加速度。 */
SimStatus gravity_model_acceleration(
    const GravityModel *model,
    Vec3 position_ecef_m,
    Vec3 *acceleration_ecef_mps2)
{
    double radius;
    double scale;

    if (model == 0 || acceleration_ecef_mps2 == 0) {
        return SIM_ERR_INVALID_ARG;
    }
    if (!isfinite(model->gravitational_parameter) ||
        model->gravitational_parameter <= 0.0 ||
        !vec3_isfinite(position_ecef_m)) {
        return SIM_ERR_NUMERIC;
    }
    if (model->enabled == 0) {
        *acceleration_ecef_mps2 = vec3_make(0.0, 0.0, 0.0);
        return SIM_OK;
    }
    radius = vec3_norm(position_ecef_m);
    if (radius <= 1.0) {
        return SIM_ERR_OUT_OF_RANGE;
    }
    scale = -model->gravitational_parameter / (radius * radius * radius);
    *acceleration_ecef_mps2 = vec3_scale(position_ecef_m, scale);
    return SIM_OK;
}
