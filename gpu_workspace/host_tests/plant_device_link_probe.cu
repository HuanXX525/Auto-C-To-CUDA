#include "env/missile_plant_6dof.h"

#include "../converted/common/vec3.cu"
#include "../converted/common/matrix3.cu"
#include "../converted/common/quaternion.cu"
#include "../converted/environment/missile_plant_6dof.cu"

__global__ void plant_probe_kernel(PlantState6Dof *state,
                                   const PlantInput6Dof *input) {
    if (threadIdx.x == 0) {
        (void)missile_plant_step(state, input, 0.01, INTEGRATOR_EULER);
    }
}
