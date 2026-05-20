/* Auto-converted from PolyBench/C: mvt */
/* Source: mvt.c */
#include "../tool.h"

int A[2000][2000];
int x1[2000];
int x2[2000];
int y_1[2000];
int y_2[2000];

int main()
{
  int i, j;

  /* Initialize arrays */
  for (i = 0; i < 2000; i++)
  {
  x1[i] = (int) (i % 2000) / 2000;
  x2[i] = (int) ((i + 1) % 2000) / 2000;
  y_1[i] = (int) ((i + 3) % 2000) / 2000;
  y_2[i] = (int) ((i + 4) % 2000) / 2000;
  for (j = 0; j < 2000; j++)
  A[i][j] = (int) (i*j % 2000) / 2000;
  }

  /* Computation */
  double t0 = now_ms();
    for (i = 0; i < 2000; i++)
      for (j = 0; j < 2000; j++)
        x1[i] = x1[i] + A[i][j] * y_1[j];
    for (i = 0; i < 2000; i++)
      for (j = 0; j < 2000; j++)
        x2[i] = x2[i] + A[j][i] * y_2[j];
  double t1 = now_ms();

#ifdef AUTOC2CUDATEST
  printf("mvt: CUDA %.3f ms\n", t1 - t0);
  save_binary("bin/cuda_out.bin", x1, 8000);
#else
  printf("mvt: CPU  %.3f ms\n", t1 - t0);
  save_binary("bin/c_out.bin", x1, 8000);
#endif

  return 0;
}