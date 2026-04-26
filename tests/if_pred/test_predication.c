// test_predication.c
#include <stdio.h>

// ============================================
// 应该被谓词化的场景
// ============================================

// 场景 1：基本 if-else 赋值
void test_basic_if_else(int* a, int* b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] > 0) {
            b[i] = a[i];
        } else {
            b[i] = 0;
        }
        // 期望 → b[i] = (a[i] > 0) ? a[i] : 0;
    }
}

// 场景 2：仅有 if，无 else
void test_if_only(int* a, int n, int threshold) {
    for (int i = 0; i < n; i++) {
        if (a[i] < threshold) {
            a[i] = threshold;
        }
        // 期望 → a[i] = (a[i] < threshold) ? threshold : a[i];
    }
}

// 场景 3：浮点条件
void test_float(float* x, float* y, int n) {
    for (int i = 0; i < n; i++) {
        if (x[i] >= 0.0f) {
            y[i] = x[i];
        } else {
            y[i] = -x[i];
        }
        // 期望 → y[i] = (x[i] >= 0.0f) ? x[i] : -x[i];  (手写 abs)
    }
}

// 场景 4：复合条件表达式
void test_compound_cond(int* a, int* b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] > 0 && a[i] < 100) {
            b[i] = a[i];
        } else {
            b[i] = -1;
        }
        // 期望 → b[i] = (a[i] > 0 && a[i] < 100) ? a[i] : -1;
    }
}

// 场景 5：ReLU 激活函数（深度学习常见模式）
void test_relu(float* data, int n) {
    for (int i = 0; i < n; i++) {
        if (data[i] < 0.0f) {
            data[i] = 0.0f;
        }
        // 期望 → data[i] = (data[i] < 0.0f) ? 0.0f : data[i];
    }
}

// 场景 6：min/max 模式
void test_clamp(int* a, int n, int lo, int hi) {
    for (int i = 0; i < n; i++) {
        if (a[i] < lo) {
            a[i] = lo;
        }
        if (a[i] > hi) {
            a[i] = hi;
        }
        // 期望 → 两个 if 各自被谓词化
    }
}


// ============================================
// 不应该被谓词化的场景
// ============================================

// 反例 1：分支内有函数调用（有副作用）
void test_reject_func_call(int* a, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] < 0) {
            printf("negative at %d\n", i);  // 副作用，不能谓词化
            a[i] = 0;
        }
    }
}

// 反例 2：分支内有多条语句
void test_reject_multi_stmt(int* a, int* b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] > 0) {
            a[i] = a[i] * 2;
            b[i] = a[i] + 1;  // 两条赋值，不是简单谓词化
        }
    }
}

// 反例 3：嵌套 if
void test_reject_nested_if(int* a, int* b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] > 0) {
            if (a[i] > 100) {   // 嵌套控制流
                b[i] = 100;
            } else {
                b[i] = a[i];
            }
        }
    }
}

// 反例 4：if-else 赋值给不同左值
void test_reject_diff_lhs(int* a, int* b, int* c, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] > 0) {
            b[i] = a[i];       // 赋值给 b
        } else {
            c[i] = a[i];       // 赋值给 c，左值不同
        }
    }
}

// 反例 5：分支内有循环
void test_reject_inner_loop(int* a, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] > 0) {
            for (int j = 0; j < a[i]; j++) {  // 内层循环
                a[i]--;
            }
        }
    }
}

// 反例 6：不在循环内的 if（可选，看你是否限制只处理循环体内的 if）
void test_standalone_if(int x, int* result) {
    if (x > 0) {
        *result = x;
    } else {
        *result = -x;
    }
    // 这个是否谓词化取决于你的策略
    // 如果只关注 GPU warp divergence，可以跳过非循环内的 if
}


// ============================================
// 验证正确性
// ============================================
static void print_int_preview(const char* label, const int* data, int n) {
    int preview = n < 16 ? n : 16;
    printf("%-14s", label);
    for (int i = 0; i < preview; i++) {
        printf("%d ", data[i]);
    }
    if (n > preview) {
        printf("... ");
    }
    printf("(n=%d)\n", n);
}

static void print_float_preview(const char* label, const float* data, int n) {
    int preview = n < 16 ? n : 16;
    printf("%-14s", label);
    for (int i = 0; i < preview; i++) {
        printf("%.1f ", data[i]);
    }
    if (n > preview) {
        printf("... ");
    }
    printf("(n=%d)\n", n);
}

int main() {
    enum { TEST_SIZE = 128 };
    int a[TEST_SIZE];
    int b[TEST_SIZE] = {0};
    int a2[TEST_SIZE];
    int clamp[TEST_SIZE];
    float f[TEST_SIZE];
    float g[TEST_SIZE] = {0};
    float relu[TEST_SIZE];
    int n = TEST_SIZE;

    for (int i = 0; i < n; i++) {
        int spread = (i * 19) % 211 - 105;
        a[i] = spread;
        a2[i] = spread;
        clamp[i] = (i * 31) % 401 - 200;
        f[i] = ((float)((i * 13) % 41) - 20.0f) / 2.0f;
        relu[i] = ((float)((i * 17) % 57) - 28.0f) / 3.0f;
    }

    test_basic_if_else(a, b, n);
    print_int_preview("basic_if_else:", b, n);

    test_if_only(a2, n, 0);
    print_int_preview("if_only:", a2, n);

    test_float(f, g, n);
    print_float_preview("float_abs:", g, n);

    test_relu(relu, n);
    print_float_preview("relu:", relu, n);

    test_clamp(clamp, n, 0, 10);
    print_int_preview("clamp:", clamp, n);

    return 0;
}
