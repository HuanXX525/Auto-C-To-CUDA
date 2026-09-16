#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "host_scenario_mapper.hpp"

namespace {

int g_failures = 0;

void check(bool condition, const char *label) {
    if (!condition) {
        ++g_failures;
        std::printf("scenario_mapper_probe: FAIL %s\n", label);
    }
}

bool close(double left, double right) {
    return (left - right) * (left - right) <= 1.0e-30;
}

/* Writes a scratch scenario JSON and maps it. */
SimStatus map_scratch(const char *path, const char *content,
                      gpu_campaign::DeviceScenarioAero *out) {
    std::FILE *file = std::fopen(path, "w");
    if (file == nullptr) {
        return SIM_ERR_INVALID_ARG;
    }
    std::fputs(content, file);
    std::fclose(file);
    return gpu_campaign::load_scenario_aero(path, out);
}

}  // namespace

int main(int argc, char **argv) {
    if (argc != 3) return EXIT_FAILURE;
    const char *baseline_scenario = argv[1];
    const char *scratch_path = argv[2];

    /* 1. Real baseline scenario.json: mapped values must equal its
       aerodynamics and wind sections exactly. */
    {
        gpu_campaign::DeviceScenarioAero mapped{};
        const SimStatus status = gpu_campaign::load_scenario_aero(
            baseline_scenario, &mapped);
        check(status == SIM_OK, "baseline_status");
        check(mapped.force.enabled == 1u, "baseline_enabled");
        check(close(mapped.force.reference_area_m2, 0.01),
              "baseline_reference_area");
        check(close(mapped.force.reference_length_m, 1.0),
              "baseline_reference_length");
        check(close(mapped.drag_coefficient, 0.1), "baseline_drag");
        check(close(mapped.force.control_force_coefficient, 0.002),
              "baseline_control_force");
        check(close(mapped.force.control_moment_coefficient, 0.001),
              "baseline_control_moment");
        check(close(mapped.wind_velocity_ecef_mps.x, 0.0) &&
              close(mapped.wind_velocity_ecef_mps.y, 0.0) &&
              close(mapped.wind_velocity_ecef_mps.z, 0.0),
              "baseline_wind");
        check(mapped.table_path_enabled == 0u, "baseline_no_table_path");
        check(mapped.policy_override_enabled == 0u, "baseline_no_policy");
        check(mapped.policy ==
                  gpu_campaign::DeviceExtrapolationPolicy::ClampAndWarn,
              "baseline_default_policy");
    }

    /* 2. Missing keys fall back to the env_app.c defaults. */
    {
        gpu_campaign::DeviceScenarioAero mapped{};
        const SimStatus status = map_scratch(
            scratch_path, "{\"schema_version\": 1}", &mapped);
        check(status == SIM_OK, "defaults_status");
        check(mapped.force.enabled == 0u, "defaults_enabled");
        check(close(mapped.force.reference_area_m2, 0.0), "defaults_area");
        check(close(mapped.force.reference_length_m, 0.0), "defaults_length");
        check(close(mapped.drag_coefficient, 0.0), "defaults_drag");
        check(close(mapped.force.control_force_coefficient, 0.0),
              "defaults_control_force");
        check(close(mapped.force.control_moment_coefficient, 0.0),
              "defaults_control_moment");
        check(close(mapped.wind_velocity_ecef_mps.x, 0.0) &&
              close(mapped.wind_velocity_ecef_mps.y, 0.0) &&
              close(mapped.wind_velocity_ecef_mps.z, 0.0),
              "defaults_wind");
    }

    /* 3. Full mapping with wind, table path and policy override. */
    {
        gpu_campaign::DeviceScenarioAero mapped{};
        const SimStatus status = map_scratch(
            scratch_path,
            "{\"aerodynamics\": {"
            "\"enabled\": true,"
            "\"reference_area_m2\": 0.02,"
            "\"reference_length_m\": 1.5,"
            "\"drag_coefficient\": 0.3,"
            "\"control_force_coefficient\": 0.004,"
            "\"control_moment_coefficient\": 0.002,"
            "\"table_path\": \"data/aero/custom.aero\","
            "\"table_extrapolation_policy\": \"ERROR\"},"
            "\"atmosphere\": {"
            "\"wind_velocity_ecef_mps\": [1.5, -2.5, 0.5]}}",
            &mapped);
        check(status == SIM_OK, "override_status");
        check(mapped.force.enabled == 1u, "override_enabled");
        check(close(mapped.force.reference_area_m2, 0.02), "override_area");
        check(close(mapped.force.reference_length_m, 1.5), "override_length");
        check(close(mapped.drag_coefficient, 0.3), "override_drag");
        check(close(mapped.force.control_force_coefficient, 0.004),
              "override_control_force");
        check(close(mapped.force.control_moment_coefficient, 0.002),
              "override_control_moment");
        check(close(mapped.wind_velocity_ecef_mps.x, 1.5) &&
              close(mapped.wind_velocity_ecef_mps.y, -2.5) &&
              close(mapped.wind_velocity_ecef_mps.z, 0.5),
              "override_wind");
        check(mapped.table_path_enabled == 1u, "override_table_path_enabled");
        check(std::strcmp(mapped.table_path, "data/aero/custom.aero") == 0,
              "override_table_path");
        check(mapped.policy_override_enabled == 1u, "override_policy_enabled");
        check(mapped.policy == gpu_campaign::DeviceExtrapolationPolicy::Error,
              "override_policy_error");
    }

    /* 4. HOLD_LAST_VALID is rejected: the device table does not implement
       that extrapolation semantics yet. */
    {
        gpu_campaign::DeviceScenarioAero mapped{};
        const SimStatus status = map_scratch(
            scratch_path,
            "{\"aerodynamics\": {\"table_extrapolation_policy\": "
            "\"HOLD_LAST_VALID\"}}",
            &mapped);
        check(status == SIM_ERR_CONFIG, "hold_last_valid_rejected");
    }

    /* 5. Unknown policy strings are rejected like env_app.c. */
    {
        gpu_campaign::DeviceScenarioAero mapped{};
        const SimStatus status = map_scratch(
            scratch_path,
            "{\"aerodynamics\": {\"table_extrapolation_policy\": \"BOGUS\"}}",
            &mapped);
        check(status == SIM_ERR_CONFIG, "unknown_policy_rejected");
    }

    /* 6. CLAMP_AND_WARN string maps to the clamp policy. */
    {
        gpu_campaign::DeviceScenarioAero mapped{};
        const SimStatus status = map_scratch(
            scratch_path,
            "{\"aerodynamics\": {\"table_extrapolation_policy\": "
            "\"CLAMP_AND_WARN\"}}",
            &mapped);
        check(status == SIM_OK, "clamp_policy_status");
        check(mapped.policy ==
                  gpu_campaign::DeviceExtrapolationPolicy::ClampAndWarn,
              "clamp_policy");
    }

    /* 7. A missing file is an error, not silent defaults. */
    {
        gpu_campaign::DeviceScenarioAero mapped{};
        const SimStatus status = gpu_campaign::load_scenario_aero(
            "/nonexistent/scenario.json", &mapped);
        check(status != SIM_OK, "missing_file_rejected");
    }

    if (g_failures != 0) {
        std::printf("scenario_mapper_probe: FAIL count=%d\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("scenario_mapper_probe: PASS\n");
    return EXIT_SUCCESS;
}
