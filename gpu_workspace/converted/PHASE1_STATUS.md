# Phase 1 module probe status

These results are from the current checked-out Aircraft-Simu worktree. A
successful `nvcc -dc` result means host/device compilation of the generated
translation unit succeeded; it does not yet mean that the module is callable
from a kernel. Device-link probes are currently enabled only for the minimal
vertical slice and `missile_plant_6dof`.

| Source module | Converter | nvcc `-dc` | Current decision |
|---|---:|---:|---|
| `actuator_model.c` | pass | pass | host-buildable; device header qualification pending |
| `matrix3.c` | pass + audited sanitizer | pass | device-linkable after signed loop-index adaptation; broader numerical regression pending |
| `aero_model.c` | pass | pass | host-buildable; table layout/device API pending |
| `aero_surrogate.c` | pass | **fail** | host file-loading path remains blocked: generated `_Bool` is not accepted by nvcc |
| `aero_surrogate_device.c` | manual split + pass | pass | device evaluation path; host numeric probe and device-link probe pass |
| `atmosphere_model.c` | pass | pass | host-buildable; device header qualification pending |
| `earth_model.c` | pass | pass | host-buildable; device header qualification pending |
| `geo_coordinate.c` | pass | pass | host-buildable; device header qualification pending |
| `gravity_model.c` | pass | pass | host-buildable; device header qualification pending |
| `mass_model.c` | pass | pass | host-buildable; device header qualification pending |
| `propulsion_model.c` | pass | pass | host-buildable; device header qualification pending |
| `sensor_noise.c` | pass + helper sanitizer | pass | device-callable from the campaign sensor path; deterministic zero-noise configuration used for first slice |
| `sensor_vector3.c` | pass + helper sanitizer | pass | fixed-capacity delay state initialized on device; device callability exercised by campaign |
| `sensor_imu.c`, `sensor_accel.c`, `sensor_speed.c` | pass | pass | wrappers linked into campaign sensor frame builder |
| `sensor_seeker.c` | pass + helper sanitizer | pass | deterministic in-memory target truth adapter; LOS/range/closing velocity feed PNG |
| `flight_control_sim/src/*.c` selected set | pass + helper/scheduler adaptations | pass | full `flight_controller_step` call graph linked into single-instance campaign |
| `missile_plant_6dof.c` | pass | pass | device-linkable; host Euler probe passes |

The `_Bool` failure is intentionally retained as a conversion defect instead
of being hidden by a compiler flag. The generated source and compiler log are
the inputs for the next manual adaptation step.
