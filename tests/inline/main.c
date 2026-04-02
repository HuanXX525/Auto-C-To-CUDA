#include <stdio.h>

void func();
int arr[5] = {0, 0, 0, 0, 0};

// zhushi 

int main()
{
    /* 注释 */

    for (int i = 0; i < 5; i++)
    {
        func();
    }
    for(int i=0;i<5;i++){
        printf("%d ",arr[i]);
    }
    return 0;
}

void func()
{
    int i = 5;
    arr[0] = 0;
}