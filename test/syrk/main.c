/* Auto-converted from PolyBench/C: syrk */
/* Source: syrk.c */
#include "../tool.h"

double C[1200][1200];
double A[1200][1000];

int main()
{
  int i, j, k;
  double alpha = 1;
  double beta = 1;

  /* Initialize arrays */
  for (i = 0; i < 1200; i++)
  for (j = 0; j < 1000; j++)
  A[i][j] = (double) ((i*j+1)%1200) / 1200.0;
  for (i = 0; i < 1200; i++)
  for (j = 0; j < 1200; j++)
  C[i][j] = (double) ((i*j+2)%1000) / 1000.0;

  /* Computation */
  double t0 = now_ms();
    for (i = 0; i < 1200; i++) {
      for (j = 0; j <= i; j++)
        C[i][j] *= beta;
      for (k = 0; k < 1000; k++) {
        for (j = 0; j <= i; j++)
          C[i][j] += alpha * A[i][k] * A[j][k];
      }
    }
  double t1 = now_ms();

#ifdef AUTOC2CUDATEST
  printf("syrk: CUDA %.3f ms\n", t1 - t0);
  save_binary("bin/cuda_out.bin", C, 11520000);
#else
  printf("syrk: CPU  %.3f ms\n", t1 - t0);
  save_binary("bin/c_out.bin", C, 11520000);
#endif

  return 0;
}