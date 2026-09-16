#include <cstdio>
#include <cstdlib>

#include "device_environment.cuh"

int main() {
    const AeroTableSample samples[] = {
        {0.0, 0.0, 0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0},
        {1.0, 0.1, 0.1, 10.0, 20.0, 30.0, 40.0, 50.0, 60.0},
    };
    gpu_campaign::DeviceAeroTable aero{};
    AeroTableSample selected{};
    std::uint32_t flags = 0;
    if (gpu_campaign::device_aero_table_init(
            &aero, samples, 2u,
            gpu_campaign::DeviceExtrapolationPolicy::ClampAndWarn) != SIM_OK ||
        gpu_campaign::device_aero_table_lookup(&aero, 1.0, 0.1, 0.1,
                                               &selected, &flags) != SIM_OK ||
        selected.cx != 10.0 || flags != 0u) {
        return EXIT_FAILURE;
    }
    if (gpu_campaign::device_aero_table_lookup(
            &aero, 2.0, 0.2, 0.2, &selected, &flags) != SIM_OK ||
        (flags & gpu_campaign::kDeviceTableFlagExtrapolated) == 0u) {
        return EXIT_FAILURE;
    }
    gpu_campaign::DeviceAeroTable strict_aero{};
    if (gpu_campaign::device_aero_table_init(
            &strict_aero, samples, 2u,
            gpu_campaign::DeviceExtrapolationPolicy::Error) != SIM_OK ||
        gpu_campaign::device_aero_table_lookup(
            &strict_aero, 2.0, 0.2, 0.2, &selected, &flags) != SIM_ERR_OUT_OF_RANGE) {
        return EXIT_FAILURE;
    }
    const double heights[] = {0.0, 10.0, 20.0, 30.0};
    gpu_campaign::DeviceTerrainGrid terrain{};
    double height = 0.0;
    if (gpu_campaign::device_terrain_grid_init(
            &terrain, heights, 2u, 2u, 0.0, 1.0, 0.0, 1.0,
            gpu_campaign::DeviceExtrapolationPolicy::ClampAndWarn) != SIM_OK ||
        gpu_campaign::device_terrain_grid_height(&terrain, 0.5, 0.5,
                                                 &height, &flags) != SIM_OK ||
        height != 15.0 || flags != 0u) {
        return EXIT_FAILURE;
    }
    std::printf("device_environment_probe: PASS aero_cx=%.1f terrain_height=%.1f\n",
                selected.cx, height);
    return EXIT_SUCCESS;
}
