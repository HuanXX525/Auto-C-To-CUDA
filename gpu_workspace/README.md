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

The current container can compile and device-link the executable, but its CUDA
runtime reports an incompatible/missing driver. A successful `make` therefore
does not yet constitute a runtime GPU result.

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
