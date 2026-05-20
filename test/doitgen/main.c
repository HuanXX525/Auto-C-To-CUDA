/* Auto-converted from PolyBench/C: doitgen */
/* Source: doitgen.c */
#include "../tool.h"

int A[150][140][160];
int C4[160][160];
int sum[160];

int main()
{
  int r, q, p, s;
  int i, j, k;

  /* Initialize arrays */
  for (i = 0; i < 150; i++)
  for (j = 0; j < 140; j++)
  for (k = 0; k < 160; k++)
  A[i][j][k] = (int) ((i*j + k)%160) / 160;
  for (i = 0; i < 160; i++)
  for (j = 0; j < 160; j++)
  C4[i][j] = (int) (i*j % 160) / 160;

  /* Computation */
  double t0 = now_ms();
    for (r = 0; r < 150; r++)
      for (q = 0; q < 140; q++)  {
        for (p = 0; p < 160; p++)  {
  	sum[p] = 0.0;
  	for (s = 0; s < 160; s++)
  	  sum[p] += A[r][q][s] * C4[s][p];
        }
        for (p = 0; p < 160; p++)
  	A[r][q][p] = sum[p];
      }
  double t1 = now_ms();

#ifdef AUTOC2CUDATEST
  printf("doitgen: CUDA %.3f ms\n", t1 - t0);
  save_binary("bin/cuda_out.bin", A, 13440000);
#else
  printf("doitgen: CPU  %.3f ms\n", t1 - t0);
  save_binary("bin/c_out.bin", A, 13440000);
#endif

  return 0;
}