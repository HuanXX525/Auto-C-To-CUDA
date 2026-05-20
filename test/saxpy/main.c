/*
 * SAXPY: y[i] = 2 * x[i] + z[i]
 * 典型 BLAS Level 1 操作，完全并行
 */
#include "../tool.h"

#define N (1024 * 1024)

int x[N];
int y[N];
int z[N];

int main()
{
    for (int i = 0; i < N; i++) {
        x[i] = i;
        z[i] = 1;
    }

    double t0 = now_ms();
    for (int i = 0; i < N; i++) {
        y[i] = 2 * x[i] + z[i];
    }
    double t1 = now_ms();

#ifdef AUTOC2CUDATEST
    printf("saxpy: CUDA %.3f ms\n", t1 - t0);
    save_binary("bin/cuda_out.bin", y, N * sizeof(int));
#else
    printf("saxpy: CPU  %.3f ms\n", t1 - t0);
    save_binary("bin/c_out.bin", y, N * sizeof(int));
#endif
    return 0;
}
