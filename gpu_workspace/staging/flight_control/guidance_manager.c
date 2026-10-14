/** @file guidance_manager.c
 *  @brief 飞控制导管理器实现。
 */
#include "fc/guidance_manager.h"

#include <string.h>

SimStatus guidance_manager_init(GuidanceManager *manager, const GuidancePngConfig *png_config)
{
    if (manager == 0 || png_config == 0) {
        return SIM_ERR_INVALID_ARG;
    }
    (void)memset(manager, 0, sizeof(*manager));
    manager->png_config = *png_config;
    return SIM_OK;
}

SimStatus guidance_manager_update(
    GuidanceManager *manager,
    const NavState *nav,
    GuidancePngOutput *out)
{
    GuidancePngInput input;
    SimStatus status;

    if (manager == 0 || nav == 0 || out == 0) {
        return SIM_ERR_INVALID_ARG;
    }
    if (!navigation_has_guidance_solution(nav)) {
        manager->rejected_count += 1u;
        return SIM_ERR_OUT_OF_RANGE;
    }
    input.range_m = nav->range;
    input.closing_velocity_mps = nav->closing_velocity;
    input.los_unit_ecef = nav->los_unit_ecef;
    input.los_rate_ecef = nav->los_rate_ecef;
    status = guidance_png_update(&manager->png_config, &input, out);
    if (status == SIM_OK) {
        manager->accepted_count += 1u;
    } else {
        manager->rejected_count += 1u;
    }
    return status;
}
