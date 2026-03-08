#include <stdio.h>
#include"head.h"

int main(){
    for (int i = 0; i < N; ++i) {
        loop();
    }

    for (int i = 0; i < N; ++i) {
        printf("%d ", arr[i]);
    }

    return 0;
}