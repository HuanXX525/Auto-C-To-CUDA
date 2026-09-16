#ifndef GPU_CAMPAIGN_HOST_CAMPAIGN_CONFIG_HPP
#define GPU_CAMPAIGN_HOST_CAMPAIGN_CONFIG_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "gpu_types.cuh"

namespace gpu_campaign {

struct HostTerrainAsset {
    std::vector<std::string> tile_paths;
    std::string index_path;
    std::string key;
};

struct HostFaultAsset {
    std::string path;
    FaultInjection definition;
};

struct HostInstancePlan {
    DeviceInstanceConfig device;
    std::string scenario_path;
    std::string flight_control_path;
    std::string faults_path;
    std::string map_database_path;
    std::string map_tile_index_path;
    std::string aero_surrogate_model_version;
    std::string aero_surrogate_training_data_version;
};

struct HostCampaignPlan {
    std::string runtime_path;
    std::string campaign_id;
    std::string output_dir;
    std::string instance_dir_template;
    std::string requested_schedule;
    std::string network_host;
    std::string environment_program;
    std::string flight_control_program;
    std::uint32_t configured_instance_count;
    std::uint32_t max_parallel_instances;
    std::uint32_t environment_base_port;
    std::uint32_t flight_control_base_port;
    std::uint32_t flush_every_steps;
    std::uint64_t base_random_seed;
    bool binary_logs;
    bool event_log;
    DeviceCampaignConfig device;
    std::vector<HostInstancePlan> instances;
    std::vector<std::string> aero_assets;
    std::vector<std::string> aero_surrogate_assets;
    std::vector<HostTerrainAsset> terrain_assets;
    std::vector<HostFaultAsset> fault_assets;
};

/* Load and validate the unmodified Aircraft-Simu runtime schema and every
   referenced per-instance input.  Relative references are resolved by
   walking from the runtime file toward its repository root, so baseline
   paths such as configs/baseline/scenario.json do not depend on cwd. */
SimStatus load_campaign_plan(const char *runtime_path, HostCampaignPlan *out,
                             std::string *error);

}  // namespace gpu_campaign

#endif
