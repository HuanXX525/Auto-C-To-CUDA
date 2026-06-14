/* Auto-converted from PolyBench/C: gemm */
/* Source: gemm.c */
#include "../tool.h"

double C[1000][1100];
double A[1000][1200];
double B[1200][1100];

int main()
{
  int i, j, k;
  double alpha = 1;
  double beta = 1;

  /* Initialize arrays */
  for (i = 0; i < 1000; i++)
  for (j = 0; j < 1100; j++)
  C[i][j] = (double) ((i*j+1) % 1000) / 1000.0;
  for (i = 0; i < 1000; i++)
  for (j = 0; j < 1200; j++)
  A[i][j] = (double) (i*(j+1) % 1200) / 1200.0;
  for (i = 0; i < 1200; i++)
  for (j = 0; j < 1100; j++)
  B[i][j] = (double) (i*(j+2) % 1100) / 1100.0;

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
  save_binary("bin/cuda_out.bin", C, 8800000);
#else
  printf("gemm: CPU  %.3f ms\n", t1 - t0);
  save_binary("bin/c_out.bin", C, 8800000);
#endif

  return 0;
}