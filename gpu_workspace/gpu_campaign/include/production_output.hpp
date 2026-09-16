#ifndef GPU_CAMPAIGN_PRODUCTION_OUTPUT_HPP
#define GPU_CAMPAIGN_PRODUCTION_OUTPUT_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "host_campaign_config.hpp"

namespace gpu_campaign {

struct GpuExecutionInfo {
    std::string mapping;
    std::string trace_mode;
    std::string gpu_name;
    std::uint32_t cuda_runtime_version;
    std::uint32_t lanes_per_block;
    std::uint32_t chunk_steps;
    std::uint64_t chunk_count;
    double h2d_ms;
    double kernel_ms;
    double d2h_ms;
    double host_encode_ms;
    double output_ms;
    double wall_ms;
};

SimStatus write_production_outputs(
    const HostCampaignPlan &plan,
    const std::vector<DeviceInstanceState> &states,
    const std::vector<std::vector<DeviceTraceRecord>> &traces,
    const GpuExecutionInfo &execution,
    std::string *error);

}  // namespace gpu_campaign

#endif
