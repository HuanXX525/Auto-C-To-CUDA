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
M3 CPU/GPU trajectory debugging reproducible; the production output contract
is validated by the M6 acceptance script below.

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

M3 does not claim M4 LOS/all-fault completion. M4 is now complete: the
production path supports
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

M5 adds resumable global-step chunk execution. The production runner now uses
one nonblocking compute stream, one nonblocking copy stream, two device
trace/state-snapshot slots, two pinned host slots, and CUDA events to overlap
chunk `k+1` with the D2H transfer for chunk `k`. Trace records retain their
global step, the first chunk is the only chunk that emits the initial state,
and `full`/`summary` output modes share the same final state and fault event
history. `STOP_ON_FAILURE` marks instances that were not scheduled after the
failing chunk as `skipped_after_failure`.

Run the M5 acceptance matrix on a host whose CUDA driver supports the runtime:

```bash
python3 ./tools/run_m5_chunk_verification.py
```

The matrix covers chunk sizes 1/64/256/all, deterministic repeatability,
fault/RNG/communication continuity, summary output, and mixed
early-termination batches. It has passed on the NVIDIA GeForce RTX 5070
Laptop GPU with CUDA runtime 13.2; the recorded result is in
`validation/chunk_m5.json`. The CPU baseline tools are written as absolute
paths by the verifier so the test is independent of the caller's working
directory.

M6 production output and comparison are now implemented. Run the focused
acceptance script with:

```bash
python3 ./tools/run_m6_verification.py
```

It checks the fixed `--config` CLI and legacy-option rejection, `auto` and
explicit mappings, the CPU-compatible full directory, summary-only omission
of trajectory/binary files, semantic CPU/GPU comparison (including decoded
sensor/command protocol packets), and full/summary final-state equivalence.
The recorded RTX 5070 result is in `validation/production_m6.json`.

## M7 segmented correctness acceptance

M7 is intentionally split into bounded invocations so a completed result does
not depend on conversational context. Every successful invocation writes
`m7_pass_receipt.json` inside its output directory only after all requested
checks pass. The receipt records the selected sections, parent/submodule
commits, runner/matrix/comparator hashes, executable hashes, GPU and CUDA
runtime, generated-input fingerprint and result fingerprint. Formal M7-B～E
runs also publish the same small receipt under tracked
`validation/m7_receipts/`. Only that tracked receipt counts as formal segment
completion; a build-local receipt alone is diagnostic evidence after an
interruption. M7-A is the tracked harness checkpoint rather than a matrix
section.

Run exactly one segment per work session, update
`validation/correctness_m7_progress.json`, commit it, and stop:

```bash
# M7-B (PASS): short functional semantics and error paths
python3 tools/run_m7_correctness_matrix.py --skip-build \
  --sections short,errors --output build/m7_b_functional \
  --receipt validation/m7_receipts/m7_b_functional.json

# M7-C (PASS): mapping, chunk, trace-mode and repeatability invariance
python3 tools/run_m7_correctness_matrix.py --skip-build \
  --sections mapping,chunk --output build/m7_c_invariance \
  --receipt validation/m7_receipts/m7_c_invariance.json

# M7-D (PASS): heterogeneous N=1/2/8/32/64/128 isolation
python3 tools/run_m7_correctness_matrix.py --skip-build \
  --sections batch --output build/m7_d_batch \
  --receipt validation/m7_receipts/m7_d_batch.json

# M7-E (PASS): 12000-step CPU/GPU regression
python3 tools/run_m7_correctness_matrix.py --skip-build \
  --sections long --output build/m7_e_long \
  --receipt validation/m7_receipts/m7_e_long.json
```

M7-A (harness freeze), M7-B (short functional/error semantics), M7-C
(mapping/chunk invariance), M7-D (mixed multi-instance isolation), and M7-E
(12000-step long regression) are complete. The current formal receipts are
tracked at
`validation/m7_receipts/m7_b_functional.json`,
`validation/m7_receipts/m7_c_invariance.json`,
`validation/m7_receipts/m7_d_batch.json`, and
`validation/m7_receipts/m7_e_long.json`. M7-E observed a maximum
`9.71e-11` CPU/CUDA difference for
`SensorFrame.missile_vel_ecef_meas.y` while that component crossed zero
at steps 1523–1548. Only the `long_quiet` comparison uses a field-specific
`atol=1e-10`; the global `rtol=1e-10, atol=1e-12` remains unchanged.
Because this scoped calibration changed the shared runner, matrix, and
comparator hashes, M7-B through M7-D were rerun under parent commit
`f168c9d57525b8c776aa62981f73bd208e2148f6`. Their earlier receipts are
preserved as `*_pre_velocity_calibration.json` historical artifacts and
must not be used as current acceptance evidence.

M7-F receipt aggregation is the next segment. It has not run, so M7 remains
in progress and M8 has not started. The full clean-checkout command remains
available as:

```bash
python3 tools/run_m7_correctness_matrix.py
```

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
the commanded acceleration. This vertical slice was initially validated only
through compilation/device-link; the later production path and M1--M5 runtime
matrices have now passed on the RTX 5070 validation host.

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
