/* Auto-converted from PolyBench/C: gesummv */
/* Source: gesummv.c */
#include "../tool.h"

double A[1300][1300];
double B[1300][1300];
double tmp[1300];
double x[1300];
double y[1300];

int main()
{
  int i, j;
  double alpha = 1;
  double beta = 1;

  /* Initialize arrays */
  for (i = 0; i < 1300; i++)
  {
  x[i] = (double)( i % 1300) / 1300.0;
  for (j = 0; j < 1300; j++) {
  A[i][j] = (double) ((i*j+1) % 1300) / 1300.0;
  B[i][j] = (double) ((i*j+2) % 1300) / 1300.0;
  }
  }

  /* Computation */
  double t0 = now_ms();
    for (i = 0; i < 1300; i++)
      {
        tmp[i] = 0.0;
        y[i] = 0.0;
        for (j = 0; j < 1300; j++)
  	{
  	  tmp[i] = A[i][j] * x[j] + tmp[i];
  	  y[i] = B[i][j] * x[j] + y[i];
  	}
        y[i] = alpha * tmp[i] + beta * y[i];
      }
  double t1 = now_ms();

#ifdef AUTOC2CUDATEST
  printf("gesummv: CUDA %.3f ms\n", t1 - t0);
  save_binary("bin/cuda_out.bin", y, 10400);
#else
  printf("gesummv: CPU  %.3f ms\n", t1 - t0);
  save_binary("bin/c_out.bin", y, 10400);
#endif

  return 0;
}