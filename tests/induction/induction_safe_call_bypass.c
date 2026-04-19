#include <math.h>
#include <stdio.h>

int main() {
    int a[16]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    int b[16]={0};

    for (int i = 0; i < 8; i++) {
        float s = sinf((float)i);
        int t = i + 1;
        a[t] = b[t] + (int)s;
    }

    printf("%d\n", a[1]);
    return 0;
}
