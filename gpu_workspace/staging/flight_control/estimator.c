/** @file estimator.c
 *  @brief 飞控状态估计器实现。
 */
#include "fc/estimator.h"

#include <string.h>

SimStatus estimator_init(Estimator *estimator)
{
    if (estimator == 0) {
        return SIM_ERR_INVALID_ARG;
    }
    (void)memset(estimator, 0, sizeof(*estimator));
    return SIM_OK;
}

SimStatus estimator_update(Estimator *estimator, const SensorFrame *sensor, NavState *out)
{
    SimStatus status;
    NavState next;

    if (estimator == 0 || sensor == 0 || out == 0) {
        return SIM_ERR_INVALID_ARG;
    }
    status = navigation_update_from_sensor(sensor, &next);
    if (status == SIM_OK) {
        estimator->nav = next;
        estimator->accepted_frames += 1u;
        *out = next;
    } else {
        estimator->rejected_frames += 1u;
    }
    return status;
}
