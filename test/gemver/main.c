/* Auto-converted from PolyBench/C: gemver */
/* Source: gemver.c */
#include "../tool.h"

int A[2000][2000];
int u1[2000];
int v1[2000];
int u2[2000];
int v2[2000];
int w[2000];
int x[2000];
int y[2000];
int z[2000];

int main()
{
  int i, j;
  int fn = (int)2000;
  int alpha = 1;
  int beta = 1;

  /* Initialize arrays */
  for (i = 0; i < 2000; i++)
  {
  u1[i] = i;
  u2[i] = ((i+1)/fn)/2.0;
  v1[i] = ((i+1)/fn)/4.0;
  v2[i] = ((i+1)/fn)/6.0;
  y[i] = ((i+1)/fn)/8.0;
  z[i] = ((i+1)/fn)/9.0;
  x[i] = 0.0;
  w[i] = 0.0;
  for (j = 0; j < 2000; j++)
  A[i][j] = (int) (i*j % 2000) / 2000;
  }

  /* Computation */
  double t0 = now_ms();
  
    for (i = 0; i < 2000; i++)
      for (j = 0; j < 2000; j++)
        A[i][j] = A[i][j] + u1[i] * v1[j] + u2[i] * v2[j];
  
    for (i = 0; i < 2000; i++)
      for (j = 0; j < 2000; j++)
        x[i] = x[i] + beta * A[j][i] * y[j];
  
    for (i = 0; i < 2000; i++)
      x[i] = x[i] + z[i];
  
    for (i = 0; i < 2000; i++)
      for (j = 0; j < 2000; j++)
        w[i] = w[i] +  alpha * A[i][j] * x[j];
  
  double t1 = now_ms();

#ifdef AUTOC2CUDATEST
  printf("gemver: CUDA %.3f ms\n", t1 - t0);
  save_binary("bin/cuda_out.bin", w, 8000);
#else
  printf("gemver: CPU  %.3f ms\n", t1 - t0);
  save_binary("bin/c_out.bin", w, 8000);
#endif

  return 0;
}