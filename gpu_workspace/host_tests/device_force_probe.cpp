#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "device_force.cuh"

extern "C" {
#include "common/matrix3.h"
#include "common/quaternion.h"
#include "env/aero_database.h"
#include "env/aero_model.h"
}

namespace {

int g_failures = 0;

bool close(double left, double right) {
    return std::fabs(left - right) <= 1.0e-14;
}

/* One host-vs-device comparison over a single query point. */
void compare_point(const AeroModel *host_model,
                   const gpu_campaign::DeviceAeroForceConfig *device_config,
                   const gpu_campaign::DeviceAeroTable *device_table,
                   double density, double mach, Vec3 velocity,
                   double pitch, double yaw, const char *label) {
    Vec3 host_force{}, host_moment{};
    Vec3 device_force{}, device_moment{};
    std::uint32_t host_flags = 0u;
    std::uint32_t device_flags = 0u;
    const SimStatus host_status = aero_model_evaluate(
        host_model, density, mach, velocity, pitch, yaw,
        &host_force, &host_moment, &host_flags);
    const SimStatus device_status = gpu_campaign::device_aero_force_evaluate(
        device_config, device_table, density, mach, velocity, pitch, yaw,
        &device_force, &device_moment, &device_flags);
    const bool pass = host_status == device_status &&
        host_flags == device_flags &&
        close(host_force.x, device_force.x) &&
        close(host_force.y, device_force.y) &&
        close(host_force.z, device_force.z) &&
        close(host_moment.x, device_moment.x) &&
        close(host_moment.y, device_moment.y) &&
        close(host_moment.z, device_moment.z);
    if (!pass) {
        ++g_failures;
        std::printf("device_force_probe: FAIL %s host_status=%u device_status=%u "
                    "host_flags=0x%x device_flags=0x%x\n",
                    label, host_status, device_status, host_flags, device_flags);
    }
}

}  // namespace

int main(int argc, char **argv) {
    if (argc != 2) return EXIT_FAILURE;
    /* Two-sample table: mach in [0,1], alpha in [-0.2,0.2], beta in [-0.2,0.2]. */
    const AeroTableSample samples[] = {
        {0.0, -0.2, -0.2, 0.10, 0.02, 0.03, 0.04, 0.05, 0.06},
        {1.0, 0.2, 0.2, 0.40, 0.08, 0.09, 0.10, 0.11, 0.12},
    };
    if (aero_database_write_file(
            argv[1], samples, 2u, AERO_DB_EXTRAPOLATION_CLAMP_AND_WARN) != SIM_OK) {
        return EXIT_FAILURE;
    }
    AeroDatabase database{};
    if (aero_database_load_file(argv[1], &database) != SIM_OK) return EXIT_FAILURE;
    AeroModel host_model{};
    host_model.enabled = 1;
    host_model.reference_area_m2 = 0.01;
    host_model.reference_length_m = 1.0;
    host_model.drag_coefficient = 0.0;
    host_model.control_force_coefficient = 0.002;
    host_model.control_moment_coefficient = 0.001;
    host_model.database = &database;

    gpu_campaign::DeviceAeroTable device_table{};
    if (gpu_campaign::device_aero_table_init(
            &device_table, samples, 2u,
            gpu_campaign::DeviceExtrapolationPolicy::ClampAndWarn) != SIM_OK) {
        return EXIT_FAILURE;
    }
    gpu_campaign::DeviceAeroForceConfig device_config{
        1u, 0.01, 1.0, 0.002, 0.001};

    const double density = 1.225;

    /* 1. Original single happy-path point (kept for the fixed reference). */
    {
        const Vec3 velocity = vec3_make(100.0, 5.0, 2.0);
        compare_point(&host_model, &device_config, &device_table,
                     density, vec3_norm(velocity) / 340.0, velocity, 0.01, -0.02,
                     "happy_path");
    }

    /* 2. Grid sweep across mach/alpha/beta including out-of-table values.
       Velocity components are chosen directly so alpha=atan2(z,x) and
       beta_ratio=y/speed hit the requested corners; mach is decoupled. */
    const double machs[] = {-0.1, 0.0, 0.3, 0.999, 1.0, 1.5};
    const double alphas[] = {-0.4, -0.2, 0.0, 0.15, 0.2, 0.4};
    const double betas[] = {-0.4, -0.2, 0.1, 0.2, 0.4};
    int sweep_count = 0;
    for (const double mach : machs) {
        for (const double alpha : alphas) {
            for (const double beta : betas) {
                /* Build a velocity with the requested alpha and beta.
                   Choose speed so beta_ratio stays in [-1,1] for the asin. */
                const double speed = 100.0;
                const double beta_ratio = std::sin(beta);
                const double horizontal = speed * std::cos(beta);
                const Vec3 velocity = vec3_make(
                    horizontal * std::cos(alpha),
                    speed * beta_ratio,
                    horizontal * std::sin(alpha));
                compare_point(&host_model, &device_config, &device_table,
                              density, mach, velocity, 0.03, -0.04,
                              "sweep");
                ++sweep_count;
            }
        }
    }

    /* 3. beta_ratio clamping: pure lateral velocity drives |y|/speed = 1. */
    {
        const Vec3 velocity = vec3_make(0.0, 100.0, 0.0);
        compare_point(&host_model, &device_config, &device_table,
                     density, 0.5, velocity, 0.0, 0.0, "beta_clamp_positive");
    }
    {
        const Vec3 velocity = vec3_make(0.0, -100.0, 0.0);
        compare_point(&host_model, &device_config, &device_table,
                     density, 0.5, velocity, 0.0, 0.0, "beta_clamp_negative");
    }

    /* 4. Disabled model returns zero force/moment with SIM_OK. */
    {
        AeroModel disabled_model = host_model;
        disabled_model.enabled = 0;
        gpu_campaign::DeviceAeroForceConfig disabled_config = device_config;
        disabled_config.enabled = 0u;
        const Vec3 velocity = vec3_make(100.0, 5.0, 2.0);
        compare_point(&disabled_model, &disabled_config, &device_table,
                      density, 0.3, velocity, 0.01, -0.02, "disabled_model");
    }

    /* 5. Near-zero speed guard (<= 1e-9) returns zeros with SIM_OK. */
    {
        const Vec3 velocity = vec3_make(1.0e-12, 0.0, 0.0);
        compare_point(&host_model, &device_config, &device_table,
                      density, 0.3, velocity, 0.0, 0.0, "zero_speed");
    }

    /* 6. Invalid-argument branch: negative density is rejected by both. */
    {
        const Vec3 velocity = vec3_make(100.0, 5.0, 2.0);
        compare_point(&host_model, &device_config, &device_table,
                      -1.0, 0.3, velocity, 0.0, 0.0, "invalid_density");
    }

    /* 7. Invalid-argument branch: NaN mach is rejected by both. */
    {
        const Vec3 velocity = vec3_make(100.0, 5.0, 2.0);
        compare_point(&host_model, &device_config, &device_table,
                      density, std::nan(""), velocity, 0.0, 0.0, "invalid_mach_nan");
    }

    /* 8. Invalid-argument branch: negative reference area is rejected. */
    {
        AeroModel bad_model = host_model;
        bad_model.reference_area_m2 = -0.01;
        gpu_campaign::DeviceAeroForceConfig bad_config = device_config;
        bad_config.reference_area_m2 = -0.01;
        const Vec3 velocity = vec3_make(100.0, 5.0, 2.0);
        compare_point(&bad_model, &bad_config, &device_table,
                      density, 0.3, velocity, 0.0, 0.0, "invalid_area");
    }

    /* 9. Air velocity in body frame: the device helper must reproduce the
       exact environment_force_model.c sequence
       (quat_to_dcm -> transpose -> rotate(vel - wind)). */
    int air_points = 0;
    {
        const Quat attitudes[] = {
            quat_identity(),
            Quat{0.7071067811865476, 0.0, 0.7071067811865476, 0.0},
            Quat{0.7071067811865476, 0.0, 0.0, 0.7071067811865476},
            Quat{0.9238795325112867, 0.0, 0.3826834323650898, 0.0},
            Quat{2.0, 1.0, 0.5, -0.5},  /* non-normalized, valid */
        };
        const Vec3 velocities[] = {
            vec3_make(100.0, 5.0, 2.0),
            vec3_make(-390.0, -675.0, 0.0),
            vec3_make(0.0, 0.0, 0.0),
        };
        const Vec3 winds[] = {
            vec3_make(0.0, 0.0, 0.0),
            vec3_make(3.0, -2.0, 1.0),
        };
        for (const Quat attitude : attitudes) {
            for (const Vec3 velocity : velocities) {
                for (const Vec3 wind : winds) {
                    /* Reference: environment_force_model.c lines 88-104. */
                    Matrix3 body_to_ecef;
                    if (quat_to_dcm(attitude, &body_to_ecef) != SIM_OK) {
                        ++g_failures;
                        continue;
                    }
                    const Matrix3 ecef_to_body = matrix3_transpose(body_to_ecef);
                    const Vec3 reference = matrix3_multiply_vec3(
                        ecef_to_body, vec3_sub(velocity, wind));
                    Vec3 device_result{};
                    const SimStatus device_status = gpu_campaign::
                        device_air_velocity_body(
                            attitude, velocity, wind, &device_result);
                    if (device_status != SIM_OK ||
                        !close(reference.x, device_result.x) ||
                        !close(reference.y, device_result.y) ||
                        !close(reference.z, device_result.z)) {
                        ++g_failures;
                        std::printf(
                            "device_force_probe: FAIL air_velocity "
                            "status=%u ref=(%.9f,%.9f,%.9f) "
                            "dev=(%.9f,%.9f,%.9f)\n",
                            device_status, reference.x, reference.y,
                            reference.z, device_result.x, device_result.y,
                            device_result.z);
                    }
                    ++air_points;
                }
            }
        }
        /* 9b. Zero quaternion propagates the quat_normalize error. */
        {
            const Quat zero_quat = Quat{0.0, 0.0, 0.0, 0.0};
            Matrix3 unused;
            const SimStatus reference_status = quat_to_dcm(zero_quat, &unused);
            Vec3 device_result{};
            const SimStatus device_status = gpu_campaign::device_air_velocity_body(
                zero_quat, vec3_make(1.0, 0.0, 0.0),
                vec3_make(0.0, 0.0, 0.0), &device_result);
            if (device_status != reference_status || device_status == SIM_OK) {
                ++g_failures;
                std::printf(
                    "device_force_probe: FAIL air_velocity_zero_quat "
                    "ref_status=%u dev_status=%u\n",
                    reference_status, device_status);
            }
            ++air_points;
        }
        /* 9c. Mach helper mirrors the guarded division. */
        {
            const Vec3 air = vec3_make(30.0, 40.0, 0.0);
            if (!close(gpu_campaign::device_mach_from_air_velocity(air, 340.0),
                       50.0 / 340.0) ||
                !close(gpu_campaign::device_mach_from_air_velocity(air, 0.0),
                       0.0) ||
                !close(gpu_campaign::device_mach_from_air_velocity(air, -1.0),
                       0.0)) {
                ++g_failures;
                std::printf("device_force_probe: FAIL mach_helper\n");
            }
            ++air_points;
        }
    }

    aero_database_unload(&database);
    if (g_failures != 0) {
        std::printf("device_force_probe: FAIL count=%d\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("device_force_probe: PASS points=%d\n",
                1 + sweep_count + 7 + air_points);
    return EXIT_SUCCESS;
}
