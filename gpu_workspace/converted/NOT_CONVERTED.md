# Conversion status

This file records module-level conversion decisions. A generated `.cu` file is
not considered device-callable until it passes both `nvcc -dc` and a kernel
device-link probe.

## Initial vertical slice

| Module | Status | Notes |
|---|---|---|
| `common/vec3.c` | device-linkable | Generated and compiled with nvcc; minimal kernel callability probe passes. |
| `common/quaternion.c` | device-linkable | Generated and compiled with nvcc; minimal kernel callability probe passes. |
| `common/random.c` | device-linkable | Generated and compiled with nvcc; Box-Muller runtime math validation remains pending. |
| `common/matrix3.c` | device-linkable-with-adaptation | Generated loop lowering required the audited signed-index sanitizer; common host/device probes pass, broader numerical regression remains pending. |
| `environment_sim/src/missile_plant_6dof.c` | device-linkable | Static helper qualification added in the derived output; host Euler probe and device-link probe pass. |
| `environment_sim/src/aero_surrogate.c` | host-only | The original file-loading path remains host-only; its generated full translation contains `_Bool` and is not part of the device build. |
| `gpu_workspace/staging/environment/aero_surrogate_device.c` | device-linkable | Manual device evaluation split; host numeric probe and device-link probe pass. |
| `environment_sim/src/sensor_{noise,vector3,imu,accel,speed,seeker}.c` | device-linkable in campaign | Fixed-capacity sensor implementations are compiled and called by `simulate_chunk`; helper qualification is recorded in `tools/sanitize_sensor_generated.py`. Ring-buffer view storage now uses a relative address offset rather than a pointer in the derived device ABI. |
| `flight_control_sim/src/{navigation,estimator,guidance_png,guidance_manager,autopilot,command_manager,fc_health,fc_modes,fc_scheduler,safety_monitor,fc_state}.c` | device-linkable in campaign | Full single-instance `flight_controller_step` call graph is compiled and linked; scheduler string handling is a device-safe derived adaptation. |
| remaining environment/flight-control modules | pending | Terrain, file-backed tables, fault scripts and multi-instance scheduling remain outside Phase 2. |
