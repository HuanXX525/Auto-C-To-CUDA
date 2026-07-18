#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "heat3d_common.h"

static double *g_cpu_results = NULL;
static int     g_cpu_N      = 0;
static int     g_cpu_SIZE   = 0;

double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

void cpu_heat3d_single(double *A, double *B, int SIZE, int TSTEPS)
{
    double (*a)[SIZE][SIZE] = (double (*)[SIZE][SIZE])A;
    double (*b)[SIZE][SIZE] = (double (*)[SIZE][SIZE])B;
    int i, j, k, t;

    for (i = 0; i < SIZE; i++)
        for (j = 0; j < SIZE; j++)
            for (k = 0; k < SIZE; k++)
                a[i][j][k] = b[i][j][k] = (double)(i + j + (SIZE - k)) * 10.0 / (double)SIZE;

    for (t = 1; t <= TSTEPS; t++)
    {
        for (i = 1; i < SIZE - 1; i++)
            for (j = 1; j < SIZE - 1; j++)
                for (k = 1; k < SIZE - 1; k++)
                    b[i][j][k] = 0.125 * (a[i+1][j][k] - 2.0*a[i][j][k] + a[i-1][j][k])
                               + 0.125 * (a[i][j+1][k] - 2.0*a[i][j][k] + a[i][j-1][k])
                               + 0.125 * (a[i][j][k+1] - 2.0*a[i][j][k] + a[i][j][k-1])
                               + a[i][j][k];

        for (i = 1; i < SIZE - 1; i++)
            for (j = 1; j < SIZE - 1; j++)
                for (k = 1; k < SIZE - 1; k++)
                    a[i][j][k] = 0.125 * (b[i+1][j][k] - 2.0*b[i][j][k] + b[i-1][j][k])
                               + 0.125 * (b[i][j+1][k] - 2.0*b[i][j][k] + b[i][j-1][k])
                               + 0.125 * (b[i][j][k+1] - 2.0*b[i][j][k] + b[i][j][k-1])
                               + b[i][j][k];
    }
}

double cpu_heat3d_sequential(int N, int SIZE, int TSTEPS)
{
    if (g_cpu_results) free(g_cpu_results);
    g_cpu_N    = N;
    g_cpu_SIZE = SIZE;
    g_cpu_results = (double *)malloc((size_t)N * SIZE * SIZE * SIZE * sizeof(double));

    double t0 = now_ms();

    for (int r = 0; r < N; r++) {
        double *A = g_cpu_results + (size_t)r * SIZE * SIZE * SIZE;
        double *B = (double *)malloc((size_t)SIZE * SIZE * SIZE * sizeof(double));
        cpu_heat3d_single(A, B, SIZE, TSTEPS);
        free(B);
    }

    double t1 = now_ms();
    return t1 - t0;
}

double cpu_heat3d_result_at(int run, int i, int j, int k, int SIZE)
{
    if (!g_cpu_results || run >= g_cpu_N) return 0.0;
    return g_cpu_results[RUN_IDX(run, i, j, k, SIZE)];
}
