/* Auto-converted from PolyBench/C: jacobi-1d */
/* Source: jacobi-1d.c */
#include "../tool.h"

double A[2000];
double B[2000];
int TSTEPS = 500;

int main()
{
  int t, i;

  /* Initialize arrays */
  for (i = 0; i < 2000; i++)
  {
  A[i] = (double)(i + 2) / 2000.0;
  B[i] = (double)(i + 3) / 2000.0;
  }

  /* Computation */
  double t0 = now_ms();
    for (t = 0; t < TSTEPS; t++)
      {
        for (i = 1; i < 2000 - 1; i++)
  	B[i] = 0.33333 * (A[i-1] + A[i] + A[i + 1]);
        for (i = 1; i < 2000 - 1; i++)
  	A[i] = 0.33333 * (B[i-1] + B[i] + B[i + 1]);
      }
  double t1 = now_ms();

#ifdef AUTOC2CUDATEST
  printf("jacobi-1d: CUDA %.3f ms\n", t1 - t0);
  save_binary("bin/cuda_out.bin", A, 16000);
#else
  printf("jacobi-1d: CPU  %.3f ms\n", t1 - t0);
  save_binary("bin/c_out.bin", A, 16000);
#endif

  return 0;
}