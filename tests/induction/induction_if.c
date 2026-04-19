#include <stdio.h>

int main() {
    int a[16]={0};

    for (int i = 0; i < 8; i++) {
        int t = i + 1;
        if (i > 2) {
            a[t] = i;
        }
    }

    for(int j=1;j<16;j++)printf("%d%s", a[j],j==15? "}\n":",");
    return 0;
}
