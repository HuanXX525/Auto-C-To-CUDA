#include <math.h>
#include <stdio.h>
#define CUDA_BLOCK_X 128
#define CUDA_BLOCK_Y 1
#define CUDA_BLOCK_Z 1
#define AUTOC2CUDATEST

__global__ void _auto_kernel_0(int arr[10])
{
  int thread_x_id;thread_x_id = blockIdx.x * blockDim.x + threadIdx.x;
  if (thread_x_id) 
    if (thread_x_id <= 10) {
      arr[1 * thread_x_id + -1] = ((int )(pow((double )(1 * thread_x_id + -1),(double )0)));
    }
}
int arr[10] = {(1), (2), (3), (4), (5), (6), (7), (8), (9), (0)};

int get2()
{
  return 1;
}

int get3()
{
  int js = 2;
  return 1;
}

int main()
{
  int i_nom_9;
  int i_nom_8;
  int i_nom_7;
  int i;
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
/* 非同名库函数 */
  for (i_nom_7 = 1; i_nom_7 <= 10; i_nom_7 += 1) {
    printf("%d ",arr[1 * i_nom_7 + -1]);
  }
/* 测试无局部变量的无参数内联 */
  for (i_nom_8 = 1; i_nom_8 <= 10; i_nom_8 += 1) {
/* --- AUTO_CUDA_INLINE_SENTINEL --- */
;
    int rose_temp__3;
{
{
        rose_temp__3 = 1;
        goto rose_inline_end__2;
      }
      rose_inline_end__2:
      ;
    }
    arr[1 * i_nom_8 + -1] = arr[1 * i_nom_8 + -1] + rose_temp__3;
  }
/* 测试带局部变量的无参数内联 */
  for (i_nom_9 = 1; i_nom_9 <= 10; i_nom_9 += 1) {
/* --- AUTO_CUDA_INLINE_SENTINEL --- */
;
    int rose_temp__6;
{
      int js = 2;
{
        rose_temp__6 = 1;
        goto rose_inline_end__5;
      }
      rose_inline_end__5:
      ;
    }
    arr[1 * i_nom_9 + -1] += rose_temp__6;
  }
  return 0;
}
