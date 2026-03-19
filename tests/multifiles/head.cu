#include"head.h"
#include<math.h>
#define CUDA_BLOCK_X 128
#define CUDA_BLOCK_Y 1
#define CUDA_BLOCK_Z 1

__global__ void _auto_kernel_2(float arr[1000])
{
  int thread_x_id;thread_x_id = blockIdx.x * blockDim.x + threadIdx.x;
  if (thread_x_id) 
    if (thread_x_id <= 1000) {
      arr[1 * thread_x_id + -1] += ((float )(1 * thread_x_id + -1));
    }
}

__global__ void _auto_kernel_1(float arr[1000])
{
  int thread_x_id;thread_x_id = blockIdx.x * blockDim.x + threadIdx.x;
  if (thread_x_id) 
    if (thread_x_id <= 1000) {
      arr[1 * thread_x_id + -1] = ((float )0);
    }
}
float arr[1000];

void init()
{
  int i;
{
/* Auto-generated code for call to _auto_kernel_1 */
    typedef float _narray_arr;
    _narray_arr *d_arr;
    cudaMalloc((void **) &d_arr, sizeof(float ) * 1000);
    cudaMemcpy(d_arr, arr, sizeof(float ) * 1000, cudaMemcpyHostToDevice);
    int CUDA_GRID_X;
    CUDA_GRID_X = (1000 + CUDA_BLOCK_X - 1)/CUDA_BLOCK_X;
    int CUDA_GRID_Y;
    CUDA_GRID_Y = (1 + CUDA_BLOCK_Y - 1)/CUDA_BLOCK_Y;
    int CUDA_GRID_Z;
    CUDA_GRID_Z = (1 + CUDA_BLOCK_Z - 1)/CUDA_BLOCK_Z;
    const dim3 CUDA_blockSize(CUDA_BLOCK_X, CUDA_BLOCK_Y, CUDA_BLOCK_Z);
    const dim3 CUDA_gridSize(CUDA_GRID_X, CUDA_GRID_Y, CUDA_GRID_Z);
    _auto_kernel_1<<<CUDA_gridSize,CUDA_blockSize>>>(d_arr);
    cudaMemcpy(arr, d_arr, sizeof(float ) * 1000, cudaMemcpyDeviceToHost);
  }
}

void inlineTest()
{
  int j;
{
{
/* Auto-generated code for call to _auto_kernel_2 */
      typedef float _narray_arr;
      _narray_arr *d_arr;
    cudaMalloc((void **) &d_arr, sizeof(float ) * 1000);
    cudaMemcpy(d_arr, arr, sizeof(float ) * 1000, cudaMemcpyHostToDevice);
      int CUDA_GRID_X;
    CUDA_GRID_X = (1000 + CUDA_BLOCK_X - 1)/CUDA_BLOCK_X;
      int CUDA_GRID_Y;
    CUDA_GRID_Y = (1 + CUDA_BLOCK_Y - 1)/CUDA_BLOCK_Y;
      int CUDA_GRID_Z;
    CUDA_GRID_Z = (1 + CUDA_BLOCK_Z - 1)/CUDA_BLOCK_Z;
    const dim3 CUDA_blockSize(CUDA_BLOCK_X, CUDA_BLOCK_Y, CUDA_BLOCK_Z);
    const dim3 CUDA_gridSize(CUDA_GRID_X, CUDA_GRID_Y, CUDA_GRID_Z);
    _auto_kernel_2<<<CUDA_gridSize,CUDA_blockSize>>>(d_arr);
    cudaMemcpy(arr, d_arr, sizeof(float ) * 1000, cudaMemcpyDeviceToHost);
    }
  }
}
