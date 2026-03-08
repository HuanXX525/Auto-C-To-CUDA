#include"head.h"
#define CUDA_BLOCK_X 128
#define CUDA_BLOCK_Y 1
#define CUDA_BLOCK_Z 1

__global__ void _auto_kernel_0(int arr[10])
{
  int thread_x_id;thread_x_id = blockIdx.x * blockDim.x + threadIdx.x;
  if (thread_x_id) 
    if (thread_x_id <= 10) {
      arr[1 * thread_x_id + -1] += 1 * thread_x_id + -1;
    }
}
int arr[10] = {(0), (1), (2), (3), (4), (5), (6), (7), (8), (9)};

void loop()
{
  int i;
{
{
/* Auto-generated code for call to _auto_kernel_0 */
      typedef int _narray_arr;
      _narray_arr *d_arr;
    cudaMalloc((void **) &d_arr, sizeof(int ) * 10);
    cudaMemcpy(d_arr, arr, sizeof(int ) * 10, cudaMemcpyHostToDevice);
      int CUDA_GRID_X;
    CUDA_GRID_X = (10 + CUDA_BLOCK_X - 1)/CUDA_BLOCK_X;
      int CUDA_GRID_Y;
    CUDA_GRID_Y = (1 + CUDA_BLOCK_Y - 1)/CUDA_BLOCK_Y;
      int CUDA_GRID_Z;
    CUDA_GRID_Z = (1 + CUDA_BLOCK_Z - 1)/CUDA_BLOCK_Z;
    const dim3 CUDA_blockSize(CUDA_BLOCK_X, CUDA_BLOCK_Y, CUDA_BLOCK_Z);
    const dim3 CUDA_gridSize(CUDA_GRID_X, CUDA_GRID_Y, CUDA_GRID_Z);
    _auto_kernel_0<<<CUDA_gridSize,CUDA_blockSize>>>(d_arr);
    cudaMemcpy(arr, d_arr, sizeof(int ) * 10, cudaMemcpyDeviceToHost);
    }
  }
}
