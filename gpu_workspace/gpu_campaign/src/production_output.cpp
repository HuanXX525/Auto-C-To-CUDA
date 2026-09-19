#define _POSIX_C_SOURCE 200809L

#include "production_output.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#include "common/math_constants.h"

extern "C" {
#include "common/packet.h"
}

namespace gpu_campaign {
namespace {

SimStatus fail(const std::string &message, std::string *error) {
    if (error != nullptr) *error = message;
    return SIM_ERR_IO;
}

bool directory_exists(const std::string &path) {
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

SimStatus make_directories(const std::string &path, std::string *error) {
    if (path.empty()) return fail("empty output directory", error);
    std::string current;
    std::size_t cursor = 0u;
    if (path[0] == '/') {
        current = "/";
        cursor = 1u;
    }
    while (cursor <= path.size()) {
        const std::size_t slash = path.find('/', cursor);
        const std::string part = path.substr(
            cursor, slash == std::string::npos ? std::string::npos :
                                                 slash - cursor);
        if (!part.empty()) {
            if (!current.empty() && current.back() != '/') current += '/';
            current += part;
            if (!directory_exists(current) &&
                mkdir(current.c_str(), 0777) != 0 && errno != EEXIST) {
                return fail("cannot create output directory: " + current,
                            error);
            }
        }
        if (slash == std::string::npos) break;
        cursor = slash + 1u;
    }
    return SIM_OK;
}

void write_json_string(std::FILE *file, const std::string &value) {
    std::fputc('"', file);
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') {
            std::fputc('\\', file);
            std::fputc(c, file);
        } else if (c >= 0x20u) {
            std::fputc(c, file);
        } else {
            std::fprintf(file, "\\u%04x", static_cast<unsigned int>(c));
        }
    }
    std::fputc('"', file);
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
    }
    return "unknown";
}

const char *terrain_policy_name(std::uint32_t policy) {
    switch (static_cast<DeviceTerrainMissingPolicy>(policy)) {
    case DeviceTerrainMissingPolicy::FlatFill: return "FLAT_FILL";
    case DeviceTerrainMissingPolicy::Error: return "ERROR";
    case DeviceTerrainMissingPolicy::Nearest: return "NEAREST";
    }
    return "UNKNOWN";
}

double quaternion_norm_error(Quat q) {
    return std::fabs(std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y +
                               q.z * q.z) - 1.0);
}

double dcm_orthogonality_error(Quat q) {
    Matrix3 dcm{};
    if (quat_to_dcm(q, &dcm) != SIM_OK) return HUGE_VAL;
    const Matrix3 product = matrix3_multiply(dcm, matrix3_transpose(dcm));
    double sum = 0.0;
    for (std::uint32_t row = 0u; row < 3u; ++row) {
        for (std::uint32_t column = 0u; column < 3u; ++column) {
            const double expected = row == column ? 1.0 : 0.0;
            const double difference = product.m[row][column] - expected;
            sum += difference * difference;
        }
    }
    return std::sqrt(sum);
}

double minimum_inertia(const DeviceTraceRecord &record,
                       const DeviceInstanceConfig &config) {
    if (config.initial_mass_kg <= 0.0) return 0.0;
    const double scale = record.mass_kg / config.initial_mass_kg;
    double value = config.initial_inertia_b.m[0][0] * scale;
    value = std::min(value, config.initial_inertia_b.m[1][1] * scale);
    value = std::min(value, config.initial_inertia_b.m[2][2] * scale);
    return value;
}

std::uint32_t degradation_flags(const DeviceTraceRecord &record,
                                double quat_error, double dcm_error,
                                double inertia_min) {
    std::uint32_t flags = 0u;
    if (record.aero_flags != 0u) flags |= 1u << 0u;
    if ((record.environment_flags & (1u << 2u)) != 0u) flags |= 1u << 1u;
    if (!std::isfinite(record.mass_kg) || record.mass_kg <= 0.0) flags |= 1u << 2u;
    if (!std::isfinite(inertia_min) || inertia_min <= 0.0) flags |= 1u << 3u;
    if (!std::isfinite(quat_error) || quat_error > 1.0e-6) flags |= 1u << 4u;
    if (!std::isfinite(dcm_error) || dcm_error > 1.0e-6) flags |= 1u << 5u;
    return flags;
}

SimStatus write_manifest(const std::string &directory,
                         const HostCampaignPlan &plan,
                         const HostInstancePlan &instance,
                         const HostTerrainAsset *terrain,
                         std::string *error) {
    const std::string path = directory + "/run_manifest.json";
    std::FILE *file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) return fail("cannot write " + path, error);
    const DeviceInstanceConfig &cfg = instance.device;
    std::fprintf(file, "{\n  \"schema_version\": 1,\n  \"instance_id\": %u,\n",
                 cfg.instance_id);
    std::fprintf(file, "  \"software_version\": \"0.1.0\",\n");
    std::fprintf(file, "  \"protocol_version\": \"1.0\",\n");
    std::fprintf(file, "  \"scenario_path\": ");
    write_json_string(file, instance.scenario_display_path);
    std::fprintf(file, ",\n  \"runtime_path\": ");
    write_json_string(file, plan.runtime_display_path);
    std::fprintf(file, ",\n  \"faults_path\": ");
    write_json_string(file, instance.faults_display_path);
    const std::size_t terrain_path_count = terrain == nullptr
        ? 0u : terrain->tile_paths.size();
    std::fprintf(file,
        ",\n  \"dt_s\": %.17g,\n  \"max_time_s\": %.17g,\n"
        "  \"environment_port\": %u,\n  \"flight_control_port\": %u,\n"
        "  \"random_seed\": %llu,\n  \"initial_mass_kg\": %.17g,\n"
        "  \"initial_propellant_mass_kg\": %.17g,\n"
        "  \"gravity_enabled\": %s,\n  \"atmosphere_enabled\": %s,\n"
        "  \"aerodynamics_enabled\": %s,\n  \"aero_table_enabled\": %s,\n"
        "  \"aero_surrogate_enabled\": %s,\n  \"propulsion_enabled\": %s,\n"
        "  \"earth_rotation_enabled\": %s,\n  \"terrain_enabled\": %s,\n"
        "  \"terrain_los_occlusion_enabled\": %s,\n"
        "  \"terrain_missing_policy\": \"%s\",\n"
        "  \"terrain_flat_fill_height_m\": %.17g,\n"
        "  \"terrain_cache_tile_count\": %u,\n"
        "  \"terrain_tile_index_path_enabled\": %s,\n",
        cfg.dt, cfg.max_time,
        plan.environment_base_port + 2u * cfg.instance_id,
        plan.flight_control_base_port + 2u * cfg.instance_id,
        static_cast<unsigned long long>(cfg.random_seed), cfg.initial_mass_kg,
        cfg.initial_propellant_mass_kg,
        cfg.gravity.enabled != 0 ? "true" : "false",
        cfg.atmosphere.enabled != 0 ? "true" : "false",
        cfg.aero_force.enabled != 0u ? "true" : "false",
        cfg.aero_asset_index != kInvalidAssetIndex ? "true" : "false",
        cfg.aero_surrogate_asset_index != kInvalidAssetIndex ? "true" : "false",
        cfg.propulsion.enabled != 0 ? "true" : "false",
        cfg.enable_earth_rotation != 0u ? "true" : "false",
        cfg.terrain_enabled != 0u ? "true" : "false",
        cfg.los_occlusion_enabled != 0u ? "true" : "false",
        terrain_policy_name(cfg.terrain_missing_policy),
        cfg.terrain_flat_fill_height_m, cfg.terrain_cache_tile_count,
        instance.map_tile_index_path.empty() ? "false" : "true");
    if (instance.map_tile_index_path.empty()) {
        std::fputs("  \"terrain_tile_index_path\": null,\n", file);
    } else {
        std::fputs("  \"terrain_tile_index_path\": ", file);
        write_json_string(file, instance.map_tile_index_path);
        std::fputs(",\n", file);
    }
    std::fprintf(file, "  \"terrain_tile_path_count\": %u,\n"
                       "  \"terrain_tile_paths\": [",
                 static_cast<unsigned int>(terrain_path_count));
    for (std::size_t index = 0u; index < terrain_path_count; ++index) {
        if (index != 0u) std::fputs(", ", file);
        write_json_string(file, terrain->tile_paths[index]);
    }
    std::fprintf(file, "],\n  \"binary_logs\": %s,\n"
                       "  \"event_log\": %s\n}\n",
                 plan.binary_logs ? "true" : "false",
                 plan.event_log ? "true" : "false");
    std::fclose(file);
    return SIM_OK;
}

SimStatus write_trajectory(
    const std::string &directory,
    const std::vector<DeviceTraceRecord> &trace,
    std::string *error) {
    const std::string path = directory + "/trajectory.csv";
    std::FILE *file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) return fail("cannot write " + path, error);
    std::fprintf(file,
        "time_s,missile_x_ecef_m,missile_y_ecef_m,missile_z_ecef_m,"
        "missile_vx_ecef_mps,missile_vy_ecef_mps,missile_vz_ecef_mps,"
        "missile_ax_ecef_mps2,missile_ay_ecef_mps2,missile_az_ecef_mps2,"
        "missile_lat_deg,missile_lon_deg,missile_height_m,missile_agl_m,"
        "missile_mass_kg,missile_propellant_mass_kg,force_b_x_n,force_b_y_n,"
        "force_b_z_n,moment_b_x_nm,moment_b_y_nm,moment_b_z_nm,"
        "target_x_ecef_m,target_y_ecef_m,target_z_ecef_m,target_lat_deg,"
        "target_lon_deg,target_height_m,range_m,closing_velocity_mps\n");
    for (const DeviceTraceRecord &row : trace) {
        std::fprintf(file,
            "%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,"
            "%.9f,%.9f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,"
            "%.6f,%.6f,%.6f,%.6f,%.6f,%.9f,%.9f,%.6f,%.6f,%.6f\n",
            row.time, row.position_ecef.x, row.position_ecef.y,
            row.position_ecef.z, row.velocity_ecef.x, row.velocity_ecef.y,
            row.velocity_ecef.z, row.acceleration_ecef.x,
            row.acceleration_ecef.y, row.acceleration_ecef.z,
            row.latitude_rad * SIM_RAD_TO_DEG,
            row.longitude_rad * SIM_RAD_TO_DEG, row.height_m,
            row.height_agl_m, row.mass_kg, row.propellant_mass_kg,
            row.force_b_n.x, row.force_b_n.y, row.force_b_n.z,
            row.moment_b_nm.x, row.moment_b_nm.y, row.moment_b_nm.z,
            row.target_position_ecef.x, row.target_position_ecef.y,
            row.target_position_ecef.z,
            row.target_latitude_rad * SIM_RAD_TO_DEG,
            row.target_longitude_rad * SIM_RAD_TO_DEG, row.target_height_m,
            row.truth_range_m, row.truth_closing_velocity_mps);
    }
    std::fclose(file);
    return SIM_OK;
}

SimStatus write_diagnostics(
    const std::string &directory, const DeviceInstanceConfig &config,
    const std::vector<DeviceTraceRecord> &trace, std::string *error) {
    const std::string path = directory + "/trajectory_diagnostics.csv";
    std::FILE *file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) return fail("cannot write " + path, error);
    std::fprintf(file,
        "time_s,quat_norm_error,dcm_orthogonality_error,mass_kg,"
        "propellant_mass_kg,inertia_min_diag_kgm2,aero_model_flags,"
        "model_degradation_flags,aero_uncertainty_scale,integrator_type,dt_s,"
        "force_norm_n,moment_norm_nm\n");
    for (const DeviceTraceRecord &row : trace) {
        const double quat_error = quaternion_norm_error(row.attitude);
        const double dcm_error = dcm_orthogonality_error(row.attitude);
        const double inertia_min = minimum_inertia(row, config);
        std::fprintf(file,
            "%.6f,%.12e,%.12e,%.9f,%.9f,%.9f,%u,%u,%.9f,%u,%.9f,%.9f,%.9f\n",
            row.time, quat_error, dcm_error, row.mass_kg,
            row.propellant_mass_kg, inertia_min, row.aero_flags,
            degradation_flags(row, quat_error, dcm_error, inertia_min), 0.0,
            config.integrator, config.dt, vec3_norm(row.force_b_n),
            vec3_norm(row.moment_b_nm));
    }
    std::fclose(file);
    return SIM_OK;
}

SimStatus write_binary_logs(
    const std::string &directory, std::uint32_t instance_id,
    const std::vector<DeviceTraceRecord> &trace, std::string *error) {
    const std::string sensor_path = directory + "/sensor_log.bin";
    const std::string command_path = directory + "/command_log.bin";
    std::FILE *sensor_file = std::fopen(sensor_path.c_str(), "wb");
    std::FILE *command_file = std::fopen(command_path.c_str(), "wb");
    if (sensor_file == nullptr || command_file == nullptr) {
        if (sensor_file != nullptr) std::fclose(sensor_file);
        if (command_file != nullptr) std::fclose(command_file);
        return fail("cannot write binary protocol logs", error);
    }
    unsigned char sensor_packet[SIM_SENSOR_PACKET_WIRE_SIZE];
    unsigned char command_packet[SIM_CONTROL_PACKET_WIRE_SIZE];
    for (const DeviceTraceRecord &row : trace) {
        if (row.has_io_frame == 0u) continue;
        std::size_t size = 0u;
        SimStatus status = packet_encode_sensor_frame(
            instance_id, &row.sensor_frame, sensor_packet,
            sizeof(sensor_packet), &size);
        if (status == SIM_OK &&
            std::fwrite(sensor_packet, 1u, size, sensor_file) != size) {
            status = SIM_ERR_IO;
        }
        if (status == SIM_OK) {
            status = packet_encode_control_command(
                instance_id, &row.control_command, command_packet,
                sizeof(command_packet), &size);
        }
        if (status == SIM_OK &&
            std::fwrite(command_packet, 1u, size, command_file) != size) {
            status = SIM_ERR_IO;
        }
        if (status != SIM_OK) {
            std::fclose(sensor_file);
            std::fclose(command_file);
            return fail("cannot encode binary protocol logs", error);
        }
    }
    std::fclose(sensor_file);
    std::fclose(command_file);
    return SIM_OK;
}

SimStatus write_events(
    const std::string &directory, const HostCampaignPlan &plan,
    const HostInstancePlan &instance, const DeviceInstanceState &state,
    const std::vector<DeviceTraceRecord> &trace, std::string *error) {
    const std::string path = directory + "/event_log.txt";
    std::FILE *file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) return fail("cannot write " + path, error);
    std::fprintf(file, "0.000000 level=INFO event=SIMULATION_START detail=lockstep\n");
    const HostFaultAsset *asset = instance.device.fault_asset_index <
            plan.fault_assets.size()
        ? &plan.fault_assets[instance.device.fault_asset_index] : nullptr;
    for (const DeviceTraceRecord &row : trace) {
        if (asset == nullptr || row.has_io_frame == 0u) continue;
        const double event_time = row.time - instance.device.dt;
        for (std::uint32_t index = 0u;
             index < asset->definition.fault_count && index < 32u; ++index) {
            const std::uint32_t bit = 1u << index;
            const FaultDefinition &fault = asset->definition.faults[index];
            if ((row.fault_started_mask & bit) != 0u) {
                std::fprintf(file,
                    "%.6f level=INFO event=FAULT_START detail=id=%s target=%s type=%s\n",
                    event_time, fault.id, fault.target_text, fault.type_text);
            }
            if ((row.fault_ended_mask & bit) != 0u) {
                std::fprintf(file,
                    "%.6f level=INFO event=FAULT_END detail=id=%s target=%s type=%s\n",
                    event_time, fault.id, fault.target_text, fault.type_text);
            }
        }
    }
    if (state.exit_reason == static_cast<std::uint32_t>(DeviceExitReason::Hit)) {
        std::fprintf(file, "%.6f level=INFO event=HIT detail=truth_range_threshold\n",
                     state.plant.time);
    } else if (state.exit_reason == static_cast<std::uint32_t>(
                   DeviceExitReason::SurfaceCollision)) {
        std::fprintf(file,
            "%.6f level=INFO event=SURFACE_COLLISION detail=agl_non_positive\n",
            state.plant.time);
    }
    std::fprintf(file, "%.6f level=INFO event=SIMULATION_STOP detail=%s\n",
                 state.plant.time, exit_reason_name(state.exit_reason));
    std::fclose(file);
    return SIM_OK;
}

SimStatus write_summary(const std::string &directory,
                        const DeviceInstanceConfig &config,
                        const HostTerrainAsset *terrain,
                        const DeviceInstanceState &state,
                        std::string *error) {
    const std::string path = directory + "/summary.json";
    std::FILE *file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) return fail("cannot write " + path, error);
    const DeviceFaultStats &faults = state.fault_stats;
    const DeviceDiagnosticStats &diagnostics = state.diagnostic_stats;
    std::fprintf(file,
        "{\n  \"hit_flag\": %s,\n  \"miss_distance\": %.6f,\n"
        "  \"time_of_closest_approach\": %.6f,\n"
        "  \"simulation_steps\": %u,\n  \"exit_reason\": \"%s\",\n"
        "  \"fault_configured_count\": %u,\n  \"fault_start_count\": %u,\n"
        "  \"fault_end_count\": %u,\n  \"fault_active_step_count\": %u,\n"
        "  \"fault_sensor_affected_step_count\": %u,\n"
        "  \"fault_actuator_affected_step_count\": %u,\n"
        "  \"fault_max_concurrent_active\": %u,\n"
        "  \"terrain_cache_path_count\": %u,\n"
        "  \"terrain_cache_capacity\": %u,\n"
        "  \"terrain_cache_loaded_count\": %u,\n"
        "  \"terrain_cache_load_count\": %u,\n"
        "  \"terrain_cache_eviction_count\": 0,\n"
        "  \"diagnostic_sample_count\": %u,\n"
        "  \"max_quat_norm_error\": %.12e,\n"
        "  \"max_dcm_orthogonality_error\": %.12e,\n"
        "  \"min_mass_kg\": %.9f,\n"
        "  \"min_inertia_diag_kgm2\": %.9f,\n"
        "  \"aero_model_flags_or\": %u,\n"
        "  \"model_degradation_flags_or\": %u,\n"
        "  \"aero_extrapolated_sample_count\": %u\n}\n",
        state.hit != 0u ? "true" : "false", state.min_range_m,
        state.time_of_closest_s, state.global_step,
        exit_reason_name(state.exit_reason), faults.configured_fault_count,
        faults.fault_start_count, faults.fault_end_count,
        faults.active_step_count, faults.sensor_affected_step_count,
        faults.actuator_affected_step_count, faults.max_concurrent_active,
        terrain == nullptr ? 0u : static_cast<unsigned int>(terrain->tile_paths.size()),
        config.terrain_cache_tile_count,
        terrain == nullptr ? 0u : static_cast<unsigned int>(terrain->tile_paths.size()),
        terrain == nullptr ? 0u : static_cast<unsigned int>(terrain->tile_paths.size()),
        diagnostics.sample_count, diagnostics.max_quat_norm_error,
        diagnostics.max_dcm_orthogonality_error, diagnostics.min_mass_kg,
        diagnostics.min_inertia_diag_kgm2,
        diagnostics.aero_model_flags_or,
        diagnostics.model_degradation_flags_or,
        diagnostics.aero_extrapolated_sample_count);
    std::fclose(file);
    return SIM_OK;
}

}  // namespace

SimStatus write_production_outputs(
    const HostCampaignPlan &plan,
    const std::vector<DeviceInstanceState> &states,
    const std::vector<std::vector<DeviceTraceRecord>> &traces,
    const GpuExecutionInfo &execution,
    std::string *error) {
    if (states.size() != plan.instances.size() ||
        (!traces.empty() && traces.size() != states.size())) {
        return fail("production output shape mismatch", error);
    }
    SimStatus status = make_directories(plan.output_dir, error);
    if (status != SIM_OK) return status;
    for (std::size_t index = 0u; index < states.size(); ++index) {
        char name[64];
        const HostInstancePlan &instance = plan.instances[index];
        const HostTerrainAsset *terrain = nullptr;
        if (instance.device.terrain_asset_index != kInvalidAssetIndex &&
            instance.device.terrain_asset_index < plan.terrain_assets.size()) {
            terrain = &plan.terrain_assets[instance.device.terrain_asset_index];
        }
        std::snprintf(name, sizeof(name), "instance_%04u",
                      instance.device.instance_id);
        const std::string directory = plan.output_dir + "/" + name;
        status = make_directories(directory, error);
        if (status == SIM_OK) status = write_manifest(
            directory, plan, instance, terrain, error);
        if (status == SIM_OK && !traces.empty()) {
            status = write_trajectory(directory, traces[index], error);
        }
        if (status == SIM_OK && !traces.empty()) {
            status = write_diagnostics(directory, instance.device,
                                       traces[index], error);
        }
        if (status == SIM_OK && plan.binary_logs && !traces.empty()) {
            status = write_binary_logs(
                directory, instance.device.instance_id,
                traces[index], error);
        }
        if (status == SIM_OK && plan.event_log) {
            status = write_events(directory, plan, instance,
                                  states[index],
                                  traces.empty() ?
                                      std::vector<DeviceTraceRecord>{} :
                                      traces[index], error);
        }
        if (status == SIM_OK) status = write_summary(
            directory, instance.device, terrain, states[index], error);
        if (status != SIM_OK) return status;
    }

    const std::string campaign_path = plan.output_dir + "/campaign_summary.json";
    std::FILE *campaign = std::fopen(campaign_path.c_str(), "wb");
    if (campaign == nullptr) return fail("cannot write " + campaign_path, error);
    std::uint32_t failed = 0u;
    std::uint32_t hits = 0u;
    for (const DeviceInstanceState &state : states) {
        if (state.terminal_status != SIM_OK) ++failed;
        if (state.hit != 0u) ++hits;
    }
    std::fprintf(campaign,
        "{\n  \"campaign_id\": ");
    write_json_string(campaign, plan.campaign_id);
    std::fprintf(campaign,
        ",\n  \"instance_count\": %zu,\n  \"completed_count\": %zu,\n"
        "  \"failed_count\": %u,\n  \"summary_available_count\": %zu,\n"
        "  \"hit_count\": %u\n}\n",
        states.size(), states.size() - failed, failed, states.size(), hits);
    std::fclose(campaign);

    const std::string gpu_path = plan.output_dir + "/gpu_execution.json";
    std::FILE *gpu = std::fopen(gpu_path.c_str(), "wb");
    if (gpu == nullptr) return fail("cannot write " + gpu_path, error);
    std::fprintf(gpu, "{\n  \"schema_version\": 1,\n  \"gpu\": ");
    write_json_string(gpu, execution.gpu_name);
    std::fprintf(gpu,
        ",\n  \"cuda_runtime_version\": %d,\n  \"mapping\": ",
        execution.cuda_runtime_version);
    write_json_string(gpu, execution.mapping);
    std::fprintf(gpu, ",\n  \"trace_mode\": ");
    write_json_string(gpu, execution.trace_mode);
    std::fprintf(gpu,
        ",\n  \"lanes_per_block\": %u,\n  \"chunk_steps\": %u,\n"
        "  \"chunk_count\": %llu,\n  \"h2d_ms\": %.6f,\n"
        "  \"kernel_ms\": %.6f,\n  \"d2h_ms\": %.6f,\n"
        "  \"host_encode_ms\": %.6f,\n  \"output_ms\": %.6f,\n"
        "  \"wall_ms\": %.6f\n}\n",
        execution.lanes_per_block, execution.chunk_steps,
        static_cast<unsigned long long>(execution.chunk_count),
        execution.h2d_ms, execution.kernel_ms, execution.d2h_ms,
        execution.host_encode_ms, execution.output_ms, execution.wall_ms);
    std::fclose(gpu);
    return SIM_OK;
}

}  // namespace gpu_campaign
