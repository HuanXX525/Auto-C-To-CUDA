/* Auto-converted from PolyBench/C: covariance */
/* Source: covariance.c */
#include "../tool.h"

int data[1400][1200];
int cov[1200][1200];
int mean[1200];

int main()
{
  int i, j, k;
  int float_n;

  /* Initialize arrays */
  for (i = 0; i < 1400; i++)
  for (j = 0; j < 1200; j++)
  data[i][j] = ((int) i*j) / 1200;

  /* Computation */
  double t0 = now_ms();
    for (j = 0; j < 1200; j++)
      {
        mean[j] = 0.0;
        for (i = 0; i < 1400; i++)
          mean[j] += data[i][j];
        mean[j] /= float_n;
      }
  
    for (i = 0; i < 1400; i++)
      for (j = 0; j < 1200; j++)
        data[i][j] -= mean[j];
  
    for (i = 0; i < 1200; i++)
      for (j = i; j < 1200; j++)
        {
          cov[i][j] = 0.0;
          for (k = 0; k < 1400; k++)
  	  cov[i][j] += data[k][i] * data[k][j];
          cov[i][j] /= (float_n - 1.0);
          cov[j][i] = cov[i][j];
        }
  double t1 = now_ms();

#ifdef AUTOC2CUDATEST
  printf("covariance: CUDA %.3f ms\n", t1 - t0);
  save_binary("bin/cuda_out.bin", cov, 5760000);
#else
  printf("covariance: CPU  %.3f ms\n", t1 - t0);
  save_binary("bin/c_out.bin", cov, 5760000);
#endif

  return 0;
}