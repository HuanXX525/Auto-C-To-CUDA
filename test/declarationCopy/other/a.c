#include <math.h>
int add1(int value){
    return value + 1;
}
int funcpack(int value)
{
    int t = add1(value);
    return round(pow((double)t, 2));
}


