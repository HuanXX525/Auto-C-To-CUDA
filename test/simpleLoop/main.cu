/* 简单的整数向量加法测试 */
#include "../tool.h"
// 定义规模
#define N 1024
// 1. 使用静态全局数组（不需要 malloc）
// 全局数组会自动初始化为 0
#define CUDA_BLOCK_X 128
#define CUDA_BLOCK_Y 1
#define CUDA_BLOCK_Z 1
#define AUTOC2CUDATEST

__global__ void _auto_kernel_1(int a[1024],int b[1024],int c[1024])
{
  int thread_x_id;thread_x_id = blockIdx.x * blockDim.x + threadIdx.x;
  if (thread_x_id) 
    if (thread_x_id <= 1024) {
      c[1 * thread_x_id + -1] = a[1 * thread_x_id + -1] ^ b[1 * thread_x_id + -1];
    }
}

__global__ void _auto_kernel_0(int a[1024],int b[1024])
{
  int thread_x_id;thread_x_id = blockIdx.x * blockDim.x + threadIdx.x;
  if (thread_x_id) 
    if (thread_x_id <= 1024) {
      a[1 * thread_x_id + -1] = (1 * thread_x_id + -1) * 2;
      b[1 * thread_x_id + -1] = 1 * thread_x_id + -1 + 10;
    }
}
int a[1024];
int b[1024];
int c[1024];
// 辅助函数：保存二进制

int main()
{
  int i_nom_1;
  int i;
{
/* Auto-generated code for call to _auto_kernel_0 */
    typedef int _narray_a;
    _narray_a *d_a;
    cudaMalloc((void **) &d_a, sizeof(int ) * 1024);
    cudaMemcpy(d_a, a, sizeof(int ) * 1024, cudaMemcpyHostToDevice);
    typedef int _narray_b;
    _narray_b *d_b;
    cudaMalloc((void **) &d_b, sizeof(int ) * 1024);
    cudaMemcpy(d_b, b, sizeof(int ) * 1024, cudaMemcpyHostToDevice);
    int CUDA_GRID_X;
    CUDA_GRID_X = (1024 + CUDA_BLOCK_X - 1)/CUDA_BLOCK_X;
    int CUDA_GRID_Y;
    CUDA_GRID_Y = (1 + CUDA_BLOCK_Y - 1)/CUDA_BLOCK_Y;
    int CUDA_GRID_Z;
    CUDA_GRID_Z = (1 + CUDA_BLOCK_Z - 1)/CUDA_BLOCK_Z;
    const dim3 CUDA_blockSize(CUDA_BLOCK_X, CUDA_BLOCK_Y, CUDA_BLOCK_Z);
    const dim3 CUDA_gridSize(CUDA_GRID_X, CUDA_GRID_Y, CUDA_GRID_Z);
    _auto_kernel_0<<<CUDA_gridSize,CUDA_blockSize>>>(d_a, d_b);
    cudaMemcpy(a, d_a, sizeof(int ) * 1024, cudaMemcpyDeviceToHost);
    cudaMemcpy(b, d_b, sizeof(int ) * 1024, cudaMemcpyDeviceToHost);
  }
{
/* Auto-generated code for call to _auto_kernel_1 */
    typedef int _narray_a;
    _narray_a *d_a;
    cudaMalloc((void **) &d_a, sizeof(int ) * 1024);
    cudaMemcpy(d_a, a, sizeof(int ) * 1024, cudaMemcpyHostToDevice);
    typedef int _narray_b;
    _narray_b *d_b;
    cudaMalloc((void **) &d_b, sizeof(int ) * 1024);
    cudaMemcpy(d_b, b, sizeof(int ) * 1024, cudaMemcpyHostToDevice);
    typedef int _narray_c;
    _narray_c *d_c;
    cudaMalloc((void **) &d_c, sizeof(int ) * 1024);
    cudaMemcpy(d_c, c, sizeof(int ) * 1024, cudaMemcpyHostToDevice);
    int CUDA_GRID_X;
    CUDA_GRID_X = (1024 + CUDA_BLOCK_X - 1)/CUDA_BLOCK_X;
    int CUDA_GRID_Y;
    CUDA_GRID_Y = (1 + CUDA_BLOCK_Y - 1)/CUDA_BLOCK_Y;
    int CUDA_GRID_Z;
    CUDA_GRID_Z = (1 + CUDA_BLOCK_Z - 1)/CUDA_BLOCK_Z;
    const dim3 CUDA_blockSize(CUDA_BLOCK_X, CUDA_BLOCK_Y, CUDA_BLOCK_Z);
    const dim3 CUDA_gridSize(CUDA_GRID_X, CUDA_GRID_Y, CUDA_GRID_Z);
    _auto_kernel_1<<<CUDA_gridSize,CUDA_blockSize>>>(d_a, d_b, d_c);
    cudaMemcpy(a, d_a, sizeof(int ) * 1024, cudaMemcpyDeviceToHost);
    cudaMemcpy(b, d_b, sizeof(int ) * 1024, cudaMemcpyDeviceToHost);
    cudaMemcpy(c, d_c, sizeof(int ) * 1024, cudaMemcpyDeviceToHost);
  }
// 4. 保存结果用于二进制对比
#ifdef AUTOC2CUDATEST
  save_binary("bin/cuda_out.bin",c,1024 * sizeof(int ));
#else
#endif
  return 0;
}
