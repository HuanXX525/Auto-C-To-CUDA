#include "common/vec3.h"
#include "common/quaternion.h"

// The generated implementation is included only for this probe so nvcc can
// inspect the transitive call graph. It is not the final build arrangement.
#include "../converted/common/vec3.cu"
#include "../converted/common/quaternion.cu"

__global__ void probe_kernel(Vec3 *out_vec, Quat *out_quat) {
    if (threadIdx.x == 0) {
        *out_vec = vec3_add(vec3_make(1.0, 2.0, 3.0), vec3_make(4.0, 5.0, 6.0));
        *out_quat = quat_identity();
    }
}
