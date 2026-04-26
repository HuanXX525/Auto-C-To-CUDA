#include "compute.h"

/* 来自 declarationCopy: 模拟 pow 函数 */
int funcpack(int value)
{
    return value * value;
}

/* 来自 branchWithFunc: 重型循环计算 */
int compute_value(int x, int y)
{
    int result = 1;
    for (int i = 0; i < y; i++) {
        result = result * (x + i);
    }
    return result;
}

/* 来自 branchWithFunc: 条件分支计算 */
int compute_value_branch(int x, int threshold)
{
    if (x > threshold) {
        return x * x + threshold;
    } else {
        return x + threshold;
    }
}

/* 来自 branchWithFunc: 数组处理 */
void compute_array(int* arr, int size)
{
    for (int i = 0; i < size; i++) {
        arr[i] = (arr[i] * 3 + 7) % 100;
    }
}