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
#include "env/actuator_model.h"
#include "env/earth_model.h"
#include "env/geo_coordinate.h"
#include "env/gravity_model.h"
#include "env/mass_model.h"
#include "env/propulsion_model.h"
#include "env/fault_injection.h"
#include "fc/fc_state.h"
#include "device_environment.cuh"
#include "device_force.cuh"

namespace gpu_campaign {

constexpr std::uint32_t kInvalidAssetIndex = UINT32_MAX;
constexpr std::uint32_t kCommunicationDelayCapacity = 17u;

enum class DeviceFailureStrategy : std::uint32_t {
    ContinueOnFailure = 0,
    StopOnFailure = 1,
};

enum class DeviceTerrainMissingPolicy : std::uint32_t {
    FlatFill = 0,
    Error = 1,
    Nearest = 2,
};

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

/* Production campaign metadata.  Network ports and process scheduling are
   deliberately absent: the single-process GPU executor accepts those JSON
   fields for compatibility but does not use them for device execution. */
struct DeviceCampaignConfig {
    std::uint32_t instance_count;
    std::uint32_t failure_strategy;
    std::uint32_t trace_mode;
    std::uint32_t chunk_steps;
};

/* Per-launch scheduling metadata. Trace records are chunk-local, while each
   record's step field and DeviceInstanceState::global_step remain global. */
struct DeviceChunkLaunch {
    std::uint32_t instance_count;
    std::uint32_t max_steps;
    std::uint32_t trace_capacity;
    std::uint32_t packed_lanes;
    std::uint32_t write_initial_state;
    std::uint32_t trace_enabled;
};

/* A terrain resource owns a contiguous range in the campaign-wide flat tile
   array. Individual instance configs reference this descriptor by index. */
struct DeviceTerrainSet {
    std::uint32_t first_grid;
    std::uint32_t grid_count;
    std::uint32_t missing_policy;
    std::uint32_t reserved;
    double flat_fill_height_m;
};

/* Pointer-free, per-instance configuration produced from runtime.json and
   its referenced scenario/flight-control/fault files.  Large immutable
   assets are referenced by integer indexes into campaign-owned arrays. */
struct DeviceInstanceConfig {
    std::uint32_t instance_id;
    std::uint32_t integrator;
    std::uint64_t random_seed;
    double dt;
    double max_time;
    double hit_radius_m;

    EarthModel earth;
    std::uint32_t enable_earth_rotation;
    double earth_rotation_rate_radps;

    Vec3 initial_missile_position_ecef;
    Vec3 initial_missile_velocity_ecef;
    Vec3 initial_target_position_ecef;
    Vec3 initial_target_velocity_ecef;
    double initial_mass_kg;
    double initial_propellant_mass_kg;
    Matrix3 initial_inertia_b;

    double actuator_time_constant_s;
    double actuator_position_limit;
    double actuator_rate_limit;

    GravityModel gravity;
    AtmosphereModel atmosphere;
    PropulsionModel propulsion;
    DeviceAeroForceConfig aero_force;
    double aero_drag_coefficient;
    Vec3 wind_velocity_ecef_mps;
    std::uint32_t aero_table_policy;
    std::uint32_t aero_height_envelope_enabled;
    double aero_height_min_m;
    double aero_height_max_m;
    std::uint32_t aero_actuator_envelope_enabled;
    std::uint32_t reserved_aero;
    double aero_actuator_min_rad;
    double aero_actuator_max_rad;

    DeviceSensorScenario sensors;
    FlightControllerConfig flight_control;

    std::uint32_t terrain_enabled;
    std::uint32_t los_occlusion_enabled;
    std::uint32_t terrain_missing_policy;
    std::uint32_t terrain_cache_tile_count;
    double terrain_flat_fill_height_m;

    std::uint32_t aero_asset_index;
    std::uint32_t aero_surrogate_asset_index;
    std::uint32_t terrain_asset_index;
    std::uint32_t fault_asset_index;
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
    Hit = 8,
    Timeout = 9,
    ConfigError = 10,
    NumericError = 11,
    LosTerrainError = 12,
    TerrainError = 13,
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
    Vec3 acceleration_ecef;
    double latitude_rad;
    double longitude_rad;
    double height_m;
    double height_agl_m;
    double mass_kg;
    double propellant_mass_kg;
    Vec3 target_position_ecef;
    Vec3 target_velocity_ecef;
    double target_latitude_rad;
    double target_longitude_rad;
    double target_height_m;
    double truth_range_m;
    double truth_closing_velocity_mps;
    double actuator_position[3];
    double actuator_rate[3];
    double mass_flow_kgps;
    std::uint32_t fault_started_mask;
    std::uint32_t fault_ended_mask;
    std::uint32_t fault_active_mask;
    std::uint32_t reserved_fault;
    SensorFrame sensor_frame;
    ControlCommand control_command;
    std::uint32_t has_io_frame;
    std::uint32_t reserved_io;
};

struct DeviceSensorSuite {
    ImuSensor imu;
    AccelSensor accel;
    SpeedSensor speed;
    SeekerSensor seeker;
    Vec3 target_position_ecef;
    Vec3 target_velocity_ecef;
};

struct DeviceCommunicationDelayLine {
    SensorFrame frames[kCommunicationDelayCapacity];
    std::uint32_t head;
    std::uint32_t count;
};

struct DeviceCommunicationReorderState {
    SensorFrame previous;
    std::uint32_t has_previous;
};

struct DeviceFaultStats {
    std::uint32_t configured_fault_count;
    std::uint32_t fault_start_count;
    std::uint32_t fault_end_count;
    std::uint32_t active_step_count;
    std::uint32_t sensor_affected_step_count;
    std::uint32_t actuator_affected_step_count;
    std::uint32_t max_concurrent_active;
    std::uint32_t reserved;
};

struct DeviceDiagnosticStats {
    double max_quat_norm_error;
    double max_dcm_orthogonality_error;
    double min_mass_kg;
    double min_inertia_diag_kgm2;
    std::uint32_t aero_model_flags_or;
    std::uint32_t aero_extrapolated_sample_count;
    std::uint32_t model_degradation_flags_or;
    std::uint32_t sample_count;
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

    /* Production runtime state. All mutable history is instance-local so a
       chunk boundary or another lane cannot reset or contaminate it. */
    MassModel mass;
    Matrix3 initial_inertia_b;
    double initial_mass_kg;
    ActuatorState acceleration_actuators[3];
    LlaCoord target_lla;
    double min_range_m;
    double time_of_closest_s;
    double last_mass_flow_kgps;
    AeroTableSample last_valid_aero_sample;
    std::uint32_t have_last_valid_aero_sample;
    std::uint32_t hit;
    std::uint32_t global_step;
    std::uint32_t chunk_records_written;
    std::uint32_t terminal_status;
    SensorFrame last_sensor;
    ControlCommand last_command;
    DeviceCommunicationDelayLine communication_delay;
    DeviceCommunicationReorderState communication_reorder;
    DeviceFaultStats fault_stats;
    DeviceDiagnosticStats diagnostic_stats;
    std::uint32_t last_fault_started_mask;
    std::uint32_t last_fault_ended_mask;
    std::uint32_t last_fault_active_mask;
    std::uint32_t reserved_production;
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
static_assert(std::is_trivially_copyable<DeviceCampaignConfig>::value,
              "DeviceCampaignConfig must be trivially copyable");
static_assert(std::is_standard_layout<DeviceCampaignConfig>::value,
              "DeviceCampaignConfig must use standard layout");
static_assert(std::is_trivially_copyable<DeviceChunkLaunch>::value,
              "DeviceChunkLaunch must be trivially copyable");
static_assert(std::is_standard_layout<DeviceChunkLaunch>::value,
              "DeviceChunkLaunch must use standard layout");
static_assert(std::is_trivially_copyable<DeviceTerrainSet>::value,
              "DeviceTerrainSet must be trivially copyable");
static_assert(std::is_standard_layout<DeviceTerrainSet>::value,
              "DeviceTerrainSet must use standard layout");
static_assert(std::is_trivially_copyable<DeviceInstanceConfig>::value,
              "DeviceInstanceConfig must be trivially copyable");
static_assert(std::is_standard_layout<DeviceInstanceConfig>::value,
              "DeviceInstanceConfig must use standard layout");

}  // namespace gpu_campaign

#endif
