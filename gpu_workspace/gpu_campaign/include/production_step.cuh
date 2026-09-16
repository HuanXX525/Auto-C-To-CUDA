#ifndef GPU_CAMPAIGN_PRODUCTION_STEP_CUH
#define GPU_CAMPAIGN_PRODUCTION_STEP_CUH

#include "gpu_types.cuh"

namespace gpu_campaign {

/* Execute a bounded, resumable chunk of the CPU-equivalent in-process
   sensor -> flight-control -> actuator -> environment -> plant chain. */
__global__ void simulate_production_chunk(
    DeviceInstanceState *states,
    DeviceTraceRecord *trace,
    DeviceChunkLaunch launch,
    const DeviceInstanceConfig *configs,
    const DeviceAeroTable *aero_tables,
    std::uint32_t aero_table_count,
    const AeroSurrogateModel *aero_surrogates,
    std::uint32_t aero_surrogate_count,
    const DeviceTerrainSet *terrain_sets,
    std::uint32_t terrain_set_count,
    const DeviceTerrainGrid *terrain_grids,
    std::uint32_t terrain_grid_count,
    const FaultInjection *fault_scripts,
    std::uint32_t fault_script_count);

}  // namespace gpu_campaign

#endif
