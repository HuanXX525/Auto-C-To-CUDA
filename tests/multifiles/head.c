#include"head.h"
#include<math.h>

float arr[N];

void init()
{
    for(int i=0;i<N;i++)
        arr[i] = 1;
}

void inlineTest(int j){
    // for (int j = 0; j < N;j++){
        arr[j] += sqrt(j);
    // }
}