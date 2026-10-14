/** @file propulsion_model.c
 *  @brief 定推力推进模型实现。
 */
#include "env/propulsion_model.h"

#include <math.h>

/** @brief 在燃烧时间内输出定推力，燃烧结束后输出零。 */
SimStatus propulsion_model_evaluate(
    const PropulsionModel *model,
    double time_s,
    Vec3 *force_b_n,
    double *mass_flow_kgps)
{
    Vec3 direction;
    SimStatus status;

    if (model == 0 || force_b_n == 0 || mass_flow_kgps == 0 ||
        !isfinite(time_s) || time_s < 0.0 ||
        !isfinite(model->thrust_n) || model->thrust_n < 0.0 ||
        !isfinite(model->mass_flow_kgps) || model->mass_flow_kgps < 0.0 ||
        !isfinite(model->burn_time_s) || model->burn_time_s < 0.0) {
        return SIM_ERR_INVALID_ARG;
    }
    if (model->enabled == 0 || time_s >= model->burn_time_s) {
        *force_b_n = vec3_make(0.0, 0.0, 0.0);
        *mass_flow_kgps = 0.0;
        return SIM_OK;
    }
    status = vec3_normalize(model->thrust_direction_b, &direction);
    if (status != SIM_OK) {
        return status;
    }
    *force_b_n = vec3_scale(direction, model->thrust_n);
    *mass_flow_kgps = model->mass_flow_kgps;
    return SIM_OK;
}
