/*
 * DCE defMap 覆盖 bug 验证用例
 *
 * 预期行为：t = i * 2 不应被删除，因为 arr[t] = 1 依赖它。
 * 实际行为：defMap["t"] 被后一条 t = i * 3 覆盖，
 *           导致 arr[t]=1 错误依赖 t=i*3，t=i*2 被误删。
 *
 * 语义正确性验证：
 *   原始：arr[i*2] = 1, arr[i*3] = 2  → arr[0]=1,arr[2]=1,arr[4]=1...  arr[3]=2,arr[6]=2...
 *   误删后：t=i*2 被删，arr[t]=1 中 t 会读到 i*3 的值 → arr[i*3]=1 (错误!)
 *           即 arr[0]=1,arr[3]=1,arr[6]=1...  arr[3]=2,arr[6]=2...
 *           arr[3] 和 arr[6] 等会被先赋 1 再赋 2，或先赋 2 再赋 1，取决于顺序。
 */

#include <stdio.h>

#define N 10

int arr[30];

int main()
{
    int i, t;

    for (i = 0; i < N; i++)
    {
        t = i * 2;        /* 语句A: 定义 t (第一次) — 应被 arr[t]=1 依赖 */
        arr[t] = 1;       /* 使用 t (数组写, 绝对有用) — reaching def 应是语句A */
        t = i * 3;        /* 语句B: 定义 t (第二次) — 覆盖 defMap["t"] */
        arr[t] = 2;       /* 使用 t (数组写, 绝对有用) — reaching def 是语句B */
    }

    /* 打印结果，验证语义正确性 */
    printf("arr[0]=%d (应为2)\n", arr[0]);   /* i=0: t=0*2=0,arr[0]=1; t=0*3=0,arr[0]=2 → 最终2 */
    printf("arr[2]=%d (应为1)\n", arr[2]);   /* i=1: t=1*2=2, arr[2]=1 */
    printf("arr[4]=%d (应为1)\n", arr[4]);   /* i=2: t=2*2=4, arr[4]=1 */
    printf("arr[3]=%d (应为2)\n", arr[3]);   /* i=1: t=1*3=3, arr[3]=2 */
    printf("arr[6]=%d (应为1)\n", arr[6]);   /* i=2:arr[6]=2; i=3:t=3*2=6,arr[6]=1 → 最终1 */
    printf("arr[9]=%d (应为2)\n", arr[9]);   /* i=3: t=3*3=9, arr[9]=2 */

    return 0;
}
