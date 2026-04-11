/* 内联后缺失数学库声明的问题 */
#include "./other/a.h"
#include "../tool.h"
int arr[100];
int main(){
    for (int i = 0; i < 100;i++){
        // Call a function in a.c; must have a statement of function
        arr[i] = funcpack(i);
    }

    // 4. 保存结果用于二进制对比
#ifdef AUTOC2CUDATEST
    save_binary("bin/cuda_out.bin", arr, 100 * sizeof(int));
#else
    save_binary("bin/c_out.bin", arr, 100 * sizeof(int));
#endif
    return 0;
}