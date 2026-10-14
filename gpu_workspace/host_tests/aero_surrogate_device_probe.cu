#include "env/aero_surrogate.h"

#include "../converted/environment/aero_surrogate_device.cu"

__global__ void aero_surrogate_probe_kernel(
    const AeroSurrogateModel *model,
    AeroSurrogateCoefficients *out) {
    if (threadIdx.x == 0) {
        (void)aero_surrogate_evaluate(model, 2.0, 0.1, -0.05, out);
    }
}
