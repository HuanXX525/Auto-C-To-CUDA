#include"head.h"
#include<math.h>

float arr[N];

void init()
{
    for(int i=0;i<N;i++)
        arr[i] = 0;
}

void inlineTest(){
    for (int j = 0; j < N;j++){
        arr[j] += sqrt(j);
    }
}