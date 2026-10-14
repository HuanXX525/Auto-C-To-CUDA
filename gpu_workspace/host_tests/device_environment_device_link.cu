#include "device_environment.cuh"

__global__ void device_environment_probe_kernel(
    gpu_campaign::DeviceAeroTable *aero,
    gpu_campaign::DeviceTerrainGrid *terrain,
    AeroTableSample *sample,
    double *height) {
    if (blockIdx.x == 0 && threadIdx.x == 0) {
        std::uint32_t flags = 0;
        (void)gpu_campaign::device_aero_table_lookup(
            aero, 0.5, 0.0, 0.0, sample, &flags);
        (void)gpu_campaign::device_terrain_grid_height(
            terrain, 0.5, 0.5, height, &flags);
    }
}
