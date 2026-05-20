/* Auto-converted from PolyBench/C: bicg */
/* Source: bicg.c */
#include "../tool.h"

int A[2100][1900];
int s[1900];
int q[2100];
int p[1900];
int r[2100];

int main()
{
  int i, j;

  /* Initialize arrays */
  for (i = 0; i < 1900; i++)
  p[i] = (int)(i % 1900) / 1900;
  for (i = 0; i < 2100; i++) {
  r[i] = (int)(i % 2100) / 2100;
  for (j = 0; j < 1900; j++)
  A[i][j] = (int) (i*(j+1) % 2100)/2100;
  }

  /* Computation */
  double t0 = now_ms();
    for (i = 0; i < 1900; i++)
      s[i] = 0;
    for (i = 0; i < 2100; i++)
      {
        q[i] = 0.0;
        for (j = 0; j < 1900; j++)
  	{
  	  s[j] = s[j] + r[i] * A[i][j];
  	  q[i] = q[i] + A[i][j] * p[j];
  	}
      }
  double t1 = now_ms();

#ifdef AUTOC2CUDATEST
  printf("bicg: CUDA %.3f ms\n", t1 - t0);
  save_binary("bin/cuda_out.bin", s, 7600);
#else
  printf("bicg: CPU  %.3f ms\n", t1 - t0);
  save_binary("bin/c_out.bin", s, 7600);
#endif

  return 0;
}