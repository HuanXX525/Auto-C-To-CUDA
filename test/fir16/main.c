/*
 * 16-tap FIR 滤波器（数字信号处理）
 * y[i] = sum_{k=0}^{15} h[k] * x[i-k]
 */
#include "../tool.h"

#define N (20 * 1024 * 1024)
#define NTAPS 16
#define MASK 0xFF

int x[N];
int y[N];

int main()
{
    for (int i = 0; i < N; i++)
        x[i] = i & MASK;

    for (int i = 0; i < NTAPS - 1; i++)
        y[i] = 0;

    double t0 = now_ms();
    for (int i = NTAPS - 1; i < N; i++) {
        y[i] = 1*x[i] + 2*x[i-1] + 3*x[i-2] + 4*x[i-3] + 5*x[i-4] + 6*x[i-5] + 7*x[i-6] + 8*x[i-7]
             + 9*x[i-8] + 10*x[i-9] + 11*x[i-10] + 12*x[i-11] + 13*x[i-12] + 14*x[i-13] + 15*x[i-14] + 16*x[i-15];
    }
    double t1 = now_ms();

#ifdef AUTOC2CUDATEST
    printf("fir16: CUDA %.3f ms\n", t1 - t0);
    save_binary("bin/cuda_out.bin", y, N * sizeof(int));
#else
    printf("fir16: CPU  %.3f ms\n", t1 - t0);
    save_binary("bin/c_out.bin", y, N * sizeof(int));
#endif
    return 0;
}
