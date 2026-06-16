/* Auto-converted from PolyBench/C: 3mm */
/* Source: 3mm.c */
#include "../tool.h"

double E[800][900];
double A[800][1000];
double B[1000][900];
double F[900][1100];
double C[900][1200];
double D[1200][1100];
double G[800][1100];

int main()
{
  int i, j, k;

  /* Initialize arrays */
  for (i = 0; i < 800; i++)
  for (j = 0; j < 1000; j++)
  A[i][j] = (double) ((i*j+1) % 800) / (5.0*800);
  for (i = 0; i < 1000; i++)
  for (j = 0; j < 900; j++)
  B[i][j] = (double) ((i*(j+1)+2) % 900) / (5.0*900);
  for (i = 0; i < 900; i++)
  for (j = 0; j < 1200; j++)
  C[i][j] = (double) (i*(j+3) % 1100) / (5.0*1100);
  for (i = 0; i < 1200; i++)
  for (j = 0; j < 1100; j++)
  D[i][j] = (double) ((i*(j+2)+2) % 1000) / (5.0*1000);

  /* Computation */
  double t0 = now_ms();
    for (i = 0; i < 800; i++)
      for (j = 0; j < 900; j++)
        {
  	E[i][j] = 0.0;
  	for (k = 0; k < 1000; ++k)
  	  E[i][j] += A[i][k] * B[k][j];
        }
    for (i = 0; i < 900; i++)
      for (j = 0; j < 1100; j++)
        {
  	F[i][j] = 0.0;
  	for (k = 0; k < 1200; ++k)
  	  F[i][j] += C[i][k] * D[k][j];
        }
    for (i = 0; i < 800; i++)
      for (j = 0; j < 1100; j++)
        {
  	G[i][j] = 0.0;
  	for (k = 0; k < 900; ++k)
  	  G[i][j] += E[i][k] * F[k][j];
        }
  double t1 = now_ms();

#ifdef AUTOC2CUDATEST
  printf("3mm: CUDA %.3f ms\n", t1 - t0);
  save_binary("bin/cuda_out.bin", G, 7040000);
#else
  printf("3mm: CPU  %.3f ms\n", t1 - t0);
  save_binary("bin/c_out.bin", G, 7040000);
#endif

  return 0;
}