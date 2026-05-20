/* Auto-converted from PolyBench/C: fdtd-2d */
/* Source: fdtd-2d.c */
#include "../tool.h"

int ex[1000][1200];
int ey[1000][1200];
int hz[1000][1200];
int _fict_[500];

int main()
{
  int t, i, j;

  /* Initialize arrays */
  for (i = 0; i < 500; i++)
  _fict_[i] = (int) i;
  for (i = 0; i < 1000; i++)
  for (j = 0; j < 1200; j++)
  {
  ex[i][j] = ((int) i*(j+1)) / 1000;
  ey[i][j] = ((int) i*(j+2)) / 1200;
  hz[i][j] = ((int) i*(j+3)) / 1000;
  }

  /* Computation */
  double t0 = now_ms();
  
    for(t = 0; t < 500; t++)
      {
        for (j = 0; j < 1200; j++)
  	ey[0][j] = _fict_[t];
        for (i = 1; i < 1000; i++)
  	for (j = 0; j < 1200; j++)
  	  ey[i][j] = ey[i][j] - 0.5*(hz[i][j]-hz[i-1][j]);
        for (i = 0; i < 1000; i++)
  	for (j = 1; j < 1200; j++)
  	  ex[i][j] = ex[i][j] - 0.5*(hz[i][j]-hz[i][j-1]);
        for (i = 0; i < 1000 - 1; i++)
  	for (j = 0; j < 1200 - 1; j++)
  	  hz[i][j] = hz[i][j] - 0.7*  (ex[i][j+1] - ex[i][j] +
  				       ey[i+1][j] - ey[i][j]);
      }
  
  double t1 = now_ms();

#ifdef AUTOC2CUDATEST
  printf("fdtd-2d: CUDA %.3f ms\n", t1 - t0);
  save_binary("bin/cuda_out.bin", ex, 4800000);
#else
  printf("fdtd-2d: CPU  %.3f ms\n", t1 - t0);
  save_binary("bin/c_out.bin", ex, 4800000);
#endif

  return 0;
}