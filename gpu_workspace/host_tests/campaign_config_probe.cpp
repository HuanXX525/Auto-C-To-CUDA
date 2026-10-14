#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

#include "host_campaign_config.hpp"

namespace {

int failures = 0;

void check(bool condition, const char *label) {
    if (!condition) {
        ++failures;
        std::printf("campaign_config_probe: FAIL %s\n", label);
    }
}

void check_plan(const gpu_campaign::HostCampaignPlan &plan,
                std::uint32_t expected_instances,
                std::uint64_t first_seed,
                std::uint64_t last_seed) {
    check(plan.device.instance_count == expected_instances, "device_instance_count");
    check(plan.instances.size() == expected_instances, "host_instance_count");
    check(plan.configured_instance_count == expected_instances,
          "configured_instance_count");
    check(plan.fault_assets.size() == 1u, "fault_asset_deduplication");
    check(plan.aero_assets.empty(), "baseline_constant_aero");
    check(plan.terrain_assets.empty(), "baseline_flat_fill_terrain");
    if (plan.instances.empty()) return;
    const auto &first = plan.instances.front().device;
    const auto &last = plan.instances.back().device;
    check(first.random_seed == first_seed, "first_seed");
    check(last.random_seed == last_seed, "last_seed");
    check(std::fabs(first.dt - 0.01) < 1.0e-15, "dt");
    check(std::fabs(first.max_time - 120.0) < 1.0e-12, "max_time");
    check(std::fabs(first.hit_radius_m - 5.0) < 1.0e-12, "hit_radius");
    check(first.integrator == INTEGRATOR_RK4, "integrator");
    check(std::fabs(first.initial_mass_kg - 100.0) < 1.0e-12, "mass");
    check(std::fabs(first.initial_inertia_b.m[0][0] - 1.0) < 1.0e-12,
          "inertia");
    check(first.enable_earth_rotation == 1u, "earth_rotation");
    check(first.terrain_enabled == 1u, "terrain_enabled");
    check(first.los_occlusion_enabled == 1u, "los_enabled");
    check(first.sensors.enabled == 1u, "sensors_mapped");
    check(std::fabs(first.flight_control.guidance.navigation_constant - 4.0) <
              1.0e-12,
          "flight_control_guidance");
    check(first.flight_control.scheduler_task_count == 6u,
          "flight_control_scheduler");
    check(std::isfinite(first.initial_missile_position_ecef.x) &&
              std::isfinite(first.initial_target_position_ecef.x),
          "initial_ecef");
}

}  // namespace

int main(int argc, char **argv) {
    static_assert(std::is_trivially_copyable<gpu_campaign::DeviceInstanceConfig>::value,
                  "device instance config must remain POD");
    if (argc != 6) return EXIT_FAILURE;
    std::string error;
    gpu_campaign::HostCampaignPlan single;
    SimStatus status = gpu_campaign::load_campaign_plan(argv[1], &single, &error);
    if (status != SIM_OK) {
        std::printf("campaign_config_probe: FAIL runtime_1 %s\n", error.c_str());
        return EXIT_FAILURE;
    }
    check_plan(single, 1u, 12345u, 12345u);

    gpu_campaign::HostCampaignPlan small;
    status = gpu_campaign::load_campaign_plan(argv[2], &small, &error);
    if (status != SIM_OK) {
        std::printf("campaign_config_probe: FAIL runtime_2 %s\n", error.c_str());
        return EXIT_FAILURE;
    }
    check_plan(small, 2u, 12345u, 12346u);

    gpu_campaign::HostCampaignPlan large;
    status = gpu_campaign::load_campaign_plan(argv[3], &large, &error);
    if (status != SIM_OK) {
        std::printf("campaign_config_probe: FAIL runtime_128 %s\n", error.c_str());
        return EXIT_FAILURE;
    }
    check_plan(large, 128u, 20000u, 20127u);

    gpu_campaign::HostCampaignPlan missing;
    status = gpu_campaign::load_campaign_plan("/nonexistent/runtime.json",
                                              &missing, &error);
    check(status != SIM_OK, "missing_runtime_rejected");
    status = gpu_campaign::load_campaign_plan(argv[4], &missing, &error);
    check(status != SIM_OK, "missing_instance_reference_rejected");
    status = gpu_campaign::load_campaign_plan(argv[5], &missing, &error);
    check(status == SIM_ERR_OUT_OF_RANGE, "invalid_instance_count_rejected");

    if (failures != 0) {
        std::printf("campaign_config_probe: FAIL count=%d\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("campaign_config_probe: PASS N=1/2/128 assets=faults:1 "
                "aero:0 terrain:0 prelaunch-errors:3\n");
    return EXIT_SUCCESS;
}
