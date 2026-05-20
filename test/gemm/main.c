/* Auto-converted from PolyBench/C: gemm */
/* Source: gemm.c */
#include "../tool.h"

int C[1000][1100];
int A[1000][1200];
int B[1200][1100];

int main()
{
  int i, j, k;
  int alpha;
  int beta;

  /* Initialize arrays */
  for (i = 0; i < 1000; i++)
  for (j = 0; j < 1100; j++)
  C[i][j] = (int) ((i*j+1) % 1000) / 1000;
  for (i = 0; i < 1000; i++)
  for (j = 0; j < 1200; j++)
  A[i][j] = (int) (i*(j+1) % 1200) / 1200;
  for (i = 0; i < 1200; i++)
  for (j = 0; j < 1100; j++)
  B[i][j] = (int) (i*(j+2) % 1100) / 1100;

  /* Computation */
  double t0 = now_ms();
    for (i = 0; i < 1000; i++) {
      for (j = 0; j < 1100; j++)
  	C[i][j] *= beta;
      for (k = 0; k < 1200; k++) {
         for (j = 0; j < 1100; j++)
  	  C[i][j] += alpha * A[i][k] * B[k][j];
      }
    }
  double t1 = now_ms();

#ifdef AUTOC2CUDATEST
  printf("gemm: CUDA %.3f ms\n", t1 - t0);
  save_binary("bin/cuda_out.bin", C, 4400000);
#else
  printf("gemm: CPU  %.3f ms\n", t1 - t0);
  save_binary("bin/c_out.bin", C, 4400000);
#endif

  return 0;
}