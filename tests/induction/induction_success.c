#include <stdio.h>

int main() {
    int a[16]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    int b[16]={0};

    for (int i = 0; i < 8; i++) {
        int t = i + 1;
        a[t] = b[t];
    }

    //printf("%d\n", a[1]);
    printf("arr = {");
    for(int j=0;j<16;j++) printf("%d%s", a[j], j==15 ? "}\n" : ",");
    return 0;
}
