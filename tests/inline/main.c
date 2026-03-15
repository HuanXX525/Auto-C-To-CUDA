#include <stdio.h>

void func(int *arr){
    for(int i=0;i<5;i++){
        arr[i] += 10;
    }
}

int main(){
    int arr[5] = {0, 0, 0, 0, 0};
    for (int i = 0; i < 5; i++)
    {
        func(arr);
    }
    for(int i=0;i<5;i++){
        printf("%d ",arr[i]);
    }
    return 0;
}