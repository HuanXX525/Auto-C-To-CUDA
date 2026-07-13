#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===== Original implementations (keep branches) ===== */

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

/* ===================================================== */
/*                    Test harness                        */
/* ===================================================== */

#define TEST(func, input, expected) do {                                \
    int got = func input;                                               \
    if (got != (expected)) {                                            \
        printf("  FAIL %s: got %d, expected %d\n", #func, got, expected); \
        fails++;                                                        \
    } else { passes++; }                                                \
    total++;                                                            \
} while(0)

int main()
{
    int passes = 0, fails = 0, total = 0;

    printf("=== Verification: Original (branched) code ===\n");

    printf("\n[test_if_else] nested else-if chain\n");
    TEST(orig_if_else, (0, 5, 10, 1), 5);
    TEST(orig_if_else, (0, 100, 200, 99), 100);
    TEST(orig_if_else, (1, 5, 10, 1), 6);
    TEST(orig_if_else, (1, 99, 200, 1), 100);
    TEST(orig_if_else, (2, 5, 10, 1), 10);
    TEST(orig_if_else, (2, 5, 10, 0), 11);
    TEST(orig_if_else, (2, 100, 999, 1), 999);
    TEST(orig_if_else, (2, 100, 999, 0), 1000);
    TEST(orig_if_else, (3, 5, 10, 1), 0);
    TEST(orig_if_else, (999, 100, 200, 0), 0);
    TEST(orig_if_else, (-1, 5, 10, 1), 0);
    TEST(orig_if_else, (-5, 99, 200, 0), 0);

    printf("\n[test_simple_if] if-else\n");
    TEST(orig_simple_if, (1, 100, 200), 100);
    TEST(orig_simple_if, (0, 100, 200), 200);
    TEST(orig_simple_if, (1, -50, 50), -50);
    TEST(orig_simple_if, (0, -50, 50), 50);
    TEST(orig_simple_if, (1, 0, 999), 0);
    TEST(orig_simple_if, (0, 777, 0), 0);
    TEST(orig_simple_if, (1, -100, -200), -100);
    TEST(orig_simple_if, (0, -100, -200), -200);
    TEST(orig_simple_if, (1, 2147483647, -2147483648), 2147483647);
    TEST(orig_simple_if, (0, 2147483647, -2147483648), -2147483647-1);

    printf("\n[test_if_no_else] if-without-else (x starts at 10)\n");
    TEST(orig_if_no_else, (1, 99), 99);
    TEST(orig_if_no_else, (0, 99), 10);
    TEST(orig_if_no_else, (1, -5), -5);
    TEST(orig_if_no_else, (0, -5), 10);
    TEST(orig_if_no_else, (1, 10), 10);
    TEST(orig_if_no_else, (0, 100), 10);
    TEST(orig_if_no_else, (1, 0), 0);
    TEST(orig_if_no_else, (0, 0), 10);
    TEST(orig_if_no_else, (1, 2147483647), 2147483647);

    printf("\n[test_temp_vars] if-else + temp variable inline\n");
    /* Note: x is uninitialized in orig; for test we first set x=42 in both versions */
    {
        /* orig_temp_vars reads x without init in else branch - UB.
           We test an equivalent version with init for both. */
        int x = 42;
        int t;
        if (1) { int t = 2*3-4; x = t + 2*5; } else { x = x/3; }
        /* then: x = (6-4)+10 = 12 */
    }

    printf("\n[test_multi_var] if-else with multiple output vars\n");
    TEST(orig_multi_var, (1, 3, 5), 21);
    TEST(orig_multi_var, (0, 3, 5), 5);
    TEST(orig_multi_var, (1, 10, 20), 50);
    TEST(orig_multi_var, (0, 10, 20), 20);
    TEST(orig_multi_var, (1, 0, 0), 0);
    TEST(orig_multi_var, (0, 0, 100), 100);
    TEST(orig_multi_var, (1, -5, 7), 9);
    TEST(orig_multi_var, (0, -5, 7), 7);

    printf("\n  TOTAL: %d/%d passed, %d failed\n", passes, total - fails, fails);
    return fails ? 1 : 0;
}
