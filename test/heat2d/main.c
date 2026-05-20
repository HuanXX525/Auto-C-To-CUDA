/*
 * 2D 热传导方程 5 点模板（偏微分方程数值解）
 * b[i] = (a[i-W] + a[i-1] + a[i] + a[i+1] + a[i+W]) / 5
 */
#include "../tool.h"

#define W 6000
#define H 5000
#define N (W * H)
#define MASK 0xFF

int a[N];
int b[N];

int main()
{
    for (int i = 0; i < N; i++)
        a[i] = i & MASK;

    for (int i = 0; i < N; i++)
        b[i] = 0;

    double t0 = now_ms();
    for (int i = W + 1; i < N - W - 1; i++) {
        b[i] = (a[i-W] + a[i-1] + a[i] + a[i+1] + a[i+W]) / 5;
    }
    double t1 = now_ms();

#ifdef AUTOC2CUDATEST
    printf("heat2d: CUDA %.3f ms\n", t1 - t0);
    save_binary("bin/cuda_out.bin", b, N * sizeof(int));
#else
    printf("heat2d: CPU  %.3f ms\n", t1 - t0);
    save_binary("bin/c_out.bin", b, N * sizeof(int));
#endif
    return 0;
}
