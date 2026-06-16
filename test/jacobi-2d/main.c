/* Auto-converted from PolyBench/C: jacobi-2d */
/* Source: jacobi-2d.c */
#include "../tool.h"

double A[1300][1300];
double B[1300][1300];
int TSTEPS = 500;

int main()
{
  int t, i, j;

  /* Initialize arrays */
  for (i = 0; i < 1300; i++)
  for (j = 0; j < 1300; j++)
  {
  A[i][j] = ((double) i*(j+2) + 2) / 1300.0;
  B[i][j] = ((double) i*(j+3) + 3) / 1300.0;
  }

  /* Computation */
  double t0 = now_ms();
    for (t = 0; t < TSTEPS; t++)
      {
        for (i = 1; i < 1300 - 1; i++)
  	for (j = 1; j < 1300 - 1; j++)
  	  B[i][j] = 0.2 * (A[i][j] + A[i][j-1] + A[i][1+j] + A[1+i][j] + A[i-1][j]);
        for (i = 1; i < 1300 - 1; i++)
  	for (j = 1; j < 1300 - 1; j++)
  	  A[i][j] = 0.2 * (B[i][j] + B[i][j-1] + B[i][1+j] + B[1+i][j] + B[i-1][j]);
      }
  double t1 = now_ms();

#ifdef AUTOC2CUDATEST
  printf("jacobi-2d: CUDA %.3f ms\n", t1 - t0);
  save_binary("bin/cuda_out.bin", A, 13520000);
#else
  printf("jacobi-2d: CPU  %.3f ms\n", t1 - t0);
  save_binary("bin/c_out.bin", A, 13520000);
#endif

  return 0;
}