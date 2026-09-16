#include "production_runner.hpp"

#include <cuda_runtime.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
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
    bool upload(const std::vector<T> &values, const char *label) {
        if (!allocate(values.size(), label)) return false;
        if (values.empty()) return true;
        return cuda_ok(cudaMemcpy(data_, values.data(), values.size() * sizeof(T),
                                  cudaMemcpyHostToDevice), label);
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

    DeviceArray<DeviceInstanceConfig> device_configs;
    DeviceArray<DeviceInstanceState> device_states;
    DeviceArray<DeviceTraceRecord> device_trace;
    DeviceArray<DeviceAeroTable> device_aero;
    DeviceArray<AeroSurrogateModel> device_surrogates;
    DeviceArray<DeviceTerrainSet> device_terrain_sets;
    DeviceArray<DeviceTerrainGrid> device_terrain_grids;
    DeviceArray<FaultInjection> device_faults;
    const std::uint32_t trace_capacity = options.trace_mode ==
        ProductionTraceMode::Full ? options.chunk_steps + 1u : 0u;
    if (!device_configs.upload(configs, "cudaMalloc/copy(instance configs)") ||
        !device_states.allocate(instances, "cudaMalloc(states)") ||
        !device_trace.allocate(static_cast<std::size_t>(instances) *
                                   trace_capacity, "cudaMalloc(chunk trace)") ||
        !device_aero.upload(aero_tables, "cudaMalloc/copy(aero tables)") ||
        !device_surrogates.upload(aero_surrogates,
                                  "cudaMalloc/copy(aero surrogates)") ||
        !device_terrain_sets.upload(terrain_sets,
                                    "cudaMalloc/copy(terrain sets)") ||
        !device_terrain_grids.upload(terrain_grids,
                                     "cudaMalloc/copy(terrain grids)") ||
        !device_faults.upload(fault_scripts,
                              "cudaMalloc/copy(fault scripts)")) {
        return EXIT_FAILURE;
    }
    if (!cuda_ok(cudaMemset(device_states.get(), 0,
                            instances * sizeof(DeviceInstanceState)),
                 "cudaMemset(states)")) return EXIT_FAILURE;

    std::vector<DeviceInstanceState> host_states(instances);
    std::vector<DeviceTraceRecord> host_chunk(
        static_cast<std::size_t>(instances) * trace_capacity);
    std::vector<std::vector<DeviceTraceRecord>> traces;
    if (options.trace_mode == ProductionTraceMode::Full) traces.resize(instances);

    cudaEvent_t kernel_start = nullptr;
    cudaEvent_t kernel_stop = nullptr;
    if (!cuda_ok(cudaEventCreate(&kernel_start), "cudaEventCreate(start)") ||
        !cuda_ok(cudaEventCreate(&kernel_stop), "cudaEventCreate(stop)")) {
        return EXIT_FAILURE;
    }
    float kernel_ms = 0.0f;
    const auto wall_start = std::chrono::steady_clock::now();
    bool first_chunk = true;
    bool any_active = true;
    std::uint64_t chunk_count = 0u;
    std::uint64_t maximum_chunks = 1u;
    for (const DeviceInstanceConfig &config : configs) {
        const std::uint64_t expected_steps = static_cast<std::uint64_t>(
            config.max_time / config.dt) + 3u;
        maximum_chunks = std::max(
            maximum_chunks,
            (expected_steps + options.chunk_steps - 1u) / options.chunk_steps + 1u);
    }
    while (any_active && chunk_count < maximum_chunks) {
        if (trace_capacity != 0u &&
            !cuda_ok(cudaMemset(device_trace.get(), 0,
                                host_chunk.size() * sizeof(DeviceTraceRecord)),
                     "cudaMemset(chunk trace)")) {
            return EXIT_FAILURE;
        }
        DeviceChunkLaunch launch{};
        launch.instance_count = instances;
        launch.max_steps = options.chunk_steps;
        launch.trace_capacity = trace_capacity;
        launch.packed_lanes = mapping == ProductionMapping::Packed ? lanes : 0u;
        launch.write_initial_state = first_chunk ? 1u : 0u;
        launch.trace_enabled = options.trace_mode == ProductionTraceMode::Full
            ? 1u : 0u;
        if (!cuda_ok(cudaEventRecord(kernel_start), "cudaEventRecord(start)")) {
            return EXIT_FAILURE;
        }
        simulate_production_chunk<<<block_count, block_size>>>(
            device_states.get(), device_trace.get(), launch,
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
            !cuda_ok(cudaEventRecord(kernel_stop), "cudaEventRecord(stop)") ||
            !cuda_ok(cudaEventSynchronize(kernel_stop),
                     "cudaEventSynchronize(stop)")) {
            return EXIT_FAILURE;
        }
        ++chunk_count;
        float chunk_ms = 0.0f;
        if (!cuda_ok(cudaEventElapsedTime(&chunk_ms, kernel_start, kernel_stop),
                     "cudaEventElapsedTime(kernel)")) return EXIT_FAILURE;
        kernel_ms += chunk_ms;
        if (!cuda_ok(cudaMemcpy(host_states.data(), device_states.get(),
                                instances * sizeof(DeviceInstanceState),
                                cudaMemcpyDeviceToHost),
                     "cudaMemcpy(states D2H)")) return EXIT_FAILURE;
        if (trace_capacity != 0u) {
            if (!cuda_ok(cudaMemcpy(host_chunk.data(), device_trace.get(),
                                    host_chunk.size() * sizeof(DeviceTraceRecord),
                                    cudaMemcpyDeviceToHost),
                         "cudaMemcpy(trace D2H)")) return EXIT_FAILURE;
            for (std::uint32_t index = 0u; index < instances; ++index) {
                const DeviceTraceRecord *source = host_chunk.data() +
                    static_cast<std::size_t>(index) * trace_capacity;
                traces[index].insert(
                    traces[index].end(), source,
                    source + host_states[index].chunk_records_written);
            }
        }
        any_active = false;
        bool chunk_failure = false;
        for (const DeviceInstanceState &state : host_states) {
            any_active = any_active || state.active != 0u;
            chunk_failure = chunk_failure ||
                (state.active == 0u && state.terminal_status != SIM_OK);
        }
        if (chunk_failure && plan.device.failure_strategy ==
                static_cast<std::uint32_t>(DeviceFailureStrategy::StopOnFailure)) {
            break;
        }
        first_chunk = false;
    }
    const auto wall_stop = std::chrono::steady_clock::now();
    (void)cudaEventDestroy(kernel_stop);
    (void)cudaEventDestroy(kernel_start);
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
    execution.kernel_ms = kernel_ms;
    execution.wall_ms = wall_ms;
    const auto output_start = std::chrono::steady_clock::now();
    std::string output_error;
    const SimStatus output_status = write_production_outputs(
        plan, host_states, traces, execution, &output_error);
    const auto output_stop = std::chrono::steady_clock::now();
    execution.output_ms = std::chrono::duration<double, std::milli>(
        output_stop - output_start).count();
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
    return EXIT_SUCCESS;
}

}  // namespace gpu_campaign
