# Phase 1 module probe status

These results are from the current checked-out Aircraft-Simu worktree. A
successful `nvcc -dc` result means host/device compilation of the generated
translation unit succeeded; it does not by itself mean that the module is
callable from a kernel. The M3 production campaign now device-links the
physical modules called out below and verifies them against the CPU baseline.

| Source module | Converter | nvcc `-dc` | Current decision |
|---|---:|---:|---|
| `actuator_model.c` | pass + audited sanitizer | pass | device-linked in M3; actuator dynamics CPU/GPU golden passes |
| `matrix3.c` | pass + audited sanitizer | pass | device-linkable after signed loop-index adaptation; broader numerical regression pending |
| `aero_model.c` | pass | pass | host-buildable; table layout/device API pending |
| `aero_surrogate.c` | pass | **fail** | host file-loading path remains blocked: generated `_Bool` is not accepted by nvcc |
| `aero_surrogate_device.c` | manual split + pass | pass | device evaluation path; host numeric probe and device-link probe pass |
| `atmosphere_model.c` | pass | pass | device-linked in the production force chain |
| `earth_model.c` | pass | pass | device-linked for Earth rotation and virtual-force terms |
| `geo_coordinate.c` | pass + helper sanitizer | pass | device-linked for ECEF/LLA/AGL state updates |
| `gravity_model.c` | pass + audited sanitizer | pass | device-linked in M3; gravity CPU/GPU golden passes |
| `mass_model.c` | pass + audited sanitizer | pass | device-linked in M3; mass/inertia CPU/GPU golden passes |
| `propulsion_model.c` | pass + audited sanitizer | pass | device-linked in M3; propulsion CPU/GPU golden passes |
| `sensor_noise.c` | pass + helper sanitizer | pass | device-callable from the campaign sensor path; deterministic zero-noise configuration used for first slice |
| `sensor_vector3.c` | pass + helper sanitizer | pass | fixed-capacity delay state initialized on device; device callability exercised by campaign |
| `sensor_imu.c`, `sensor_accel.c`, `sensor_speed.c` | pass | pass | wrappers linked into campaign sensor frame builder |
| `sensor_seeker.c` | pass + helper sanitizer | pass | deterministic in-memory target truth adapter; LOS/range/closing velocity feed PNG |
| `flight_control_sim/src/*.c` selected set | pass + helper/scheduler adaptations | pass | full `flight_controller_step` call graph linked into single-instance campaign |
| `missile_plant_6dof.c` | pass | pass | device-linked; production Euler/RK4 paths participate in CPU/GPU regression |

The `_Bool` failure is intentionally retained for the host-only surrogate file
instead of being hidden by a compiler flag. The split
`aero_surrogate_device.c` evaluation path is the device implementation used by
the production campaign.
