#include <math.h>
#include <stdio.h>

int main() {
    int a[16]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    int b[16]={0};

    for (int i = 0; i < 8; i++) {
        int t = i + 1;
        a[(int)sinf((float)i)] = b[t];
    }

    printf("%d\n", a[0]);
    return 0;
}
