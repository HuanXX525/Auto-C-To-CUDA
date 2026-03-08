#include"head.h"

int arr[N] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

void loop()
{
    for(int i = 0;i < N;++i){
        arr[i] += i;
    }
}
