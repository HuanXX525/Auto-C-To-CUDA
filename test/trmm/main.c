/* Auto-converted from PolyBench/C: trmm */
/* Source: trmm.c */
#include "../tool.h"

int A[1000][1000];
int B[1000][1200];

int main()
{
  int i, j, k;
  int alpha = 1;

  /* Initialize arrays */
  for (i = 0; i < 1000; i++) {
  for (j = 0; j < i; j++) {
  A[i][j] = (int)((i+j) % 1000)/1000;
  }
  A[i][i] = 1.0;
  for (j = 0; j < 1200; j++) {
  B[i][j] = (int)((1200+(i-j)) % 1200)/1200;
  }
  }

  /* Computation */
  double t0 = now_ms();
    for (i = 0; i < 1000; i++)
       for (j = 0; j < 1200; j++) {
          for (k = i+1; k < 1000; k++)
             B[i][j] += A[k][i] * B[k][j];
          B[i][j] = alpha * B[i][j];
       }
  double t1 = now_ms();

#ifdef AUTOC2CUDATEST
  printf("trmm: CUDA %.3f ms\n", t1 - t0);
  save_binary("bin/cuda_out.bin", B, 4800000);
#else
  printf("trmm: CPU  %.3f ms\n", t1 - t0);
  save_binary("bin/c_out.bin", B, 4800000);
#endif

  return 0;
}