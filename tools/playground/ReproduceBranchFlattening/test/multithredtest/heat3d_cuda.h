#pragma once

double gpu_heat3d_batched(int N, int SIZE, int TSTEPS,
                          double *out_gpu_ms, double *out_total_ms);
const double* gpu_get_results(void);
int gpu_get_result_N(void);
int gpu_get_result_SIZE(void);
void gpu_free_results(void);
