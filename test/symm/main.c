/* Auto-converted from PolyBench/C: symm */
/* Source: symm.c */
#include "../tool.h"

double C[1000][1200];
double A[1000][1000];
double B[1000][1200];

int main()
{
  int i, j, k;
double temp2 = 0;
  double alpha = 1;
  double beta = 1;

  /* Initialize arrays */
  for (i = 0; i < 1000; i++)
  for (j = 0; j < 1200; j++) {
  C[i][j] = (double) ((i+j) % 100) / 1000.0;
  B[i][j] = (double) ((1200+i-j) % 100) / 1000.0;
  }
  for (i = 0; i < 1000; i++) {
  for (j = 0; j <=i; j++)
  A[i][j] = (double) ((i+j) % 100) / 1000.0;
  for (j = i+1; j < 1000; j++)
  A[i][j] = -999; //regions of arrays that should not be used
  }

  /* Computation */
  double t0 = now_ms();
     for (i = 0; i < 1000; i++)
        for (j = 0; j < 1200; j++ )
        {
          temp2 = 0;
          for (k = 0; k < i; k++) {
             C[k][j] += alpha*B[i][j] * A[i][k];
             temp2 += B[k][j] * A[i][k];
          }
          C[i][j] = beta * C[i][j] + alpha*B[i][j] * A[i][i] + alpha * temp2;
       }
  double t1 = now_ms();

#ifdef AUTOC2CUDATEST
  printf("symm: CUDA %.3f ms\n", t1 - t0);
  save_binary("bin/cuda_out.bin", C, 9600000);
#else
  printf("symm: CPU  %.3f ms\n", t1 - t0);
  save_binary("bin/c_out.bin", C, 9600000);
#endif

  return 0;
}