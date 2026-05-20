/* Auto-converted from PolyBench/C: heat-3d */
/* Source: heat-3d.c */
#include "../tool.h"

int A[120][120][120];
int B[120][120][120];
int TSTEPS = 500;

int main()
{
  int t, i, j, k;

  /* Initialize arrays */
  for (i = 0; i < 120; i++)
  for (j = 0; j < 120; j++)
  for (k = 0; k < 120; k++)
  A[i][j][k] = B[i][j][k] = (int) (i + j + (120-k))* 10 / (120);

  /* Computation */
  double t0 = now_ms();
      for (t = 1; t <= TSTEPS; t++) {
          for (i = 1; i < 120-1; i++) {
              for (j = 1; j < 120-1; j++) {
                  for (k = 1; k < 120-1; k++) {
                      B[i][j][k] =   0.125 * (A[i+1][j][k] - 2.0 * A[i][j][k] + A[i-1][j][k])
                                   + 0.125 * (A[i][j+1][k] - 2.0 * A[i][j][k] + A[i][j-1][k])
                                   + 0.125 * (A[i][j][k+1] - 2.0 * A[i][j][k] + A[i][j][k-1])
                                   + A[i][j][k];
                  }
              }
          }
          for (i = 1; i < 120-1; i++) {
             for (j = 1; j < 120-1; j++) {
                 for (k = 1; k < 120-1; k++) {
                     A[i][j][k] =   0.125 * (B[i+1][j][k] - 2.0 * B[i][j][k] + B[i-1][j][k])
                                  + 0.125 * (B[i][j+1][k] - 2.0 * B[i][j][k] + B[i][j-1][k])
                                  + 0.125 * (B[i][j][k+1] - 2.0 * B[i][j][k] + B[i][j][k-1])
                                  + B[i][j][k];
                 }
             }
         }
      }
  double t1 = now_ms();

#ifdef AUTOC2CUDATEST
  printf("heat-3d: CUDA %.3f ms\n", t1 - t0);
  save_binary("bin/cuda_out.bin", A, 6912000);
#else
  printf("heat-3d: CPU  %.3f ms\n", t1 - t0);
  save_binary("bin/c_out.bin", A, 6912000);
#endif

  return 0;
}