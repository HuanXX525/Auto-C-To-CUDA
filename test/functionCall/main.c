/*
 * 复杂测试案例：融合 declarationCopy + simpleLoop + branchWithFunc 三个测试
 * - 多文件形式
 * - 循环中存在分支和函数调用
 * - 数组下标是仿射访问
 * - 使用 int arr[] 形式
 * - 可并行的重型数据计算
 */
#include "../tool.h"


#define N 1024
#define M 64

/* 使用静态全局数组 - int arr[] 形式 */
int a[N];
// int b[N];
int c[N];

int complex(int i){
    return i / 2;
}

int main()
{

    for (int i = 0; i < N;i++){
        a[i] = i;
    }

    for (int i = 0; i < N;i++){
        a[i] = complex(i);
    }

    /* 保存结果用于二进制对比 */
#ifdef AUTOC2CUDATEST
    save_binary("bin/cuda_out.bin", c, N * sizeof(int));
#else
    save_binary("bin/c_out.bin", c, N * sizeof(int));
#endif
    return 0;
}