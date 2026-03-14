#include <math.h>

int arr[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0};

int main(){
    for (int i = 0; i < 10; i++){
        arr[i] = pow(arr[i], 2);
    }
    for (int i = 0; i < 10; i++)
    {
        printf("%d ", arr[i]);
    }
    return 0;
}