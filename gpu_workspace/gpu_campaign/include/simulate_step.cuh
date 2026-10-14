#ifndef GPU_CAMPAIGN_SIMULATE_STEP_CUH
#define GPU_CAMPAIGN_SIMULATE_STEP_CUH

#include "gpu_types.cuh"

namespace gpu_campaign {

/**
 * Execute one instance for a bounded chunk of serial time steps.
 * The first release intentionally uses one thread per block for correctness.
 */
__global__ void simulate_chunk(DeviceInstanceState *state,
                               DeviceTraceRecord *trace,
                               DeviceConfig config,
                               const DeviceAeroTable *aero_table,
                               const DeviceTerrainGrid *terrain_grid,
                               const FaultInjection *fault_script);

}  // namespace gpu_campaign

#endif
