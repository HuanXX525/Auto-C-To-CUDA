#include <cstdio>

#include "common/matrix3.h"
#include "common/quaternion.h"
#include "env/missile_plant_6dof.h"
#include "env/sensor_accel.h"
#include "env/sensor_imu.h"
#include "env/sensor_seeker.h"
#include "env/sensor_speed.h"
#include "fc/fc_state.h"

namespace {

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
    for (int i = 0; i < 3; ++i) config.axis[i] = noise_config();
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
    config.scheduler_base_rate_hz = 100.0;
    return config;
}

}  // namespace

int main() {
    const double dt = 0.01;
    const SensorVector3Config sensor_config = vector_config();
    const FlightControllerConfig controller_config = fc_config();
    ImuSensor imu{};
    AccelSensor accel{};
    SpeedSensor speed{};
    SeekerSensor seeker{};
    FlightController controller{};
    PlantState6Dof plant{};
    PlantInput6Dof input{};
    SeekerSensorConfig seeker_config{};
    SensorFrame frame{};
    ControlCommand command{};
    SeekerTruth truth{};
    SeekerMeasurement measurement{};
    SensorSampleStatus sample_status{};
    SimStatus status;

    seeker_config.enabled = 1;
    seeker_config.sample_period_s = dt;
    seeker_config.range = noise_config();
    seeker_config.closing_velocity = noise_config();
    for (int i = 0; i < 3; ++i) {
        seeker_config.los_unit_axis[i] = noise_config();
        seeker_config.los_rate_axis[i] = noise_config();
    }
    status = sensor_imu_init(&imu, &sensor_config, 11u, dt);
    if (status == SIM_OK) status = sensor_accel_init(&accel, &sensor_config, 13u, dt);
    if (status == SIM_OK) status = sensor_speed_init(&speed, &sensor_config, 17u, dt);
    if (status == SIM_OK) status = sensor_seeker_init(&seeker, &seeker_config, 19u, dt);
    if (status == SIM_OK) status = flight_controller_init(&controller, &controller_config);
    if (status != SIM_OK) return 1;

    plant.q_bi = quat_identity();
    plant.vel_ecef = vec3_make(10.0, 0.0, 0.0);
    plant.mass = 2.0;
    plant.inertia_b = matrix3_identity();
    input.gravity_ecef_mps2 = vec3_make(0.0, -9.8, 0.0);
    truth.target_position_ecef_m = vec3_make(1000.0, 100.0, 0.0);
    for (unsigned int step = 0; step < 8u; ++step) {
        frame = SensorFrame{};
        frame.seq = step + 1u;
        frame.sim_time = plant.time;
        frame.dt = dt;
        frame.missile_lat_rad_meas = plant.lat_rad;
        frame.missile_lon_rad_meas = plant.lon_rad;
        frame.missile_height_m_meas = plant.height_m;
        frame.missile_height_agl_m_meas = plant.height_agl_m;
        status = sensor_imu_update(&imu, plant.time, dt, plant.omega_b,
                                   &frame.missile_gyro_b_meas, &sample_status);
        if (status == SIM_OK) status = sensor_accel_update(
            &accel, plant.time, dt, plant.accel_ecef,
            &frame.missile_accel_ecef_meas, &sample_status);
        if (status == SIM_OK) status = sensor_speed_update(
            &speed, plant.time, dt, plant.vel_ecef,
            &frame.missile_vel_ecef_meas, &sample_status);
        truth.missile_position_ecef_m = plant.pos_ecef;
        truth.missile_velocity_ecef_mps = plant.vel_ecef;
        if (status == SIM_OK) status = sensor_seeker_update(
            &seeker, plant.time, dt, &truth, &measurement, &sample_status);
        if (status != SIM_OK) return 2;
        frame.sensor_valid_flags = SIM_SENSOR_VALID_IMU_GYRO |
            SIM_SENSOR_VALID_ACCEL | SIM_SENSOR_VALID_SPEED |
            SIM_SENSOR_VALID_GEODETIC | SIM_SENSOR_VALID_SEEKER;
        frame.target_range_meas = measurement.range_m;
        frame.target_los_unit_ecef_meas = measurement.los_unit_ecef;
        frame.target_los_rate_ecef_meas = measurement.los_rate_ecef_radps;
        frame.target_closing_velocity_meas = measurement.closing_velocity_mps;
        status = flight_controller_step(&controller, &frame, &command);
        if (status != SIM_OK) return 3;
        input.force_b_n = vec3_scale(command.accel_cmd_ecef, plant.mass);
        status = missile_plant_step(&plant, &input, dt, INTEGRATOR_RK4);
        if (status != SIM_OK) return 4;
    }
    std::printf("campaign_chain_compare: steps=8 time=%.12f mode=%u status=0x%08x "
                "sensor_flags=0x%08x cmd_ax=%.12f\n",
                plant.time, command.command_mode, command.command_status,
                frame.sensor_valid_flags, command.accel_cmd_ecef.x);
    return 0;
}
