#include "production_step.cuh"

#include <cmath>
#include <cstddef>

#include "device_force.cuh"

namespace gpu_campaign {
namespace {

constexpr std::uint32_t kModelDegradationAeroFlags = 1u << 0u;
constexpr std::uint32_t kModelDegradationTerrainWarning = 1u << 1u;
constexpr std::uint32_t kModelDegradationMassInvalid = 1u << 2u;
constexpr std::uint32_t kModelDegradationInertiaInvalid = 1u << 3u;
constexpr std::uint32_t kModelDegradationAttitudeError = 1u << 4u;
constexpr std::uint32_t kModelDegradationDcmError = 1u << 5u;

__device__ void terminate(DeviceInstanceState *state, DeviceExitReason reason,
                          SimStatus status) {
    state->active = 0u;
    state->exit_reason = static_cast<std::uint32_t>(reason);
    state->terminal_status = static_cast<std::uint32_t>(status);
}

__device__ std::uint32_t active_fault_mask(const FaultInjection &faults) {
    std::uint32_t mask = 0u;
    const std::size_t count = faults.fault_count < ENV_MAX_FAULTS
        ? faults.fault_count : ENV_MAX_FAULTS;
    for (std::size_t index = 0u; index < count; ++index) {
        if (faults.faults[index].was_active != 0) {
            mask |= (1u << static_cast<std::uint32_t>(index));
        }
    }
    return mask;
}

__device__ bool fault_effects_affect_sensor(const FaultStepEffects &effects) {
    return effects.sensor_valid_clear_mask != 0u ||
        effects.sensor_fault_set_mask != 0u ||
        effects.seeker_range_bias_m != 0.0 ||
        vec3_norm(effects.seeker_los_unit_bias) > 0.0 ||
        vec3_norm(effects.seeker_los_rate_bias_radps) > 0.0 ||
        effects.seeker_closing_velocity_bias_mps != 0.0 ||
        vec3_norm(effects.gyro_bias_b_radps) > 0.0 ||
        vec3_norm(effects.accel_bias_ecef_mps2) > 0.0 ||
        vec3_norm(effects.speed_bias_ecef_mps) > 0.0 ||
        effects.communication_delay_enabled != 0 ||
        effects.communication_reorder_enabled != 0;
}

__device__ bool fault_effects_affect_actuator(const FaultStepEffects &effects) {
    for (std::uint32_t index = 0u; index < 3u; ++index) {
        if (effects.actuator_stuck[index] != 0 ||
            effects.actuator_command_scale[index] != 1.0) return true;
    }
    return false;
}

__device__ SimStatus terrain_height(
    const DeviceInstanceConfig &config,
    const DeviceTerrainSet *terrain_sets, std::uint32_t terrain_set_count,
    const DeviceTerrainGrid *terrain_grids, std::uint32_t terrain_grid_count,
    double latitude_rad, double longitude_rad, double *height_m,
    std::uint32_t *flags) {
    if (height_m == nullptr || !__builtin_isfinite(latitude_rad) ||
        !__builtin_isfinite(longitude_rad)) return SIM_ERR_INVALID_ARG;
    if (flags != nullptr) *flags = 0u;
    if (config.terrain_enabled == 0u) {
        *height_m = config.terrain_flat_fill_height_m;
        return SIM_OK;
    }

    if (config.terrain_asset_index != kInvalidAssetIndex) {
        if (terrain_sets == nullptr ||
            config.terrain_asset_index >= terrain_set_count) {
            return SIM_ERR_CONFIG;
        }
        const DeviceTerrainSet &set = terrain_sets[config.terrain_asset_index];
        if (set.first_grid > terrain_grid_count ||
            set.grid_count > terrain_grid_count - set.first_grid ||
            (set.grid_count != 0u && terrain_grids == nullptr)) {
            return SIM_ERR_CONFIG;
        }
        for (std::uint32_t offset = 0u; offset < set.grid_count; ++offset) {
            const DeviceTerrainGrid &grid = terrain_grids[set.first_grid + offset];
            if (latitude_rad >= grid.lat_min && latitude_rad <= grid.lat_max &&
                longitude_rad >= grid.lon_min && longitude_rad <= grid.lon_max) {
                return device_terrain_grid_height(
                    &grid, latitude_rad, longitude_rad, height_m, flags);
            }
        }
    }

    const DeviceTerrainMissingPolicy policy =
        static_cast<DeviceTerrainMissingPolicy>(config.terrain_missing_policy);
    if (policy == DeviceTerrainMissingPolicy::FlatFill) {
        if (flags != nullptr) *flags |= kDeviceTerrainFlagMissingTile;
        *height_m = config.terrain_flat_fill_height_m;
        return SIM_OK;
    }
    if (policy == DeviceTerrainMissingPolicy::Nearest &&
        config.terrain_asset_index != kInvalidAssetIndex &&
        terrain_sets != nullptr && terrain_grids != nullptr &&
        config.terrain_asset_index < terrain_set_count) {
        const DeviceTerrainSet &set = terrain_sets[config.terrain_asset_index];
        const DeviceTerrainGrid *nearest = nullptr;
        double nearest_distance = 1.0e300;
        for (std::uint32_t offset = 0u; offset < set.grid_count; ++offset) {
            const DeviceTerrainGrid &grid = terrain_grids[set.first_grid + offset];
            const double sample_lat = latitude_rad < grid.lat_min ? grid.lat_min :
                (latitude_rad > grid.lat_max ? grid.lat_max : latitude_rad);
            const double sample_lon = longitude_rad < grid.lon_min ? grid.lon_min :
                (longitude_rad > grid.lon_max ? grid.lon_max : longitude_rad);
            const double dlat = latitude_rad - sample_lat;
            const double dlon = longitude_rad - sample_lon;
            const double distance = dlat * dlat + dlon * dlon;
            if (nearest == nullptr || distance < nearest_distance) {
                nearest = &grid;
                nearest_distance = distance;
            }
        }
        if (nearest != nullptr) {
            const double sample_lat = latitude_rad < nearest->lat_min ?
                nearest->lat_min : (latitude_rad > nearest->lat_max ?
                    nearest->lat_max : latitude_rad);
            const double sample_lon = longitude_rad < nearest->lon_min ?
                nearest->lon_min : (longitude_rad > nearest->lon_max ?
                    nearest->lon_max : longitude_rad);
            if (flags != nullptr) *flags |= kDeviceTerrainFlagMissingTile;
            return device_terrain_grid_height(
                nearest, sample_lat, sample_lon, height_m, nullptr);
        }
    }
    return policy == DeviceTerrainMissingPolicy::Error ||
            policy == DeviceTerrainMissingPolicy::Nearest
        ? SIM_ERR_OUT_OF_RANGE : SIM_ERR_CONFIG;
}

__device__ SimStatus line_of_sight_occluded(
    const DeviceInstanceConfig &config,
    const DeviceTerrainSet *terrain_sets, std::uint32_t terrain_set_count,
    const DeviceTerrainGrid *terrain_grids, std::uint32_t terrain_grid_count,
    Vec3 start_ecef_m, Vec3 end_ecef_m, std::uint32_t *terrain_flags,
    int *occluded) {
    if (occluded == nullptr || !vec3_isfinite(start_ecef_m) ||
        !vec3_isfinite(end_ecef_m)) return SIM_ERR_INVALID_ARG;
    *occluded = 0;
    if (terrain_flags != nullptr) *terrain_flags = 0u;
    const Vec3 segment = vec3_sub(end_ecef_m, start_ecef_m);
    for (std::uint32_t index = 1u; index <= 16u; ++index) {
        const double fraction = static_cast<double>(index) / 17.0;
        EcefCoord sample_ecef{};
        LlaCoord sample_lla{};
        double terrain_height_m = 0.0;
        std::uint32_t flags = 0u;
        sample_ecef.position_m = vec3_add(
            start_ecef_m, vec3_scale(segment, fraction));
        SimStatus status = geo_ecef_to_lla(
            &config.earth, &sample_ecef, &sample_lla);
        if (status == SIM_OK) {
            status = terrain_height(
                config, terrain_sets, terrain_set_count, terrain_grids,
                terrain_grid_count, sample_lla.lat_rad, sample_lla.lon_rad,
                &terrain_height_m, &flags);
        }
        if (terrain_flags != nullptr) *terrain_flags |= flags;
        if (status != SIM_OK) return status;
        if (sample_lla.height_m <= terrain_height_m) {
            *occluded = 1;
            return SIM_OK;
        }
    }
    return SIM_OK;
}

__device__ SimStatus update_geodetic(
    DeviceInstanceState *state, const DeviceInstanceConfig &config,
    const DeviceTerrainSet *terrain_sets, std::uint32_t terrain_set_count,
    const DeviceTerrainGrid *terrain_grids, std::uint32_t terrain_grid_count) {
    EcefCoord missile_ecef{};
    EcefCoord target_ecef{};
    LlaCoord missile_lla{};
    double terrain_height_m = 0.0;
    std::uint32_t terrain_flags = 0u;
    missile_ecef.position_m = state->plant.pos_ecef;
    target_ecef.position_m = state->sensors.target_position_ecef;
    SimStatus status = geo_ecef_to_lla(&config.earth, &missile_ecef,
                                       &missile_lla);
    if (status == SIM_OK) {
        status = geo_ecef_to_lla(&config.earth, &target_ecef,
                                 &state->target_lla);
    }
    if (status == SIM_OK) {
        status = terrain_height(config, terrain_sets, terrain_set_count,
                                terrain_grids, terrain_grid_count,
                                missile_lla.lat_rad, missile_lla.lon_rad,
                                &terrain_height_m, &terrain_flags);
    }
    if (status != SIM_OK) return status;
    state->plant.lat_rad = missile_lla.lat_rad;
    state->plant.lon_rad = missile_lla.lon_rad;
    state->plant.height_m = missile_lla.height_m;
    state->plant.height_agl_m = config.terrain_enabled != 0u
        ? missile_lla.height_m - terrain_height_m : missile_lla.height_m;
    if ((terrain_flags & kDeviceTerrainFlagMissingTile) != 0u) {
        state->environment_flags |= 1u << 2u;
    }
    return SIM_OK;
}

__device__ SimStatus initialize_instance(
    DeviceInstanceState *state, const DeviceInstanceConfig &config,
    const DeviceTerrainSet *terrain_sets, std::uint32_t terrain_set_count,
    const DeviceTerrainGrid *terrain_grids, std::uint32_t terrain_grid_count,
    const FaultInjection *fault_scripts, std::uint32_t fault_script_count) {
    state->instance_id = config.instance_id;
    state->plant = PlantState6Dof{};
    state->plant.pos_ecef = config.initial_missile_position_ecef;
    state->plant.vel_ecef = config.initial_missile_velocity_ecef;
    state->plant.q_bi = quat_identity();
    state->plant.mass = config.initial_mass_kg;
    state->plant.inertia_b = config.initial_inertia_b;
    state->initial_inertia_b = config.initial_inertia_b;
    state->initial_mass_kg = config.initial_mass_kg;
    state->sensors.target_position_ecef = config.initial_target_position_ecef;
    state->sensors.target_velocity_ecef = config.initial_target_velocity_ecef;
    state->active = 1u;
    state->exit_reason = static_cast<std::uint32_t>(DeviceExitReason::Active);
    state->terminal_status = SIM_OK;
    state->min_range_m = vec3_norm(vec3_sub(
        state->sensors.target_position_ecef, state->plant.pos_ecef));
    state->time_of_closest_s = 0.0;

    SimStatus status = mass_model_init(
        &state->mass,
        config.initial_mass_kg - config.initial_propellant_mass_kg,
        config.initial_propellant_mass_kg);
    for (std::uint32_t index = 0u; index < 3u; ++index) {
        state->acceleration_actuators[index] = ActuatorState{};
        state->acceleration_actuators[index].pos_min =
            -config.actuator_position_limit;
        state->acceleration_actuators[index].pos_max =
            config.actuator_position_limit;
        state->acceleration_actuators[index].rate_limit =
            config.actuator_rate_limit;
        state->acceleration_actuators[index].time_constant =
            config.actuator_time_constant_s;
    }
    if (status == SIM_OK) {
        status = sensor_imu_init(
            &state->sensors.imu, &config.sensors.imu,
            config.random_seed ^ UINT64_C(0x494D5501), config.dt);
    }
    if (status == SIM_OK) {
        status = sensor_accel_init(
            &state->sensors.accel, &config.sensors.accel,
            config.random_seed ^ UINT64_C(0x41434301), config.dt);
    }
    if (status == SIM_OK) {
        status = sensor_speed_init(
            &state->sensors.speed, &config.sensors.speed,
            config.random_seed ^ UINT64_C(0x53504401), config.dt);
    }
    if (status == SIM_OK) {
        status = sensor_seeker_init(
            &state->sensors.seeker, &config.sensors.seeker,
            config.random_seed ^ UINT64_C(0x53454B01), config.dt);
    }
    if (status == SIM_OK) {
        status = flight_controller_init(&state->flight_controller,
                                        &config.flight_control);
    }
    if (status == SIM_OK) {
        if (config.fault_asset_index == kInvalidAssetIndex) {
            fault_injection_init_empty(&state->faults);
        } else if (fault_scripts == nullptr ||
                   config.fault_asset_index >= fault_script_count) {
            status = SIM_ERR_CONFIG;
        } else {
            state->faults = fault_scripts[config.fault_asset_index];
            state->fault_stats.configured_fault_count =
                static_cast<std::uint32_t>(state->faults.fault_count);
        }
    }
    bool terrain_failed = false;
    if (status == SIM_OK) {
        status = update_geodetic(state, config, terrain_sets,
                                 terrain_set_count, terrain_grids,
                                 terrain_grid_count);
        terrain_failed = status != SIM_OK;
    }
    state->initialized = status == SIM_OK ? 1u : (terrain_failed ? 2u : 0u);
    return status;
}

__device__ void apply_sample_status(
    SensorFrame *frame, const SensorSampleStatus &sample_status,
    std::uint32_t valid_flag, std::uint32_t dropout_flag) {
    if (sample_status.valid != 0) frame->sensor_valid_flags |= valid_flag;
    if (sample_status.dropped != 0) frame->sensor_fault_flags |= dropout_flag;
    if (sample_status.delay_warmup != 0) {
        frame->sensor_fault_flags |= SIM_SENSOR_FAULT_DELAY_WARMUP;
    }
}

__device__ SimStatus build_sensor_frame(
    DeviceInstanceState *state, const DeviceInstanceConfig &config,
    SensorFrame *frame) {
    if (state == nullptr || frame == nullptr) return SIM_ERR_INVALID_ARG;
    *frame = SensorFrame{};
    frame->seq = state->global_step;
    frame->sim_time = state->plant.time;
    frame->dt = config.dt;
    frame->missile_lat_rad_meas = state->plant.lat_rad;
    frame->missile_lon_rad_meas = state->plant.lon_rad;
    frame->missile_height_m_meas = state->plant.height_m;
    frame->missile_height_agl_m_meas = state->plant.height_agl_m;
    frame->sensor_valid_flags = SIM_SENSOR_VALID_GEODETIC;

    SensorSampleStatus imu_status{};
    SensorSampleStatus accel_status{};
    SensorSampleStatus speed_status{};
    SensorSampleStatus seeker_status{};
    SimStatus status = sensor_imu_update(
        &state->sensors.imu, state->plant.time, config.dt,
        state->plant.omega_b, &frame->missile_gyro_b_meas, &imu_status);
    if (status == SIM_OK) {
        status = sensor_accel_update(
            &state->sensors.accel, state->plant.time, config.dt,
            state->plant.accel_ecef, &frame->missile_accel_ecef_meas,
            &accel_status);
    }
    if (status == SIM_OK) {
        status = sensor_speed_update(
            &state->sensors.speed, state->plant.time, config.dt,
            state->plant.vel_ecef, &frame->missile_vel_ecef_meas,
            &speed_status);
    }
    SeekerTruth truth{};
    truth.missile_position_ecef_m = state->plant.pos_ecef;
    truth.missile_velocity_ecef_mps = state->plant.vel_ecef;
    truth.target_position_ecef_m = state->sensors.target_position_ecef;
    truth.target_velocity_ecef_mps = state->sensors.target_velocity_ecef;
    SeekerMeasurement measurement{};
    if (status == SIM_OK) {
        status = sensor_seeker_update(
            &state->sensors.seeker, state->plant.time, config.dt, &truth,
            &measurement, &seeker_status);
    }
    if (status != SIM_OK) return status;
    frame->target_range_meas = measurement.range_m;
    frame->target_los_unit_ecef_meas = measurement.los_unit_ecef;
    frame->target_los_rate_ecef_meas = measurement.los_rate_ecef_radps;
    frame->target_closing_velocity_meas = measurement.closing_velocity_mps;
    apply_sample_status(frame, imu_status, SIM_SENSOR_VALID_IMU_GYRO,
                        SIM_SENSOR_FAULT_IMU_DROPOUT);
    apply_sample_status(frame, accel_status, SIM_SENSOR_VALID_ACCEL,
                        SIM_SENSOR_FAULT_ACCEL_DROPOUT);
    apply_sample_status(frame, speed_status, SIM_SENSOR_VALID_SPEED,
                        SIM_SENSOR_FAULT_SPEED_DROPOUT);
    apply_sample_status(frame, seeker_status, SIM_SENSOR_VALID_SEEKER,
                        SIM_SENSOR_FAULT_SEEKER_DROPOUT);
    return SIM_OK;
}

__device__ void communication_reset(DeviceInstanceState *state) {
    state->communication_delay = DeviceCommunicationDelayLine{};
    state->communication_reorder = DeviceCommunicationReorderState{};
}

__device__ SimStatus apply_communication_reorder(
    DeviceCommunicationReorderState *state, const FaultStepEffects &effects,
    SensorFrame *sensor) {
    if (effects.communication_reorder_enabled == 0) {
        *state = DeviceCommunicationReorderState{};
        return SIM_OK;
    }
    const SensorFrame current = *sensor;
    if (state->has_previous != 0u) {
        *sensor = state->previous;
        state->previous = current;
        return SIM_OK;
    }
    state->previous = current;
    state->has_previous = 1u;
    sensor->sensor_valid_flags &= ~(SIM_SENSOR_VALID_SEEKER |
        SIM_SENSOR_VALID_IMU_GYRO | SIM_SENSOR_VALID_ACCEL |
        SIM_SENSOR_VALID_SPEED);
    sensor->sensor_fault_flags |= SIM_SENSOR_FAULT_DELAY_WARMUP;
    return SIM_OK;
}

__device__ SimStatus apply_communication_delay(
    DeviceCommunicationDelayLine *delay, const FaultStepEffects &effects,
    SensorFrame *sensor) {
    if (effects.communication_delay_enabled == 0) {
        *delay = DeviceCommunicationDelayLine{};
        return SIM_OK;
    }
    const std::uint32_t steps = effects.communication_delay_steps;
    if (steps == 0u || steps >= kCommunicationDelayCapacity ||
        delay->count >= kCommunicationDelayCapacity) {
        return SIM_ERR_OUT_OF_RANGE;
    }
    const SensorFrame current = *sensor;
    const std::uint32_t tail =
        (delay->head + delay->count) % kCommunicationDelayCapacity;
    delay->frames[tail] = current;
    ++delay->count;
    if (delay->count > steps) {
        *sensor = delay->frames[delay->head];
        delay->head = (delay->head + 1u) % kCommunicationDelayCapacity;
        --delay->count;
        return SIM_OK;
    }
    sensor->sensor_valid_flags &= ~(SIM_SENSOR_VALID_SEEKER |
        SIM_SENSOR_VALID_IMU_GYRO | SIM_SENSOR_VALID_ACCEL |
        SIM_SENSOR_VALID_SPEED);
    sensor->sensor_fault_flags |= SIM_SENSOR_FAULT_DELAY_WARMUP;
    return SIM_OK;
}

__device__ const DeviceAeroTable *resolve_aero_table(
    const DeviceInstanceConfig &config, const DeviceAeroTable *tables,
    std::uint32_t count, SimStatus *status) {
    if (config.aero_asset_index == kInvalidAssetIndex) return nullptr;
    if (tables == nullptr || config.aero_asset_index >= count) {
        *status = SIM_ERR_CONFIG;
        return nullptr;
    }
    return &tables[config.aero_asset_index];
}

__device__ const AeroSurrogateModel *resolve_aero_surrogate(
    const DeviceInstanceConfig &config, const AeroSurrogateModel *models,
    std::uint32_t count, SimStatus *status) {
    if (config.aero_surrogate_asset_index == kInvalidAssetIndex) return nullptr;
    if (models == nullptr || config.aero_surrogate_asset_index >= count) {
        *status = SIM_ERR_CONFIG;
        return nullptr;
    }
    return &models[config.aero_surrogate_asset_index];
}

__device__ SimStatus update_truth(
    DeviceInstanceState *state, const DeviceInstanceConfig &config,
    const ControlCommand &command, const FaultStepEffects &fault_effects,
    const DeviceAeroTable *aero_tables, std::uint32_t aero_table_count,
    const AeroSurrogateModel *aero_surrogates,
    std::uint32_t aero_surrogate_count) {
    double commands[3] = {command.accel_cmd_ecef.x, command.accel_cmd_ecef.y,
                          command.accel_cmd_ecef.z};
    fault_injection_apply_actuators(
        &fault_effects, state->acceleration_actuators, commands);
    double actual[3] = {0.0, 0.0, 0.0};
    SimStatus status = SIM_OK;
    for (std::uint32_t index = 0u; index < 3u && status == SIM_OK; ++index) {
        status = actuator_model_step(&state->acceleration_actuators[index],
                                     commands[index], config.dt);
        actual[index] = state->acceleration_actuators[index].pos;
    }
    if (status != SIM_OK) return status;
    const Vec3 virtual_acceleration = vec3_make(actual[0], actual[1], actual[2]);

    Matrix3 body_to_ecef{};
    status = quat_to_dcm(state->plant.q_bi, &body_to_ecef);
    if (status != SIM_OK) return status;
    const Matrix3 ecef_to_body = matrix3_transpose(body_to_ecef);
    const Vec3 air_velocity_ecef = vec3_sub(
        state->plant.vel_ecef, config.wind_velocity_ecef_mps);
    const Vec3 air_velocity_body = matrix3_multiply_vec3(
        ecef_to_body, air_velocity_ecef);
    AtmosphereState atmosphere{};
    status = atmosphere_model_evaluate(
        &config.atmosphere, state->plant.height_m, &atmosphere);
    if (status != SIM_OK) return status;
    const double mach = atmosphere.speed_of_sound_mps > 0.0
        ? vec3_norm(air_velocity_body) / atmosphere.speed_of_sound_mps : 0.0;
    const DeviceAeroTable *table = resolve_aero_table(
        config, aero_tables, aero_table_count, &status);
    const AeroSurrogateModel *surrogate = resolve_aero_surrogate(
        config, aero_surrogates, aero_surrogate_count, &status);
    if (status != SIM_OK) return status;
    Vec3 aerodynamic_force{};
    Vec3 aerodynamic_moment{};
    std::uint32_t aero_flags = 0u;
    status = device_aero_force_evaluate_complete(
        &config.aero_force, config.aero_drag_coefficient, table, surrogate,
        atmosphere.density_kgpm3, mach, air_velocity_body,
        command.actuator_cmd[0], command.actuator_cmd[1],
        &aerodynamic_force, &aerodynamic_moment, &aero_flags);
    if (status != SIM_OK) return status;

    Vec3 propulsion_force{};
    double requested_mass_flow = 0.0;
    status = propulsion_model_evaluate(
        &config.propulsion, state->plant.time, &propulsion_force,
        &requested_mass_flow);
    if (status != SIM_OK) return status;
    double burn_fraction = 1.0;
    if (requested_mass_flow > 0.0) {
        const double requested_propellant = requested_mass_flow * config.dt;
        if (state->mass.propellant_mass_kg <= 0.0) burn_fraction = 0.0;
        else if (requested_propellant > state->mass.propellant_mass_kg) {
            burn_fraction = state->mass.propellant_mass_kg /
                            requested_propellant;
        }
    }
    propulsion_force = vec3_scale(propulsion_force, burn_fraction);
    const double mass_flow = requested_mass_flow * burn_fraction;
    const Vec3 virtual_force = matrix3_multiply_vec3(
        ecef_to_body, vec3_scale(virtual_acceleration, state->plant.mass));
    PlantInput6Dof input{};
    input.force_b_n = vec3_add(
        virtual_force, vec3_add(aerodynamic_force, propulsion_force));
    input.moment_b_nm = aerodynamic_moment;
    status = gravity_model_acceleration(
        &config.gravity, state->plant.pos_ecef,
        &input.gravity_ecef_mps2);
    if (status != SIM_OK) return status;
    input.enable_earth_rotation = config.enable_earth_rotation != 0u ? 1 : 0;
    input.earth_rotation_ecef_radps = vec3_make(
        0.0, 0.0, config.earth_rotation_rate_radps);

    state->plant_input = input;
    state->trace_mach = mach;
    state->trace_density = atmosphere.density_kgpm3;
    state->trace_aero_flags = aero_flags;
    state->last_mass_flow_kgps = mass_flow;
    status = missile_plant_step(
        &state->plant, &input, config.dt,
        static_cast<IntegratorType>(config.integrator));
    if (status != SIM_OK) return status;
    status = mass_model_step(&state->mass, mass_flow, config.dt);
    if (status != SIM_OK) return status;
    state->plant.mass = state->mass.mass_kg;
    if (state->initial_mass_kg > 0.0) {
        const double scale = state->mass.mass_kg / state->initial_mass_kg;
        for (std::uint32_t row = 0u; row < 3u; ++row) {
            for (std::uint32_t column = 0u; column < 3u; ++column) {
                state->plant.inertia_b.m[row][column] =
                    state->initial_inertia_b.m[row][column] * scale;
            }
        }
    }
    for (std::uint32_t index = 0u; index < 3u; ++index) {
        state->plant.actuator_pos[index] =
            state->acceleration_actuators[index].pos;
        state->plant.actuator_rate[index] =
            state->acceleration_actuators[index].rate;
    }
    state->sensors.target_position_ecef = vec3_add(
        state->sensors.target_position_ecef,
        vec3_scale(state->sensors.target_velocity_ecef, config.dt));
    return SIM_OK;
}

__device__ void update_diagnostics(DeviceInstanceState *state) {
    const Quat q = state->plant.q_bi;
    const double norm = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y +
                                  q.z * q.z);
    const double quat_error = std::fabs(norm - 1.0);
    double dcm_error = 1.0e300;
    Matrix3 dcm{};
    if (quat_to_dcm(q, &dcm) == SIM_OK) {
        const Matrix3 product = matrix3_multiply(dcm, matrix3_transpose(dcm));
        double sum = 0.0;
        for (std::uint32_t row = 0u; row < 3u; ++row) {
            for (std::uint32_t column = 0u; column < 3u; ++column) {
                const double expected = row == column ? 1.0 : 0.0;
                const double error = product.m[row][column] - expected;
                sum += error * error;
            }
        }
        dcm_error = std::sqrt(sum);
    }
    double min_inertia = state->plant.inertia_b.m[0][0];
    if (state->plant.inertia_b.m[1][1] < min_inertia) {
        min_inertia = state->plant.inertia_b.m[1][1];
    }
    if (state->plant.inertia_b.m[2][2] < min_inertia) {
        min_inertia = state->plant.inertia_b.m[2][2];
    }
    DeviceDiagnosticStats &stats = state->diagnostic_stats;
    if (stats.sample_count == 0u || quat_error > stats.max_quat_norm_error) {
        stats.max_quat_norm_error = quat_error;
    }
    if (stats.sample_count == 0u || dcm_error > stats.max_dcm_orthogonality_error) {
        stats.max_dcm_orthogonality_error = dcm_error;
    }
    if (stats.sample_count == 0u || state->mass.mass_kg < stats.min_mass_kg) {
        stats.min_mass_kg = state->mass.mass_kg;
    }
    if (stats.sample_count == 0u || min_inertia < stats.min_inertia_diag_kgm2) {
        stats.min_inertia_diag_kgm2 = min_inertia;
    }
    stats.aero_model_flags_or |= state->trace_aero_flags;
    if ((state->trace_aero_flags & kDeviceTableFlagExtrapolated) != 0u) {
        ++stats.aero_extrapolated_sample_count;
    }
    std::uint32_t degradation = 0u;
    if (state->trace_aero_flags != 0u) degradation |= kModelDegradationAeroFlags;
    if ((state->environment_flags & (1u << 2u)) != 0u) {
        degradation |= kModelDegradationTerrainWarning;
    }
    if (!__builtin_isfinite(state->mass.mass_kg) || state->mass.mass_kg <= 0.0) {
        degradation |= kModelDegradationMassInvalid;
    }
    if (!__builtin_isfinite(min_inertia) || min_inertia <= 0.0) {
        degradation |= kModelDegradationInertiaInvalid;
    }
    if (!__builtin_isfinite(quat_error) || quat_error > 1.0e-6) {
        degradation |= kModelDegradationAttitudeError;
    }
    if (!__builtin_isfinite(dcm_error) || dcm_error > 1.0e-6) {
        degradation |= kModelDegradationDcmError;
    }
    stats.model_degradation_flags_or |= degradation;
    ++stats.sample_count;
}

__device__ void write_trace(DeviceTraceRecord *record,
                            const DeviceInstanceState &state) {
    if (record == nullptr) return;
    *record = DeviceTraceRecord{};
    record->step = state.global_step;
    record->status = state.terminal_status;
    record->active = state.active;
    record->time = state.plant.time;
    record->position_ecef = state.plant.pos_ecef;
    record->velocity_ecef = state.plant.vel_ecef;
    record->attitude = state.plant.q_bi;
    record->sensor_valid_flags = state.last_sensor.sensor_valid_flags;
    record->sensor_fault_flags = state.last_sensor.sensor_fault_flags;
    record->command_mode = state.last_command.command_mode;
    record->command_status = state.last_command.command_status;
    record->environment_flags = state.environment_flags;
    record->accel_command_ecef = state.last_command.accel_cmd_ecef;
    record->force_b_n = state.plant_input.force_b_n;
    record->moment_b_nm = state.plant_input.moment_b_nm;
    record->mach = state.trace_mach;
    record->density = state.trace_density;
    record->aero_flags = state.trace_aero_flags;
    record->acceleration_ecef = state.plant.accel_ecef;
    record->latitude_rad = state.plant.lat_rad;
    record->longitude_rad = state.plant.lon_rad;
    record->height_m = state.plant.height_m;
    record->height_agl_m = state.plant.height_agl_m;
    record->mass_kg = state.mass.mass_kg;
    record->propellant_mass_kg = state.mass.propellant_mass_kg;
    record->target_position_ecef = state.sensors.target_position_ecef;
    record->target_velocity_ecef = state.sensors.target_velocity_ecef;
    record->target_latitude_rad = state.target_lla.lat_rad;
    record->target_longitude_rad = state.target_lla.lon_rad;
    record->target_height_m = state.target_lla.height_m;
    const Vec3 relative_position = vec3_sub(
        state.sensors.target_position_ecef, state.plant.pos_ecef);
    record->truth_range_m = vec3_norm(relative_position);
    Vec3 los_unit = vec3_make(1.0, 0.0, 0.0);
    if (record->truth_range_m > 1.0e-6 &&
        vec3_normalize(relative_position, &los_unit) == SIM_OK) {
        record->truth_closing_velocity_mps = -vec3_dot(
            los_unit, vec3_sub(state.sensors.target_velocity_ecef,
                               state.plant.vel_ecef));
    }
    for (std::uint32_t index = 0u; index < 3u; ++index) {
        record->actuator_position[index] =
            state.acceleration_actuators[index].pos;
        record->actuator_rate[index] = state.acceleration_actuators[index].rate;
    }
    record->mass_flow_kgps = state.last_mass_flow_kgps;
    record->fault_started_mask = state.last_fault_started_mask;
    record->fault_ended_mask = state.last_fault_ended_mask;
    record->fault_active_mask = state.last_fault_active_mask;
    record->sensor_frame = state.last_sensor;
    record->control_command = state.last_command;
    record->has_io_frame = state.global_step == 0u ? 0u : 1u;
}

}  // namespace

__global__ void simulate_production_chunk(
    DeviceInstanceState *states, DeviceTraceRecord *trace,
    DeviceChunkLaunch launch, const DeviceInstanceConfig *configs,
    const DeviceAeroTable *aero_tables, std::uint32_t aero_table_count,
    const AeroSurrogateModel *aero_surrogates,
    std::uint32_t aero_surrogate_count,
    const DeviceTerrainSet *terrain_sets, std::uint32_t terrain_set_count,
    const DeviceTerrainGrid *terrain_grids, std::uint32_t terrain_grid_count,
    const FaultInjection *fault_scripts, std::uint32_t fault_script_count) {
    const std::uint32_t instance_index = launch.packed_lanes != 0u
        ? blockIdx.x * blockDim.x + threadIdx.x : blockIdx.x;
    if ((launch.packed_lanes == 0u && threadIdx.x != 0u) ||
        states == nullptr || configs == nullptr ||
        launch.instance_begin > launch.instance_end ||
        launch.instance_end > launch.instance_count ||
        instance_index < launch.instance_begin ||
        instance_index >= launch.instance_end || launch.max_steps == 0u ||
        (launch.valid_steps != 0u && launch.valid_steps < launch.max_steps)) {
        return;
    }
    DeviceInstanceState *state = states + instance_index;
    const DeviceInstanceConfig &config = configs[instance_index];
    DeviceTraceRecord *instance_trace = nullptr;
    if (launch.trace_enabled != 0u && trace != nullptr &&
        launch.trace_capacity != 0u) {
        instance_trace = trace + static_cast<std::size_t>(instance_index) *
            launch.trace_capacity;
    }
    state->chunk_records_written = 0u;
    if (state->initialized == 0u) {
        const SimStatus status = initialize_instance(
            state, config, terrain_sets, terrain_set_count, terrain_grids,
            terrain_grid_count, fault_scripts, fault_script_count);
        if (status != SIM_OK) {
            const bool terrain_failure = state->initialized == 2u;
            /* Keep a terminal initialization sentinel.  A failed instance is
               still present in CONTINUE_ON_FAILURE campaigns, so a later
               chunk must not attempt to initialize it again from the zeroed
               state buffer. */
            state->initialized = 3u;
            terminate(state, terrain_failure
                ? DeviceExitReason::TerrainError : DeviceExitReason::ConfigError,
                status);
            return;
        }
        communication_reset(state);
        update_diagnostics(state);
    }
    if (state->active == 0u) return;

    if (launch.write_initial_state != 0u && state->global_step == 0u &&
        instance_trace != nullptr && state->chunk_records_written <
                                      launch.trace_capacity) {
        write_trace(instance_trace, *state);
        ++state->chunk_records_written;
    }

    for (std::uint32_t local_step = 0u; local_step < launch.max_steps;
         ++local_step) {
        /* env_app's while (time <= max_time) guard runs before all per-step
           checks.  Keeping this guard first also preserves the CPU closest-
           approach summary when floating-point accumulation crosses the
           configured limit at the end of the previous step. */
        if (state->plant.time > config.max_time) {
            terminate(state, DeviceExitReason::Timeout, SIM_OK);
            break;
        }
        if (config.terrain_enabled != 0u && state->plant.height_agl_m <= 0.0) {
            terminate(state, DeviceExitReason::SurfaceCollision, SIM_OK);
            break;
        }
        const double range = vec3_norm(vec3_sub(
            state->sensors.target_position_ecef, state->plant.pos_ecef));
        if (range < state->min_range_m) {
            state->min_range_m = range;
            state->time_of_closest_s = state->plant.time;
        }
        if (range <= config.hit_radius_m) {
            state->hit = 1u;
            terminate(state, DeviceExitReason::Hit, SIM_OK);
            break;
        }
        FaultStepEffects fault_effects{};
        const std::uint32_t before_mask = active_fault_mask(state->faults);
        std::size_t transition_count = 0u;
        SimStatus status = fault_injection_update(
            &state->faults, state->plant.time, &fault_effects, nullptr, 0u,
            &transition_count);
        if (status != SIM_OK) {
            terminate(state, DeviceExitReason::FaultError, status);
            break;
        }
        const std::uint32_t after_mask = active_fault_mask(state->faults);
        state->last_fault_started_mask = after_mask & ~before_mask;
        state->last_fault_ended_mask = before_mask & ~after_mask;
        state->last_fault_active_mask = after_mask;
        state->fault_stats.fault_start_count += static_cast<std::uint32_t>(
            __popc(state->last_fault_started_mask));
        state->fault_stats.fault_end_count += static_cast<std::uint32_t>(
            __popc(state->last_fault_ended_mask));
        if (fault_effects.active_fault_count > 0u) {
            ++state->fault_stats.active_step_count;
        }
        if (fault_effects.active_fault_count >
            state->fault_stats.max_concurrent_active) {
            state->fault_stats.max_concurrent_active =
                static_cast<std::uint32_t>(fault_effects.active_fault_count);
        }
        if (fault_effects_affect_sensor(fault_effects)) {
            ++state->fault_stats.sensor_affected_step_count;
        }
        if (fault_effects_affect_actuator(fault_effects)) {
            ++state->fault_stats.actuator_affected_step_count;
        }

        SensorFrame sensor{};
        status = build_sensor_frame(state, config, &sensor);
        if (status != SIM_OK) {
            terminate(state, DeviceExitReason::SensorError, status);
            break;
        }
        if (config.los_occlusion_enabled != 0u) {
            int occluded = 0;
            std::uint32_t terrain_flags = 0u;
            status = line_of_sight_occluded(
                config, terrain_sets, terrain_set_count, terrain_grids,
                terrain_grid_count, state->plant.pos_ecef,
                state->sensors.target_position_ecef, &terrain_flags,
                &occluded);
            if ((terrain_flags & kDeviceTerrainFlagMissingTile) != 0u) {
                state->environment_flags |= 1u << 2u;
            }
            if (status != SIM_OK) {
                terminate(state, DeviceExitReason::LosTerrainError, status);
                break;
            }
            if (occluded != 0) {
                sensor.sensor_valid_flags &= ~SIM_SENSOR_VALID_SEEKER;
                sensor.sensor_fault_flags |= SIM_SENSOR_FAULT_LOS_OCCLUDED;
            }
        }
        fault_injection_apply_sensor(&fault_effects, &sensor);
        status = apply_communication_reorder(
            &state->communication_reorder, fault_effects, &sensor);
        if (status == SIM_OK) {
            status = apply_communication_delay(
                &state->communication_delay, fault_effects, &sensor);
        }
        if (status != SIM_OK) {
            terminate(state, DeviceExitReason::FaultError, status);
            break;
        }
        ControlCommand command{};
        status = flight_controller_step(
            &state->flight_controller, &sensor, &command);
        if (status != SIM_OK) {
            terminate(state, DeviceExitReason::FlightControlError, status);
            break;
        }
        status = update_truth(
            state, config, command, fault_effects, aero_tables,
            aero_table_count, aero_surrogates, aero_surrogate_count);
        if (status != SIM_OK) {
            terminate(state, DeviceExitReason::PlantError, status);
            break;
        }
        status = update_geodetic(state, config, terrain_sets,
                                 terrain_set_count, terrain_grids,
                                 terrain_grid_count);
        if (status != SIM_OK) {
            terminate(state, DeviceExitReason::NumericError, status);
            break;
        }
        state->last_sensor = sensor;
        state->last_command = command;
        ++state->global_step;
        state->completed_steps = state->global_step;
        update_diagnostics(state);
        if (instance_trace != nullptr && state->chunk_records_written <
                                         launch.trace_capacity) {
            write_trace(instance_trace + state->chunk_records_written, *state);
            ++state->chunk_records_written;
        }
    }
}

}  // namespace gpu_campaign
