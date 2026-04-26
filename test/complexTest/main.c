/*
 * 复杂测试案例：融合 declarationCopy + simpleLoop + branchWithFunc 三个测试
 * - 多文件形式
 * - 循环中存在分支和函数调用
 * - 数组下标是仿射访问
 * - 使用 int arr[] 形式
 * - 可并行的重型数据计算
 */
#include "../tool.h"
#include "./other/compute.h"

#define N 1024
#define M 64

/* 使用静态全局数组 - int arr[] 形式 */
int a[N];
int b[N];
int c[N];
int data[N];
int result[N];
int temp[N];

int main()
{
    /* === 来自 simpleLoop: 向量初始化 === */
    for (int i = 0; i < N; i++)
    {
        a[i] = i * 2;
        b[i] = i + 10;
    }

    /* === 来自 simpleLoop: 核心计算循环 === */
    for (int i = 0; i < N; i++)
    {
        c[i] = a[i] ^ b[i];
    }

    /* === 来自 branchWithFunc: 初始化数据 === */
    for (int i = 0; i < N; i++)
    {
        data[i] = i;
        result[i] = 0;
        temp[i] = 0;
    }

    /* === 来自 simpleLoop: 混合最终结果 === */
    for (int i = 0; i < N; i++)
    {
        if(i < 512){
            c[i] = c[i] + result[i] + a[i];
        }else{
            c[i] = 666;
        }
    }

    /* 保存结果用于二进制对比 */
#ifdef AUTOC2CUDATEST
    save_binary("bin/cuda_out.bin", c, N * sizeof(int));
#else
    save_binary("bin/c_out.bin", c, N * sizeof(int));
#endif
    return 0;
}