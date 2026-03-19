#include <stdio.h>

#define N 1000

float arr[N];

int main()
{
    for (int i = 0; i < N; ++i)
    {
        arr[i] = 0;
    }

    for (int i = 0; i < N; i++)
    {
        for (int j = 0; j < N; j++)
        {
            // arr[j] += sqrt(j); // 已经验证bug不是因为此处的白名单函数导致
            arr[j] += j;
        }
    }

    for (int i = 0; i < 10; i++)
    {
        printf("%f ", arr[i]);
    }

    return 0;
}