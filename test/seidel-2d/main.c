/* Auto-converted from PolyBench/C: seidel-2d */
/* Source: seidel-2d.c */
#include "../tool.h"

double A[2000][2000];
int TSTEPS = 500;

int main()
{
  int t, i, j;

  /* Initialize arrays */
  for (i = 0; i < 2000; i++)
  for (j = 0; j < 2000; j++)
  A[i][j] = ((double) i*(j+2) + 2) / 2000.0;

  /* Computation */
  double t0 = now_ms();
    for (t = 0; t <= TSTEPS - 1; t++)
      for (i = 1; i<= 2000 - 2; i++)
        for (j = 1; j <= 2000 - 2; j++)
  	A[i][j] = (A[i-1][j-1] + A[i-1][j] + A[i-1][j+1]
  		   + A[i][j-1] + A[i][j] + A[i][j+1]
  		   + A[i+1][j-1] + A[i+1][j] + A[i+1][j+1])/9.0;
  double t1 = now_ms();

#ifdef AUTOC2CUDATEST
  printf("seidel-2d: CUDA %.3f ms\n", t1 - t0);
  save_binary("bin/cuda_out.bin", A, 32000000);
#else
  printf("seidel-2d: CPU  %.3f ms\n", t1 - t0);
  save_binary("bin/c_out.bin", A, 32000000);
#endif

  return 0;
}