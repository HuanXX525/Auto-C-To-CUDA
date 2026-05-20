/* Auto-converted from PolyBench/C: correlation */
/* Source: correlation.c */
#include "../tool.h"
#include <math.h>

int data[1400][1200];
int corr[1200][1200];
int mean[1200];
int stddev[1200];

int main()
{
  int i, j, k;
int eps = 0.1;
  int float_n;

  /* Initialize arrays */
  for (i = 0; i < 1400; i++)
  for (j = 0; j < 1200; j++)
  data[i][j] = (int)(i*j)/1200 + i;

  /* Computation */
  double t0 = now_ms();
    for (j = 0; j < 1200; j++)
      {
        mean[j] = 0.0;
        for (i = 0; i < 1400; i++)
  	mean[j] += data[i][j];
        mean[j] /= float_n;
      }
  
  
     for (j = 0; j < 1200; j++)
      {
        stddev[j] = 0.0;
        for (i = 0; i < 1400; i++)
          stddev[j] += (data[i][j] - mean[j]) * (data[i][j] - mean[j]);
        stddev[j] /= float_n;
        stddev[j] = sqrt(stddev[j]);
        stddev[j] = stddev[j] <= eps ? 1.0 : stddev[j];
      }
  
    for (i = 0; i < 1400; i++)
      for (j = 0; j < 1200; j++)
        {
          data[i][j] -= mean[j];
          data[i][j] /= sqrt(float_n) * stddev[j];
        }
  
    for (i = 0; i < 1200-1; i++)
      {
        corr[i][i] = 1.0;
        for (j = i+1; j < 1200; j++)
          {
            corr[i][j] = 0.0;
            for (k = 0; k < 1400; k++)
              corr[i][j] += (data[k][i] * data[k][j]);
            corr[j][i] = corr[i][j];
          }
      }
    corr[1200-1][1200-1] = 1.0;
  double t1 = now_ms();

#ifdef AUTOC2CUDATEST
  printf("correlation: CUDA %.3f ms\n", t1 - t0);
  save_binary("bin/cuda_out.bin", corr, 5760000);
#else
  printf("correlation: CPU  %.3f ms\n", t1 - t0);
  save_binary("bin/c_out.bin", corr, 5760000);
#endif

  return 0;
}