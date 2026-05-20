/*
 * 向量加法: c[i] = a[i] + b[i]
 * 完全并行，无依赖
 */
#include "../tool.h"

#define N (1024 * 1024)

int a[N];
int b[N];
int c[N];

int main()
{
    for (int i = 0; i < N; i++) {
        a[i] = i;
        b[i] = N - i;
    }

    double t0 = now_ms();
    for (int i = 0; i < N; i++) {
        c[i] = a[i] + b[i];
    }
    double t1 = now_ms();

#ifdef AUTOC2CUDATEST
    printf("vectorAdd: CUDA %.3f ms\n", t1 - t0);
    save_binary("bin/cuda_out.bin", c, N * sizeof(int));
#else
    printf("vectorAdd: CPU  %.3f ms\n", t1 - t0);
    save_binary("bin/c_out.bin", c, N * sizeof(int));
#endif
    return 0;
}
