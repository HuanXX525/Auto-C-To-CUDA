#include "nbody.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char* argv[]) {
    // 设置随机种子
    srand(time(NULL));
    
    // 参数设置
    int num_particles = 100;    // 粒子数量
    double G = 1.0;             // 万有引力常数
    double dt = 0.01;           // 时间步长
    int steps = 1000;           // 模拟步数
    int print_interval = 100;   // 输出间隔
    
    // 从命令行读取参数（可选）
    if (argc >= 2) {
        num_particles = atoi(argv[1]);
        if (num_particles <= 0) {
            fprintf(stderr, "错误: 粒子数量必须大于0\n");
            return 1;
        }
    }
    
    if (argc >= 3) {
        steps = atoi(argv[2]);
        if (steps <= 0) {
            fprintf(stderr, "错误: 模拟步数必须大于0\n");
            return 1;
        }
    }
    
    printf("N-Body 粒子模拟\n");
    printf("================\n");
    printf("粒子数量: %d\n", num_particles);
    printf("模拟步数: %d\n", steps);
    printf("时间步长: %.6f\n", dt);
    printf("万有引力常数: %.6f\n\n", G);
    
    // 初始化系统
    NBodySystem system;
    init_system(&system, num_particles, G, dt);
    init_particles(&system);
    
    printf("初始状态:\n");
    print_particles(&system);
    printf("\n开始模拟...\n\n");
    
    // 开始计时
    clock_t start = clock();
    
    // 主模拟循环
    for (int i = 0; i < steps; i++) {
        step(&system);
        
        // 定期输出进度
        if (i % print_interval == 0) {
            printf("已完成 %d 步 (共 %d 步)\n", i, steps);
        }
    }
    
    // 结束计时
    clock_t end = clock();
    double elapsed_time = (double)(end - start) / CLOCKS_PER_SEC;
    
    printf("\n模拟完成！\n");
    printf("总耗时: %.6f 秒\n", elapsed_time);
    printf("平均每步耗时: %.6f 秒\n", elapsed_time / steps);
    
    // 输出最终状态（只输出前10个粒子）
    printf("\n最终状态 (前10个粒子):\n");
    int print_count = (num_particles < 10) ? num_particles : 10;
    printf("粒子数量: %d\n", system.n);
    printf("时间步长: %.6f\n", system.dt);
    printf("万有引力常数: %.6f\n\n", system.G);
    
    for (int i = 0; i < print_count; i++) {
        printf("粒子 %d:\n", i);
        printf("  位置: (%.6f, %.6f, %.6f)\n", 
               system.particles[i].x, 
               system.particles[i].y, 
               system.particles[i].z);
        printf("  速度: (%.6f, %.6f, %.6f)\n", 
               system.particles[i].vx, 
               system.particles[i].vy, 
               system.particles[i].vz);
        printf("  质量: %.6f\n", system.particles[i].mass);
    }
    
    if (num_particles > 10) {
        printf("... (省略其他 %d 个粒子)\n", num_particles - 10);
    }
    
    // 释放内存
    free_system(&system);
    
    return 0;
}