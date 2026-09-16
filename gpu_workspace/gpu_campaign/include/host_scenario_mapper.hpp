#ifndef GPU_CAMPAIGN_HOST_SCENARIO_MAPPER_HPP
#define GPU_CAMPAIGN_HOST_SCENARIO_MAPPER_HPP

#include <cstdint>

#include "common/vec3.h"
#include "device_environment.cuh"
#include "device_force.cuh"
#include "gpu_types.cuh"

namespace gpu_campaign {

/* Host-side mapping of the scenario.json aerodynamics and wind sections to
   the pointer-free device force configuration. Mirrors the key names,
   defaults and hard-error behaviour of env_app.c scenario loading. */
struct DeviceScenarioAero {
    DeviceAeroForceConfig force;
    double drag_coefficient;      /* scenario-only; used by the surrogate branch */
    Vec3 wind_velocity_ecef_mps;
    DeviceExtrapolationPolicy policy;  /* effective table policy */
    char table_path[192];
    std::uint32_t table_path_enabled;
    std::uint32_t policy_override_enabled;
};

/* Parses scenario_path with the original common/config.c reader. Missing
   keys fall back to the env_app.c defaults (disabled, zero coefficients,
   zero wind). Unknown extrapolation policies are errors; HOLD_LAST_VALID is
   preserved explicitly for the stateful production lookup path. */
SimStatus load_scenario_aero(const char *scenario_path, DeviceScenarioAero *out);

/* Parses the sensors.* section of scenario.json into the pointer-free device
   sensor scenario (planA Phase 4 items 1/2: sensor noise, delay, dropout).
   Mirrors load_vector_sensor_config / load_seeker_config in env_app.c,
   including the per-sensor min/max clamps and the hard error when an
   "enabled" key is missing. */
SimStatus load_scenario_sensors(const char *scenario_path, double simulation_dt_s,
                                 DeviceSensorScenario *out);

/* Parses a faults.json script with the original fault_injection loader and
   applies the GPU-campaign scope guard: actuator targets (STUCK/SCALE) and
   communication faults (COMMUNICATION_DELAY/COMMUNICATION_REORDER) are
   rejected because the legacy campaign kernel has no actuator model or
   communication layer. Sensor-frame/sensor-target faults (BIAS, DRIFT,
   RAMP_BIAS, HOLD_VALUE, DROPOUT, FORCE_INVALID) are supported. */
SimStatus load_fault_script(const char *faults_path, FaultInjection *out);

}  // namespace gpu_campaign

#endif
