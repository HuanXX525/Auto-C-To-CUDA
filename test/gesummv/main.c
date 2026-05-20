/* Auto-converted from PolyBench/C: gesummv */
/* Source: gesummv.c */
#include "../tool.h"

int A[1300][1300];
int B[1300][1300];
int tmp[1300];
int x[1300];
int y[1300];

int main()
{
  int i, j;
  int alpha;
  int beta;

  /* Initialize arrays */
  for (i = 0; i < 1300; i++)
  {
  x[i] = (int)( i % 1300) / 1300;
  for (j = 0; j < 1300; j++) {
  A[i][j] = (int) ((i*j+1) % 1300) / 1300;
  B[i][j] = (int) ((i*j+2) % 1300) / 1300;
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
  save_binary("bin/cuda_out.bin", y, 5200);
#else
  printf("gesummv: CPU  %.3f ms\n", t1 - t0);
  save_binary("bin/c_out.bin", y, 5200);
#endif

  return 0;
}