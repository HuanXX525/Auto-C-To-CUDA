/* CPU trajectory golden baseline for the aero-dynamics wiring.
 *
 * The chain mirrors gpu_campaign/src/simulate_step.cu for campaign
 * instance_id=0 (same seeds, sensor/FC config, initial state, target), but
 * replaces the temporary force mapping
 *     force_b_n = accel_cmd_ecef * mass
 * with the environment_force_model.c composition that the device kernel
 * will adopt:
 *     air_b        = R_ecef2body * (vel_ecef - wind_ecef)
 *     density/sos  = ISA atmosphere at plant.height_m
 *     mach         = |air_b| / sos   (guarded)
 *     aero_force, aero_moment = aero_model_evaluate(table branch)
 *     virtual_b    = R_ecef2body * (accel_cmd_ecef * mass)
 *     force_b_n    = virtual_b + aero_force
 *     moment_b_nm  = aero_moment
 * Gravity stays the campaign constant (0, -9.8, 0); propulsion is disabled by
 * the baseline scenario; pitch/yaw actuator inputs are 0.0 because the
 * campaign ControlCommand carries no actuator angles yet.
 *
 * All dynamics run the original staging C sources compiled on the host; the
 * scenario parameters come from the real configs/baseline/scenario.json via
 * host_scenario_mapper. The probe runs the chain twice and requires bitwise
 * identical records before writing the golden file.
 */
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "common/math_constants.h"
#include "common/matrix3.h"
#include "common/quaternion.h"
#include "common/vec3.h"
#include "env/aero_database.h"
#include "env/aero_model.h"
#include "env/atmosphere_model.h"
#include "env/earth_model.h"
#include "env/geo_coordinate.h"
#include "env/missile_plant_6dof.h"
#include "env/sensor_accel.h"
#include "env/sensor_imu.h"
#include "env/sensor_seeker.h"
#include "env/sensor_speed.h"
#include "fc/fc_state.h"
#include "host_environment_packer.hpp"
#include "host_scenario_mapper.hpp"

namespace {

constexpr unsigned kGoldenSteps = 64u;
constexpr double kDt = 0.01;

/* WGS-84 scene basis for terrain dynamics: identical construction to
   main.cu make_terrain_scene_basis (origin LLA(30deg, 120deg, 1000 m),
   east/up local axes). */
struct SceneBasis {
    Vec3 origin;
    Vec3 east;
    Vec3 up;
};

SceneBasis make_terrain_scene_basis() {
    const EarthModel earth = earth_model_wgs84();
    LlaCoord origin_lla{};
    origin_lla.lat_rad = 30.0 * SIM_DEG_TO_RAD;
    origin_lla.lon_rad = 120.0 * SIM_DEG_TO_RAD;
    origin_lla.height_m = 1000.0;
    EcefCoord origin_ecef{};
    SceneBasis basis{};
    if (geo_lla_to_ecef(&earth, &origin_lla, &origin_ecef) != SIM_OK) {
        std::fprintf(stderr, "campaign_trajectory_golden: scene origin "
                             "conversion failed\n");
        std::exit(EXIT_FAILURE);
    }
    const double sin_lat = std::sin(origin_lla.lat_rad);
    const double cos_lat = std::cos(origin_lla.lat_rad);
    const double sin_lon = std::sin(origin_lla.lon_rad);
    const double cos_lon = std::cos(origin_lla.lon_rad);
    basis.origin = origin_ecef.position_m;
    basis.east = vec3_make(-sin_lon, cos_lon, 0.0);
    basis.up = vec3_make(cos_lat * cos_lon, cos_lat * sin_lon, sin_lat);
    return basis;
}

/* Geodetic refresh mirroring simulate_step.cu update_geodetic: ECEF->LLA
   (WGS-84), then AGL = height - terrain height (device grid lookup,
   host-evaluated). Returns 0 on success. */
int refresh_geodetic(const EarthModel *earth,
                     const gpu_campaign::DeviceTerrainGrid *terrain_grid,
                     PlantState6Dof *plant) {
    EcefCoord ecef;
    LlaCoord lla;

    ecef.position_m = plant->pos_ecef;
    if (geo_ecef_to_lla(earth, &ecef, &lla) != SIM_OK) {
        return 1;
    }
    plant->lat_rad = lla.lat_rad;
    plant->lon_rad = lla.lon_rad;
    plant->height_m = lla.height_m;
    if (terrain_grid != nullptr) {
        double terrain_height = 0.0;
        std::uint32_t flags = 0u;
        if (gpu_campaign::device_terrain_grid_height(
                terrain_grid, lla.lat_rad, lla.lon_rad, &terrain_height,
                &flags) != SIM_OK) {
            return 2;
        }
        plant->height_agl_m = lla.height_m - terrain_height;
    } else {
        plant->height_agl_m = lla.height_m;
    }
    return 0;
}

SensorNoiseConfig noise_config() {
    SensorNoiseConfig config{};
    config.min_value = -1.0e300;
    config.max_value = 1.0e300;
    return config;
}

SensorVector3Config vector_config() {
    SensorVector3Config config{};
    config.enabled = 1;
    config.sample_period_s = 0.01;
    config.delay_s = 0.0;
    config.dropout_probability = 0.0;
    for (int i = 0; i < 3; ++i) config.axis[i] = noise_config();
    return config;
}

SeekerSensorConfig seeker_config() {
    SeekerSensorConfig config{};
    config.enabled = 1;
    config.sample_period_s = 0.01;
    config.delay_s = 0.0;
    config.dropout_probability = 0.0;
    config.range = noise_config();
    config.closing_velocity = noise_config();
    for (int i = 0; i < 3; ++i) {
        config.los_unit_axis[i] = noise_config();
        config.los_rate_axis[i] = noise_config();
    }
    return config;
}

FlightControllerConfig fc_config() {
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

struct GoldenRecord {
    unsigned step;
    unsigned aero_flags;
    unsigned command_mode;
    unsigned command_status;
    double time;
    double pos_ecef[3];
    double vel_ecef[3];
    double attitude[4];
    double force_b_n[3];
    double moment_b_nm[3];
    double accel_cmd_ecef[3];
    double mach;
    double density;
};

/* Per-sensor dropped-sample counts over the run (dropout evidence), plus
   steps with at least one active fault (fault-injection evidence) and the
   surface-collision step (-1 = ran to completion). */
struct DropCounts {
    unsigned imu;
    unsigned accel;
    unsigned speed;
    unsigned seeker;
    unsigned fault_active_steps;
    int surface_collision_step = -1;  /* -1 = ran to completion */
};

/* Terrain-dynamics context: non-null switches the chain to the WGS-84 scene
   with per-step geodetic refresh and surface-collision termination,
   mirroring the kernel's terrain_grid_enabled path. */
struct TerrainContext {
    const SceneBasis *scene;
    const EarthModel *earth;
    const gpu_campaign::DeviceTerrainGrid *grid;
};

int records_identical(const GoldenRecord *left, const GoldenRecord *right,
                      unsigned count) {
    return std::memcmp(left, right, count * sizeof(GoldenRecord)) == 0;
}

/* Runs the full chain once, filling records[0..steps]. Returns 0 on success.
   When scenario_sensors is non-null its per-sensor configs replace the
   zero-noise defaults (planA Phase 4 items 1/2). When fault_script is
   non-null its faults are applied between sensor sampling and flight
   control, mirroring the kernel order (planA Phase 4 item 3). */
int run_chain(const AeroModel *aero, const AtmosphereModel *atmosphere,
              Vec3 wind_ecef, const gpu_campaign::DeviceSensorScenario *scenario_sensors,
              const FaultInjection *fault_script, const TerrainContext *terrain,
              GoldenRecord *records, DropCounts *drops, unsigned steps) {
    /* Local copy per run: was_active starts cleared, mirroring the kernel
       which re-uploads fresh instance state (faults zeroed) per launch. */
    FaultInjection local_faults{};
    const FaultInjection *faults = nullptr;
    if (fault_script != nullptr) {
        local_faults = *fault_script;
        faults = &local_faults;
    }
    const SensorVector3Config default_sensors = vector_config();
    const SeekerSensorConfig default_seeker = seeker_config();
    const SensorVector3Config &imu_cfg = scenario_sensors != nullptr
        ? scenario_sensors->imu : default_sensors;
    const SensorVector3Config &accel_cfg = scenario_sensors != nullptr
        ? scenario_sensors->accel : default_sensors;
    const SensorVector3Config &speed_cfg = scenario_sensors != nullptr
        ? scenario_sensors->speed : default_sensors;
    const SeekerSensorConfig &seeker_cfg = scenario_sensors != nullptr
        ? scenario_sensors->seeker : default_seeker;
    const FlightControllerConfig controller_cfg = fc_config();
    ImuSensor imu{};
    AccelSensor accel{};
    SpeedSensor speed{};
    SeekerSensor seeker{};
    FlightController controller{};
    PlantState6Dof plant{};
    PlantInput6Dof input{};
    SensorFrame frame{};
    ControlCommand command{};
    SeekerTruth truth{};
    SeekerMeasurement measurement{};
    SimStatus status;

    /* Campaign instance 0: seeds 11/13/17/19, speed 10, target (1000,100,0). */
    status = sensor_imu_init(&imu, &imu_cfg, 11u, kDt);
    if (status == SIM_OK) {
        status = sensor_accel_init(&accel, &accel_cfg, 13u, kDt);
    }
    if (status == SIM_OK) {
        status = sensor_speed_init(&speed, &speed_cfg, 17u, kDt);
    }
    if (status == SIM_OK) {
        status = sensor_seeker_init(&seeker, &seeker_cfg, 19u, kDt);
    }
    if (status == SIM_OK) {
        status = flight_controller_init(&controller, &controller_cfg);
    }
    if (status != SIM_OK) return 1;

    plant.q_bi = quat_identity();
    plant.mass = 2.0;
    plant.inertia_b = matrix3_identity();
    input.earth_rotation_ecef_radps = vec3_make(0.0, 0.0, 0.0);
    input.enable_earth_rotation = 0;
    if (terrain != nullptr) {
        /* WGS-84 scene: origin LLA(30deg,120deg,1000m), speed east, gravity
           down, target 1000 m east / 100 m up (instance 0 offsets). */
        plant.pos_ecef = terrain->scene->origin;
        plant.vel_ecef = vec3_scale(terrain->scene->east, 10.0);
        input.gravity_ecef_mps2 = vec3_scale(terrain->scene->up, -9.8);
        plant.lat_rad = 30.0 * SIM_DEG_TO_RAD;
        plant.lon_rad = 120.0 * SIM_DEG_TO_RAD;
        plant.height_m = 1000.0;
        truth.target_position_ecef_m = vec3_add(
            vec3_add(terrain->scene->origin,
                     vec3_scale(terrain->scene->east, 1000.0)),
            vec3_scale(terrain->scene->up, 100.0));
    } else {
        plant.vel_ecef = vec3_make(10.0, 0.0, 0.0);
        input.gravity_ecef_mps2 = vec3_make(0.0, -9.8, 0.0);
        truth.target_position_ecef_m = vec3_make(1000.0, 100.0, 0.0);
    }
    truth.target_velocity_ecef_mps = vec3_make(0.0, 0.0, 0.0);
    /* Initial geodetic refresh (mirrors the kernel init path). */
    if (terrain != nullptr &&
        refresh_geodetic(terrain->earth, terrain->grid, &plant) != 0) {
        return 9;
    }

    GoldenRecord *record = &records[0];
    record->step = 0u;
    record->time = plant.time;
    record->pos_ecef[0] = plant.pos_ecef.x;
    record->pos_ecef[1] = plant.pos_ecef.y;
    record->pos_ecef[2] = plant.pos_ecef.z;
    record->vel_ecef[0] = plant.vel_ecef.x;
    record->vel_ecef[1] = plant.vel_ecef.y;
    record->vel_ecef[2] = plant.vel_ecef.z;
    record->attitude[0] = plant.q_bi.w;
    record->attitude[1] = plant.q_bi.x;
    record->attitude[2] = plant.q_bi.y;
    record->attitude[3] = plant.q_bi.z;

    for (unsigned step = 0; step < steps; ++step) {
        /* Terrain dynamics: surface-collision check at the step top using
           the previous step's AGL, mirroring the kernel. Records beyond
           this index stay zero (both sides). */
        if (terrain != nullptr && plant.height_agl_m <= 0.0) {
            drops->surface_collision_step = static_cast<int>(step);
            return 0;
        }
        /* Sensor frame: mirrors simulate_step.cu build_sensor_frame. */
        SensorSampleStatus imu_status{};
        SensorSampleStatus accel_status{};
        SensorSampleStatus speed_status{};
        SensorSampleStatus seeker_status{};
        frame = SensorFrame{};
        frame.seq = step + 1u;
        frame.sim_time = plant.time;
        frame.dt = kDt;
        frame.missile_lat_rad_meas = plant.lat_rad;
        frame.missile_lon_rad_meas = plant.lon_rad;
        frame.missile_height_m_meas = plant.height_m;
        frame.missile_height_agl_m_meas = plant.height_agl_m;
        status = sensor_imu_update(&imu, plant.time, kDt, plant.omega_b,
                                   &frame.missile_gyro_b_meas, &imu_status);
        if (status == SIM_OK) {
            status = sensor_accel_update(&accel, plant.time, kDt,
                                         plant.accel_ecef,
                                         &frame.missile_accel_ecef_meas,
                                         &accel_status);
        }
        if (status == SIM_OK) {
            status = sensor_speed_update(&speed, plant.time, kDt,
                                         plant.vel_ecef,
                                         &frame.missile_vel_ecef_meas,
                                         &speed_status);
        }
        truth.missile_position_ecef_m = plant.pos_ecef;
        truth.missile_velocity_ecef_mps = plant.vel_ecef;
        if (status == SIM_OK) {
            status = sensor_seeker_update(&seeker, plant.time, kDt, &truth,
                                          &measurement, &seeker_status);
        }
        if (status == SIM_OK) {
            drops->imu += imu_status.dropped != 0 ? 1u : 0u;
            drops->accel += accel_status.dropped != 0 ? 1u : 0u;
            drops->speed += speed_status.dropped != 0 ? 1u : 0u;
            drops->seeker += seeker_status.dropped != 0 ? 1u : 0u;
        }
        if (status != SIM_OK) return 2;
        if (imu_status.valid != 0) {
            frame.sensor_valid_flags |= SIM_SENSOR_VALID_IMU_GYRO;
        }
        if (accel_status.valid != 0) {
            frame.sensor_valid_flags |= SIM_SENSOR_VALID_ACCEL;
        }
        if (speed_status.valid != 0) {
            frame.sensor_valid_flags |= SIM_SENSOR_VALID_SPEED;
        }
        frame.sensor_valid_flags |= SIM_SENSOR_VALID_GEODETIC;
        if (seeker_status.valid != 0) {
            frame.sensor_valid_flags |= SIM_SENSOR_VALID_SEEKER;
            frame.target_range_meas = measurement.range_m;
            frame.target_los_unit_ecef_meas = measurement.los_unit_ecef;
            frame.target_los_rate_ecef_meas = measurement.los_rate_ecef_radps;
            frame.target_closing_velocity_meas = measurement.closing_velocity_mps;
        }

        /* Fault injection between sensor sampling and flight control,
           mirroring simulate_step.cu (update at step-start time, then apply
           to the assembled frame). */
        if (faults != nullptr) {
            FaultStepEffects fault_effects;
            std::size_t fault_transition_count = 0u;
            status = fault_injection_update(&local_faults, plant.time,
                                            &fault_effects, nullptr, 0u,
                                            &fault_transition_count);
            if (status == SIM_OK) {
                fault_injection_apply_sensor(&fault_effects, &frame);
                if (fault_effects.active_fault_count > 0u) {
                    ++drops->fault_active_steps;
                }
            }
            if (status != SIM_OK) return 8;
        }

        status = flight_controller_step(&controller, &frame, &command);
        if (status != SIM_OK) return 3;

        /* Target force composition (environment_force_model.c order). */
        Matrix3 body_to_ecef;
        status = quat_to_dcm(plant.q_bi, &body_to_ecef);
        if (status != SIM_OK) return 4;
        const Matrix3 ecef_to_body = matrix3_transpose(body_to_ecef);
        const Vec3 air_velocity_b = matrix3_multiply_vec3(
            ecef_to_body, vec3_sub(plant.vel_ecef, wind_ecef));
        AtmosphereState atmosphere_state{};
        status = atmosphere_model_evaluate(
            atmosphere, plant.height_m, &atmosphere_state);
        if (status != SIM_OK) return 5;
        const double mach =
            atmosphere_state.speed_of_sound_mps > 0.0
                ? vec3_norm(air_velocity_b) / atmosphere_state.speed_of_sound_mps
                : 0.0;
        Vec3 aero_force_b_n{};
        Vec3 aero_moment_b_nm{};
        unsigned aero_flags = 0u;
        status = aero_model_evaluate(
            aero, atmosphere_state.density_kgpm3, mach, air_velocity_b,
            0.0, 0.0, &aero_force_b_n, &aero_moment_b_nm, &aero_flags);
        if (status != SIM_OK) return 6;
        const Vec3 virtual_force_b_n = matrix3_multiply_vec3(
            ecef_to_body, vec3_scale(command.accel_cmd_ecef, plant.mass));
        input.force_b_n = vec3_add(virtual_force_b_n, aero_force_b_n);
        input.moment_b_nm = aero_moment_b_nm;

        status = missile_plant_step(&plant, &input, kDt, INTEGRATOR_RK4);
        if (status != SIM_OK) return 7;
        /* Terrain dynamics: geodetic refresh after the plant step
           (kernel update_geodetic), feeding the next collision check. */
        if (terrain != nullptr &&
            refresh_geodetic(terrain->earth, terrain->grid, &plant) != 0) {
            return 9;
        }

        record = &records[step + 1u];
        record->step = step + 1u;
        record->aero_flags = aero_flags;
        record->command_mode = command.command_mode;
        record->command_status = command.command_status;
        record->time = plant.time;
        record->pos_ecef[0] = plant.pos_ecef.x;
        record->pos_ecef[1] = plant.pos_ecef.y;
        record->pos_ecef[2] = plant.pos_ecef.z;
        record->vel_ecef[0] = plant.vel_ecef.x;
        record->vel_ecef[1] = plant.vel_ecef.y;
        record->vel_ecef[2] = plant.vel_ecef.z;
        record->attitude[0] = plant.q_bi.w;
        record->attitude[1] = plant.q_bi.x;
        record->attitude[2] = plant.q_bi.y;
        record->attitude[3] = plant.q_bi.z;
        record->force_b_n[0] = input.force_b_n.x;
        record->force_b_n[1] = input.force_b_n.y;
        record->force_b_n[2] = input.force_b_n.z;
        record->moment_b_nm[0] = aero_moment_b_nm.x;
        record->moment_b_nm[1] = aero_moment_b_nm.y;
        record->moment_b_nm[2] = aero_moment_b_nm.z;
        record->accel_cmd_ecef[0] = command.accel_cmd_ecef.x;
        record->accel_cmd_ecef[1] = command.accel_cmd_ecef.y;
        record->accel_cmd_ecef[2] = command.accel_cmd_ecef.z;
        record->mach = mach;
        record->density = atmosphere_state.density_kgpm3;
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    if ((argc != 5 && argc != 6 && argc != 7) ||
        (std::strcmp(argv[4], "sensors") != 0 &&
         std::strcmp(argv[4], "quiet") != 0)) {
        std::fprintf(stderr,
                     "usage: campaign_trajectory_golden <scenario.json> "
                     "<aero_table_file> <golden_output> sensors|quiet "
                     "[faults.json|-] [terrain.tile]\n");
        return EXIT_FAILURE;
    }

    /* Scenario parameters (original common/config.c reader). */
    gpu_campaign::DeviceScenarioAero scenario{};
    if (gpu_campaign::load_scenario_aero(argv[1], &scenario) != SIM_OK) {
        std::fprintf(stderr, "campaign_trajectory_golden: scenario load "
                             "failed: %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    const bool use_scenario_sensors = std::strcmp(argv[4], "sensors") == 0;
    gpu_campaign::DeviceSensorScenario sensor_scenario{};
    if (use_scenario_sensors &&
        gpu_campaign::load_scenario_sensors(argv[1], kDt, &sensor_scenario) !=
            SIM_OK) {
        std::fprintf(stderr, "campaign_trajectory_golden: scenario sensor "
                             "load failed: %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    /* Optional fault script (planA Phase 4 item 3), loaded with the
       original parser plus the GPU campaign scope guard. Use "-" to skip
       when a terrain tile follows. */
    FaultInjection host_faults{};
    const FaultInjection *fault_script = nullptr;
    if ((argc == 6 && std::strcmp(argv[5], "-") != 0) ||
        (argc == 7 && std::strcmp(argv[5], "-") != 0)) {
        if (gpu_campaign::load_fault_script(argv[5], &host_faults) != SIM_OK) {
            std::fprintf(stderr, "campaign_trajectory_golden: fault script "
                                 "load failed: %s\n", argv[5]);
            return EXIT_FAILURE;
        }
        fault_script = &host_faults;
    }
    /* Optional terrain tile (planA Phase 4 item 4): switches the chain to
       the WGS-84 scene with geodetic refresh and collision termination. */
    gpu_campaign::DeviceTerrainGrid host_terrain_grid{};
    SceneBasis terrain_scene = make_terrain_scene_basis();
    EarthModel terrain_earth = earth_model_wgs84();
    TerrainContext terrain_ctx{&terrain_scene, &terrain_earth, nullptr};
    const TerrainContext *terrain = nullptr;
    if (argc == 7) {
        if (gpu_campaign::pack_terrain_tile_file(
                argv[6], &host_terrain_grid,
                gpu_campaign::DeviceExtrapolationPolicy::ClampAndWarn) != SIM_OK) {
            std::fprintf(stderr, "campaign_trajectory_golden: terrain tile "
                                 "load failed: %s\n", argv[6]);
            return EXIT_FAILURE;
        }
        terrain_ctx.grid = &host_terrain_grid;
        terrain = &terrain_ctx;
    }

    /* Self-contained aero table: same two samples as device_force_probe. */
    const AeroTableSample samples[] = {
        {0.0, -0.2, -0.2, 0.10, 0.02, 0.03, 0.04, 0.05, 0.06},
        {1.0, 0.2, 0.2, 0.40, 0.08, 0.09, 0.10, 0.11, 0.12},
    };
    if (aero_database_write_file(
            argv[2], samples, 2u, AERO_DB_EXTRAPOLATION_CLAMP_AND_WARN) !=
        SIM_OK) {
        return EXIT_FAILURE;
    }
    AeroDatabase database{};
    if (aero_database_load_file(argv[2], &database) != SIM_OK) {
        return EXIT_FAILURE;
    }
    AeroModel aero{};
    aero.enabled = scenario.force.enabled != 0u ? 1 : 0;
    aero.reference_area_m2 = scenario.force.reference_area_m2;
    aero.reference_length_m = scenario.force.reference_length_m;
    aero.drag_coefficient = scenario.drag_coefficient;
    aero.control_force_coefficient = scenario.force.control_force_coefficient;
    aero.control_moment_coefficient = scenario.force.control_moment_coefficient;
    aero.database = &database;

    AtmosphereModel atmosphere = atmosphere_model_isa();
    atmosphere.maximum_model_height_m = 11000.0;  /* scenario value */

    /* Two full runs must agree bitwise before the baseline is accepted. */
    const gpu_campaign::DeviceSensorScenario *scenario_sensors =
        use_scenario_sensors ? &sensor_scenario : nullptr;
    GoldenRecord *first = new GoldenRecord[kGoldenSteps + 1u]();
    GoldenRecord *second = new GoldenRecord[kGoldenSteps + 1u]();
    DropCounts drops_first{};
    DropCounts drops_second{};
    int run_status = run_chain(&aero, &atmosphere,
                               scenario.wind_velocity_ecef_mps,
                               scenario_sensors, fault_script, terrain,
                               first, &drops_first, kGoldenSteps);
    if (run_status != 0) {
        std::fprintf(stderr,
                     "campaign_trajectory_golden: chain failed at gate %d\n",
                     run_status);
        return EXIT_FAILURE;
    }
    run_status = run_chain(&aero, &atmosphere,
                           scenario.wind_velocity_ecef_mps,
                           scenario_sensors, fault_script, terrain,
                           second, &drops_second, kGoldenSteps);
    if (run_status != 0 ||
        !records_identical(first, second, kGoldenSteps + 1u) ||
        std::memcmp(&drops_first, &drops_second, sizeof(DropCounts)) != 0) {
        std::fprintf(stderr, "campaign_trajectory_golden: non-deterministic "
                             "chain\n");
        return EXIT_FAILURE;
    }

    /* Last written record: the collision record when the terrain stops the
       run early, otherwise the full run. */
    const int collision_step = drops_first.surface_collision_step;
    const unsigned last_row = collision_step >= 0
        ? static_cast<unsigned>(collision_step) : kGoldenSteps;

    /* Sanity: the aero path must be active and the run must stay finite. */
    double max_aero_force = 0.0;
    for (unsigned step = 1; step <= last_row; ++step) {
        const GoldenRecord &record = first[step];
        const double magnitude =
            record.force_b_n[0] * record.force_b_n[0] +
            record.force_b_n[1] * record.force_b_n[1] +
            record.force_b_n[2] * record.force_b_n[2];
        if (magnitude > max_aero_force) max_aero_force = magnitude;
        if (!std::isfinite(record.time) || !std::isfinite(record.mach) ||
            !std::isfinite(record.density) ||
            !std::isfinite(record.pos_ecef[0]) ||
            !std::isfinite(record.pos_ecef[1]) ||
            !std::isfinite(record.pos_ecef[2])) {
            std::fprintf(stderr, "campaign_trajectory_golden: non-finite "
                                 "record at step %u\n", step);
            return EXIT_FAILURE;
        }
    }
    if (max_aero_force <= 0.0) {
        std::fprintf(stderr, "campaign_trajectory_golden: aero force never "
                             "contributed\n");
        return EXIT_FAILURE;
    }

    /* Write the golden file. */
    std::FILE *out = std::fopen(argv[3], "w");
    if (out == nullptr) {
        std::fprintf(stderr, "campaign_trajectory_golden: cannot write %s\n",
                     argv[3]);
        return EXIT_FAILURE;
    }
    std::fprintf(out,
                 "# campaign_trajectory_golden v1 steps=%u dt=%.17g "
                 "instance=0\n",
                 last_row, kDt);
    std::fprintf(out, "# columns: step time pos_ecef[3] vel_ecef[3] "
                 "attitude_wxyz[4] force_b_n[3] moment_b_nm[3] "
                 "accel_cmd_ecef[3] mach density aero_flags "
                 "command_mode command_status\n");
    std::fprintf(out, "# dropout_events: imu=%u accel=%u speed=%u "
                 "seeker=%u total=%u\n",
                 drops_first.imu, drops_first.accel, drops_first.speed,
                 drops_first.seeker,
                 drops_first.imu + drops_first.accel + drops_first.speed +
                     drops_first.seeker);
    std::fprintf(out, "# fault_active_steps: %u\n",
                 drops_first.fault_active_steps);
    std::fprintf(out, "# surface_collision_step: %d\n", collision_step);
    for (unsigned step = 0; step <= last_row; ++step) {
        const GoldenRecord &record = first[step];
        const double doubles[] = {
            record.time,
            record.pos_ecef[0], record.pos_ecef[1], record.pos_ecef[2],
            record.vel_ecef[0], record.vel_ecef[1], record.vel_ecef[2],
            record.attitude[0], record.attitude[1], record.attitude[2],
            record.attitude[3],
            record.force_b_n[0], record.force_b_n[1], record.force_b_n[2],
            record.moment_b_nm[0], record.moment_b_nm[1], record.moment_b_nm[2],
            record.accel_cmd_ecef[0], record.accel_cmd_ecef[1],
            record.accel_cmd_ecef[2],
            record.mach, record.density,
        };
        std::fprintf(out, "%u", record.step);
        for (const double value : doubles) {
            std::fprintf(out, " %.17g", value);
        }
        std::fprintf(out, " %u %u %u\n", record.aero_flags,
                     record.command_mode, record.command_status);
    }
    std::fclose(out);
    aero_database_unload(&database);

    const GoldenRecord &last = first[last_row];
    std::printf("campaign_trajectory_golden: PASS steps=%u time=%.6f "
                "final_pos=(%.9f,%.9f,%.9f) final_vel=(%.9f,%.9f,%.9f) "
                "mach=%.6f density=%.9f aero_flags=0x%x mode=%u "
                "status=0x%x dropout_events=imu:%u,accel:%u,speed:%u,"
                "seeker:%u fault_active_steps=%u surface_collision_step=%d\n",
                last_row, last.time, last.pos_ecef[0], last.pos_ecef[1],
                last.pos_ecef[2], last.vel_ecef[0], last.vel_ecef[1],
                last.vel_ecef[2], last.mach, last.density, last.aero_flags,
                last.command_mode, last.command_status,
                drops_first.imu, drops_first.accel, drops_first.speed,
                drops_first.seeker, drops_first.fault_active_steps,
                collision_step);
    delete[] first;
    delete[] second;
    return EXIT_SUCCESS;
}
