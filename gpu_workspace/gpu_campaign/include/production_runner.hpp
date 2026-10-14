#ifndef GPU_CAMPAIGN_PRODUCTION_RUNNER_HPP
#define GPU_CAMPAIGN_PRODUCTION_RUNNER_HPP

#include <cstdint>

namespace gpu_campaign {

enum class ProductionTraceMode : std::uint32_t {
    Full = 0,
    Summary = 1,
};

enum class ProductionMapping : std::uint32_t {
    Auto = 0,
    Serial = 1,
    Packed = 2,
};

struct ProductionOptions {
    const char *config_path;
    const char *output_dir;
    ProductionTraceMode trace_mode;
    ProductionMapping mapping;
    std::uint32_t chunk_steps;
    std::uint32_t lanes_per_block;
};

int run_production_campaign(const ProductionOptions &options);

}  // namespace gpu_campaign

#endif
