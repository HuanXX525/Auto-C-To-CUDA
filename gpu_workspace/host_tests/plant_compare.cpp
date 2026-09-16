#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "env/missile_plant_6dof.h"

namespace {
void require(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "plant_compare: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

bool close_value(double actual, double expected) {
    return std::fabs(actual - expected) <=
           1.0e-12 + 1.0e-10 * std::fabs(expected);
}
}

int main() {
    PlantState6Dof state{};
    state.q_bi = quat_identity();
    state.vel_ecef = vec3_make(10.0, 0.0, 0.0);
    state.mass = 2.0;
    state.inertia_b = matrix3_identity();

    PlantInput6Dof input{};
    input.force_b_n = vec3_make(2.0, 0.0, 0.0);
    input.gravity_ecef_mps2 = vec3_make(0.0, -9.8, 0.0);
    input.earth_rotation_ecef_radps = vec3_make(0.0, 0.0, 0.0);

    require(missile_plant_step(&state, &input, 0.1, INTEGRATOR_EULER) == SIM_OK,
            "Euler step returned an error");
    require(close_value(state.time, 0.1), "time mismatch");
    require(close_value(state.pos_ecef.x, 1.0), "position mismatch");
    require(close_value(state.vel_ecef.x, 10.1), "x velocity mismatch");
    require(close_value(state.vel_ecef.y, -0.98), "y velocity mismatch");
    require(close_value(state.q_bi.w, 1.0) && close_value(state.q_bi.x, 0.0) &&
                close_value(state.q_bi.y, 0.0) && close_value(state.q_bi.z, 0.0),
            "attitude mismatch");

    std::puts("plant_compare: PASS");
    return EXIT_SUCCESS;
}
