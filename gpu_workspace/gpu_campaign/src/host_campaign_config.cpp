#include "host_campaign_config.hpp"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <unistd.h>

#include "common/math_constants.h"
#include "env/geo_coordinate.h"
#include "host_scenario_mapper.hpp"

extern "C" {
#include "common/config.h"
}

namespace gpu_campaign {
namespace {

constexpr double kEarthRotationRadps = 7.2921150e-5;

SimStatus fail(SimStatus status, const std::string &message,
               std::string *error) {
    if (error != nullptr) *error = message;
    return status;
}

std::string parent_path(const std::string &path) {
    const std::string::size_type slash = path.find_last_of('/');
    if (slash == std::string::npos) return ".";
    if (slash == 0u) return "/";
    return path.substr(0u, slash);
}

std::string join_path(const std::string &left, const std::string &right) {
    if (left.empty() || left == ".") return right;
    if (left == "/") return left + right;
    return left + "/" + right;
}

bool canonical_existing(const std::string &path, std::string *out) {
    char resolved[PATH_MAX];
    if (realpath(path.c_str(), resolved) == nullptr) return false;
    *out = resolved;
    return true;
}

bool resolve_reference(const std::string &owner_path, const std::string &value,
                       std::string *out) {
    if (value.empty()) return false;
    if (value[0] == '/') return canonical_existing(value, out);
    if (canonical_existing(value, out)) return true;
    std::string directory = parent_path(owner_path);
    for (;;) {
        if (canonical_existing(join_path(directory, value), out)) return true;
        if (directory == "/" || directory == ".") break;
        const std::string parent = parent_path(directory);
        if (parent == directory) break;
        directory = parent;
    }
    return false;
}

bool get_optional_string(const ConfigTree *tree, const char *path,
                         std::string *out) {
    char value[PATH_MAX];
    if (config_get_string(tree, path, value, sizeof(value)) != SIM_OK) {
        return false;
    }
    *out = value;
    return true;
}

SimStatus require_string_value(const ConfigTree *tree, const char *path,
                               const char *expected, std::string *error) {
    char value[64];
    const SimStatus status = config_get_string(tree, path, value, sizeof(value));
    if (status != SIM_OK) {
        return fail(status, "missing or invalid " + std::string(path), error);
    }
    if (std::strcmp(value, expected) != 0) {
        return fail(SIM_ERR_CONFIG, "unsupported " + std::string(path) +
                    ": " + value, error);
    }
    return SIM_OK;
}

SimStatus load_optional_envelope(const ConfigTree *tree, const char *minimum_key,
                                 const char *maximum_key, std::uint32_t *enabled,
                                 double *minimum, double *maximum,
                                 std::string *error) {
    const SimStatus min_status = config_get_double(tree, minimum_key, minimum);
    const SimStatus max_status = config_get_double(tree, maximum_key, maximum);
    if (min_status != SIM_OK && min_status != SIM_ERR_CONFIG) {
        return fail(min_status, "invalid " + std::string(minimum_key), error);
    }
    if (max_status != SIM_OK && max_status != SIM_ERR_CONFIG) {
        return fail(max_status, "invalid " + std::string(maximum_key), error);
    }
    if ((min_status == SIM_OK) != (max_status == SIM_OK)) {
        return fail(SIM_ERR_CONFIG, "aerodynamic envelope bounds must be paired", error);
    }
    if (min_status != SIM_OK) {
        *enabled = 0u;
        *minimum = 0.0;
        *maximum = 0.0;
        return SIM_OK;
    }
    if (!std::isfinite(*minimum) || !std::isfinite(*maximum) ||
        *maximum < *minimum) {
        return fail(SIM_ERR_OUT_OF_RANGE, "invalid aerodynamic envelope range", error);
    }
    *enabled = 1u;
    return SIM_OK;
}

bool valid_sensor_noise(const SensorNoiseConfig &noise) {
    return std::isfinite(noise.bias) && std::isfinite(noise.white_noise_std) &&
        std::isfinite(noise.random_walk_std) && std::isfinite(noise.min_value) &&
        std::isfinite(noise.max_value) && std::isfinite(noise.resolution) &&
        noise.white_noise_std >= 0.0 && noise.random_walk_std >= 0.0 &&
        noise.resolution >= 0.0 && noise.min_value <= noise.max_value;
}

bool valid_vector_sensor(const SensorVector3Config &sensor) {
    if (!std::isfinite(sensor.sample_period_s) || sensor.sample_period_s <= 0.0 ||
        !std::isfinite(sensor.delay_s) || sensor.delay_s < 0.0 ||
        !std::isfinite(sensor.dropout_probability) ||
        sensor.dropout_probability < 0.0 || sensor.dropout_probability > 1.0) {
        return false;
    }
    for (std::uint32_t axis = 0u; axis < 3u; ++axis) {
        if (!valid_sensor_noise(sensor.axis[axis])) return false;
    }
    return true;
}

bool valid_seeker_sensor(const SeekerSensorConfig &sensor) {
    if (!std::isfinite(sensor.sample_period_s) || sensor.sample_period_s <= 0.0 ||
        !std::isfinite(sensor.delay_s) || sensor.delay_s < 0.0 ||
        !std::isfinite(sensor.dropout_probability) ||
        sensor.dropout_probability < 0.0 || sensor.dropout_probability > 1.0 ||
        !valid_sensor_noise(sensor.range) ||
        !valid_sensor_noise(sensor.closing_velocity)) {
        return false;
    }
    for (std::uint32_t axis = 0u; axis < 3u; ++axis) {
        if (!valid_sensor_noise(sensor.los_unit_axis[axis]) ||
            !valid_sensor_noise(sensor.los_rate_axis[axis])) return false;
    }
    return true;
}

SimStatus load_flight_control_config(const std::string &path,
                                     FlightControllerConfig *out,
                                     std::string *error) {
    ConfigTree tree{};
    SimStatus status = config_load_file(path.c_str(), &tree);
    if (status != SIM_OK) {
        return fail(status, "cannot load flight-control config: " + path, error);
    }
    const auto finish = [&tree, error](SimStatus result,
                                      const std::string &message) {
        config_free(&tree);
        return result == SIM_OK ? result : fail(result, message, error);
    };
    status = config_validate_schema(&tree, 1u);
    if (status != SIM_OK) return finish(status, "invalid flight-control schema: " + path);
    std::memset(out, 0, sizeof(*out));

    status = require_string_value(&tree, "guidance.type", "PNG_3D", error);
    if (status != SIM_OK) return finish(status, error == nullptr ?
        "unsupported guidance type" : *error);

    status = config_get_double(&tree, "guidance.navigation_constant",
                               &out->guidance.navigation_constant);
    if (status == SIM_OK) {
        status = config_get_double(&tree, "guidance.max_accel_mps2",
                                   &out->guidance.max_accel_mps2);
    }
    if (status == SIM_OK) {
        status = config_get_double(&tree, "guidance.max_accel_rate_mps3",
                                   &out->guidance.max_accel_rate_mps3);
    }
    if (status != SIM_OK) return finish(status, "invalid guidance config: " + path);

    out->autopilot.enable_attitude_loop = 1;
    out->autopilot.enable_control_allocation = 1;
    out->autopilot.max_attitude_cmd_rad = 0.35;
    out->autopilot.max_body_rate_cmd_radps = 1.0;
    out->autopilot.attitude_time_constant_s = 0.25;
    out->autopilot.gyro_damping_gain = 0.2;
    out->autopilot.fin_accel_effectiveness_mps2_per_rad = 150.0;
    out->autopilot.max_fin_deflection_rad = 0.35;
    (void)config_get_bool(&tree, "autopilot.enable_attitude_loop",
                          &out->autopilot.enable_attitude_loop);
    (void)config_get_bool(&tree, "autopilot.enable_control_allocation",
                          &out->autopilot.enable_control_allocation);
    (void)config_get_double(&tree, "autopilot.max_attitude_cmd_rad",
                            &out->autopilot.max_attitude_cmd_rad);
    (void)config_get_double(&tree, "autopilot.max_body_rate_cmd_radps",
                            &out->autopilot.max_body_rate_cmd_radps);
    (void)config_get_double(&tree, "autopilot.attitude_time_constant_s",
                            &out->autopilot.attitude_time_constant_s);
    (void)config_get_double(&tree, "autopilot.gyro_damping_gain",
                            &out->autopilot.gyro_damping_gain);
    (void)config_get_double(
        &tree, "autopilot.fin_accel_effectiveness_mps2_per_rad",
        &out->autopilot.fin_accel_effectiveness_mps2_per_rad);
    (void)config_get_double(&tree, "autopilot.max_fin_deflection_rad",
                            &out->autopilot.max_fin_deflection_rad);

    status = config_get_double(&tree, "safety.sensor_timeout_s",
                               &out->safety.sensor_timeout_s);
    if (status == SIM_OK) {
        status = config_get_double(&tree, "safety.command_hold_s",
                                   &out->safety.command_hold_s);
    }
    if (status == SIM_OK) {
        status = config_get_bool(&tree, "safety.reject_nan",
                                 &out->safety.reject_nan);
    }
    if (status == SIM_OK) {
        status = config_get_bool(&tree, "safety.reject_old_seq",
                                 &out->safety.reject_old_seq);
    }
    if (status != SIM_OK) return finish(status, "invalid safety config: " + path);
    out->safety.max_consecutive_bad_frames = 3u;
    if (!std::isfinite(out->guidance.navigation_constant) ||
        !std::isfinite(out->guidance.max_accel_mps2) ||
        !std::isfinite(out->guidance.max_accel_rate_mps3) ||
        out->guidance.navigation_constant < 0.0 ||
        out->guidance.max_accel_mps2 < 0.0 ||
        out->guidance.max_accel_rate_mps3 < 0.0 ||
        !std::isfinite(out->safety.sensor_timeout_s) ||
        !std::isfinite(out->safety.command_hold_s) ||
        out->safety.sensor_timeout_s < 0.0 ||
        out->safety.command_hold_s < 0.0) {
        return finish(SIM_ERR_OUT_OF_RANGE, "invalid flight-control numeric range: " + path);
    }

    status = config_get_double(&tree, "scheduler.base_rate_hz",
                               &out->scheduler_base_rate_hz);
    if (status != SIM_OK || !std::isfinite(out->scheduler_base_rate_hz) ||
        out->scheduler_base_rate_hz <= 0.0) {
        return finish(status == SIM_OK ? SIM_ERR_OUT_OF_RANGE : status,
                      "invalid scheduler base rate: " + path);
    }
    size_t task_count = 0u;
    status = config_get_array_count(&tree, "scheduler.tasks", &task_count);
    if (status != SIM_OK || task_count > FC_SCHEDULER_MAX_TASKS) {
        return finish(status == SIM_OK ? SIM_ERR_OUT_OF_RANGE : status,
                      "invalid scheduler task table: " + path);
    }
    out->scheduler_task_count = static_cast<std::uint32_t>(task_count);
    for (size_t index = 0u; index < task_count; ++index) {
        char key[96];
        unsigned int period = 0u;
        std::snprintf(key, sizeof(key), "scheduler.tasks[%u].name",
                      static_cast<unsigned int>(index));
        status = config_get_string(&tree, key, out->scheduler_tasks[index].name,
                                   sizeof(out->scheduler_tasks[index].name));
        if (status != SIM_OK) return finish(status, "invalid scheduler task name: " + path);
        std::snprintf(key, sizeof(key), "scheduler.tasks[%u].period_ticks",
                      static_cast<unsigned int>(index));
        status = config_get_uint32(&tree, key, &period);
        if (status != SIM_OK || period == 0u) {
            return finish(status == SIM_OK ? SIM_ERR_OUT_OF_RANGE : status,
                          "invalid scheduler task period: " + path);
        }
        out->scheduler_tasks[index].period_ticks = period;
    }
    const char *required[] = {"receive", "navigation", "guidance",
                              "controller", "safety"};
    for (const char *name : required) {
        bool found = false;
        for (std::uint32_t index = 0u; index < out->scheduler_task_count; ++index) {
            if (std::strcmp(out->scheduler_tasks[index].name, name) == 0) {
                found = true;
                break;
            }
        }
        if (!found) return finish(SIM_ERR_CONFIG, "missing scheduler task " + std::string(name));
    }
    return finish(SIM_OK, "");
}

SimStatus parse_integrator(const ConfigTree *tree, std::uint32_t *out) {
    char value[32];
    SimStatus status = config_get_string(tree, "plant.integrator", value,
                                         sizeof(value));
    if (status != SIM_OK) return status;
    if (std::strcmp(value, "EULER") == 0) *out = INTEGRATOR_EULER;
    else if (std::strcmp(value, "RK2") == 0) *out = INTEGRATOR_RK2;
    else if (std::strcmp(value, "RK4") == 0) *out = INTEGRATOR_RK4;
    else return SIM_ERR_CONFIG;
    return SIM_OK;
}

SimStatus resolve_optional_asset(const ConfigTree *tree, const char *key,
                                 const std::string &owner,
                                 std::string *resolved, bool *found,
                                 std::string *error) {
    std::string raw;
    *found = get_optional_string(tree, key, &raw);
    if (!*found) return SIM_OK;
    if (!resolve_reference(owner, raw, resolved)) {
        return fail(SIM_ERR_IO, "asset not found for " + std::string(key) +
                    ": " + raw, error);
    }
    return SIM_OK;
}

SimStatus load_scenario_config(const std::string &path,
                               DeviceInstanceConfig *out,
                               HostInstancePlan *host,
                               std::string *aero_path,
                               std::string *surrogate_path,
                               HostTerrainAsset *terrain,
                               std::string *error) {
    ConfigTree tree{};
    SimStatus status = config_load_file(path.c_str(), &tree);
    if (status != SIM_OK) return fail(status, "cannot load scenario: " + path, error);
    const auto finish = [&tree, error](SimStatus result,
                                      const std::string &message) {
        config_free(&tree);
        return result == SIM_OK ? result : fail(result, message, error);
    };
    status = config_validate_schema(&tree, 1u);
    if (status != SIM_OK) return finish(status, "invalid scenario schema: " + path);

    status = require_string_value(&tree, "earth.model", "WGS84", error);
    if (status == SIM_OK) {
        status = require_string_value(&tree, "plant.model", "SIX_DOF", error);
    }
    if (status == SIM_OK) {
        status = require_string_value(&tree, "target.model", "SCRIPTED", error);
    }
    if (status != SIM_OK) return finish(status, error == nullptr ?
        "unsupported scenario model" : *error);

    char text[64];
    status = config_get_string(&tree, "simulation.mode", text, sizeof(text));
    if (status != SIM_OK || std::strcmp(text, "LOCKSTEP") != 0) {
        return finish(status == SIM_OK ? SIM_ERR_CONFIG : status,
                      "GPU campaign requires LOCKSTEP simulation mode");
    }
    status = config_get_double(&tree, "simulation.dt", &out->dt);
    if (status == SIM_OK) status = config_get_double(&tree, "simulation.max_time", &out->max_time);
    if (status == SIM_OK) status = config_get_double(&tree, "simulation.hit_radius_m", &out->hit_radius_m);
    if (status != SIM_OK || out->dt <= 0.0 || out->max_time < 0.0 ||
        out->hit_radius_m < 0.0) {
        return finish(status == SIM_OK ? SIM_ERR_OUT_OF_RANGE : status,
                      "invalid simulation timing or hit radius");
    }
    status = parse_integrator(&tree, &out->integrator);
    if (status != SIM_OK) return finish(status, "invalid plant integrator");

    out->earth = earth_model_wgs84();
    int rotation_enabled = 0;
    status = config_get_bool(&tree, "earth.enable_rotation_terms",
                             &rotation_enabled);
    out->enable_earth_rotation = status == SIM_OK && rotation_enabled != 0 ? 1u : 0u;
    out->earth_rotation_rate_radps = kEarthRotationRadps;

    double inertia[3] = {0.0, 0.0, 0.0};
    status = config_get_double(&tree, "plant.mass_kg", &out->initial_mass_kg);
    if (status == SIM_OK) {
        status = config_get_double(&tree, "plant.propellant_mass_kg",
                                   &out->initial_propellant_mass_kg);
    }
    if (status == SIM_OK) status = config_get_double_array(&tree, "plant.inertia_diag", inertia, 3u);
    if (status == SIM_OK) status = config_get_double(&tree, "plant.command_tau_s", &out->actuator_time_constant_s);
    if (status == SIM_OK) status = config_get_double(&tree, "plant.acceleration_limit_mps2", &out->actuator_position_limit);
    if (status == SIM_OK) status = config_get_double(&tree, "plant.acceleration_rate_limit_mps3", &out->actuator_rate_limit);
    if (status != SIM_OK || out->initial_mass_kg <= 0.0 ||
        out->initial_propellant_mass_kg < 0.0 ||
        out->initial_propellant_mass_kg > out->initial_mass_kg ||
        out->actuator_time_constant_s <= 0.0 ||
        out->actuator_position_limit <= 0.0 || out->actuator_rate_limit <= 0.0 ||
        inertia[0] <= 0.0 || inertia[1] <= 0.0 || inertia[2] <= 0.0) {
        return finish(status == SIM_OK ? SIM_ERR_OUT_OF_RANGE : status,
                      "invalid plant mass, inertia, or actuator limits");
    }
    out->initial_inertia_b = matrix3_zero();
    out->initial_inertia_b.m[0][0] = inertia[0];
    out->initial_inertia_b.m[1][1] = inertia[1];
    out->initial_inertia_b.m[2][2] = inertia[2];

    out->gravity = gravity_model_wgs84();
    (void)config_get_bool(&tree, "gravity.enabled", &out->gravity.enabled);
    out->atmosphere = atmosphere_model_isa();
    (void)config_get_bool(&tree, "atmosphere.enabled", &out->atmosphere.enabled);
    (void)config_get_double(&tree, "atmosphere.maximum_model_height_m",
                            &out->atmosphere.maximum_model_height_m);
    out->propulsion = PropulsionModel{};
    out->propulsion.thrust_direction_b = vec3_make(1.0, 0.0, 0.0);
    (void)config_get_bool(&tree, "propulsion.enabled", &out->propulsion.enabled);
    (void)config_get_double(&tree, "propulsion.thrust_n", &out->propulsion.thrust_n);
    (void)config_get_double(&tree, "propulsion.mass_flow_kgps", &out->propulsion.mass_flow_kgps);
    (void)config_get_double(&tree, "propulsion.burn_time_s", &out->propulsion.burn_time_s);
    double vector[3] = {1.0, 0.0, 0.0};
    if (config_get_double_array(&tree, "propulsion.thrust_direction_b", vector, 3u) == SIM_OK) {
        out->propulsion.thrust_direction_b = vec3_make(vector[0], vector[1], vector[2]);
    }
    if (!std::isfinite(out->atmosphere.maximum_model_height_m) ||
        out->atmosphere.maximum_model_height_m <= 0.0 ||
        !std::isfinite(out->propulsion.thrust_n) ||
        out->propulsion.thrust_n < 0.0 ||
        !std::isfinite(out->propulsion.mass_flow_kgps) ||
        out->propulsion.mass_flow_kgps < 0.0 ||
        !std::isfinite(out->propulsion.burn_time_s) ||
        out->propulsion.burn_time_s < 0.0 ||
        !vec3_isfinite(out->propulsion.thrust_direction_b) ||
        (out->propulsion.enabled != 0 &&
         vec3_norm(out->propulsion.thrust_direction_b) <= 1.0e-12)) {
        return finish(SIM_ERR_OUT_OF_RANGE,
                      "invalid atmosphere or propulsion numeric range");
    }

    DeviceScenarioAero aero{};
    status = load_scenario_aero(path.c_str(), &aero);
    if (status != SIM_OK) return finish(status, "invalid aerodynamics config: " + path);
    out->aero_force = aero.force;
    out->aero_drag_coefficient = aero.drag_coefficient;
    out->wind_velocity_ecef_mps = aero.wind_velocity_ecef_mps;
    out->aero_table_policy = static_cast<std::uint32_t>(aero.policy);
    if (!std::isfinite(out->aero_force.reference_area_m2) ||
        out->aero_force.reference_area_m2 < 0.0 ||
        !std::isfinite(out->aero_force.reference_length_m) ||
        out->aero_force.reference_length_m < 0.0 ||
        !std::isfinite(out->aero_drag_coefficient) ||
        out->aero_drag_coefficient < 0.0 ||
        !std::isfinite(out->aero_force.control_force_coefficient) ||
        !std::isfinite(out->aero_force.control_moment_coefficient) ||
        !vec3_isfinite(out->wind_velocity_ecef_mps)) {
        return finish(SIM_ERR_OUT_OF_RANGE,
                      "invalid aerodynamic or wind numeric range");
    }
    if (aero.table_path_enabled != 0u) {
        if (!resolve_reference(path, aero.table_path, aero_path)) {
            return finish(SIM_ERR_IO, "aero table not found: " + std::string(aero.table_path));
        }
    }
    status = load_optional_envelope(
        &tree, "aerodynamics.table_height_min_m",
        "aerodynamics.table_height_max_m", &out->aero_height_envelope_enabled,
        &out->aero_height_min_m, &out->aero_height_max_m, error);
    if (status == SIM_OK) {
        status = load_optional_envelope(
            &tree, "aerodynamics.table_actuator_min_rad",
            "aerodynamics.table_actuator_max_rad",
            &out->aero_actuator_envelope_enabled,
            &out->aero_actuator_min_rad, &out->aero_actuator_max_rad, error);
    }
    if (status != SIM_OK) return finish(status, error == nullptr ?
        "invalid aerodynamic envelope" : *error);
    bool surrogate_found = false;
    status = resolve_optional_asset(&tree, "aerodynamics.surrogate_model_path",
                                    path, surrogate_path, &surrogate_found, error);
    if (status != SIM_OK) return finish(status, error == nullptr ?
        "invalid aerodynamic surrogate" : *error);
    if (surrogate_found) {
        if (!get_optional_string(&tree, "aerodynamics.surrogate_model_version",
                                 &host->aero_surrogate_model_version)) {
            host->aero_surrogate_model_version = "UNSPECIFIED";
        }
        if (!get_optional_string(&tree,
                "aerodynamics.surrogate_training_data_version",
                &host->aero_surrogate_training_data_version)) {
            host->aero_surrogate_training_data_version = "UNSPECIFIED";
        }
    }
    status = load_scenario_sensors(path.c_str(), out->dt, &out->sensors);
    if (status != SIM_OK) return finish(status, "invalid sensor config: " + path);
    if (!valid_vector_sensor(out->sensors.imu) ||
        !valid_vector_sensor(out->sensors.accel) ||
        !valid_vector_sensor(out->sensors.speed) ||
        !valid_seeker_sensor(out->sensors.seeker)) {
        return finish(SIM_ERR_OUT_OF_RANGE, "invalid sensor numeric range: " + path);
    }

    double missile_lla[3];
    double target_lla[3];
    double missile_velocity[3];
    double target_velocity[3];
    status = config_get_double_array(&tree, "missile.initial_lla_deg_m", missile_lla, 3u);
    if (status == SIM_OK) status = config_get_double_array(&tree, "missile.initial_velocity_ecef_mps", missile_velocity, 3u);
    if (status == SIM_OK) status = config_get_double_array(&tree, "target.initial_lla_deg_m", target_lla, 3u);
    if (status == SIM_OK) status = config_get_double_array(&tree, "target.initial_velocity_ecef_mps", target_velocity, 3u);
    if (status != SIM_OK) return finish(status, "invalid missile or target initial state");
    LlaCoord missile_coord{missile_lla[0] * SIM_DEG_TO_RAD,
                           missile_lla[1] * SIM_DEG_TO_RAD, missile_lla[2]};
    LlaCoord target_coord{target_lla[0] * SIM_DEG_TO_RAD,
                          target_lla[1] * SIM_DEG_TO_RAD, target_lla[2]};
    EcefCoord missile_ecef{};
    EcefCoord target_ecef{};
    status = geo_lla_to_ecef(&out->earth, &missile_coord, &missile_ecef);
    if (status == SIM_OK) status = geo_lla_to_ecef(&out->earth, &target_coord, &target_ecef);
    if (status != SIM_OK) return finish(status, "initial LLA to ECEF conversion failed");
    out->initial_missile_position_ecef = missile_ecef.position_m;
    out->initial_missile_velocity_ecef = vec3_make(missile_velocity[0], missile_velocity[1], missile_velocity[2]);
    out->initial_target_position_ecef = target_ecef.position_m;
    out->initial_target_velocity_ecef = vec3_make(target_velocity[0], target_velocity[1], target_velocity[2]);

    int enabled = 0;
    if (config_get_bool(&tree, "map.enable_terrain", &enabled) == SIM_OK) {
        out->terrain_enabled = enabled != 0 ? 1u : 0u;
    }
    if (config_get_bool(&tree, "map.enable_los_occlusion", &enabled) == SIM_OK) {
        out->los_occlusion_enabled = enabled != 0 ? 1u : 0u;
    }
    if (out->terrain_enabled != 0u || out->los_occlusion_enabled != 0u) {
        status = require_string_value(&tree, "map.terrain.interpolation",
                                      "BILINEAR", error);
        if (status == SIM_OK) {
            status = require_string_value(&tree, "map.terrain.height_reference",
                                          "ELLIPSOID", error);
        }
        if (status != SIM_OK) return finish(status, error == nullptr ?
            "unsupported terrain configuration" : *error);
    }
    std::string policy;
    if (!get_optional_string(&tree, "map.missing_tile_policy", &policy) || policy == "FLAT_FILL") {
        out->terrain_missing_policy = static_cast<std::uint32_t>(DeviceTerrainMissingPolicy::FlatFill);
    } else if (policy == "ERROR") {
        out->terrain_missing_policy = static_cast<std::uint32_t>(DeviceTerrainMissingPolicy::Error);
    } else if (policy == "NEAREST") {
        out->terrain_missing_policy = static_cast<std::uint32_t>(DeviceTerrainMissingPolicy::Nearest);
    } else {
        return finish(SIM_ERR_CONFIG, "invalid terrain missing-tile policy");
    }
    (void)config_get_double(&tree, "map.terrain.flat_fill_height_m",
                            &out->terrain_flat_fill_height_m);
    unsigned int cache_count = 16u;
    (void)config_get_uint32(&tree, "map.terrain.cache_tile_count", &cache_count);
    if (cache_count == 0u || cache_count > 64u) {
        return finish(SIM_ERR_OUT_OF_RANGE, "invalid terrain cache size");
    }
    out->terrain_cache_tile_count = cache_count;
    (void)get_optional_string(&tree, "map.database_path", &host->map_database_path);
    (void)get_optional_string(&tree, "map.tile_index", &host->map_tile_index_path);

    std::string resolved;
    bool asset_found = false;
    status = resolve_optional_asset(&tree, "map.tile_path", path, &resolved,
                                    &asset_found, error);
    if (status != SIM_OK) return finish(status, error == nullptr ? "terrain tile error" : *error);
    if (asset_found) terrain->tile_paths.push_back(resolved);
    size_t tile_count = 0u;
    if (config_get_array_count(&tree, "map.tile_paths", &tile_count) == SIM_OK) {
        if (tile_count > 64u) return finish(SIM_ERR_OUT_OF_RANGE, "too many terrain tiles");
        for (size_t index = 0u; index < tile_count; ++index) {
            char key[64];
            char raw[PATH_MAX];
            std::snprintf(key, sizeof(key), "map.tile_paths[%u]",
                          static_cast<unsigned int>(index));
            status = config_get_string(&tree, key, raw, sizeof(raw));
            if (status != SIM_OK || !resolve_reference(path, raw, &resolved)) {
                return finish(status == SIM_OK ? SIM_ERR_IO : status,
                              "terrain tile not found: " + std::string(raw));
            }
            terrain->tile_paths.push_back(resolved);
        }
    }
    status = resolve_optional_asset(&tree, "map.tile_index_path", path,
                                    &terrain->index_path, &asset_found, error);
    if (status != SIM_OK) {
        return finish(status, error == nullptr ? "terrain index error" : *error);
    }
    terrain->key = terrain->index_path;
    for (const std::string &tile : terrain->tile_paths) terrain->key += "|" + tile;

    return finish(SIM_OK, "");
}

std::uint32_t intern_string(const std::string &value,
                            std::vector<std::string> *assets) {
    const auto found = std::find(assets->begin(), assets->end(), value);
    if (found != assets->end()) {
        return static_cast<std::uint32_t>(found - assets->begin());
    }
    assets->push_back(value);
    return static_cast<std::uint32_t>(assets->size() - 1u);
}

std::uint32_t intern_terrain(const HostTerrainAsset &value,
                             std::vector<HostTerrainAsset> *assets) {
    for (std::uint32_t index = 0u; index < assets->size(); ++index) {
        if ((*assets)[index].key == value.key) return index;
    }
    assets->push_back(value);
    return static_cast<std::uint32_t>(assets->size() - 1u);
}

SimStatus load_fault_asset(const std::string &path, FaultInjection *out,
                           std::string *error) {
    ConfigTree tree{};
    SimStatus status = config_load_file(path.c_str(), &tree);
    if (status != SIM_OK) return fail(status, "cannot load faults: " + path, error);
    status = config_validate_schema(&tree, 1u);
    if (status == SIM_OK) status = fault_injection_load_config(&tree, out);
    config_free(&tree);
    if (status != SIM_OK) return fail(status, "invalid faults config: " + path, error);
    return SIM_OK;
}

std::uint32_t intern_fault(const std::string &path,
                           const FaultInjection &definition,
                           std::vector<HostFaultAsset> *assets) {
    for (std::uint32_t index = 0u; index < assets->size(); ++index) {
        if ((*assets)[index].path == path) return index;
    }
    assets->push_back(HostFaultAsset{path, definition});
    return static_cast<std::uint32_t>(assets->size() - 1u);
}

}  // namespace

SimStatus load_campaign_plan(const char *runtime_path, HostCampaignPlan *out,
                             std::string *error) {
    if (runtime_path == nullptr || out == nullptr) {
        return fail(SIM_ERR_INVALID_ARG, "runtime path and output are required", error);
    }
    std::string canonical_runtime;
    if (!canonical_existing(runtime_path, &canonical_runtime)) {
        return fail(SIM_ERR_IO, "runtime file not found: " + std::string(runtime_path), error);
    }
    ConfigTree runtime{};
    SimStatus status = config_load_file(canonical_runtime.c_str(), &runtime);
    if (status != SIM_OK) return fail(status, "cannot load runtime config", error);
    const auto finish = [&runtime, error](SimStatus result,
                                         const std::string &message) {
        config_free(&runtime);
        return result == SIM_OK ? result : fail(result, message, error);
    };
    status = config_validate_schema(&runtime, 1u);
    if (status != SIM_OK) return finish(status, "invalid runtime schema");

    *out = HostCampaignPlan{};
    out->runtime_path = canonical_runtime;
    out->instance_dir_template = "instance_${instance_id}";
    out->requested_schedule = "PARALLEL";
    out->device.chunk_steps = 256u;
    out->device.trace_mode = 0u;
    out->binary_logs = true;
    out->event_log = true;
    unsigned int value = 0u;
    status = config_get_uint32(&runtime, "campaign.instance_count", &value);
    if (status != SIM_OK || value == 0u || value > 65535u) {
        return finish(status == SIM_OK ? SIM_ERR_OUT_OF_RANGE : status,
                      "invalid campaign.instance_count");
    }
    out->configured_instance_count = value;
    (void)get_optional_string(&runtime, "campaign.campaign_id", &out->campaign_id);
    if (config_get_uint32(&runtime, "campaign.max_parallel_instances", &value) != SIM_OK || value == 0u) value = 1u;
    out->max_parallel_instances = value;
    if (config_get_uint32(&runtime, "campaign.base_random_seed", &value) == SIM_OK) {
        out->base_random_seed = value;
    } else {
        out->base_random_seed = 1u;
    }
    std::string strategy = "CONTINUE_ON_FAILURE";
    (void)get_optional_string(&runtime, "campaign.failure_strategy", &strategy);
    if (strategy == "CONTINUE_ON_FAILURE") {
        out->device.failure_strategy = static_cast<std::uint32_t>(DeviceFailureStrategy::ContinueOnFailure);
    } else if (strategy == "STOP_ON_FAILURE") {
        out->device.failure_strategy = static_cast<std::uint32_t>(DeviceFailureStrategy::StopOnFailure);
    } else {
        return finish(SIM_ERR_CONFIG, "invalid campaign.failure_strategy");
    }
    (void)get_optional_string(&runtime, "campaign.schedule", &out->requested_schedule);
    if (out->requested_schedule != "PARALLEL" && out->requested_schedule != "SEQUENTIAL") {
        return finish(SIM_ERR_CONFIG, "invalid campaign.schedule");
    }

    if (!get_optional_string(&runtime, "logging.output_dir", &out->output_dir)) {
        return finish(SIM_ERR_CONFIG, "logging.output_dir is required");
    }
    (void)get_optional_string(&runtime, "logging.instance_dir_template", &out->instance_dir_template);
    int bool_value = 1;
    if (config_get_bool(&runtime, "logging.binary_logs", &bool_value) == SIM_OK) out->binary_logs = bool_value != 0;
    bool_value = 1;
    if (config_get_bool(&runtime, "logging.event_log", &bool_value) == SIM_OK) out->event_log = bool_value != 0;
    if (config_get_uint32(&runtime, "logging.flush_every_steps", &value) != SIM_OK || value == 0u) value = 100u;
    out->flush_every_steps = value;

    (void)get_optional_string(&runtime, "network.host", &out->network_host);
    if (config_get_uint32(&runtime, "network.environment_base_port", &value) == SIM_OK) out->environment_base_port = value;
    if (config_get_uint32(&runtime, "network.flight_control_base_port", &value) == SIM_OK) out->flight_control_base_port = value;
    (void)get_optional_string(&runtime, "tools.environment_program", &out->environment_program);
    (void)get_optional_string(&runtime, "tools.flight_control_program", &out->flight_control_program);

    size_t raw_count = 0u;
    status = config_get_array_count(&runtime, "instances", &raw_count);
    if (status != SIM_OK || raw_count == 0u || raw_count > 65535u) {
        return finish(status == SIM_OK ? SIM_ERR_OUT_OF_RANGE : status,
                      "invalid instances array");
    }
    for (size_t index = 0u; index < raw_count; ++index) {
        char key[96];
        int enabled = 1;
        std::snprintf(key, sizeof(key), "instances[%u].enabled",
                      static_cast<unsigned int>(index));
        (void)config_get_bool(&runtime, key, &enabled);
        if (enabled == 0) continue;

        HostInstancePlan instance{};
        instance.device.aero_asset_index = kInvalidAssetIndex;
        instance.device.aero_surrogate_asset_index = kInvalidAssetIndex;
        instance.device.terrain_asset_index = kInvalidAssetIndex;
        instance.device.fault_asset_index = kInvalidAssetIndex;
        std::snprintf(key, sizeof(key), "instances[%u].instance_id",
                      static_cast<unsigned int>(index));
        status = config_get_uint32(&runtime, key, &instance.device.instance_id);
        if (status != SIM_OK) return finish(status, "missing instance_id");
        for (const HostInstancePlan &existing : out->instances) {
            if (existing.device.instance_id == instance.device.instance_id) {
                return finish(SIM_ERR_CONFIG, "duplicate instance_id");
            }
        }
        std::snprintf(key, sizeof(key), "instances[%u].random_seed",
                      static_cast<unsigned int>(index));
        if (config_get_uint32(&runtime, key, &value) == SIM_OK) {
            instance.device.random_seed = value;
        } else {
            instance.device.random_seed = out->base_random_seed + instance.device.instance_id;
        }

        struct ReferenceField { const char *name; std::string *destination; };
        ReferenceField refs[] = {{"scenario", &instance.scenario_path},
                                 {"flight_control", &instance.flight_control_path},
                                 {"faults", &instance.faults_path}};
        for (ReferenceField &ref : refs) {
            char raw[PATH_MAX];
            std::snprintf(key, sizeof(key), "instances[%u].%s",
                          static_cast<unsigned int>(index), ref.name);
            status = config_get_string(&runtime, key, raw, sizeof(raw));
            if (status != SIM_OK || !resolve_reference(canonical_runtime, raw, ref.destination)) {
                return finish(status == SIM_OK ? SIM_ERR_IO : status,
                              "cannot resolve instance reference " + std::string(ref.name));
            }
        }

        std::string aero_path;
        std::string surrogate_path;
        HostTerrainAsset terrain;
        status = load_scenario_config(instance.scenario_path, &instance.device,
                                      &instance, &aero_path, &surrogate_path,
                                      &terrain, error);
        if (status != SIM_OK) return finish(status, error == nullptr ? "scenario mapping failed" : *error);
        status = load_flight_control_config(instance.flight_control_path,
                                            &instance.device.flight_control, error);
        if (status != SIM_OK) return finish(status, error == nullptr ? "flight-control mapping failed" : *error);
        if (!aero_path.empty()) {
            instance.device.aero_asset_index = intern_string(aero_path, &out->aero_assets);
        }
        if (!surrogate_path.empty()) {
            instance.device.aero_surrogate_asset_index = intern_string(
                surrogate_path, &out->aero_surrogate_assets);
        }
        if (!terrain.key.empty()) {
            instance.device.terrain_asset_index = intern_terrain(terrain, &out->terrain_assets);
        }
        FaultInjection faults{};
        status = load_fault_asset(instance.faults_path, &faults, error);
        if (status != SIM_OK) return finish(status, error == nullptr ? "fault mapping failed" : *error);
        instance.device.fault_asset_index = intern_fault(instance.faults_path, faults,
                                                         &out->fault_assets);
        out->instances.push_back(instance);
    }
    if (out->instances.empty()) return finish(SIM_ERR_CONFIG, "no enabled instances");
    out->device.instance_count = static_cast<std::uint32_t>(out->instances.size());
    if (out->max_parallel_instances > out->device.instance_count) {
        out->max_parallel_instances = out->device.instance_count;
    }
    return finish(SIM_OK, "");
}

}  // namespace gpu_campaign
