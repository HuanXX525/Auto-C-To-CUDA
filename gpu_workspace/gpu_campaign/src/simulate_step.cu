#include "simulate_step.cuh"

namespace gpu_campaign {
namespace {

__device__ SensorNoiseConfig zero_noise() {
    SensorNoiseConfig noise{};
    noise.min_value = -1.0e300;
    noise.max_value = 1.0e300;
    return noise;
}

__device__ SensorVector3Config zero_vector_sensor_config() {
    SensorVector3Config config{};
    config.enabled = 1;
    config.sample_period_s = 0.01;
    config.delay_s = 0.0;
    config.dropout_probability = 0.0;
    for (std::uint32_t index = 0; index < 3; ++index) {
        config.axis[index] = zero_noise();
    }
    return config;
}

__device__ SeekerSensorConfig zero_seeker_config() {
    SeekerSensorConfig config{};
    config.enabled = 1;
    config.sample_period_s = 0.01;
    config.delay_s = 0.0;
    config.dropout_probability = 0.0;
    config.range = zero_noise();
    config.closing_velocity = zero_noise();
    for (std::uint32_t index = 0; index < 3; ++index) {
        config.los_unit_axis[index] = zero_noise();
        config.los_rate_axis[index] = zero_noise();
    }
    return config;
}

__device__ FlightControllerConfig campaign_fc_config() {
    FlightControllerConfig config{};
    config.guidance.navigation_constant = 3.0;
    config.guidance.max_accel_mps2 = 50.0;
    config.guidance.max_accel_rate_mps3 = 500.0;
    config.autopilot.enable_attitude_loop = 1;
    config.autopilot.enable_control_allocation = 1;
    config.autopilot.max_attitude_cmd_rad = 0.35;
    config.autopilot.max_body_rate_cmd_radps = 1.0;
    config.autopilot.attitude_time_constant_s = 0.25;
    config.autopilot.gyro_damping_gain = 0.2;
    config.autopilot.fin_accel_effectiveness_mps2_per_rad = 150.0;
    config.autopilot.max_fin_deflection_rad = 0.35;
    config.safety.sensor_timeout_s = 0.1;
    config.safety.command_hold_s = 0.1;
    config.safety.reject_nan = 1;
    config.safety.reject_old_seq = 1;
    config.safety.max_consecutive_bad_frames = 3;
    config.scheduler_base_rate_hz = 100.0;
    return config;
}

__device__ SimStatus initialize_instance(DeviceInstanceState *state,
                                         const DeviceConfig &config,
                                         const FaultInjection *fault_script) {
    const SensorVector3Config default_vector_config = zero_vector_sensor_config();
    const SeekerSensorConfig default_seeker_config = zero_seeker_config();
    const SensorVector3Config &imu_config = config.sensors.enabled != 0u
        ? config.sensors.imu : default_vector_config;
    const SensorVector3Config &accel_config = config.sensors.enabled != 0u
        ? config.sensors.accel : default_vector_config;
    const SensorVector3Config &speed_config = config.sensors.enabled != 0u
        ? config.sensors.speed : default_vector_config;
    const SeekerSensorConfig &seeker_config = config.sensors.enabled != 0u
        ? config.sensors.seeker : default_seeker_config;
    const FlightControllerConfig fc_config = campaign_fc_config();
    SimStatus status;

    const std::uint64_t seed_base = 11u +
        static_cast<std::uint64_t>(state->instance_id) * 100u;
    status = sensor_imu_init(&state->sensors.imu, &imu_config, seed_base, config.dt);
    if (status == SIM_OK) {
        status = sensor_accel_init(&state->sensors.accel, &accel_config, seed_base + 2u, config.dt);
    }
    if (status == SIM_OK) {
        status = sensor_speed_init(&state->sensors.speed, &speed_config, seed_base + 6u, config.dt);
    }
    if (status == SIM_OK) {
        status = sensor_seeker_init(&state->sensors.seeker, &seeker_config, seed_base + 8u, config.dt);
    }
    if (status == SIM_OK) {
        status = flight_controller_init(&state->flight_controller, &fc_config);
    }
    /* Target position: the legacy geocentric scene maps (x, y, z) directly;
       the terrain scene places the same offsets along the WGS-84 local
       basis (1000 m east, (100 + 5*id) m up), preserving the trajectory
       geometry. */
    if (config.terrain_grid_enabled != 0u) {
        state->sensors.target_position_ecef = vec3_add(
            vec3_add(config.scene_origin_ecef,
                     vec3_scale(config.scene_east, 1000.0)),
            vec3_scale(config.scene_up,
                       100.0 + 5.0 * static_cast<double>(state->instance_id)));
    } else {
        state->sensors.target_position_ecef = vec3_make(
            1000.0, 100.0 + 5.0 * static_cast<double>(state->instance_id), 0.0);
    }
    state->sensors.target_velocity_ecef = vec3_make(0.0, 0.0, 0.0);
    /* Per-instance fault script copy: was_active starts cleared so every
       instance replays the same deterministic fault timeline. */
    if (fault_script != nullptr) {
        state->faults = *fault_script;
    } else {
        fault_injection_init_empty(&state->faults);
    }
    state->initialized = status == SIM_OK ? 1u : 0u;
    return status;
}

__device__ SimStatus build_sensor_frame(DeviceInstanceState *state,
                                        std::uint32_t sequence,
                                        double dt,
                                        SensorFrame *frame) {
    SensorSampleStatus imu_status{};
    SensorSampleStatus accel_status{};
    SensorSampleStatus speed_status{};
    SensorSampleStatus seeker_status{};
    SeekerMeasurement seeker_measurement{};
    SeekerTruth truth{};
    SimStatus status;

    if (state == nullptr || frame == nullptr) {
        return SIM_ERR_INVALID_ARG;
    }
    *frame = SensorFrame{};
    frame->seq = sequence;
    frame->sim_time = state->plant.time;
    frame->dt = dt;
    frame->missile_lat_rad_meas = state->plant.lat_rad;
    frame->missile_lon_rad_meas = state->plant.lon_rad;
    frame->missile_height_m_meas = state->plant.height_m;
    frame->missile_height_agl_m_meas = state->plant.height_agl_m;
    status = sensor_imu_update(&state->sensors.imu, state->plant.time, dt,
                               state->plant.omega_b,
                               &frame->missile_gyro_b_meas, &imu_status);
    if (status == SIM_OK) {
        status = sensor_accel_update(&state->sensors.accel, state->plant.time,
                                     dt, state->plant.accel_ecef,
                                     &frame->missile_accel_ecef_meas,
                                     &accel_status);
    }
    if (status == SIM_OK) {
        status = sensor_speed_update(&state->sensors.speed, state->plant.time,
                                     dt, state->plant.vel_ecef,
                                     &frame->missile_vel_ecef_meas,
                                     &speed_status);
    }
    truth.missile_position_ecef_m = state->plant.pos_ecef;
    truth.missile_velocity_ecef_mps = state->plant.vel_ecef;
    truth.target_position_ecef_m = state->sensors.target_position_ecef;
    truth.target_velocity_ecef_mps = state->sensors.target_velocity_ecef;
    if (status == SIM_OK) {
        status = sensor_seeker_update(&state->sensors.seeker, state->plant.time,
                                      dt, &truth, &seeker_measurement,
                                      &seeker_status);
    }
    if (status != SIM_OK) {
        return status;
    }
    if (imu_status.valid != 0) {
        frame->sensor_valid_flags |= SIM_SENSOR_VALID_IMU_GYRO;
    }
    if (accel_status.valid != 0) {
        frame->sensor_valid_flags |= SIM_SENSOR_VALID_ACCEL;
    }
    if (speed_status.valid != 0) {
        frame->sensor_valid_flags |= SIM_SENSOR_VALID_SPEED;
    }
    frame->sensor_valid_flags |= SIM_SENSOR_VALID_GEODETIC;
    if (seeker_status.valid != 0) {
        frame->sensor_valid_flags |= SIM_SENSOR_VALID_SEEKER;
        frame->target_range_meas = seeker_measurement.range_m;
        frame->target_los_unit_ecef_meas = seeker_measurement.los_unit_ecef;
        frame->target_los_rate_ecef_meas = seeker_measurement.los_rate_ecef_radps;
        frame->target_closing_velocity_meas = seeker_measurement.closing_velocity_mps;
    }
    if (imu_status.dropped != 0) frame->sensor_fault_flags |= SIM_SENSOR_FAULT_IMU_DROPOUT;
    if (accel_status.dropped != 0) frame->sensor_fault_flags |= SIM_SENSOR_FAULT_ACCEL_DROPOUT;
    if (speed_status.dropped != 0) frame->sensor_fault_flags |= SIM_SENSOR_FAULT_SPEED_DROPOUT;
    if (seeker_status.dropped != 0) frame->sensor_fault_flags |= SIM_SENSOR_FAULT_SEEKER_DROPOUT;
    if (imu_status.delay_warmup != 0 || accel_status.delay_warmup != 0 ||
        speed_status.delay_warmup != 0 || seeker_status.delay_warmup != 0) {
        frame->sensor_fault_flags |= SIM_SENSOR_FAULT_DELAY_WARMUP;
    }
    return SIM_OK;
}

__device__ void write_trace(DeviceTraceRecord *record,
                            const DeviceInstanceState &state,
                            std::uint32_t step,
                            std::uint32_t status,
                            const SensorFrame *sensor,
                            const ControlCommand *command) {
    record->step = step;
    record->status = status;
    record->active = state.active;
    record->reserved = 0;
    record->time = state.plant.time;
    record->position_ecef = state.plant.pos_ecef;
    record->velocity_ecef = state.plant.vel_ecef;
    record->attitude = state.plant.q_bi;
    record->sensor_valid_flags = sensor == nullptr ? 0u : sensor->sensor_valid_flags;
    record->sensor_fault_flags = sensor == nullptr ? 0u : sensor->sensor_fault_flags;
    record->command_mode = command == nullptr ? 0u : command->command_mode;
    record->command_status = command == nullptr ? 0u : command->command_status;
    record->environment_flags = state.environment_flags;
    record->accel_command_ecef = command == nullptr ? vec3_make(0.0, 0.0, 0.0)
                                                      : command->accel_cmd_ecef;
    record->force_b_n = state.plant_input.force_b_n;
    record->moment_b_nm = state.plant_input.moment_b_nm;
    record->mach = state.trace_mach;
    record->density = state.trace_density;
    record->aero_flags = state.trace_aero_flags;
}

__device__ void query_environment(const DeviceInstanceState *state,
                                  const DeviceAeroTable *aero_table,
                                  const DeviceTerrainGrid *terrain_grid,
                                  std::uint32_t *flags) {
    if (state == nullptr || flags == nullptr) return;
    if (aero_table != nullptr) {
        AeroTableSample sample{};
        std::uint32_t query_flags = 0;
        const double mach = vec3_norm(state->plant.vel_ecef) / 340.0;
        if (device_aero_table_lookup(aero_table, mach, 0.0, 0.0,
                                     &sample, &query_flags) != SIM_OK) {
            *flags |= (1u << 1u);
        } else if ((query_flags & kDeviceTableFlagExtrapolated) != 0u) {
            *flags |= (1u << 0u);
        }
    }
    if (terrain_grid != nullptr) {
        double height = 0.0;
        std::uint32_t query_flags = 0;
        if (device_terrain_grid_height(terrain_grid, state->plant.lat_rad,
                                       state->plant.lon_rad, &height,
                                       &query_flags) != SIM_OK) {
            *flags |= (1u << 3u);
        } else if ((query_flags & kDeviceTableFlagExtrapolated) != 0u) {
            *flags |= (1u << 2u);
        }
    }
}

/* Terrain dynamics (planA Phase 4 item 4): refresh the plant geodetic
   fields from the ECEF state, mirroring env_app.c update_geodetic_state
   (ECEF->LLA via WGS-84, then AGL = height - terrain height). */
__device__ SimStatus update_geodetic(DeviceInstanceState *state,
                                     const DeviceConfig &config,
                                     const DeviceTerrainGrid *terrain_grid) {
    EcefCoord ecef;
    LlaCoord lla;
    SimStatus status;

    ecef.position_m = state->plant.pos_ecef;
    status = geo_ecef_to_lla(&config.earth, &ecef, &lla);
    if (status != SIM_OK) {
        return status;
    }
    state->plant.lat_rad = lla.lat_rad;
    state->plant.lon_rad = lla.lon_rad;
    state->plant.height_m = lla.height_m;
    if (terrain_grid != nullptr) {
        double terrain_height = 0.0;
        std::uint32_t flags = 0u;
        status = device_terrain_grid_height(terrain_grid, lla.lat_rad,
                                            lla.lon_rad, &terrain_height,
                                            &flags);
        if (status != SIM_OK) {
            return status;
        }
        state->plant.height_agl_m = lla.height_m - terrain_height;
    } else {
        state->plant.height_agl_m = lla.height_m;
    }
    return SIM_OK;
}

}  // namespace

__global__ void simulate_chunk(DeviceInstanceState *state,
                               DeviceTraceRecord *trace,
                               DeviceConfig config,
                               const DeviceAeroTable *aero_table,
                               const DeviceTerrainGrid *terrain_grid,
                               const FaultInjection *fault_script) {
    /* Two thread mappings:
       - serial (legacy): one block per instance, only lane 0 executes.
       - packed: instance = blockIdx.x * blockDim.x + threadIdx.x, one full
         serial instance per lane. The per-instance instruction stream is
         identical in both mappings, so traces must match bit-for-bit. */
    const std::uint32_t instance_index = config.packed_lanes != 0u
        ? blockIdx.x * blockDim.x + threadIdx.x
        : blockIdx.x;
    if ((config.packed_lanes == 0u && threadIdx.x != 0u) ||
        state == nullptr || trace == nullptr ||
        config.trace_capacity == 0 || config.instance_count == 0 ||
        instance_index >= config.instance_count) {
        return;
    }

    DeviceInstanceState *instance = state + instance_index;
    DeviceTraceRecord *instance_trace = trace +
        static_cast<std::size_t>(instance_index) * config.trace_capacity;

    if (instance->active == 0) {
        write_trace(instance_trace, *instance, 0, SIM_OK, nullptr, nullptr);
        return;
    }

    if (instance->initialized == 0u) {
        const SimStatus init_status = initialize_instance(instance, config,
                                                          fault_script);
        if (init_status != SIM_OK) {
            instance->active = 0;
            instance->exit_reason = static_cast<std::uint32_t>(DeviceExitReason::SensorError);
            write_trace(instance_trace, *instance, 0,
                        static_cast<std::uint32_t>(init_status), nullptr, nullptr);
            return;
        }
        /* Initial geodetic refresh (mirrors env_app.c: the first collision
           check uses the launch-position AGL). */
        if (config.terrain_grid_enabled != 0u) {
            const SimStatus geo_status = update_geodetic(instance, config,
                                                         terrain_grid);
            if (geo_status != SIM_OK) {
                instance->active = 0;
                instance->exit_reason = static_cast<std::uint32_t>(
                    DeviceExitReason::PlantError);
                write_trace(instance_trace, *instance, 0,
                            static_cast<std::uint32_t>(geo_status),
                            nullptr, nullptr);
                return;
            }
        }
    }
    write_trace(instance_trace, *instance, 0, SIM_OK, nullptr, nullptr);

    std::uint32_t steps = config.chunk_steps;
    if (steps + 1 > config.trace_capacity) {
        steps = config.trace_capacity - 1;
    }

    for (std::uint32_t step = 0; step < steps; ++step) {
        /* Terrain dynamics (planA Phase 4 item 4): surface-collision check
           at the top of the step using the previous step's AGL, mirroring
           the env_app.c loop order. The trace record at index `step`
           (written at the end of the previous iteration) already holds the
           colliding state; records beyond stay zeroed. */
        if (config.terrain_grid_enabled != 0u &&
            instance->plant.height_agl_m <= 0.0) {
            instance->active = 0;
            instance->exit_reason = static_cast<std::uint32_t>(
                DeviceExitReason::SurfaceCollision);
            return;
        }
        SensorFrame sensor;
        ControlCommand command;
        SimStatus status = build_sensor_frame(instance, step + 1u, config.dt, &sensor);
        if (status != SIM_OK) {
            instance->active = 0;
            instance->exit_reason = static_cast<std::uint32_t>(DeviceExitReason::SensorError);
            write_trace(instance_trace + step + 1, *instance, step + 1,
                        static_cast<std::uint32_t>(status), &sensor, nullptr);
            return;
        }
        /* planA Phase 4 item 3: fault injection between sensor sampling and
           flight control, mirroring the env_app.c loop order (update uses
           the step-start time, effects mutate the frame before the FC). */
        if (fault_script != nullptr) {
            FaultStepEffects fault_effects;
            std::size_t fault_transition_count = 0;
            status = fault_injection_update(&instance->faults,
                                            instance->plant.time,
                                            &fault_effects, nullptr, 0u,
                                            &fault_transition_count);
            if (status == SIM_OK) {
                fault_injection_apply_sensor(&fault_effects, &sensor);
            }
            if (status != SIM_OK) {
                instance->active = 0;
                instance->exit_reason = static_cast<std::uint32_t>(
                    DeviceExitReason::FaultError);
                write_trace(instance_trace + step + 1, *instance, step + 1,
                            static_cast<std::uint32_t>(status), &sensor, nullptr);
                return;
            }
        }
        status = flight_controller_step(&instance->flight_controller, &sensor, &command);
        if (status != SIM_OK) {
            instance->active = 0;
            instance->exit_reason = static_cast<std::uint32_t>(DeviceExitReason::FlightControlError);
            write_trace(instance_trace + step + 1, *instance, step + 1,
                        static_cast<std::uint32_t>(status), &sensor, &command);
            return;
        }
        query_environment(instance, aero_table, terrain_grid,
                          &instance->environment_flags);
        if (config.aero_force.enabled != 0u) {
            /* environment_force_model.c composition (CPU golden baseline):
               air velocity with scenario wind, ISA atmosphere, guarded
               mach, table-branch aero force, virtual command force. */
            Vec3 air_velocity_b;
            status = device_air_velocity_body(
                instance->plant.q_bi, instance->plant.vel_ecef,
                config.wind_velocity_ecef, &air_velocity_b);
            if (status == SIM_OK) {
                AtmosphereState atmosphere_state{};
                status = atmosphere_model_evaluate(
                    &config.atmosphere, instance->plant.height_m,
                    &atmosphere_state);
                if (status == SIM_OK) {
                    const double mach = device_mach_from_air_velocity(
                        air_velocity_b, atmosphere_state.speed_of_sound_mps);
                    Vec3 aero_force_b_n;
                    Vec3 aero_moment_b_nm;
                    std::uint32_t aero_flags = 0u;
                    status = device_aero_force_evaluate(
                        &config.aero_force, aero_table,
                        atmosphere_state.density_kgpm3, mach, air_velocity_b,
                        0.0, 0.0, &aero_force_b_n, &aero_moment_b_nm,
                        &aero_flags);
                    if (status == SIM_OK) {
                        Matrix3 body_to_ecef;
                        status = quat_to_dcm(instance->plant.q_bi,
                                             &body_to_ecef);
                        if (status == SIM_OK) {
                            const Vec3 virtual_force_b_n =
                                matrix3_multiply_vec3(
                                    matrix3_transpose(body_to_ecef),
                                    vec3_scale(command.accel_cmd_ecef,
                                               instance->plant.mass));
                            instance->plant_input.force_b_n = vec3_add(
                                virtual_force_b_n, aero_force_b_n);
                            instance->plant_input.moment_b_nm = aero_moment_b_nm;
                            instance->trace_mach = mach;
                            instance->trace_density =
                                atmosphere_state.density_kgpm3;
                            instance->trace_aero_flags = aero_flags;
                        }
                    }
                }
            }
            if (status != SIM_OK) {
                instance->active = 0;
                instance->exit_reason = static_cast<std::uint32_t>(
                    DeviceExitReason::AeroError);
                write_trace(instance_trace + step + 1, *instance, step + 1,
                            static_cast<std::uint32_t>(status), &sensor,
                            &command);
                return;
            }
        } else {
            instance->plant_input.force_b_n = vec3_scale(
                command.accel_cmd_ecef, instance->plant.mass);
        }
        status = missile_plant_step(
            &instance->plant, &instance->plant_input, config.dt, INTEGRATOR_RK4);
        instance->completed_steps = step + 1;
        if (status != SIM_OK) {
            instance->active = 0;
            instance->exit_reason = static_cast<std::uint32_t>(
                DeviceExitReason::PlantError);
            write_trace(instance_trace + step + 1, *instance,
                        step + 1, static_cast<std::uint32_t>(status), &sensor, &command);
            return;
        }
        /* Terrain dynamics: geodetic refresh after the plant step
           (env_app.c update_geodetic_state), so the next iteration's
           collision check sees the post-integration AGL. */
        if (config.terrain_grid_enabled != 0u) {
            status = update_geodetic(instance, config, terrain_grid);
            if (status != SIM_OK) {
                instance->active = 0;
                instance->exit_reason = static_cast<std::uint32_t>(
                    DeviceExitReason::PlantError);
                write_trace(instance_trace + step + 1, *instance,
                            step + 1, static_cast<std::uint32_t>(status),
                            &sensor, &command);
                return;
            }
        }
        write_trace(instance_trace + step + 1, *instance, step + 1,
                    SIM_OK, &sensor, &command);
    }

    instance->active = 0;
    instance->exit_reason = static_cast<std::uint32_t>(
        DeviceExitReason::Completed);
}

}  // namespace gpu_campaign
