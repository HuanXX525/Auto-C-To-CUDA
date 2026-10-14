#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

#include "host_campaign_config.hpp"

namespace {

int failures = 0;

void check(bool condition, const char *label) {
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "m8_config_probe: FAIL %s\n", label);
    }
}

bool finite_vec3(const Vec3 &value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

void check_plan(const gpu_campaign::HostCampaignPlan &plan,
                std::uint32_t expected_instances,
                std::size_t expected_scenarios,
                std::size_t expected_fault_assets) {
    check(plan.configured_instance_count == expected_instances,
          "configured_instance_count");
    check(plan.device.instance_count == expected_instances,
          "device_instance_count");
    check(plan.instances.size() == expected_instances,
          "host_instance_count");
    check(plan.max_parallel_instances == 6u,
          "max_parallel_instances");
    check(plan.requested_schedule == "PARALLEL", "parallel_schedule");
    check(plan.aero_assets.empty(), "baseline_constant_aero");
    check(plan.terrain_assets.empty(), "terrain_assets_disabled");
    check(plan.fault_assets.size() == expected_fault_assets,
          "fault_asset_count");

    std::set<std::uint32_t> ids;
    std::set<std::uint64_t> seeds;
    std::set<std::string> scenarios;
    for (std::uint32_t index = 0u; index < plan.instances.size(); ++index) {
        const gpu_campaign::HostInstancePlan &host = plan.instances[index];
        const gpu_campaign::DeviceInstanceConfig &device = host.device;
        ids.insert(device.instance_id);
        seeds.insert(device.random_seed);
        scenarios.insert(host.scenario_path);
        check(device.instance_id == index, "contiguous_instance_id");
        check(device.random_seed == UINT64_C(12345) + UINT64_C(17) * index,
              "deterministic_seed");
        check(std::fabs(device.dt - 0.01) < 1.0e-15, "dt");
        check(std::fabs(device.max_time - 120.0) < 1.0e-12, "max_time");
        check(std::fabs(device.hit_radius_m - 1.0e-9) < 1.0e-18,
              "hit_radius");
        check(device.integrator == INTEGRATOR_RK4, "integrator");
        check(device.terrain_enabled == 0u, "terrain_disabled");
        check(device.los_occlusion_enabled == 0u, "los_disabled");
        check(device.terrain_asset_index == gpu_campaign::kInvalidAssetIndex,
              "terrain_asset_invalid");
        check(device.sensors.enabled == 1u, "sensors_mapped");
        check(finite_vec3(device.initial_missile_position_ecef) &&
                  finite_vec3(device.initial_missile_velocity_ecef) &&
                  finite_vec3(device.initial_target_position_ecef) &&
                  finite_vec3(device.initial_target_velocity_ecef),
              "finite_initial_ecef_state");
    }
    check(ids.size() == expected_instances, "unique_instance_ids");
    check(seeds.size() == expected_instances, "unique_random_seeds");
    check(scenarios.size() == expected_scenarios, "scenario_path_count");
}

bool load(const char *path, gpu_campaign::HostCampaignPlan *plan) {
    std::string error;
    const SimStatus status = gpu_campaign::load_campaign_plan(path, plan, &error);
    if (status != SIM_OK) {
        std::fprintf(stderr, "m8_config_probe: FAIL load %s: %s (status=%d)\n",
                     path, error.c_str(), static_cast<int>(status));
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc != 4) {
        std::fprintf(stderr,
                     "usage: %s UNIFORM64 UNIFORM128 HETEROGENEOUS128\n",
                     argv[0]);
        return EXIT_FAILURE;
    }

    gpu_campaign::HostCampaignPlan uniform64;
    gpu_campaign::HostCampaignPlan uniform128;
    gpu_campaign::HostCampaignPlan heterogeneous128;
    if (!load(argv[1], &uniform64) || !load(argv[2], &uniform128) ||
        !load(argv[3], &heterogeneous128)) {
        return EXIT_FAILURE;
    }

    check_plan(uniform64, 64u, 1u, 1u);
    check_plan(uniform128, 128u, 1u, 1u);
    check_plan(heterogeneous128, 128u, 128u, 6u);

    for (std::size_t index = 0u; index < uniform64.instances.size(); ++index) {
        const auto &left = uniform64.instances[index].device;
        const auto &right = uniform128.instances[index].device;
        check(left.instance_id == right.instance_id, "uniform_prefix_id");
        check(left.random_seed == right.random_seed, "uniform_prefix_seed");
        check(uniform64.instances[index].scenario_path ==
                  uniform128.instances[index].scenario_path,
              "uniform_prefix_scenario");
        check(uniform64.instances[index].faults_path ==
                  uniform128.instances[index].faults_path,
              "uniform_prefix_faults");
    }

    if (failures != 0) {
        std::fprintf(stderr, "m8_config_probe: FAIL count=%d\n", failures);
        return EXIT_FAILURE;
    }

    std::printf(
        "{\"status\":\"PASS\",\"uniform64_instances\":64,"
        "\"uniform128_instances\":128,\"heterogeneous128_instances\":128,"
        "\"device_trace_record_bytes\":%zu,"
        "\"device_instance_state_bytes\":%zu,"
        "\"device_instance_config_bytes\":%zu}\n",
        sizeof(gpu_campaign::DeviceTraceRecord),
        sizeof(gpu_campaign::DeviceInstanceState),
        sizeof(gpu_campaign::DeviceInstanceConfig));
    return EXIT_SUCCESS;
}
