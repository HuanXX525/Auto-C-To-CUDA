# GPU migration workspace

This directory is a derived workspace for the `planA.md` migration. The
original `external/Aircraft-Simu/` tree is not modified.

## Minimal conversion probe

```bash
./tools/convert_minimal.sh
./tools/build_common_probe.sh
./build/common_compare
```

Build the first single-instance CUDA campaign kernel with:

```bash
make
./build/bin/gpu_campaign --steps 8
```

## Production configuration model

M2 adds a host-side loader for the unchanged Aircraft-Simu `runtime.json`
schema and all referenced scenario, flight-control and fault files. It emits
pointer-free `DeviceCampaignConfig` and per-instance `DeviceInstanceConfig`
records, resolves paths independently of the caller's working directory, and
deduplicates immutable aero, surrogate, terrain and fault resources by integer
index. Network/process fields are retained as compatibility metadata but are
not used by the single-process GPU executor.

```bash
./tools/build_campaign_config_probe.sh
```

The probe covers N=1/2/128 and verifies that a missing runtime, a missing
per-instance reference and an invalid range all fail before any CUDA launch.
The production `gpu_campaign --config` path is now active. It maps each
instance into independent device configuration/state, runs the copied
sensor/flight-control/actuator/environment/6DOF chain, and leaves the legacy
probe CLI unchanged.

```bash
./build/bin/gpu_campaign \
  --config configs/runtime_1.json \
  --trace-mode full \
  --chunk-steps 256 \
  --mapping packed \
  --lanes-per-block 4
```

M3 qualifies and device-links actuator, gravity, propulsion and mass models.
The production force adapter follows CPU precedence for table, surrogate and
quadratic-drag paths, combines virtual control force, propulsion, gravity and
Earth-rotation terms, then updates propellant, total mass and inertia after the
configured Euler/RK4 plant step. The initial result writer is present to make
M3 CPU/GPU trajectory debugging reproducible; the complete output contract is
still an M6 gate.

Run the focused M3 acceptance matrix with:

```bash
./tools/run_m3_physics_verification.sh
```

It builds the CPU baseline without CMake, materializes unchanged-schema test
configs, and compares independent actuator, gravity and propulsion/mass/
inertia runs against both the CPU output and the tracked golden at
`rtol=1e-10, atol=1e-12`. Terrain and faults are disabled in these focused
cases. The normal baseline additionally passes the complete 978-step hit run.
Recorded results are in `validation/physics_m3.json`.

M3 does not claim M4 LOS/all-fault completion or M5 asynchronous double-
buffered chunk execution. M4 is now complete: the production path supports
16-sample ECEF/LLA LOS checks, ordered multi-tile text/binary index loading,
FLAT_FILL/ERROR missing-tile policies, all sensor/actuator/lockstep
communication faults, CPU-compatible metadata, and per-instance fault history.
Run the complete M4 matrix with:

```bash
./tools/run_m4_verification.sh
```

It compares CPU/GPU traces, diagnostics, summaries, event logs and packet
flags for clear/occluded/borderline terrain, missing-tile policies and every
fault class. The same overlap-recovery scenario is also run at N=1/8/32/128
to verify that no mutable fault, communication or LOS state crosses instances.
M5 (recoverable chunk semantics and asynchronous double buffering) remains the
next stage and is intentionally not included here.

Optional host-loaded environment inputs can now be supplied without changing
the default deterministic campaign:

```bash
./build/bin/gpu_campaign --steps 8 \
  --aero-file path/to/aero.bin --terrain-file path/to/tile.bin
```

The files are parsed on the host, packed into pointer-free device buffers, and
queried from the kernel for diagnostics. `environment_flags` reports bit 0/2
for clamped aero/terrain queries and bit 1/3 for lookup errors. The first
integration is diagnostic-only; it does not yet replace the surrogate force
model or alter plant dynamics.

The Phase 3 correctness baseline uses one block per instance:

```bash
./build/bin/gpu_campaign --steps 8 --instances 2
./build/bin/gpu_campaign --steps 8 --instances 8
./tools/run_multi_baseline.sh 8 10 2
```

Each instance has independent plant, sensor, flight-control, trace and RNG
state. Instance IDs perturb only the deterministic seed, initial speed and
target offset so batch output can be checked against repeated single-instance
runs. This is a correctness mapping, not yet a performance claim. The final
`instances=... kernel_ms_avg=... kernel_ms_median=... kernel_ms_min=...
kernel_ms_max=... instance_steps_per_sec=...` is measured with CUDA events and
contains kernel-only time;
host allocation and PCIe transfers are not included. Script arguments control
steps, repeats and warmup count.

For the first thread-mapping comparison:

```bash
./tools/run_mapping_baseline.sh 8 32 20 2
```

This compares one active lane per block with a 32-thread block whose lane 0
still executes the serial instance. It measures mapping overhead only; it is
not yet a warp-parallel implementation.

For workload scaling with the selected one-thread mapping:

```bash
./tools/run_scaling_baseline.sh 32 10 2 8 128 512 2048
```

The production-oriented horizon can be tested separately with `12000` steps.

The table/terrain audit is recorded in `converted/TABLE_TERRAIN_STATUS.md`.
The original database and terrain modules remain host-side because they combine
file I/O, lazy caches and pointer ownership; they must be converted to flat
read-only device buffers before participating in device-side force computation.
The current campaign accepts those buffers for diagnostic queries only.

The first pointer-free device adapters are now available in
`gpu_campaign/include/device_environment.cuh`. `DeviceAeroTable` provides a
fixed-capacity inverse-distance lookup matching the host implementation, with explicit error or clamp-and-warn
extrapolation, while `DeviceTerrainGrid` provides a bounded row-major height
grid with bilinear interpolation. Run
`tools/build_device_environment_probe.sh` to execute the host numeric probe and
the CUDA device-link gate. The host-side parser/packer is exercised by
`tools/build_host_environment_packer_probe.sh`; the end-to-end campaign upload
and diagnostic query can be repeated with
`tools/run_environment_campaign_probe.sh` on a machine with a working CUDA
driver.

The table-backed force calculation is isolated in
`gpu_campaign/include/device_force.cuh`. It mirrors the host
`aero_model_evaluate` table branch and is guarded by an independent
`device_force_probe` host golden comparison plus a CUDA device-link check. It
is not enabled by the default campaign yet; force-model activation requires
explicit scenario/config mapping and a new dynamics regression baseline.

The RTX 5070/CUDA 13.2 runtime has been verified. Terrain fly-over, collision
and N=1/8/32/128 isolation results are recorded in
`validation/terrain_m1.json`.

The probe currently validates host-callable generated functions. Device-callable
qualification and a kernel device-link test are separate gates; a successful
host compile is not sufficient for GPU integration.

## Single-instance sensor/control vertical slice

`gpu_campaign` now executes the real copied model chain inside one persistent
thread:

```text
sensor_imu/accel/speed/seeker
  -> SensorFrame
  -> flight_controller_step (safety, estimator, PNG, autopilot, command manager)
  -> command-driven missile_plant_step
  -> DeviceTraceRecord
```

The target is a deterministic in-memory target adapter at `(1000, 100, 0) m`;
terrain, file-backed aero tables and fault scripts are intentionally outside
this slice. Sensor delay/dropout/noise are configured off for the first
correctness run, while the actual sensor implementations and RNG state remain
on the device. The original ring-buffer API is initialized inside the kernel
so its embedded storage never refers to host memory. This is a Phase 2
adapter; the derived ring-buffer view now stores a relative address offset
instead of a pointer, so copied instance state contains no raw pointer field.

Regenerate and build the complete slice with:

```bash
./tools/convert_minimal.sh
./tools/convert_environment_probe.sh
./tools/convert_flight_control.sh
make
./tools/build_campaign_chain_probe.sh
```

The trace summary includes sensor-valid flags, flight-control mode/status and
the commanded acceleration. Runtime still requires a host with a compatible
NVIDIA driver; this container reaches compilation/device-link but fails at
`cudaGetDeviceCount` before launching a kernel.

## 6DOF probe

```bash
./tools/convert_environment_probe.sh
./build/plant_compare
```

The current environment module report is in `converted/PHASE1_STATUS.md`.
The original `aero_surrogate.c` file-loading path remains host-only because its
full generated translation contains `_Bool`. The derived
`aero_surrogate_device.c` evaluation path now passes host and device probes and
is the only surrogate path eligible for the future kernel call graph.
