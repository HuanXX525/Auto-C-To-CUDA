/* Auto-converted from PolyBench/C: syr2k */
/* Source: syr2k.c */
#include "../tool.h"

int C[1200][1200];
int A[1200][1000];
int B[1200][1000];

int main()
{
  int i, j, k;
  int alpha = 1;
  int beta = 1;

  /* Initialize arrays */
  for (i = 0; i < 1200; i++)
  for (j = 0; j < 1000; j++) {
  A[i][j] = (int) ((i*j+1)%1200) / 1200;
  B[i][j] = (int) ((i*j+2)%1000) / 1000;
  }
  for (i = 0; i < 1200; i++)
  for (j = 0; j < 1200; j++) {
  C[i][j] = (int) ((i*j+3)%1200) / 1000;
  }

  /* Computation */
  double t0 = now_ms();
    for (i = 0; i < 1200; i++) {
      for (j = 0; j <= i; j++)
        C[i][j] *= beta;
      for (k = 0; k < 1000; k++)
        for (j = 0; j <= i; j++)
  	{
  	  C[i][j] += A[j][k]*alpha*B[i][k] + B[j][k]*alpha*A[i][k];
  	}
    }
  double t1 = now_ms();

#ifdef AUTOC2CUDATEST
  printf("syr2k: CUDA %.3f ms\n", t1 - t0);
  save_binary("bin/cuda_out.bin", C, 5760000);
#else
  printf("syr2k: CPU  %.3f ms\n", t1 - t0);
  save_binary("bin/c_out.bin", C, 5760000);
#endif

  return 0;
}