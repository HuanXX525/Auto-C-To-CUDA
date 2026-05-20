/*
 * 1D 三点模板计算: b[i] = a[i-1] + a[i] + a[i+1]
 * 读/写不同数组，完全并行
 */
#include "../tool.h"

#define N (1024 * 1024)

int a[N];
int b[N];

int main()
{
    for (int i = 0; i < N; i++) {
        a[i] = i;
    }
    b[0] = a[0] + a[1];

    double t0 = now_ms();
    for (int i = 1; i < N - 1; i++) {
        b[i] = a[i - 1] + a[i] + a[i + 1];
    }
    double t1 = now_ms();

    b[N - 1] = a[N - 2] + a[N - 1];

#ifdef AUTOC2CUDATEST
    printf("conv1d: CUDA %.3f ms\n", t1 - t0);
    save_binary("bin/cuda_out.bin", b, N * sizeof(int));
#else
    printf("conv1d: CPU  %.3f ms\n", t1 - t0);
    save_binary("bin/c_out.bin", b, N * sizeof(int));
#endif
    return 0;
}
