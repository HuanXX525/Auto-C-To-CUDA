#ifndef GPU_CAMPAIGN_GPU_TYPES_CUH
#define GPU_CAMPAIGN_GPU_TYPES_CUH

#include <cstdint>
#include <type_traits>

#include "env/missile_plant_6dof.h"
#include "env/sensor_accel.h"
#include "env/sensor_imu.h"
#include "env/sensor_seeker.h"
#include "env/sensor_speed.h"
#include "env/atmosphere_model.h"
#include "env/earth_model.h"
#include "env/geo_coordinate.h"
#include "env/fault_injection.h"
#include "fc/fc_state.h"
#include "device_environment.cuh"
#include "device_force.cuh"

namespace gpu_campaign {

/* Scenario-provided sensor configurations (planA Phase 4 item 1/2: sensor
   noise, delay, dropout). The staging config structs are pointer-free POD,
   so they can be embedded by value. When enabled != 0 the kernel
   initializes each instance with these instead of the zero-noise defaults. */
struct DeviceSensorScenario {
    std::uint32_t enabled;
    SensorVector3Config imu;
    SensorVector3Config accel;
    SensorVector3Config speed;
    SeekerSensorConfig seeker;
};

struct DeviceConfig {
    double dt;
    std::uint32_t chunk_steps;
    std::uint32_t trace_capacity;
    std::uint32_t instance_count;
    std::uint32_t threads_per_instance;
    /* Thread mapping: 0 = serial (one block per instance, lane 0 executes);
       nonzero = packed lanes per block (instance = blockIdx.x * blockDim.x +
       threadIdx.x, one serial instance per lane). */
    std::uint32_t packed_lanes;
    /* Scenario dynamics: when aero_force.enabled != 0 the kernel replaces
       the legacy force_b_n = accel_cmd * mass mapping with the
       environment_force_model.c composition (air velocity, ISA atmosphere,
       aero table, virtual force). */
    DeviceAeroForceConfig aero_force;
    AtmosphereModel atmosphere;
    Vec3 wind_velocity_ecef;
    DeviceSensorScenario sensors;
    /* Terrain dynamics (planA Phase 4 item 4): when terrain_grid_enabled
       != 0 the kernel refreshes the plant geodetic fields each step
       (ECEF->LLA via the WGS-84 model, then terrain AGL) and terminates
       the instance with SurfaceCollision when AGL <= 0. The scene then
       uses WGS-84 coordinates: origin = LLA(30deg, 120deg, 1000 m) with a
       local basis (east, up) so the campaign trajectory keeps its shape
       (initial speed along east, gravity along -up, target 1000 m east and
       100 m up). */
    EarthModel earth;
    std::uint32_t terrain_grid_enabled;
    Vec3 scene_origin_ecef;
    Vec3 scene_east;
    Vec3 scene_up;
};

enum class DeviceExitReason : std::uint32_t {
    Active = 0,
    Completed = 1,
    PlantError = 2,
    SensorError = 3,
    FlightControlError = 4,
    AeroError = 5,
    FaultError = 6,
    SurfaceCollision = 7,
};

struct DeviceTraceRecord {
    std::uint32_t step;
    std::uint32_t status;
    std::uint32_t active;
    std::uint32_t reserved;
    double time;
    Vec3 position_ecef;
    Vec3 velocity_ecef;
    Quat attitude;
    std::uint32_t sensor_valid_flags;
    std::uint32_t sensor_fault_flags;
    std::uint32_t command_mode;
    std::uint32_t command_status;
    std::uint32_t environment_flags;
    Vec3 accel_command_ecef;
    /* Scenario-dynamics diagnostics, matching the CPU golden baseline
       columns. Zero while the legacy force mapping is active. */
    Vec3 force_b_n;
    Vec3 moment_b_nm;
    double mach;
    double density;
    std::uint32_t aero_flags;
};

struct DeviceSensorSuite {
    ImuSensor imu;
    AccelSensor accel;
    SpeedSensor speed;
    SeekerSensor seeker;
    Vec3 target_position_ecef;
    Vec3 target_velocity_ecef;
};

struct DeviceInstanceState {
    PlantState6Dof plant;
    PlantInput6Dof plant_input;
    std::uint32_t active;
    std::uint32_t exit_reason;
    std::uint32_t completed_steps;
    std::uint32_t instance_id;
    DeviceSensorSuite sensors;
    FlightController flight_controller;
    std::uint32_t environment_flags;
    std::uint32_t initialized;
    /* Per-step scenario-dynamics diagnostics mirrored into the trace. */
    double trace_mach;
    double trace_density;
    std::uint32_t trace_aero_flags;
    /* Per-instance fault script copy (was_active is per-instance runtime
       state; the script itself is shared read-only via the kernel
       argument). planA Phase 4 item 3. */
    FaultInjection faults;
};

static_assert(std::is_trivially_copyable<DeviceConfig>::value,
              "DeviceConfig must be trivially copyable");
static_assert(std::is_standard_layout<DeviceConfig>::value,
              "DeviceConfig must use standard layout");
static_assert(std::is_trivially_copyable<DeviceTraceRecord>::value,
              "DeviceTraceRecord must be trivially copyable");
static_assert(std::is_standard_layout<DeviceTraceRecord>::value,
              "DeviceTraceRecord must use standard layout");
static_assert(std::is_trivially_copyable<DeviceInstanceState>::value,
              "DeviceInstanceState must be trivially copyable");
static_assert(std::is_standard_layout<DeviceInstanceState>::value,
              "DeviceInstanceState must use standard layout");

}  // namespace gpu_campaign

#endif
