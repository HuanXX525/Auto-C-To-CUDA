#pragma once
#include <stddef.h>

#define DEFAULT_SIZE   32
#define DEFAULT_TSTEPS 100
#define TILE_DIM       8

static inline size_t IDX3(int i, int j, int k, int SIZE) {
    return (size_t)i * SIZE * SIZE + (size_t)j * SIZE + (size_t)k;
}

static inline size_t RUN_IDX(int run, int i, int j, int k, int SIZE) {
    return (size_t)run * SIZE * SIZE * SIZE + IDX3(i, j, k, SIZE);
}
