/* Auto-converted from PolyBench/C: atax */
/* Source: atax.c */
#include "../tool.h"

int A[1900][2100];
int x[2100];
int y[2100];
int tmp[1900];

int main()
{
  int i, j;
  int fn;

  /* Initialize arrays */
  for (i = 0; i < 2100; i++)
    x[i] = 1 + (i / fn);
  for (i = 0; i < 1900; i++)
    for (j = 0; j < 2100; j++)
      A[i][j] = (int)((i + j) % 2100) / (5 * 1900);

  /* Computation */
  double t0 = now_ms();
  for (i = 0; i < 2100; i++)
    y[i] = 0;
  for (i = 0; i < 1900; i++)
  {
    tmp[i] = 0.0;
    for (j = 0; j < 2100; j++)
      tmp[i] = tmp[i] + A[i][j] * x[j];
    for (j = 0; j < 2100; j++)
      y[j] = y[j] + A[i][j] * tmp[i];
  }
  double t1 = now_ms();

#ifdef AUTOC2CUDATEST
  printf("atax: CUDA %.3f ms\n", t1 - t0);
  save_binary("bin/cuda_out.bin", y, 8400);
#else
  printf("atax: CPU  %.3f ms\n", t1 - t0);
  save_binary("bin/c_out.bin", y, 8400);
#endif

  return 0;
}