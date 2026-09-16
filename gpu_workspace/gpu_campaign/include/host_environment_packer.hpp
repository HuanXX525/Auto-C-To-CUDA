#ifndef GPU_CAMPAIGN_HOST_ENVIRONMENT_PACKER_HPP
#define GPU_CAMPAIGN_HOST_ENVIRONMENT_PACKER_HPP

#include "common/status.h"
#include "device_environment.cuh"

namespace gpu_campaign {

/* Parse the existing host binary formats and produce pointer-free device data. */
SimStatus pack_aero_database_file(const char *path, DeviceAeroTable *out);
SimStatus pack_terrain_tile_file(
    const char *path, DeviceTerrainGrid *out,
    DeviceExtrapolationPolicy policy);

}  // namespace gpu_campaign

#endif
