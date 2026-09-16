#ifndef COMMON_DEVICE_QUALIFIERS_H
#define COMMON_DEVICE_QUALIFIERS_H

/* Keep the copied C headers usable by both the CPU reference build and nvcc. */
#ifdef __CUDACC__
#define C2CUDA_HOST_DEVICE __host__ __device__
#else
#define C2CUDA_HOST_DEVICE
#endif

#endif
