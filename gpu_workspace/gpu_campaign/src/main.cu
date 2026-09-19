#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "common/math_constants.h"
#include "gpu_types.cuh"
#include "host_environment_packer.hpp"
#include "host_scenario_mapper.hpp"
#include "production_runner.hpp"
#include "simulate_step.cuh"

namespace {

void cuda_check(cudaError_t status, const char *operation) {
    if (status != cudaSuccess) {
        std::fprintf(stderr, "gpu_campaign: %s: %s\n", operation,
                     cudaGetErrorString(status));
        std::exit(EXIT_FAILURE);
    }
}

/* WGS-84 scene basis for terrain dynamics (planA Phase 4 item 4):
   origin LLA(30 deg, 120 deg, 1000 m); local axes east and geodetic up so
   the campaign trajectory keeps its shape (initial speed along east,
   gravity along -up, target 1000 m east / (100 + 5*id) m up). Mirrors the
   same construction in the CPU golden chain. */
struct SceneBasis {
    Vec3 origin;
    Vec3 east;
    Vec3 up;
};

SceneBasis make_terrain_scene_basis() {
    const EarthModel earth = earth_model_wgs84();
    LlaCoord origin_lla{};
    origin_lla.lat_rad = 30.0 * SIM_DEG_TO_RAD;
    origin_lla.lon_rad = 120.0 * SIM_DEG_TO_RAD;
    origin_lla.height_m = 1000.0;
    EcefCoord origin_ecef{};
    SceneBasis basis{};
    if (geo_lla_to_ecef(&earth, &origin_lla, &origin_ecef) != SIM_OK) {
        std::fprintf(stderr, "gpu_campaign: scene origin conversion failed\n");
        std::exit(EXIT_FAILURE);
    }
    const double sin_lat = std::sin(origin_lla.lat_rad);
    const double cos_lat = std::cos(origin_lla.lat_rad);
    const double sin_lon = std::sin(origin_lla.lon_rad);
    const double cos_lon = std::cos(origin_lla.lon_rad);
    basis.origin = origin_ecef.position_m;
    basis.east = vec3_make(-sin_lon, cos_lon, 0.0);
    basis.up = vec3_make(cos_lat * cos_lon, cos_lat * sin_lon, sin_lat);
    return basis;
}

gpu_campaign::DeviceInstanceState make_initial_state(
    std::uint32_t instance_id, const SceneBasis *terrain_scene) {
    gpu_campaign::DeviceInstanceState state{};
    state.instance_id = instance_id;
    state.plant.q_bi = quat_identity();
    state.plant.mass = 2.0;
    state.plant.inertia_b = matrix3_identity();
    state.plant_input.earth_rotation_ecef_radps = vec3_make(0.0, 0.0, 0.0);
    if (terrain_scene != nullptr) {
        state.plant.pos_ecef = terrain_scene->origin;
        state.plant.vel_ecef = vec3_scale(
            terrain_scene->east, 10.0 + 0.5 * static_cast<double>(instance_id));
        state.plant_input.gravity_ecef_mps2 = vec3_scale(terrain_scene->up, -9.8);
        state.plant.lat_rad = 30.0 * SIM_DEG_TO_RAD;
        state.plant.lon_rad = 120.0 * SIM_DEG_TO_RAD;
        state.plant.height_m = 1000.0;
    } else {
        state.plant.vel_ecef = vec3_make(10.0 + 0.5 * static_cast<double>(instance_id),
                                         0.0, 0.0);
        state.plant_input.gravity_ecef_mps2 = vec3_make(0.0, -9.8, 0.0);
    }
    state.active = 1;
    state.exit_reason = static_cast<std::uint32_t>(
        gpu_campaign::DeviceExitReason::Active);
    return state;
}

std::uint32_t parse_u32(const char *text, const char *option,
                        std::uint32_t maximum, std::uint32_t minimum = 1u) {
    char *end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (end != text && *end == '\0' && value >= minimum && value <= maximum) {
        return static_cast<std::uint32_t>(value);
    }
    std::fprintf(stderr, "gpu_campaign: invalid %s value: %s\n", option, text);
    std::exit(EXIT_FAILURE);
}

struct CampaignOptions {
    std::uint32_t steps = 8;
    std::uint32_t instances = 1;
    std::uint32_t repeats = 1;
    std::uint32_t threads_per_instance = 1;
    std::uint32_t warmup = 1;
    bool packed = false;
    const char *aero_file = nullptr;
    const char *terrain_file = nullptr;
    const char *scenario_file = nullptr;
    const char *faults_file = nullptr;
    const char *dump_trace = nullptr;
};

CampaignOptions parse_options(int argc, char **argv) {
    CampaignOptions options;
    if ((argc - 1) % 2 != 0 || argc > 23) {
        std::fprintf(stderr, "usage: %s [--steps N] [--instances N] [--repeats N] [--threads-per-instance N] [--warmup N] [--mapping serial|packed] [--aero-file PATH] [--terrain-file PATH] [--scenario PATH] [--faults PATH] [--dump-trace PATH]\n", argv[0]);
        std::exit(EXIT_FAILURE);
    }
    for (int index = 1; index < argc; index += 2) {
        if (std::strcmp(argv[index], "--steps") == 0) {
            options.steps = parse_u32(argv[index + 1], "--steps", 1000000u);
        } else if (std::strcmp(argv[index], "--instances") == 0) {
            options.instances = parse_u32(argv[index + 1], "--instances", 65535u);
        } else if (std::strcmp(argv[index], "--repeats") == 0) {
            options.repeats = parse_u32(argv[index + 1], "--repeats", 1000u);
        } else if (std::strcmp(argv[index], "--threads-per-instance") == 0) {
            options.threads_per_instance = parse_u32(
                argv[index + 1], "--threads-per-instance", 1024u);
        } else if (std::strcmp(argv[index], "--warmup") == 0) {
            options.warmup = parse_u32(argv[index + 1], "--warmup", 100u, 0u);
        } else if (std::strcmp(argv[index], "--mapping") == 0) {
            if (std::strcmp(argv[index + 1], "serial") == 0) {
                options.packed = false;
            } else if (std::strcmp(argv[index + 1], "packed") == 0) {
                options.packed = true;
            } else {
                std::fprintf(stderr, "gpu_campaign: invalid --mapping value: %s (expected serial|packed)\n", argv[index + 1]);
                std::exit(EXIT_FAILURE);
            }
        } else if (std::strcmp(argv[index], "--aero-file") == 0) {
            options.aero_file = argv[index + 1];
        } else if (std::strcmp(argv[index], "--terrain-file") == 0) {
            options.terrain_file = argv[index + 1];
        } else if (std::strcmp(argv[index], "--scenario") == 0) {
            options.scenario_file = argv[index + 1];
        } else if (std::strcmp(argv[index], "--faults") == 0) {
            options.faults_file = argv[index + 1];
        } else if (std::strcmp(argv[index], "--dump-trace") == 0) {
            options.dump_trace = argv[index + 1];
        } else {
            std::fprintf(stderr, "usage: %s [--steps N] [--instances N] [--repeats N] [--threads-per-instance N] [--warmup N] [--mapping serial|packed] [--aero-file PATH] [--terrain-file PATH] [--scenario PATH] [--faults PATH] [--dump-trace PATH]\n", argv[0]);
            std::exit(EXIT_FAILURE);
        }
    }
    return options;
}

bool has_production_config(int argc, char **argv) {
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--config") == 0) return true;
    }
    return false;
}

gpu_campaign::ProductionOptions parse_production_options(
    int argc, char **argv) {
    gpu_campaign::ProductionOptions options{};
    options.trace_mode = gpu_campaign::ProductionTraceMode::Full;
    options.mapping = gpu_campaign::ProductionMapping::Auto;
    options.chunk_steps = 256u;
    options.lanes_per_block = 4u;
    if ((argc - 1) % 2 != 0) {
        std::fprintf(stderr, "usage: %s --config runtime.json "
                     "[--output-dir DIR] [--trace-mode full|summary] "
                     "[--chunk-steps N] [--mapping auto|serial|packed] "
                     "[--lanes-per-block 4|8|16|32]\n", argv[0]);
        std::exit(EXIT_FAILURE);
    }
    for (int index = 1; index < argc; index += 2) {
        const char *name = argv[index];
        const char *value = argv[index + 1];
        if (std::strcmp(name, "--config") == 0) {
            options.config_path = value;
        } else if (std::strcmp(name, "--output-dir") == 0) {
            options.output_dir = value;
        } else if (std::strcmp(name, "--trace-mode") == 0) {
            if (std::strcmp(value, "full") == 0) {
                options.trace_mode = gpu_campaign::ProductionTraceMode::Full;
            } else if (std::strcmp(value, "summary") == 0) {
                options.trace_mode = gpu_campaign::ProductionTraceMode::Summary;
            } else {
                std::fprintf(stderr, "gpu_campaign: invalid --trace-mode: %s\n",
                             value);
                std::exit(EXIT_FAILURE);
            }
        } else if (std::strcmp(name, "--chunk-steps") == 0) {
            options.chunk_steps = parse_u32(value, "--chunk-steps", 1000000u);
        } else if (std::strcmp(name, "--mapping") == 0) {
            if (std::strcmp(value, "auto") == 0) {
                options.mapping = gpu_campaign::ProductionMapping::Auto;
            } else if (std::strcmp(value, "serial") == 0) {
                options.mapping = gpu_campaign::ProductionMapping::Serial;
            } else if (std::strcmp(value, "packed") == 0) {
                options.mapping = gpu_campaign::ProductionMapping::Packed;
            } else {
                std::fprintf(stderr, "gpu_campaign: invalid --mapping: %s\n",
                             value);
                std::exit(EXIT_FAILURE);
            }
        } else if (std::strcmp(name, "--lanes-per-block") == 0) {
            options.lanes_per_block = parse_u32(
                value, "--lanes-per-block", 32u, 4u);
            if (options.lanes_per_block != 4u &&
                options.lanes_per_block != 8u &&
                options.lanes_per_block != 16u &&
                options.lanes_per_block != 32u) {
                std::fprintf(stderr, "gpu_campaign: --lanes-per-block must be "
                             "4, 8, 16, or 32\n");
                std::exit(EXIT_FAILURE);
            }
        } else {
            std::fprintf(stderr, "gpu_campaign: option %s cannot be combined "
                         "with --config\n", name);
            std::exit(EXIT_FAILURE);
        }
    }
    if (options.config_path == nullptr) {
        std::fprintf(stderr, "gpu_campaign: --config requires a path\n");
        std::exit(EXIT_FAILURE);
    }
    return options;
}

/* Retain the old single-instance command-line result format. */
void print_summary(const gpu_campaign::DeviceInstanceState &state,
                   const gpu_campaign::DeviceTraceRecord *trace) {
    const gpu_campaign::DeviceTraceRecord &final_trace =
        trace[state.completed_steps];
    std::printf("gpu_campaign: steps=%u final_time=%.12f final_x=%.12f "
                "final_y=%.12f exit_reason=%u sensor_flags=0x%08x "
                "command_mode=%u command_status=0x%08x environment_flags=0x%08x "
                "command_ax=%.12f\n",
                state.completed_steps, state.plant.time,
                state.plant.pos_ecef.x, state.plant.pos_ecef.y,
                state.exit_reason, final_trace.sensor_valid_flags,
                final_trace.command_mode, final_trace.command_status,
                final_trace.environment_flags,
                final_trace.accel_command_ecef.x);
}

void print_batch_summary(const gpu_campaign::DeviceInstanceState *states,
                         const gpu_campaign::DeviceTraceRecord *trace,
                         std::uint32_t instances,
                         std::uint32_t trace_capacity) {
    for (std::uint32_t index = 0; index < instances; ++index) {
        const gpu_campaign::DeviceTraceRecord *instance_trace =
            trace + static_cast<std::size_t>(index) * trace_capacity;
        const gpu_campaign::DeviceTraceRecord &final_trace =
            instance_trace[states[index].completed_steps];
        std::printf("gpu_campaign: instance=%u steps=%u final_time=%.12f "
                    "final_x=%.12f final_y=%.12f exit_reason=%u "
                    "sensor_flags=0x%08x command_mode=%u "
                    "command_status=0x%08x environment_flags=0x%08x "
                    "command_ax=%.12f\n",
                    index, states[index].completed_steps, states[index].plant.time,
                    states[index].plant.pos_ecef.x, states[index].plant.pos_ecef.y,
                    states[index].exit_reason, final_trace.sensor_valid_flags,
                    final_trace.command_mode, final_trace.command_status,
                    final_trace.environment_flags,
                    final_trace.accel_command_ecef.x);
    }
}

/* Dump instance 0 trace in the CPU golden baseline column layout:
   step time pos[3] vel[3] quat[4] force_b_n[3] moment_b_nm[3]
   accel_cmd[3] mach density aero_flags command_mode command_status. */
bool dump_instance0_trace(const gpu_campaign::DeviceTraceRecord *trace,
                          std::uint32_t trace_capacity, const char *path) {
    std::FILE *out = std::fopen(path, "w");
    if (out == nullptr) return false;
    for (std::uint32_t step = 0; step < trace_capacity; ++step) {
        const gpu_campaign::DeviceTraceRecord &record = trace[step];
        const double doubles[] = {
            record.time,
            record.position_ecef.x, record.position_ecef.y,
            record.position_ecef.z,
            record.velocity_ecef.x, record.velocity_ecef.y,
            record.velocity_ecef.z,
            record.attitude.w, record.attitude.x, record.attitude.y,
            record.attitude.z,
            record.force_b_n.x, record.force_b_n.y, record.force_b_n.z,
            record.moment_b_nm.x, record.moment_b_nm.y, record.moment_b_nm.z,
            record.accel_command_ecef.x, record.accel_command_ecef.y,
            record.accel_command_ecef.z,
            record.mach, record.density,
        };
        std::fprintf(out, "%u", record.step);
        for (const double value : doubles) {
            std::fprintf(out, " %.17g", value);
        }
        std::fprintf(out, " %u %u %u\n", record.aero_flags,
                     record.command_mode, record.command_status);
    }
    std::fclose(out);
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    if (has_production_config(argc, argv)) {
        return gpu_campaign::run_production_campaign(
            parse_production_options(argc, argv));
    }
    const CampaignOptions options = parse_options(argc, argv);
    const std::uint32_t steps = options.steps;
    const std::uint32_t instances = options.instances;
    const std::uint32_t repeats = options.repeats;
    std::uint32_t threads_per_instance = options.threads_per_instance;
    const std::uint32_t warmup = options.warmup;
    /* Packed mapping: one serial instance per lane; the block size is the
       lane count. In serial mapping threads_per_instance keeps its legacy
       meaning of threads reserved per instance (only lane 0 executes). */
    const bool packed = options.packed;
    if (packed && threads_per_instance < 2u) {
        threads_per_instance = 32u;  /* default: one warp per block */
    }
    int device_count = 0;
    cuda_check(cudaGetDeviceCount(&device_count), "cudaGetDeviceCount");
    if (device_count == 0) {
        std::fprintf(stderr, "gpu_campaign: no CUDA device available\n");
        return EXIT_FAILURE;
    }

    gpu_campaign::DeviceConfig config{};
    config.dt = 0.01;
    config.chunk_steps = steps;
    config.trace_capacity = steps + 1;
    config.instance_count = instances;
    config.threads_per_instance = threads_per_instance;
    config.packed_lanes = packed ? threads_per_instance : 0u;
    config.aero_force.enabled = 0u;  /* legacy mapping until --scenario */
    config.atmosphere = atmosphere_model_isa();
    config.wind_velocity_ecef = vec3_make(0.0, 0.0, 0.0);
    gpu_campaign::DeviceAeroTable host_aero{};
    gpu_campaign::DeviceTerrainGrid host_terrain{};
    const bool have_aero = options.aero_file != nullptr;
    const bool have_terrain = options.terrain_file != nullptr;
    if (options.scenario_file != nullptr) {
        gpu_campaign::DeviceScenarioAero scenario{};
        if (gpu_campaign::load_scenario_aero(options.scenario_file,
                                              &scenario) != SIM_OK) {
            std::fprintf(stderr, "gpu_campaign: failed to load scenario: %s\n",
                         options.scenario_file);
            return EXIT_FAILURE;
        }
        /* planA Phase 4 items 1/2: sensor noise / delay / dropout from the
           same scenario file. */
        if (gpu_campaign::load_scenario_sensors(options.scenario_file, config.dt,
                                                 &config.sensors) != SIM_OK) {
            std::fprintf(stderr, "gpu_campaign: failed to load scenario sensors: %s\n",
                         options.scenario_file);
            return EXIT_FAILURE;
        }
        if (scenario.force.enabled != 0u && !have_aero) {
            std::fprintf(stderr, "gpu_campaign: scenario enables aerodynamics "
                                 "but no --aero-file was given\n");
            return EXIT_FAILURE;
        }
        config.aero_force = scenario.force;
        config.wind_velocity_ecef = scenario.wind_velocity_ecef_mps;
        /* The golden baseline uses ISA with the scenario maximum height. */
        config.atmosphere = atmosphere_model_isa();
    }
    if (have_aero && gpu_campaign::pack_aero_database_file(
                         options.aero_file, &host_aero) != SIM_OK) {
        std::fprintf(stderr, "gpu_campaign: failed to pack aero file: %s\n",
                     options.aero_file);
        return EXIT_FAILURE;
    }
    if (have_terrain && gpu_campaign::pack_terrain_tile_file(
                            options.terrain_file, &host_terrain,
                            gpu_campaign::DeviceExtrapolationPolicy::ClampAndWarn) != SIM_OK) {
        std::fprintf(stderr, "gpu_campaign: failed to pack terrain file: %s\n",
                     options.terrain_file);
        return EXIT_FAILURE;
    }
    /* Terrain dynamics (planA Phase 4 item 4): WGS-84 scene + per-step
       geodetic refresh + surface-collision termination. */
    SceneBasis terrain_scene{};
    if (have_terrain) {
        terrain_scene = make_terrain_scene_basis();
        config.earth = earth_model_wgs84();
        config.terrain_grid_enabled = 1u;
        config.scene_origin_ecef = terrain_scene.origin;
        config.scene_east = terrain_scene.east;
        config.scene_up = terrain_scene.up;
    }
    /* Fault scripts use the original parser. Complete actuator and lockstep
       communication semantics are exercised by the production --config path;
       this legacy probe keeps its historical sensor-fault behavior. */
    ::FaultInjection host_faults{};
    if (options.faults_file != nullptr &&
        gpu_campaign::load_fault_script(options.faults_file, &host_faults) != SIM_OK) {
        std::fprintf(stderr, "gpu_campaign: failed to load faults: %s\n",
                     options.faults_file);
        return EXIT_FAILURE;
    }
    gpu_campaign::DeviceInstanceState *host_states =
        static_cast<gpu_campaign::DeviceInstanceState *>(
            std::calloc(instances, sizeof(gpu_campaign::DeviceInstanceState)));
    gpu_campaign::DeviceInstanceState *host_initial_states =
        static_cast<gpu_campaign::DeviceInstanceState *>(
            std::calloc(instances, sizeof(gpu_campaign::DeviceInstanceState)));
    gpu_campaign::DeviceTraceRecord *host_trace =
        static_cast<gpu_campaign::DeviceTraceRecord *>(
            std::calloc(static_cast<std::size_t>(instances) * config.trace_capacity,
                        sizeof(gpu_campaign::DeviceTraceRecord)));
    if (host_states == nullptr || host_initial_states == nullptr ||
        host_trace == nullptr) {
        std::fprintf(stderr, "gpu_campaign: host allocation failed\n");
        std::free(host_states);
        std::free(host_initial_states);
        std::free(host_trace);
        return EXIT_FAILURE;
    }
    for (std::uint32_t index = 0; index < instances; ++index) {
        host_initial_states[index] = make_initial_state(
            index, have_terrain ? &terrain_scene : nullptr);
    }

    gpu_campaign::DeviceInstanceState *device_states = nullptr;
    gpu_campaign::DeviceTraceRecord *device_trace = nullptr;
    gpu_campaign::DeviceAeroTable *device_aero = nullptr;
    gpu_campaign::DeviceTerrainGrid *device_terrain = nullptr;
    ::FaultInjection *device_faults = nullptr;
    cuda_check(cudaMalloc(reinterpret_cast<void **>(&device_states),
                          instances * sizeof(*host_states)), "cudaMalloc(states)");
    cuda_check(cudaMalloc(reinterpret_cast<void **>(&device_trace),
                          static_cast<std::size_t>(instances) * config.trace_capacity *
                              sizeof(*host_trace)),
                "cudaMalloc(trace)");
    if (have_aero) {
        cuda_check(cudaMalloc(reinterpret_cast<void **>(&device_aero), sizeof(host_aero)),
                   "cudaMalloc(aero)");
        cuda_check(cudaMemcpy(device_aero, &host_aero, sizeof(host_aero),
                              cudaMemcpyHostToDevice),
                   "cudaMemcpy(aero H2D)");
    }
    if (have_terrain) {
        cuda_check(cudaMalloc(reinterpret_cast<void **>(&device_terrain), sizeof(host_terrain)),
                   "cudaMalloc(terrain)");
        cuda_check(cudaMemcpy(device_terrain, &host_terrain, sizeof(host_terrain),
                              cudaMemcpyHostToDevice),
                   "cudaMemcpy(terrain H2D)");
    }
    if (options.faults_file != nullptr) {
        cuda_check(cudaMalloc(reinterpret_cast<void **>(&device_faults),
                              sizeof(host_faults)), "cudaMalloc(faults)");
        cuda_check(cudaMemcpy(device_faults, &host_faults, sizeof(host_faults),
                              cudaMemcpyHostToDevice),
                   "cudaMemcpy(faults H2D)");
    }
    cudaEvent_t kernel_start = nullptr;
    cudaEvent_t kernel_stop = nullptr;
    cuda_check(cudaEventCreate(&kernel_start), "cudaEventCreate(start)");
    cuda_check(cudaEventCreate(&kernel_stop), "cudaEventCreate(stop)");
    /* Launch geometry per mapping:
       - serial: one block per instance, blockDim = threads_per_instance.
       - packed: blockDim = lanes per block, block count = ceil(N / lanes). */
    const std::uint32_t block_size = threads_per_instance;
    const std::uint32_t block_count = packed
        ? (instances + block_size - 1u) / block_size
        : instances;
    float kernel_sum_ms = 0.0f;
    float kernel_min_ms = 1.0e30f;
    float kernel_max_ms = 0.0f;
    float kernel_samples[1000];
    for (std::uint32_t repeat = 0; repeat < warmup + repeats; ++repeat) {
        cuda_check(cudaMemcpy(device_states, host_initial_states,
                              instances * sizeof(*host_initial_states),
                              cudaMemcpyHostToDevice),
                   "cudaMemcpy(states reset H2D)");
        cuda_check(cudaMemset(device_trace, 0,
                              static_cast<std::size_t>(instances) * config.trace_capacity *
                                  sizeof(*host_trace)),
                   "cudaMemset(trace reset)");
        if (repeat < warmup) {
            gpu_campaign::simulate_chunk<<<block_count, block_size>>>(
                device_states, device_trace, config, device_aero, device_terrain,
                device_faults);
            cuda_check(cudaGetLastError(), "simulate_chunk warmup launch");
            cuda_check(cudaDeviceSynchronize(), "simulate_chunk warmup synchronize");
            continue;
        }
        cuda_check(cudaEventRecord(kernel_start), "cudaEventRecord(start)");
        gpu_campaign::simulate_chunk<<<block_count, block_size>>>(
            device_states, device_trace, config, device_aero, device_terrain,
            device_faults);
        cuda_check(cudaGetLastError(), "simulate_chunk launch");
        cuda_check(cudaEventRecord(kernel_stop), "cudaEventRecord(stop)");
        cuda_check(cudaEventSynchronize(kernel_stop), "cudaEventSynchronize(stop)");
        float kernel_ms = 0.0f;
        cuda_check(cudaEventElapsedTime(&kernel_ms, kernel_start, kernel_stop),
                   "cudaEventElapsedTime(kernel)");
        kernel_sum_ms += kernel_ms;
        kernel_samples[repeat - warmup] = kernel_ms;
        kernel_min_ms = kernel_ms < kernel_min_ms ? kernel_ms : kernel_min_ms;
        kernel_max_ms = kernel_ms > kernel_max_ms ? kernel_ms : kernel_max_ms;
    }
    std::sort(kernel_samples, kernel_samples + repeats);
    const float kernel_median_ms =
        repeats % 2 == 0
            ? 0.5f * (kernel_samples[repeats / 2 - 1] + kernel_samples[repeats / 2])
            : kernel_samples[repeats / 2];
    cuda_check(cudaDeviceSynchronize(), "simulate_chunk synchronize");
    cuda_check(cudaMemcpy(host_states, device_states,
                          instances * sizeof(*host_states),
                          cudaMemcpyDeviceToHost),
                "cudaMemcpy(states D2H)");
    cuda_check(cudaMemcpy(host_trace, device_trace,
                          static_cast<std::size_t>(instances) * config.trace_capacity *
                              sizeof(*host_trace),
                          cudaMemcpyDeviceToHost),
                          "cudaMemcpy(trace D2H)");

    if (instances == 1) {
        print_summary(host_states[0], host_trace);
    } else {
        print_batch_summary(host_states, host_trace, instances, config.trace_capacity);
    }
    if (options.dump_trace != nullptr &&
        !dump_instance0_trace(host_trace, config.trace_capacity,
                              options.dump_trace)) {
        std::fprintf(stderr, "gpu_campaign: failed to write trace dump: %s\n",
                     options.dump_trace);
        return EXIT_FAILURE;
    }
    const double kernel_seconds = static_cast<double>(kernel_sum_ms / repeats) / 1000.0;
    const double instance_steps_per_second =
        kernel_seconds > 0.0
            ? (static_cast<double>(instances) * static_cast<double>(steps)) /
                  kernel_seconds
            : 0.0;
    std::printf("gpu_campaign: instances=%u mapping=%s block_count=%u "
                "block_size=%u warmup=%u "
                "repeats=%u kernel_ms_avg=%.6f kernel_ms_median=%.6f "
                "kernel_ms_min=%.6f kernel_ms_max=%.6f "
                "instance_steps_per_sec=%.3f\n",
                instances, packed ? "packed" : "serial", block_count,
                block_size, warmup, repeats,
                kernel_sum_ms / repeats, kernel_median_ms,
                kernel_min_ms, kernel_max_ms, instance_steps_per_second);

    cuda_check(cudaEventDestroy(kernel_stop), "cudaEventDestroy(stop)");
    cuda_check(cudaEventDestroy(kernel_start), "cudaEventDestroy(start)");
    cuda_check(cudaFree(device_trace), "cudaFree(trace)");
    cuda_check(cudaFree(device_states), "cudaFree(states)");
    if (device_aero != nullptr) cuda_check(cudaFree(device_aero), "cudaFree(aero)");
    if (device_terrain != nullptr) cuda_check(cudaFree(device_terrain), "cudaFree(terrain)");
    if (device_faults != nullptr) cuda_check(cudaFree(device_faults), "cudaFree(faults)");
    std::free(host_states);
    std::free(host_initial_states);
    std::free(host_trace);
    return EXIT_SUCCESS;
}
