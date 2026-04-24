#include <stdio.h>

void touch(int x) {
    printf("%d\n", x);
}

int main() {
    int a[16]={0};

    for (int i = 0; i < 8; i++) {
        int t = i + 1;
        touch(t);
        a[t] = i;
    }

    for(int j=0;j<16;j++) printf("%d%s", a[j],j==15? "}\n": ",");
    return 0;
}
