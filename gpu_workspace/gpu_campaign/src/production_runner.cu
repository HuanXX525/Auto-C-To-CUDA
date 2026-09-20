#include "production_runner.hpp"

#include <cuda_runtime.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "host_campaign_config.hpp"
#include "host_environment_packer.hpp"
#include "production_output.hpp"
#include "production_step.cuh"

namespace gpu_campaign {
namespace {

bool cuda_ok(cudaError_t status, const char *operation) {
    if (status == cudaSuccess) return true;
    std::fprintf(stderr, "gpu_campaign: %s: %s\n", operation,
                 cudaGetErrorString(status));
    return false;
}

template <typename T>
class DeviceArray {
public:
    DeviceArray() : data_(nullptr), count_(0u) {}
    ~DeviceArray() {
        if (data_ != nullptr) (void)cudaFree(data_);
    }
    DeviceArray(const DeviceArray &) = delete;
    DeviceArray &operator=(const DeviceArray &) = delete;

    bool allocate(std::size_t count, const char *label) {
        count_ = count;
        if (count == 0u) return true;
        return cuda_ok(cudaMalloc(reinterpret_cast<void **>(&data_),
                                  count * sizeof(T)), label);
    }
    bool upload_async(const std::vector<T> &values, cudaStream_t stream,
                      const char *label) {
        if (!allocate(values.size(), label)) return false;
        if (values.empty()) return true;
        return cuda_ok(cudaMemcpyAsync(
            data_, values.data(), values.size() * sizeof(T),
            cudaMemcpyHostToDevice, stream), label);
    }
    T *get() { return data_; }
    const T *get() const { return data_; }
    std::size_t count() const { return count_; }

private:
    T *data_;
    std::size_t count_;
};

class CudaStream {
public:
    CudaStream() : stream_(nullptr) {}
    ~CudaStream() {
        if (stream_ != nullptr) {
            (void)cudaStreamSynchronize(stream_);
            (void)cudaStreamDestroy(stream_);
        }
    }
    CudaStream(const CudaStream &) = delete;
    CudaStream &operator=(const CudaStream &) = delete;

    bool create(const char *label) {
        return cuda_ok(cudaStreamCreateWithFlags(
            &stream_, cudaStreamNonBlocking), label);
    }
    cudaStream_t get() const { return stream_; }

private:
    cudaStream_t stream_;
};

class CudaEvent {
public:
    CudaEvent() : event_(nullptr) {}
    ~CudaEvent() {
        if (event_ != nullptr) (void)cudaEventDestroy(event_);
    }
    CudaEvent(const CudaEvent &) = delete;
    CudaEvent &operator=(const CudaEvent &) = delete;

    bool create(unsigned int flags, const char *label) {
        return cuda_ok(cudaEventCreateWithFlags(&event_, flags), label);
    }
    cudaEvent_t get() const { return event_; }

private:
    cudaEvent_t event_;
};

/* Host-side buffers used by the M5 pipeline.  Pageable vectors force a
   synchronous staging copy on most CUDA drivers; pinned storage lets the
   copy stream drain chunk k while the compute stream advances chunk k+1. */
template <typename T>
class PinnedArray {
public:
    PinnedArray() : data_(nullptr), count_(0u) {}
    ~PinnedArray() {
        if (data_ != nullptr) (void)cudaFreeHost(data_);
    }
    PinnedArray(const PinnedArray &) = delete;
    PinnedArray &operator=(const PinnedArray &) = delete;

    bool allocate(std::size_t count, const char *label) {
        count_ = count;
        if (count == 0u) return true;
        void *raw = nullptr;
        if (!cuda_ok(cudaHostAlloc(&raw, count * sizeof(T),
                                   cudaHostAllocPortable), label)) {
            return false;
        }
        data_ = static_cast<T *>(raw);
        return true;
    }

    T *get() { return data_; }
    const T *get() const { return data_; }
    std::size_t count() const { return count_; }

private:
    T *data_;
    std::size_t count_;
};

bool build_resources(
    const HostCampaignPlan &plan, std::vector<DeviceInstanceConfig> *configs,
    std::vector<DeviceAeroTable> *aero_tables,
    std::vector<AeroSurrogateModel> *aero_surrogates,
    std::vector<DeviceTerrainSet> *terrain_sets,
    std::vector<DeviceTerrainGrid> *terrain_grids,
    std::vector<FaultInjection> *fault_scripts) {
    configs->reserve(plan.instances.size());
    for (const HostInstancePlan &instance : plan.instances) {
        configs->push_back(instance.device);
    }
    aero_tables->resize(plan.aero_assets.size());
    for (std::size_t index = 0u; index < plan.aero_assets.size(); ++index) {
        const SimStatus status = pack_aero_database_file(
            plan.aero_assets[index].c_str(), &(*aero_tables)[index]);
        if (status != SIM_OK) {
            std::fprintf(stderr, "gpu_campaign: cannot pack aero asset %s "
                         "(status=%d)\n", plan.aero_assets[index].c_str(),
                         static_cast<int>(status));
            return false;
        }
    }
    aero_surrogates->resize(plan.aero_surrogate_assets.size());
    for (std::size_t index = 0u; index < plan.aero_surrogate_assets.size();
         ++index) {
        const SimStatus status = pack_aero_surrogate_file(
            plan.aero_surrogate_assets[index].c_str(),
            &(*aero_surrogates)[index]);
        if (status != SIM_OK) {
            std::fprintf(stderr, "gpu_campaign: cannot pack aero surrogate %s "
                         "(status=%d)\n",
                         plan.aero_surrogate_assets[index].c_str(),
                         static_cast<int>(status));
            return false;
        }
    }
    terrain_sets->resize(plan.terrain_assets.size());
    for (std::size_t set_index = 0u; set_index < plan.terrain_assets.size();
         ++set_index) {
        DeviceTerrainSet &set = (*terrain_sets)[set_index];
        set.first_grid = static_cast<std::uint32_t>(terrain_grids->size());
        const HostTerrainAsset &host_set = plan.terrain_assets[set_index];
        for (const std::string &path : host_set.tile_paths) {
            DeviceTerrainGrid grid{};
            const SimStatus status = pack_terrain_tile_file(
                path.c_str(), &grid, DeviceExtrapolationPolicy::Error);
            if (status != SIM_OK) {
                std::fprintf(stderr, "gpu_campaign: cannot pack terrain tile %s "
                             "(status=%d)\n", path.c_str(),
                             static_cast<int>(status));
                return false;
            }
            terrain_grids->push_back(grid);
        }
        set.grid_count = static_cast<std::uint32_t>(terrain_grids->size()) -
                         set.first_grid;
        set.missing_policy = 0u;
        set.reserved = 0u;
        set.flat_fill_height_m = 0.0;
    }
    fault_scripts->reserve(plan.fault_assets.size());
    for (const HostFaultAsset &asset : plan.fault_assets) {
        fault_scripts->push_back(asset.definition);
    }
    return true;
}

const char *exit_reason_name(std::uint32_t value) {
    switch (static_cast<DeviceExitReason>(value)) {
    case DeviceExitReason::Active: return "active";
    case DeviceExitReason::Completed: return "completed";
    case DeviceExitReason::PlantError: return "plant_error";
    case DeviceExitReason::SensorError: return "sensor_error";
    case DeviceExitReason::FlightControlError: return "flight_control_error";
    case DeviceExitReason::AeroError: return "aero_error";
    case DeviceExitReason::FaultError: return "fault_error";
    case DeviceExitReason::SurfaceCollision: return "surface_collision";
    case DeviceExitReason::Hit: return "hit";
    case DeviceExitReason::Timeout: return "timeout";
    case DeviceExitReason::ConfigError: return "config_error";
    case DeviceExitReason::NumericError: return "numeric_error";
    case DeviceExitReason::LosTerrainError: return "los_terrain_query_failed";
    case DeviceExitReason::TerrainError: return "terrain_initialization_failed";
    case DeviceExitReason::SkippedAfterFailure: return "skipped_after_failure";
    }
    return "unknown";
}

}  // namespace

int run_production_campaign(const ProductionOptions &options) {
    HostCampaignPlan plan;
    std::string error;
    const SimStatus load_status = load_campaign_plan(
        options.config_path, &plan, &error);
    if (load_status != SIM_OK) {
        std::fprintf(stderr, "gpu_campaign: invalid production config: %s "
                     "(status=%d)\n", error.c_str(),
                     static_cast<int>(load_status));
        return EXIT_FAILURE;
    }
    if (options.output_dir != nullptr) plan.output_dir = options.output_dir;
    plan.device.chunk_steps = options.chunk_steps;
    plan.device.trace_mode = static_cast<std::uint32_t>(options.trace_mode);

    std::vector<DeviceInstanceConfig> configs;
    std::vector<DeviceAeroTable> aero_tables;
    std::vector<AeroSurrogateModel> aero_surrogates;
    std::vector<DeviceTerrainSet> terrain_sets;
    std::vector<DeviceTerrainGrid> terrain_grids;
    std::vector<FaultInjection> fault_scripts;
    if (!build_resources(plan, &configs, &aero_tables, &aero_surrogates,
                         &terrain_sets, &terrain_grids, &fault_scripts)) {
        return EXIT_FAILURE;
    }

    int device_count = 0;
    if (!cuda_ok(cudaGetDeviceCount(&device_count), "cudaGetDeviceCount") ||
        device_count == 0) {
        std::fprintf(stderr, "gpu_campaign: no CUDA device available\n");
        return EXIT_FAILURE;
    }
    const std::uint32_t instances =
        static_cast<std::uint32_t>(configs.size());
    std::uint32_t lanes = options.lanes_per_block;
    ProductionMapping mapping = options.mapping;
    if (mapping == ProductionMapping::Auto) {
        mapping = ProductionMapping::Packed;
        lanes = instances <= 1023u ? 4u : 32u;
    }
    if (mapping == ProductionMapping::Serial) lanes = 1u;
    const std::uint32_t block_size = mapping == ProductionMapping::Packed
        ? lanes : 1u;
    const std::uint32_t block_count = mapping == ProductionMapping::Packed
        ? (instances + block_size - 1u) / block_size : instances;
    if (options.chunk_steps == 0u || options.chunk_steps == UINT32_MAX ||
        instances == 0u || block_size == 0u) {
        std::fprintf(stderr, "gpu_campaign: invalid production dimensions\n");
        return EXIT_FAILURE;
    }
    const std::uint32_t trace_capacity = options.chunk_steps + 1u;
    const std::size_t size_limit =
        std::numeric_limits<std::size_t>::max();
    if (static_cast<std::size_t>(instances) > size_limit / trace_capacity) {
        std::fprintf(stderr, "gpu_campaign: trace allocation is too large\n");
        return EXIT_FAILURE;
    }
    const std::size_t trace_count = static_cast<std::size_t>(instances) *
        trace_capacity;
    if (trace_count > size_limit / sizeof(DeviceTraceRecord) ||
        static_cast<std::size_t>(instances) >
            size_limit / sizeof(DeviceInstanceState)) {
        std::fprintf(stderr, "gpu_campaign: trace/state allocation is too large\n");
        return EXIT_FAILURE;
    }
    const std::size_t state_bytes = static_cast<std::size_t>(instances) *
        sizeof(DeviceInstanceState);
    const std::size_t trace_bytes = trace_count * sizeof(DeviceTraceRecord);

    DeviceArray<DeviceInstanceConfig> device_configs;
    DeviceArray<DeviceInstanceState> device_states;
    std::array<DeviceArray<DeviceInstanceState>, 2> device_state_snapshots;
    std::array<DeviceArray<DeviceTraceRecord>, 2> device_trace;
    DeviceArray<DeviceAeroTable> device_aero;
    DeviceArray<AeroSurrogateModel> device_surrogates;
    DeviceArray<DeviceTerrainSet> device_terrain_sets;
    DeviceArray<DeviceTerrainGrid> device_terrain_grids;
    DeviceArray<FaultInjection> device_faults;
    std::array<PinnedArray<DeviceInstanceState>, 2> host_state_slots;
    std::array<PinnedArray<DeviceTraceRecord>, 2> host_trace_slots;
    std::array<CudaEvent, 2> kernel_start;
    std::array<CudaEvent, 2> kernel_stop;
    std::array<CudaEvent, 2> snapshot_ready;
    std::array<CudaEvent, 2> state_ready;
    std::array<CudaEvent, 2> trace_ready;
    CudaStream init_stream;
    CudaStream compute_stream;
    CudaStream copy_stream;
    if (!init_stream.create("cudaStreamCreate(init)") ||
        !compute_stream.create("cudaStreamCreate(compute)") ||
        !copy_stream.create("cudaStreamCreate(copy)")) {
        return EXIT_FAILURE;
    }
    const auto h2d_start = std::chrono::steady_clock::now();
    bool resources_ok =
        device_configs.upload_async(
            configs, init_stream.get(), "cudaMalloc/copy(instance configs)") &&
        device_states.allocate(instances, "cudaMalloc(states)") &&
        device_aero.upload_async(
            aero_tables, init_stream.get(), "cudaMalloc/copy(aero tables)") &&
        device_surrogates.upload_async(
            aero_surrogates, init_stream.get(),
            "cudaMalloc/copy(aero surrogates)") &&
        device_terrain_sets.upload_async(
            terrain_sets, init_stream.get(),
            "cudaMalloc/copy(terrain sets)") &&
        device_terrain_grids.upload_async(
            terrain_grids, init_stream.get(),
            "cudaMalloc/copy(terrain grids)") &&
        device_faults.upload_async(
            fault_scripts, init_stream.get(),
            "cudaMalloc/copy(fault scripts)");
    for (std::size_t slot = 0u; slot < 2u && resources_ok; ++slot) {
        resources_ok = device_state_snapshots[slot].allocate(
                           instances, "cudaMalloc(state snapshot)") &&
                       device_trace[slot].allocate(
                           trace_count, "cudaMalloc(chunk trace)") &&
                       host_state_slots[slot].allocate(
                           instances, "cudaHostAlloc(state snapshot)") &&
                       host_trace_slots[slot].allocate(
                           trace_count, "cudaHostAlloc(chunk trace)");
    }
    if (!resources_ok ||
        !cuda_ok(cudaMemsetAsync(device_states.get(), 0, state_bytes,
                                 init_stream.get()),
                 "cudaMemsetAsync(states)") ||
        !cuda_ok(cudaStreamSynchronize(init_stream.get()),
                 "cudaStreamSynchronize(init)")) {
        return EXIT_FAILURE;
    }
    const double h2d_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - h2d_start).count();

    for (std::size_t slot = 0u; slot < 2u; ++slot) {
        if (!kernel_start[slot].create(cudaEventDefault,
                                       "cudaEventCreate(kernel start)") ||
            !kernel_stop[slot].create(cudaEventDefault,
                                      "cudaEventCreate(kernel stop)") ||
            !snapshot_ready[slot].create(
                cudaEventDisableTiming, "cudaEventCreate(snapshot ready)") ||
            !state_ready[slot].create(
                cudaEventDisableTiming, "cudaEventCreate(state ready)") ||
            !trace_ready[slot].create(
                cudaEventDisableTiming, "cudaEventCreate(trace ready)")) {
            return EXIT_FAILURE;
        }
    }

    std::vector<DeviceInstanceState> host_states(instances);
    std::vector<std::vector<DeviceTraceRecord>> traces(instances);
    std::vector<std::uint32_t> last_trace_step(instances, 0u);
    std::vector<unsigned char> have_trace_step(instances, 0u);
    std::array<bool, 2> pending = {{false, false}};
    std::array<std::uint64_t, 2> pending_sequence = {{0u, 0u}};
    double d2h_ms = 0.0;
    double host_encode_ms = 0.0;
    const auto drain_slot = [&](std::size_t slot) -> bool {
        if (!pending[slot]) return true;
        const auto wait_start = std::chrono::steady_clock::now();
        if (!cuda_ok(cudaEventSynchronize(trace_ready[slot].get()),
                     "cudaEventSynchronize(trace ready)")) {
            return false;
        }
        d2h_ms += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - wait_start).count();
        const auto encode_start = std::chrono::steady_clock::now();
        const DeviceInstanceState *slot_states = host_state_slots[slot].get();
        const DeviceTraceRecord *slot_trace = host_trace_slots[slot].get();
        for (std::uint32_t index = 0u; index < instances; ++index) {
            const std::uint32_t count =
                slot_states[index].chunk_records_written;
            if (count > trace_capacity) {
                std::fprintf(stderr,
                    "gpu_campaign: invalid trace count %u for instance %u\n",
                    count, index);
                return false;
            }
            const DeviceTraceRecord *source = slot_trace +
                static_cast<std::size_t>(index) * trace_capacity;
            if (count != 0u) {
                const std::uint32_t expected_step = have_trace_step[index]
                    ? last_trace_step[index] + 1u : source[0].step;
                for (std::uint32_t offset = 0u; offset < count; ++offset) {
                    if (source[offset].step != expected_step + offset) {
                        std::fprintf(stderr,
                            "gpu_campaign: non-contiguous trace step for "
                            "instance %u (got=%u expected=%u)\n", index,
                            source[offset].step, expected_step + offset);
                        return false;
                    }
                }
                last_trace_step[index] = source[count - 1u].step;
                have_trace_step[index] = 1u;
            }
            if (options.trace_mode == ProductionTraceMode::Full) {
                traces[index].insert(traces[index].end(), source,
                                     source + count);
            } else {
                /* Summary mode retains only the compact rows needed to
                   reconstruct FAULT_START/FAULT_END events.  The complete
                   trace still travels through the pinned staging slot, but
                   it is not retained in host memory or emitted to disk. */
                for (std::uint32_t offset = 0u; offset < count; ++offset) {
                    const DeviceTraceRecord &row = source[offset];
                    if ((row.fault_started_mask | row.fault_ended_mask) != 0u) {
                        traces[index].push_back(row);
                    }
                }
            }
        }
        host_encode_ms += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - encode_start).count();
        pending[slot] = false;
        return true;
    };

    const auto wall_start = std::chrono::steady_clock::now();
    double kernel_ms = 0.0;
    bool first_chunk = true;
    bool any_active = true;
    bool campaign_failed = false;
    std::uint64_t chunk_count = 0u;
    std::uint64_t maximum_chunks = 1u;
    for (const DeviceInstanceConfig &config : configs) {
        const std::uint64_t expected_steps = static_cast<std::uint64_t>(
            config.max_time / config.dt) + 3u;
        maximum_chunks = std::max(
            maximum_chunks,
            (expected_steps + options.chunk_steps - 1u) /
                options.chunk_steps + 1u);
    }
    while (any_active && chunk_count < maximum_chunks) {
        const std::size_t slot = static_cast<std::size_t>(chunk_count & 1u);
        if (!drain_slot(slot)) return EXIT_FAILURE;
        if (!cuda_ok(cudaMemsetAsync(
                         device_trace[slot].get(), 0, trace_bytes,
                         compute_stream.get()),
                     "cudaMemsetAsync(chunk trace)")) {
            return EXIT_FAILURE;
        }
        std::uint32_t first_global_step = 0u;
        if (!first_chunk) {
            first_global_step = UINT32_MAX;
            for (const DeviceInstanceState &state : host_states) {
                if (state.active != 0u) {
                    first_global_step = std::min(
                        first_global_step, state.global_step);
                }
            }
            if (first_global_step == UINT32_MAX) first_global_step = 0u;
        }
        DeviceChunkLaunch launch{};
        launch.instance_count = instances;
        launch.max_steps = options.chunk_steps;
        launch.trace_capacity = trace_capacity;
        launch.packed_lanes = mapping == ProductionMapping::Packed ? lanes : 0u;
        launch.write_initial_state = first_chunk ? 1u : 0u;
        launch.trace_enabled = 1u;
        launch.chunk_index = static_cast<std::uint32_t>(chunk_count);
        launch.first_global_step = first_global_step;
        launch.instance_begin = 0u;
        launch.instance_end = instances;
        launch.valid_steps = options.chunk_steps;
        if (!cuda_ok(cudaEventRecord(
                         kernel_start[slot].get(), compute_stream.get()),
                     "cudaEventRecord(kernel start)")) {
            return EXIT_FAILURE;
        }
        simulate_production_chunk<<<block_count, block_size, 0,
                                    compute_stream.get()>>>(
            device_states.get(), device_trace[slot].get(), launch,
            device_configs.get(), device_aero.get(),
            static_cast<std::uint32_t>(device_aero.count()),
            device_surrogates.get(),
            static_cast<std::uint32_t>(device_surrogates.count()),
            device_terrain_sets.get(),
            static_cast<std::uint32_t>(device_terrain_sets.count()),
            device_terrain_grids.get(),
            static_cast<std::uint32_t>(device_terrain_grids.count()),
            device_faults.get(),
            static_cast<std::uint32_t>(device_faults.count()));
        if (!cuda_ok(cudaGetLastError(), "simulate_production_chunk launch") ||
            !cuda_ok(cudaEventRecord(
                         kernel_stop[slot].get(), compute_stream.get()),
                     "cudaEventRecord(kernel stop)") ||
            !cuda_ok(cudaMemcpyAsync(
                         device_state_snapshots[slot].get(),
                         device_states.get(), state_bytes,
                         cudaMemcpyDeviceToDevice, compute_stream.get()),
                     "cudaMemcpyAsync(state snapshot D2D)") ||
            !cuda_ok(cudaEventRecord(
                         snapshot_ready[slot].get(), compute_stream.get()),
                     "cudaEventRecord(snapshot ready)") ||
            !cuda_ok(cudaStreamWaitEvent(
                         copy_stream.get(), snapshot_ready[slot].get(), 0u),
                     "cudaStreamWaitEvent(snapshot ready)") ||
            !cuda_ok(cudaMemcpyAsync(
                         host_state_slots[slot].get(),
                         device_state_snapshots[slot].get(), state_bytes,
                         cudaMemcpyDeviceToHost, copy_stream.get()),
                     "cudaMemcpyAsync(states D2H)") ||
            !cuda_ok(cudaEventRecord(
                         state_ready[slot].get(), copy_stream.get()),
                     "cudaEventRecord(state ready)") ||
            !cuda_ok(cudaMemcpyAsync(
                         host_trace_slots[slot].get(),
                         device_trace[slot].get(), trace_bytes,
                         cudaMemcpyDeviceToHost, copy_stream.get()),
                     "cudaMemcpyAsync(trace D2H)") ||
            !cuda_ok(cudaEventRecord(
                         trace_ready[slot].get(), copy_stream.get()),
                     "cudaEventRecord(trace ready)")) {
            return EXIT_FAILURE;
        }
        pending[slot] = true;
        pending_sequence[slot] = chunk_count;
        ++chunk_count;
        const auto state_wait_start = std::chrono::steady_clock::now();
        if (!cuda_ok(cudaEventSynchronize(state_ready[slot].get()),
                     "cudaEventSynchronize(state ready)")) {
            return EXIT_FAILURE;
        }
        d2h_ms += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - state_wait_start).count();
        std::memcpy(host_states.data(), host_state_slots[slot].get(),
                    state_bytes);
        float chunk_kernel_ms = 0.0f;
        if (!cuda_ok(cudaEventElapsedTime(
                         &chunk_kernel_ms, kernel_start[slot].get(),
                         kernel_stop[slot].get()),
                     "cudaEventElapsedTime(kernel)")) {
            return EXIT_FAILURE;
        }
        kernel_ms += chunk_kernel_ms;
        any_active = false;
        bool chunk_failure = false;
        for (const DeviceInstanceState &state : host_states) {
            any_active = any_active || state.active != 0u;
            chunk_failure = chunk_failure || state.terminal_status != SIM_OK;
        }
        campaign_failed = campaign_failed || chunk_failure;
        if (chunk_failure && plan.device.failure_strategy ==
                static_cast<std::uint32_t>(DeviceFailureStrategy::StopOnFailure)) {
            for (DeviceInstanceState &state : host_states) {
                if (state.active == 0u) continue;
                state.active = 0u;
                state.exit_reason = static_cast<std::uint32_t>(
                    DeviceExitReason::SkippedAfterFailure);
                state.terminal_status = SIM_ERR_INTERNAL;
            }
            campaign_failed = true;
            any_active = false;
        }
        first_chunk = false;
    }
    while (pending[0] || pending[1]) {
        std::size_t slot = 0u;
        if (!pending[0] ||
            (pending[1] && pending_sequence[1] < pending_sequence[0])) {
            slot = 1u;
        }
        if (!drain_slot(slot)) return EXIT_FAILURE;
    }
    const auto wall_stop = std::chrono::steady_clock::now();
    if (any_active) {
        std::fprintf(stderr, "gpu_campaign: production run exceeded the "
                     "derived chunk bound\n");
        return EXIT_FAILURE;
    }

    for (std::uint32_t index = 0u; index < instances; ++index) {
        const DeviceInstanceState &state = host_states[index];
        std::printf("gpu_campaign: instance=%u steps=%u final_time=%.12f "
                    "final_x=%.12f final_y=%.12f exit_reason=%s status=%u "
                    "hit=%u min_range=%.12f trace_records=%zu\n",
                    state.instance_id, state.global_step, state.plant.time,
                    state.plant.pos_ecef.x, state.plant.pos_ecef.y,
                    exit_reason_name(state.exit_reason), state.terminal_status,
                    state.hit, state.min_range_m,
                    options.trace_mode == ProductionTraceMode::Full
                        ? traces[index].size() : 0u);
    }
    const double wall_ms = std::chrono::duration<double, std::milli>(
        wall_stop - wall_start).count();
    cudaDeviceProp device_properties{};
    int cuda_runtime_version = 0;
    (void)cudaGetDeviceProperties(&device_properties, 0);
    (void)cudaRuntimeGetVersion(&cuda_runtime_version);
    GpuExecutionInfo execution{};
    execution.mapping = mapping == ProductionMapping::Packed ? "packed" :
                                                               "serial";
    execution.trace_mode = options.trace_mode == ProductionTraceMode::Full
        ? "full" : "summary";
    execution.gpu_name = device_properties.name;
    execution.cuda_runtime_version =
        static_cast<std::uint32_t>(cuda_runtime_version);
    execution.lanes_per_block = lanes;
    execution.chunk_steps = options.chunk_steps;
    execution.chunk_count = chunk_count;
    execution.h2d_ms = h2d_ms;
    execution.kernel_ms = kernel_ms;
    execution.d2h_ms = d2h_ms;
    execution.host_encode_ms = host_encode_ms;
    execution.wall_ms = wall_ms;
    std::string output_error;
    const SimStatus output_status = write_production_outputs(
        plan, host_states, traces, execution, &output_error);
    if (output_status != SIM_OK) {
        std::fprintf(stderr, "gpu_campaign: output failed: %s\n",
                     output_error.c_str());
        return EXIT_FAILURE;
    }
    std::printf("gpu_campaign: production instances=%u mapping=%s lanes=%u "
                "chunk_steps=%u chunks=%llu trace_mode=%s kernel_ms=%.6f "
                "wall_ms=%.6f output_dir=%s\n",
                instances,
                mapping == ProductionMapping::Packed ? "packed" : "serial",
                lanes, options.chunk_steps,
                static_cast<unsigned long long>(chunk_count),
                options.trace_mode == ProductionTraceMode::Full ? "full" :
                                                                   "summary",
                kernel_ms, wall_ms, plan.output_dir.c_str());
    return campaign_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

}  // namespace gpu_campaign
