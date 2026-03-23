#include <math.h>

__attribute__((const)) void func(){

}
void b(){

}
void a(){
    b();
}
int arr[10][10];
int main()
{
    a();
    for (int i = 0; i < 10; i++)
    {
        for (int j = 0; j < 10;j++){
            arr[i][j] = i + j;
        }
    }
}