#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "common/matrix3.h"
#include "common/quaternion.h"
#include "common/random.h"
#include "common/vec3.h"

namespace {

bool close_value(double actual, double expected, double rtol = 1.0e-12,
                double atol = 1.0e-12) {
    const double error = std::fabs(actual - expected);
    return error <= atol + rtol * std::fabs(expected);
}

void require(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "common_compare: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

}  // namespace

int main() {
    const Vec3 a = vec3_make(1.0, 2.0, 3.0);
    const Vec3 b = vec3_make(-4.0, 5.0, -6.0);
    const Vec3 sum = vec3_add(a, b);
    require(close_value(sum.x, -3.0) && close_value(sum.y, 7.0) &&
                close_value(sum.z, -3.0),
            "vec3_add mismatch");
    require(close_value(vec3_dot(a, b), -12.0), "vec3_dot mismatch");

    Matrix3 matrix = matrix3_zero();
    double value = 1.0;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            matrix.m[row][column] = value++;
        }
    }
    const Matrix3 transpose = matrix3_transpose(matrix);
    require(close_value(transpose.m[0][1], 4.0) &&
                close_value(transpose.m[2][1], 6.0),
            "matrix3_transpose mismatch");

    const Matrix3 identity = matrix3_identity();
    const Matrix3 product = matrix3_multiply(matrix, identity);
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            require(close_value(product.m[row][column], matrix.m[row][column]),
                    "matrix3_multiply mismatch");
        }
    }

    const Quat q = quat_identity();
    Quat advanced{};
    require(quat_integrate(q, vec3_make(0.0, 0.0, 1.0), 0.01, &advanced) ==
                SIM_OK,
            "quat_integrate status mismatch");
    require(close_value(advanced.w, std::cos(0.005)) &&
                close_value(advanced.z, std::sin(0.005)),
            "quat_integrate value mismatch");

    SimRandom random{};
    sim_random_seed(&random, 1234u);
    const uint64_t first = sim_random_next_u64(&random);
    require(first != 0u, "random generator returned zero for nonzero seed");
    const double uniform = sim_random_uniform01(&random);
    require(uniform >= 0.0 && uniform < 1.0, "uniform sample outside [0, 1)");

    std::puts("common_compare: PASS");
    return EXIT_SUCCESS;
}
