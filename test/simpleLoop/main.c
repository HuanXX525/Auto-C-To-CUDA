/* 简单的整数向量加法测试 */
#include "../tool.h"

// 定义规模
#define N 1024

// 1. 使用静态全局数组（不需要 malloc）
// 全局数组会自动初始化为 0
int a[N];
int b[N];
int c[N];

// 辅助函数：保存二进制


int main()
{
    // 2. 初始化数据
    for (int i = 0; i < N; i++)
    {
        a[i] = i * 2;
        b[i] = i + 10;
    }

    // 3. 待并行的核心循环 (ROSE 转换目标)
    // 计算逻辑：c[i] = a[i] ^ b[i] (按位异或，增加一点计算复杂度)
    for (int i = 0; i < N; i++)
    {
        c[i] = a[i] ^ b[i];
    }

    // 4. 保存结果用于二进制对比
#ifdef AUTOC2CUDATEST
    save_binary("bin/cuda_out.bin", c, N * sizeof(int));
#else
    save_binary("bin/c_out.bin", c, N * sizeof(int));
#endif
    return 0;
}