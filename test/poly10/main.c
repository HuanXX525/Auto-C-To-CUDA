/*
 * 10 次多项式求值（数值分析，Horner 法）
 * b[i] = c10*a[i]^10 + c9*a[i]^9 + ... + c1*a[i] + c0
 */
#include "../tool.h"

#define N (20 * 1024 * 1024)
#define MASK 0xFF

int a[N];
int b[N];
int c[11];

int main()
{
    for (int i = 0; i < N; i++)
        a[i] = i & MASK;
    for (int k = 0; k < 11; k++)
        c[k] = k + 1;

    double t0 = now_ms();
    for (int i = 0; i < N; i++) {
        b[i] = (((((((((c[10]*a[i] + c[9])*a[i] + c[8])*a[i] + c[7])*a[i] + c[6])*a[i] + c[5])*a[i] + c[4])*a[i] + c[3])*a[i] + c[2])*a[i] + c[1])*a[i] + c[0];
    }
    double t1 = now_ms();

#ifdef AUTOC2CUDATEST
    printf("poly10: CUDA %.3f ms\n", t1 - t0);
    save_binary("bin/cuda_out.bin", b, N * sizeof(int));
#else
    printf("poly10: CPU  %.3f ms\n", t1 - t0);
    save_binary("bin/c_out.bin", b, N * sizeof(int));
#endif
    return 0;
}
