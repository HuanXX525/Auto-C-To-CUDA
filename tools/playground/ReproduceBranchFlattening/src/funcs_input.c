int orig_if_else(int n, int a, int b, int m)
{
    int x;
    if (n == 0) {
        x = a;
    } else if (n == 1) {
        x = a + 1;
    } else if (n == 2) {
        if (m == 1) { x = b; } else { x = b + 1; }
    } else {
        x = 0;
    }
    return x;
}

int orig_simple_if(int condition, int a, int b)
{
    int x = 0;
    if (condition) { x = a; } else { x = b; }
    return x;
}

int orig_if_no_else(int flag, int value)
{
    int x = 10;
    if (flag) { x = value; }
    return x;
}

int orig_temp_vars(int cond, int a, int b, int c, int d)
{
    int x;
    if (cond) {
        int t = a * b - c;
        x = t + 2 * d;
    } else {
        x = x / b;
    }
    return x;
}

int orig_multi_var(int flag, int a, int b)
{
    int x, y;
    if (flag) {
        int t = a + b;
        x = t * 2;
        y = t - a;
    } else {
        x = 0;
        y = b;
    }
    return x + y;
}
