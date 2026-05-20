/* Auto-converted from PolyBench/C: 2mm */
/* Source: 2mm.c */
#include "../tool.h"

int tmp[800][900];
int A[800][1100];
int B[1100][900];
int C[900][1200];
int D[800][1200];

int main()
{
  int i, j, k;
  int alpha;
  int beta;

  /* Initialize arrays */
  for (i = 0; i < 800; i++)
  for (j = 0; j < 1100; j++)
  A[i][j] = (int) ((i*j+1) % 800) / 800;
  for (i = 0; i < 1100; i++)
  for (j = 0; j < 900; j++)
  B[i][j] = (int) (i*(j+1) % 900) / 900;
  for (i = 0; i < 900; i++)
  for (j = 0; j < 1200; j++)
  C[i][j] = (int) ((i*(j+3)+1) % 1200) / 1200;
  for (i = 0; i < 800; i++)
  for (j = 0; j < 1200; j++)
  D[i][j] = (int) (i*(j+2) % 1100) / 1100;

  /* Computation */
  double t0 = now_ms();
    for (i = 0; i < 800; i++)
      for (j = 0; j < 900; j++)
        {
  	tmp[i][j] = 0.0;
  	for (k = 0; k < 1100; ++k)
  	  tmp[i][j] += alpha * A[i][k] * B[k][j];
        }
    for (i = 0; i < 800; i++)
      for (j = 0; j < 1200; j++)
        {
  	D[i][j] *= beta;
  	for (k = 0; k < 900; ++k)
  	  D[i][j] += tmp[i][k] * C[k][j];
        }
  double t1 = now_ms();

#ifdef AUTOC2CUDATEST
  printf("2mm: CUDA %.3f ms\n", t1 - t0);
  save_binary("bin/cuda_out.bin", D, 3840000);
#else
  printf("2mm: CPU  %.3f ms\n", t1 - t0);
  save_binary("bin/c_out.bin", D, 3840000);
#endif

  return 0;
}