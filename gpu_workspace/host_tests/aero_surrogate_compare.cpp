#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "env/aero_surrogate.h"

namespace {
void require(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "aero_surrogate_compare: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}
}

int main() {
    AeroSurrogateModel model{};
    model.cx = {1.0, 2.0, 3.0, 4.0};
    model.cy = {2.0, 0.5, 1.0, -1.0};
    model.cz = {3.0, 1.0, 0.0, 0.5};
    model.cl = {4.0, 0.0, 2.0, 1.0};
    model.cm = {5.0, -1.0, 0.5, 0.25};
    model.cn = {6.0, 1.5, -0.5, 2.0};

    AeroSurrogateCoefficients output{};
    require(aero_surrogate_evaluate(&model, 2.0, 0.1, -0.05, &output) == SIM_OK,
            "evaluation returned an error");
    require(std::fabs(output.cx - (1.0 + 4.0 + 0.3 - 0.2)) < 1.0e-12,
            "cx mismatch");
    require(std::fabs(output.cn - (6.0 + 3.0 - 0.05 - 0.1)) < 1.0e-12,
            "cn mismatch");

    std::puts("aero_surrogate_compare: PASS");
    return EXIT_SUCCESS;
}
