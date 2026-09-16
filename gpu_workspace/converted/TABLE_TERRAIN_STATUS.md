# Table and terrain migration audit

The next Phase 4/5 inputs were staged without entering the campaign kernel:

| Module | Conversion probe | Decision | Blocking issue |
|---|---:|---|---|
| `environment_sim/src/aero_database.c` | generated, `nvcc` blocked | host loader + parser | generated `_Bool`; file I/O and `owned_samples`/`samples` pointers are host responsibilities |
| `environment_sim/src/terrain_model.c` | generated, `nvcc` blocked | host tile/cache manager | generated `_Bool`; lazy LRU cache, paths and tile pointers cannot be copied into device state |

The GPU-facing design is therefore a two-stage adapter:

1. Host code parses the existing binary table/tile files and validates them.
2. `gpu_campaign/include/device_environment.cuh` provides
   `DeviceAeroTable`/`DeviceTerrainGrid`, containing only flat read-only arrays,
   dimensions, bounds and an extrapolation policy. The host can upload these
   structures once per campaign and index them by each instance without file
   access or pointers. The independent host numeric and CUDA device-link probe
   is `tools/build_device_environment_probe.sh`.

The generated probes intentionally remain outside `Makefile` and
`gpu_campaign`. Their failures are recorded rather than hidden behind compiler
flags. The host-side `gpu_campaign/src/host_environment_packer.cpp` now loads
and packs the existing binary formats, and its write/read/pack/query regression
probe passes. The current campaign continues using the deterministic
surrogate/flat environment path for force computation; optional campaign wiring
currently performs diagnostic-only device queries and records extrapolation or
lookup-error bits in `DeviceTraceRecord.environment_flags`.

The table-backed force arithmetic is separately implemented in
`gpu_campaign/include/device_force.cuh` and matches the host
`aero_model_evaluate` table branch in `device_force_probe`. It remains disabled
in the campaign until scenario force parameters and CPU trajectory golden data
are mapped explicitly.

## Scenario mapping and air velocity (2026-08-31)

The two dynamics prerequisites are now in place, both gated by host golden
probes:

1. `gpu_campaign/include/device_force.cuh` adds
   `device_air_velocity_body` (mirrors `environment_force_model.c`:
   `quat_to_dcm` -> transpose -> rotate `vel_ecef - wind_ecef`) and
   `device_mach_from_air_velocity` (guarded sound-speed division). The force
   probe now covers 220 points: 188 aero table/branch points plus 32 air
   velocity/mach points, including non-normalized quaternions, zero-quaternion
   error propagation and wind subtraction. The CUDA device-link gate compiles
   the helpers against the converted `quaternion.cu`/`matrix3.cu`.
2. `gpu_campaign/src/host_scenario_mapper.cpp` maps the real
   `configs/baseline/scenario.json` aerodynamics and wind sections to the
   pointer-free `DeviceAeroForceConfig` using the original
   `common/src/config.c` reader, reproducing the `env_app.c` defaults.
   `HOLD_LAST_VALID` and unknown extrapolation policies are hard errors
   because the device table only implements ERROR/CLAMP_AND_WARN. Probe:
   `tools/build_scenario_mapper_probe.sh` (7 groups: baseline values, missing
   key defaults, full override, policy rejection, missing file).

The campaign default dynamics are still unchanged; wiring the mapped
parameters plus `device_air_velocity_body` into `simulate_step.cu` is the next
step and requires the CPU trajectory golden baseline first.

## CPU trajectory golden baseline (2026-08-31)

`tools/build_campaign_trajectory_golden.sh` produces
`golden/campaign_trajectory_golden.txt`, the reference for the upcoming
dynamics wiring. The probe (`host_tests/campaign_trajectory_golden.cpp`)
mirrors the campaign chain for instance_id=0 (same seeds 11/13/17/19, sensor
and FC configs, initial state and deterministic target) but replaces the
temporary `force_b_n = accel_cmd * mass` mapping with the
`environment_force_model.c` composition the kernel will adopt:

- `air_velocity_b = R_ecef2body * (vel_ecef - wind_ecef)` with scenario wind,
- ISA atmosphere (`atmosphere_model_evaluate`) at `plant.height_m` for density
  and sound speed, guarded mach,
- table-branch `aero_model_evaluate` (scenario coefficients, two-sample table
  also used by `device_force_probe`),
- `force_b_n = R_ecef2body * (accel_cmd * mass) + aero_force`,
  `moment_b_nm = aero_moment`; gravity stays `(0, -9.8, 0)`; propulsion is
  disabled; pitch/yaw actuator inputs are 0.0 (the campaign ControlCommand
  carries no actuator angles yet).

Every step of the chain runs the original staging C sources compiled on the
host; scenario parameters come from the real baseline `scenario.json` through
the probed mapper. The probe runs the chain twice and requires bitwise
identical records, checks finiteness and that the aero force actually
contributes, then writes 65 records (initial state + 64 steps, 26 columns,
`%.17g`) including `force_b_n`, `moment_b_nm`, `mach`, `density` and
`aero_flags` per step. The recorded `aero_flags=1` (clamp extrapolation) is
expected: gravity bends the velocity so beta leaves the two-sample table
range, which exercises the clamp path end to end.

Device wiring acceptance: the kernel trajectory (same scenario, table and
initial state) must reproduce these records field by field.

## Dynamics wiring (2026-09-01)

The kernel now computes the scenario force composition when
`--scenario PATH` is given (otherwise the legacy
`force_b_n = accel_cmd * mass` mapping is kept): `atmosphere_model.h` gained
device qualification and was re-converted; `simulate_chunk` calls
`device_air_velocity_body`, `atmosphere_model_evaluate`, guarded mach and
`device_aero_force_evaluate`, then composes virtual + aero force and stores
per-step `force_b_n`/`moment_b_nm`/`mach`/`density`/`aero_flags` in the
trace (new `DeviceTraceRecord` fields; `DeviceExitReason::AeroError` on
failure). `main.cu` loads the scenario through the probed mapper, requires
`--aero-file` when the scenario enables aerodynamics, and `--dump-trace PATH`
writes instance 0's trace in the golden column layout. Acceptance:
`tools/compare_golden_trace.py golden/campaign_trajectory_golden.txt
<dump> --tol 1e-9` (tolerance allows nvcc FMA contraction; integers must
match exactly).

**Verified on RTX 5070 (2026-09-01):** the device trace matches the CPU
golden baseline across all 65 records; worst relative deviation 9.5e-16
(double-precision epsilon, FMA contraction), integer columns exact,
`exit_reason=1`, `sensor_flags=0x1f`, `environment_flags=0x0` (the
diagnostic query uses mach/alpha/beta = 0, in-table). The legacy mapping
(without `--scenario`) also passed its first real-GPU regression
(`kernel_ms≈1.9` for 8 steps). With scenario dynamics the 64-step
single-instance kernel measures `23.06 ms` (`2775 instance_steps/s`): the
aero composition adds the `device_aero_force_evaluate` table lookup and
atmosphere evaluation per step.

**Batch isolation with scenario dynamics (2026-09-01, RTX 5070):**
`tools/run_scenario_batch_golden.sh 64` runs N=1/2/8/32 with the scenario
aero path; instance 0's 65-record trace matches the CPU golden baseline for
every N (all instances `exit_reason=1`), so the shared read-only aero table
introduces no cross-instance interference. Kernel time is flat across batch
size (~23 ms for 64 steps): 2788 / 5625 / 22167 / 86074 instance-steps/s for
N=1/2/8/32 — near-perfect parallel scaling while every SM lane remains
1-thread blocks.

**N=128 target matrix (2026-09-01, RTX 5070):** extended to N=64/128 with the
same golden acceptance — instance 0 matches, all 64/128 instances
`exit_reason=1`. Full scenario-dynamics scaling table (64 steps,
`--repeats 1 --warmup 0`):

| N | kernel_ms | instance-steps/s |
|---|-----------|------------------|
| 1 | 22.96 | 2 788 |
| 2 | 22.76 | 5 625 |
| 8 | 23.10 | 22 167 |
| 32 | 23.79 | 86 074 |
| 64 | 24.60 | 166 522 |
| 128 | 24.64 | 332 525 |

Kernel time stays within +7% from N=1 to N=128: the planA target batch size
runs at effectively the cost of a single instance, ~332k instance-steps/s on
1-thread blocks. Further gains require warp-level parallelism (Phase 4), not
more blocks.

**Thread-mapping experiment (planA 5.4, 2026-09-01, RTX 5070):** the kernel
now supports `--mapping packed` (one full serial instance per lane,
`instance = blockIdx.x * blockDim.x + threadIdx.x`); the per-instance
instruction stream is unchanged, so golden acceptance is bit-exact — packed
N=1/8/32/128 all PASS against the CPU golden baseline.

Lanes-per-block sweep at N=128 (64 steps, scenario dynamics, 5 repeats):

| mapping | block_count | kernel_ms | inst-steps/s |
|---|---|---|---|
| serial (1 lane) | 128 | 24.6 | 333k |
| packed lanes=4 | 32 | **14.0** | **585k** |
| packed lanes=8 | 16 | 15.2 | 537k |
| packed lanes=16 | 8 | 18.5 | 444k |
| packed lanes=32 | 4 | 23.2 | 354k |

Large-N comparison (64 steps, serial vs packed lanes=32):

| N | serial ms | packed ms | speedup | packed inst-steps/s |
|---|---|---|---|---|
| 512 | 46.4 | 23.4 | 2.0× | 1.40M |
| 1024 | 92.8 | 32.3 | 2.9× | 2.03M |
| 2048 | 179.5 | 39.6 | 4.5× | 3.31M |

Conclusions: (1) serial saturates at ~700k instance-steps/s — beyond the
per-SM block residency it pays one warp slot per instance, so kernel time
grows linearly with N. (2) packed amortizes instruction fetch across lanes
and stays near-flat, but needs enough blocks to cover the 48 SMs: at N=128
lanes=32 creates only 4 blocks (4 SMs busy) and loses to serial, while
lanes=4 creates 32 blocks and wins 1.76×. (3) The planA target N=128 is
fastest with packed lanes=4 so far; sub-warp sweeps (lanes=2/3) and lanes=4
at large N remain to be tested.

**Phase 4 scenario items 1/2: sensor noise / delay / dropout (2026-09-01):**
`--scenario` now also loads the sensors.* section via
`load_scenario_sensors` (host_scenario_mapper.cpp, mirroring env_app.c's
loader incl. per-sensor clamp defaults). The kernel initializes instances
with the scenario configs; without `--scenario` the zero-noise default is
kept, so the legacy path is unchanged. Golden: the probe gained a
`sensors|quiet` mode; `campaign_trajectory_golden_noise.txt` is the noise
baseline (bitwise-reproducible, quiet baseline unchanged).

GPU acceptance (RTX 5070): single instance N=1 matches the noise golden at
machine precision (max rel 1.3e-15), batch isolation N=1/2/8/32/64/128 all
PASS — per-instance sensor RNG streams and the shared read-only aero table
introduce no cross-instance interference. This also validates device-side
`sim_random_normal`/`round`/`sqrt` bit-equivalence against g++ under real
(non-zero) noise. Noise costs ~17% kernel time (27.0 vs 23.1 ms for 64
steps, N=1 serial). Remaining Phase 4 items: dropout scenario (baseline has
dropout=0), fault injection, terrain dynamics, LOS occlusion.

**Phase 4 item 2 complete: real dropout verification (2026-09-05, RTX
5070).** `tools/run_dropout_verification.sh` derives a 30%-dropout scenario
from baseline, builds the CPU dropout golden (the probe now counts dropped
samples per sensor in the golden header and asserts bitwise reproducibility
of the drop sequence), then verifies the GPU against it.

Results: 80/256 samples dropped (imu:22, accel:26, speed:13, seeker:19 ≈
31%), trajectory diverges from the noise golden, and the FC safety path is
genuinely exercised — 22 of 64 steps enter FC_COMMAND_HOLD (mode 6) with
status 0x44/0x48, versus 2 steps in the noise golden. GPU acceptance:
N=1/8/32/128 all match the CPU dropout golden at machine precision. This
proves device-side `sim_random_uniform01` drop decisions consume the RNG
stream in the exact same order as g++, and multi-instance dropout streams
stay isolated. GPU kernel time 30.8 ms for 64 steps N=1 (dropout branch adds
~14% over the noise run's 27.0 ms). Remaining Phase 4 items: fault
injection, terrain dynamics, LOS occlusion.

**Phase 4 item 3: fault injection (2026-09-07/08, COMPLETE).** The original
fault_injection.c was split for the GPU campaign: the runtime path
(fault_injection_update / fault_injection_apply_sensor, pure POD, no
pointers) is staged device-side (ROSE-converted, C2CUDA_HOST_DEVICE
statics, nvcc -dc clean), while the JSON loader (fault_injection_loader.c,
byte-identical load_config / parse_target / parse_type verified by diff)
stays host-only. The header gained an extern "C" config include guard so
C++ TUs link against the C-compiled config.c.

Integration: `--faults PATH` loads the script with the original parser plus
a GPU scope guard (actuator STUCK/SCALE and COMMUNICATION_DELAY/REORDER are
rejected — the campaign kernel has no actuator model or communication
layer); the parsed FaultInjection is uploaded once and each instance keeps
a per-instance copy (was_active replays deterministically per launch). The
kernel applies faults between sensor sampling and flight control, mirroring
the env_app.c loop order.

Verification (`tools/run_fault_verification.sh`): a four-fault script
(DROPOUT on seeker, RAMP_BIAS on gyro, HOLD_VALUE on speed, BIAS on
los_rate, windows inside 64 steps) yields fault_active_steps=44 (exactly
the union of the four windows), 12 steps in FC_COMMAND_HOLD (vs 2 in the
noise golden), and a trajectory distinct from the noise baseline;
bitwise reproducible. GPU acceptance on RTX 5070: N=1/8/32/128 all match
the CPU fault golden at machine precision, and the scope guard correctly
rejects an actuator fault. This proves device-side fault-timeline decisions
(fault_is_active epsilon windows) and all four accumulate paths (bias/ramp
scaling, hold override, valid-bit clearing) are bit-equivalent to g++.
Fault overhead ~0 kernel time (25.8 vs 23.1 ms for 64 steps N=1 serial,
within run-to-run noise). Remaining Phase 4 items: terrain dynamics, LOS
occlusion.

**Phase 4 item 4: terrain dynamics (2026-09-16, CPU golden ready, GPU
acceptance pending).** earth_model.c and geo_coordinate.c were staged and
ROSE-converted (pure math PODs; headers gained C2CUDA_HOST_DEVICE). The
kernel gained `update_geodetic` (ECEF->LLA via WGS-84 + terrain AGL,
mirroring env_app.c update_geodetic_state): an initial refresh at instance
init, a per-step refresh after the plant step, and a surface-collision
check at the top of each step (AGL <= 0 -> new exit reason SurfaceCollision
= 7, early termination).

Scene semantics: `--terrain-file` now switches the campaign to a WGS-84
scene — origin LLA(30deg, 120deg, 1000 m) with local axes
east/up (initial speed along east, gravity along -up, target 1000 m east /
(100+5*id) m up), preserving the trajectory geometry of the geocentric
scene. Without --terrain-file everything stays on the legacy geocentric
path (quiet/noise/fault goldens verified unchanged, bit-for-bit).

CPU goldens: `terrain_tile_generator` writes flat tiles around the scene
box; the fly-over tile (990 m) runs the full 64 steps with real geodetic
feedback (density 1.1119 = ISA at ~998 m vs 1.225 at 0 m in the geocentric
scene), and the collision tile (999.5 m) terminates at step 33 — exactly
the analytic drop prediction (0.5 m AGL, 4.9t^2). The collision golden is
truncated (34 rows); compare_golden_trace.py gained --allow-zero-tail for
the zero-filled trace records beyond the collision step. `tools/
run_terrain_verification.sh` verifies the GPU against both goldens (fly-over
exit_reason=1, collision exit_reason=7 at the same step) plus batch
isolation on the fly-over tile. GPU acceptance still needs to run outside
the sandbox. Remaining Phase 4 item: LOS occlusion.

**Final mapping selection (sub-warp sweep + lanes=4 large-N, 2026-09-01):**

Sub-warp sweep at N=128: lanes=2 (64 blocks) 14.55 ms, lanes=3 (43 blocks)
14.90 ms, lanes=4 (32 blocks) **14.01 ms** — lanes=4 remains the optimum;
full SM coverage beyond 32 blocks buys nothing while halving the per-warp
instance count, confirming a block-count ≈ 2/3 of the SM count is the sweet
spot for this kernel.

lanes=4 at large N (vs the lanes=32 numbers above):

| N | lanes=4 ms | lanes=32 ms | winner |
|---|---|---|---|
| 512 | **21.4** | 23.4 | lanes=4 |
| 1024 | 41.9 | **32.3** | lanes=32 |
| 2048 | 63.1 | **39.6** | lanes=32 |

The crossover sits between N=512 and N=1024: once block_count = N/4 grows
past a few hundred, warp-slot lane utilization (N/lanes warps to issue)
dominates over SM coverage and wide lanes win. Final selection:
**N ≤ 512: packed lanes=4; N ≥ 1024: packed lanes=32** (equivalently, pick
the widest lanes ∈ {4..32} that keeps block_count ≥ ~48). planA's target
N=128 runs at 585k instance-steps/s, 1.76× the serial baseline, with
bit-identical golden traces.
