#include "device_force.cuh"

__global__ void device_force_link_probe(
    const gpu_campaign::DeviceAeroForceConfig *config,
    const gpu_campaign::DeviceAeroTable *table, Vec3 velocity, Vec3 *force) {
    Vec3 moment{};
    std::uint32_t flags = 0;
    (void)gpu_campaign::device_aero_force_evaluate(
        config, table, 1.225, 0.3, velocity, 0.0, 0.0,
        force, &moment, &flags);
    /* Exercise the air-velocity helper and its quat/matrix3 dependencies in
       the device-link graph. */
    Vec3 air_velocity{};
    (void)gpu_campaign::device_air_velocity_body(
        quat_identity(), velocity, vec3_make(0.0, 0.0, 0.0), &air_velocity);
    (void)gpu_campaign::device_mach_from_air_velocity(air_velocity, 340.0);
}
