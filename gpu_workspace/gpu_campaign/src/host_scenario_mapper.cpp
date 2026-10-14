#include "host_scenario_mapper.hpp"

#include <cstdio>
#include <cstring>

extern "C" {
#include "common/config.h"
}

namespace gpu_campaign {

namespace {

constexpr std::size_t kTablePathSize = sizeof(DeviceScenarioAero::table_path);

/* Mirrors env_app.c make_noise_config / load_scalar_noise_config: start from
   the sensor-specific min/max clamp, then override with the JSON fields that
   are present. */
SensorNoiseConfig make_noise_config(double minimum, double maximum) {
    SensorNoiseConfig config{};
    config.min_value = minimum;
    config.max_value = maximum;
    return config;
}

void load_scalar_noise_field(const ConfigTree *config, const char *prefix,
                             const char *json_name, double SensorNoiseConfig::*member,
                             SensorNoiseConfig *out) {
    char path[192];
    double value = 0.0;
    std::snprintf(path, sizeof(path), "%s.%s", prefix, json_name);
    if (config_get_double(config, path, &value) == SIM_OK) {
        out->*member = value;
    }
}

void load_scalar_noise_config(const ConfigTree *config, const char *prefix,
                              double minimum, double maximum,
                              SensorNoiseConfig *out) {
    *out = make_noise_config(minimum, maximum);
    load_scalar_noise_field(config, prefix, "bias", &SensorNoiseConfig::bias, out);
    load_scalar_noise_field(config, prefix, "white_noise_std",
                            &SensorNoiseConfig::white_noise_std, out);
    load_scalar_noise_field(config, prefix, "random_walk_std",
                            &SensorNoiseConfig::random_walk_std, out);
    load_scalar_noise_field(config, prefix, "min_value",
                            &SensorNoiseConfig::min_value, out);
    load_scalar_noise_field(config, prefix, "max_value",
                            &SensorNoiseConfig::max_value, out);
    load_scalar_noise_field(config, prefix, "resolution",
                            &SensorNoiseConfig::resolution, out);
}

/* Mirrors env_app.c load_vector_noise_config: one common noise object with
   per-axis bias override. */
void load_vector_noise_config(const ConfigTree *config, const char *prefix,
                              double minimum, double maximum,
                              SensorNoiseConfig axis[3]) {
    char path[192];
    double bias[3] = {0.0, 0.0, 0.0};
    load_scalar_noise_config(config, prefix, minimum, maximum, &axis[0]);
    for (int index = 0; index < 3; ++index) {
        axis[index] = axis[0];
    }
    std::snprintf(path, sizeof(path), "%s.bias_xyz", prefix);
    if (config_get_double_array(config, path, bias, 3u) == SIM_OK) {
        for (int index = 0; index < 3; ++index) {
            axis[index].bias = bias[index];
        }
    }
}

/* Mirrors env_app.c load_vector_sensor_config. sample_period defaults to the
   simulation dt; enabled is a hard requirement. */
SimStatus load_vector_sensor_config(const ConfigTree *config, const char *prefix,
                                    double simulation_dt_s, double minimum,
                                    double maximum, SensorVector3Config *out) {
    char path[192];
    *out = SensorVector3Config{};
    out->sample_period_s = simulation_dt_s;
    std::snprintf(path, sizeof(path), "%s.enabled", prefix);
    SimStatus status = config_get_bool(config, path, &out->enabled);
    if (status != SIM_OK) {
        return status;
    }
    std::snprintf(path, sizeof(path), "%s.sample_period_s", prefix);
    (void)config_get_double(config, path, &out->sample_period_s);
    std::snprintf(path, sizeof(path), "%s.delay_s", prefix);
    (void)config_get_double(config, path, &out->delay_s);
    std::snprintf(path, sizeof(path), "%s.dropout_probability", prefix);
    (void)config_get_double(config, path, &out->dropout_probability);
    std::snprintf(path, sizeof(path), "%s.noise", prefix);
    load_vector_noise_config(config, path, minimum, maximum, out->axis);
    return SIM_OK;
}

/* Mirrors env_app.c load_seeker_config (clamps: range [0,1e9], closing
   velocity [-1e5,1e5], los_unit [-2,2], los_rate [-100,100]). */
SimStatus load_seeker_config(const ConfigTree *config, double simulation_dt_s,
                             SeekerSensorConfig *out) {
    char path[192];
    *out = SeekerSensorConfig{};
    out->sample_period_s = simulation_dt_s;
    SimStatus status = config_get_bool(config, "sensors.seeker.enabled", &out->enabled);
    if (status != SIM_OK) {
        return status;
    }
    (void)config_get_double(config, "sensors.seeker.sample_period_s",
                            &out->sample_period_s);
    (void)config_get_double(config, "sensors.seeker.delay_s", &out->delay_s);
    (void)config_get_double(config, "sensors.seeker.dropout_probability",
                            &out->dropout_probability);

    load_scalar_noise_config(config, "sensors.seeker.range_noise", 0.0, 1.0e9,
                             &out->range);
    load_vector_noise_config(config, "sensors.seeker.los_unit_noise", -2.0, 2.0,
                             out->los_unit_axis);
    load_vector_noise_config(config, "sensors.seeker.los_rate_noise", -100.0, 100.0,
                             out->los_rate_axis);
    load_scalar_noise_config(config, "sensors.seeker.closing_velocity_noise",
                             -1.0e5, 1.0e5, &out->closing_velocity);
    return SIM_OK;
}

}  // namespace

SimStatus load_scenario_aero(const char *scenario_path, DeviceScenarioAero *out) {
    if (scenario_path == nullptr || out == nullptr) {
        return SIM_ERR_INVALID_ARG;
    }
    ConfigTree scenario{};
    SimStatus status = config_load_file(scenario_path, &scenario);
    if (status != SIM_OK) {
        return status;
    }
    const auto finish = [&scenario](SimStatus result) {
        config_free(&scenario);
        return result;
    };

    /* Zero-init first so missing keys keep the env_app.c defaults. */
    std::memset(out, 0, sizeof(*out));
    out->force.enabled = 0u;
    out->drag_coefficient = 0.0;
    out->wind_velocity_ecef_mps = vec3_make(0.0, 0.0, 0.0);
    out->policy = DeviceExtrapolationPolicy::ClampAndWarn;
    out->table_path[0] = '\0';

    int enabled = 0;
    status = config_get_bool(&scenario, "aerodynamics.enabled", &enabled);
    if (status == SIM_OK) {
        out->force.enabled = enabled != 0 ? 1u : 0u;
    } else if (status == SIM_ERR_OUT_OF_RANGE) {
        return finish(status);
    }

    double value = 0.0;
    status = config_get_double(
        &scenario, "aerodynamics.reference_area_m2", &value);
    if (status == SIM_OK) {
        out->force.reference_area_m2 = value;
    } else if (status == SIM_ERR_OUT_OF_RANGE) {
        return finish(status);
    }

    status = config_get_double(
        &scenario, "aerodynamics.reference_length_m", &value);
    if (status == SIM_OK) {
        out->force.reference_length_m = value;
    } else if (status == SIM_ERR_OUT_OF_RANGE) {
        return finish(status);
    }

    status = config_get_double(
        &scenario, "aerodynamics.drag_coefficient", &value);
    if (status == SIM_OK) {
        out->drag_coefficient = value;
    } else if (status == SIM_ERR_OUT_OF_RANGE) {
        return finish(status);
    }

    status = config_get_double(
        &scenario, "aerodynamics.control_force_coefficient", &value);
    if (status == SIM_OK) {
        out->force.control_force_coefficient = value;
    } else if (status == SIM_ERR_OUT_OF_RANGE) {
        return finish(status);
    }

    status = config_get_double(
        &scenario, "aerodynamics.control_moment_coefficient", &value);
    if (status == SIM_OK) {
        out->force.control_moment_coefficient = value;
    } else if (status == SIM_ERR_OUT_OF_RANGE) {
        return finish(status);
    }

    double wind[3] = {0.0, 0.0, 0.0};
    status = config_get_double_array(
        &scenario, "atmosphere.wind_velocity_ecef_mps", wind, 3u);
    if (status == SIM_OK) {
        out->wind_velocity_ecef_mps = vec3_make(wind[0], wind[1], wind[2]);
    } else if (status == SIM_ERR_OUT_OF_RANGE) {
        return finish(status);
    }

    char path[kTablePathSize];
    status = config_get_string(
        &scenario, "aerodynamics.table_path", path, sizeof(path));
    if (status == SIM_OK) {
        std::memcpy(out->table_path, path, sizeof(out->table_path));
        out->table_path_enabled = 1u;
    } else if (status == SIM_ERR_OUT_OF_RANGE) {
        return finish(status);
    }

    char policy[kTablePathSize];
    status = config_get_string(
        &scenario, "aerodynamics.table_extrapolation_policy",
        policy, sizeof(policy));
    if (status == SIM_OK) {
        if (std::strcmp(policy, "ERROR") == 0) {
            out->policy = DeviceExtrapolationPolicy::Error;
        } else if (std::strcmp(policy, "CLAMP_AND_WARN") == 0) {
            out->policy = DeviceExtrapolationPolicy::ClampAndWarn;
        } else if (std::strcmp(policy, "HOLD_LAST_VALID") == 0) {
            out->policy = DeviceExtrapolationPolicy::HoldLastValid;
        } else {
            return finish(SIM_ERR_CONFIG);
        }
        out->policy_override_enabled = 1u;
    } else if (status == SIM_ERR_OUT_OF_RANGE) {
        return finish(status);
    }

    return finish(SIM_OK);
}

SimStatus load_scenario_sensors(const char *scenario_path, double simulation_dt_s,
                                 DeviceSensorScenario *out) {
    if (scenario_path == nullptr || out == nullptr) {
        return SIM_ERR_INVALID_ARG;
    }
    ConfigTree scenario{};
    SimStatus status = config_load_file(scenario_path, &scenario);
    if (status != SIM_OK) {
        return status;
    }
    const auto finish = [&scenario](SimStatus result) {
        config_free(&scenario);
        return result;
    };

    std::memset(out, 0, sizeof(*out));
    /* env_app.c clamps: imu gyro +/-100, accelerometer/speedometer +/-1e5. */
    status = load_vector_sensor_config(&scenario, "sensors.imu", simulation_dt_s,
                                       -100.0, 100.0, &out->imu);
    if (status == SIM_OK) {
        status = load_vector_sensor_config(&scenario, "sensors.accelerometer",
                                           simulation_dt_s, -1.0e5, 1.0e5,
                                           &out->accel);
    }
    if (status == SIM_OK) {
        status = load_vector_sensor_config(&scenario, "sensors.speedometer",
                                           simulation_dt_s, -1.0e5, 1.0e5,
                                           &out->speed);
    }
    if (status == SIM_OK) {
        status = load_seeker_config(&scenario, simulation_dt_s, &out->seeker);
    }
    if (status != SIM_OK) {
        return finish(status);
    }
    out->enabled = 1u;
    return finish(SIM_OK);
}

SimStatus load_fault_script(const char *faults_path, FaultInjection *out) {
    if (faults_path == nullptr || out == nullptr) {
        return SIM_ERR_INVALID_ARG;
    }
    ConfigTree faults_tree{};
    SimStatus status = config_load_file(faults_path, &faults_tree);
    if (status != SIM_OK) {
        return status;
    }
    status = fault_injection_load_config(&faults_tree, out);
    config_free(&faults_tree);
    if (status != SIM_OK) {
        return status;
    }
    return SIM_OK;
}

}  // namespace gpu_campaign
