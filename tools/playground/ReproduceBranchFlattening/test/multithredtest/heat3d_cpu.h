#pragma once
#include "heat3d_common.h"

void cpu_heat3d_single(double *A, double *B, int SIZE, int TSTEPS);
double cpu_heat3d_sequential(int N, int SIZE, int TSTEPS);
double cpu_heat3d_result_at(int run, int i, int j, int k, int SIZE);
double now_ms(void);
