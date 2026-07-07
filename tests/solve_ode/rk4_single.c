// #include <omp.h>
#include <stdio.h>

/*
 * 四阶 Runge-Kutta 方法解常微分方程数值
 **/

/*
 * y' = f(t, y)
 * 这里以
 * y' = t * y 为例
 * */
double f(double t, double y) {
    return t * y;
}

/*
 * 单步 RK4
 * */

double rk4_step(double t, double y, double h) {
    double k1 = f(t ,y);
    double k2 = f(t + h/2.0 , y+k1*(h/2.0));
    double k3 = f(t + h/2.0 , y+k2*(h/2.0));
    double k4 = f(t+h, y + h * k3);

    return y + h * (k1 + 2*k2 + 2*k3 + k4);
}

double solve_ode(double t0, double y0, double h, double t_end) {
    double t = t0;
    double y = y0;

    // 这里是不能并行化的，是迭代的求y,
    while ( t < t_end - 1e-12 ) {
        double step = h;

        if ( t + step > t_end ) {
            step = t_end - t;
        }

        y = rk4_step(t, y, step);
        t += step;
    }

    return y;
}

int main() {
    double t0 = 0.0;
    double h = 0.00000001;
    double t_end = 2.0;

    // 多不同的初始值
    double y0_list[] = {
        1.0, 2.0, 3.0, 4.0,
        5.0, 6.0, 7.0, 8.0
    };

    int n = sizeof(y0_list) / sizeof(double);

    double result[n];

    for(int i = 0; i < n; i++) {
        result[i] = solve_ode(t0, y0_list[i], h, t_end);
    }

//     printf("OMP Threads: %d\n", omp_get_max_threads());
    printf("t_end = %.6f\n", t_end);
    printf("y0, y(t_end)\n");

    for(int i=0; i<n; i++) {
        printf("%.6f, %.10f\n", y0_list[i], result[i]);
    }
}
