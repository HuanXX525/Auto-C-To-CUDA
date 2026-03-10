#include "nbody.h"
#include <stdlib.h>
#include <math.h>
#include <stdio.h>

// 初始化粒子系统
void init_system(NBodySystem* system, int n, double G, double dt) {
    system->n = n;
    system->G = G;
    system->dt = dt;
    system->particles = (Particle*)malloc(n * sizeof(Particle));
    
    if (system->particles == NULL) {
        fprintf(stderr, "内存分配失败\n");
        exit(1);
    }
}

// 初始化粒子的随机位置和速度
void init_particles(NBodySystem* system) {
    for (int i = 0; i < system->n; i++) {
        // 随机位置（在 -1 到 1 之间）
        system->particles[i].x = (double)rand() / RAND_MAX * 2.0 - 1.0;
        system->particles[i].y = (double)rand() / RAND_MAX * 2.0 - 1.0;
        system->particles[i].z = (double)rand() / RAND_MAX * 2.0 - 1.0;
        
        // 随机速度（在 -0.1 到 0.1 之间）
        system->particles[i].vx = (double)rand() / RAND_MAX * 0.2 - 0.1;
        system->particles[i].vy = (double)rand() / RAND_MAX * 0.2 - 0.1;
        system->particles[i].vz = (double)rand() / RAND_MAX * 0.2 - 0.1;
        
        // 随机质量（在 0.1 到 1.0 之间）
        system->particles[i].mass = (double)rand() / RAND_MAX * 0.9 + 0.1;
    }
}

// 计算粒子间的引力并更新速度
void compute_forces(NBodySystem* system) {
    int n = system->n;
    double G = system->G;
    
    // 临时存储加速度
    double* ax = (double*)calloc(n, sizeof(double));
    double* ay = (double*)calloc(n, sizeof(double));
    double* az = (double*)calloc(n, sizeof(double));
    
    if (ax == NULL || ay == NULL || az == NULL) {
        fprintf(stderr, "内存分配失败\n");
        exit(1);
    }
    
    // 计算所有粒子对之间的引力
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            if (i == j) continue;
            
            // 计算距离分量
            double dx = system->particles[j].x - system->particles[i].x;
            double dy = system->particles[j].y - system->particles[i].y;
            double dz = system->particles[j].z - system->particles[i].z;
            
            // 计算距离平方
            double dist_sq = dx * dx + dy * dy + dz * dz;
            double dist = sqrt(dist_sq);
            
            // 避免除以零和极近距离
            if (dist < 1e-10) {
                dist = 1e-10;
                dist_sq = dist * dist;
            }
            
            // 计算引力大小 F = G * m1 * m2 / r^2
            double f = G * system->particles[i].mass * system->particles[j].mass / dist_sq;
            
            // 计算加速度分量 a = F / m
            ax[i] += f * dx / dist / system->particles[i].mass;
            ay[i] += f * dy / dist / system->particles[i].mass;
            az[i] += f * dz / dist / system->particles[i].mass;
        }
    }
    
    // 更新速度
    for (int i = 0; i < n; i++) {
        system->particles[i].vx += ax[i] * system->dt;
        system->particles[i].vy += ay[i] * system->dt;
        system->particles[i].vz += az[i] * system->dt;
    }
    
    free(ax);
    free(ay);
    free(az);
}

// 更新粒子位置
void update_positions(NBodySystem* system) {
    for (int i = 0; i < system->n; i++) {
        system->particles[i].x += system->particles[i].vx * system->dt;
        system->particles[i].y += system->particles[i].vy * system->dt;
        system->particles[i].z += system->particles[i].vz * system->dt;
    }
}

// 模拟一步
void step(NBodySystem* system) {
    compute_forces(system);
    update_positions(system);
}

// 输出粒子信息
void print_particles(NBodySystem* system) {
    printf("粒子数量: %d\n", system->n);
    printf("时间步长: %.6f\n", system->dt);
    printf("万有引力常数: %.6f\n\n", system->G);
    
    for (int i = 0; i < system->n; i++) {
        printf("粒子 %d:\n", i);
        printf("  位置: (%.6f, %.6f, %.6f)\n", 
               system->particles[i].x, 
               system->particles[i].y, 
               system->particles[i].z);
        printf("  速度: (%.6f, %.6f, %.6f)\n", 
               system->particles[i].vx, 
               system->particles[i].vy, 
               system->particles[i].vz);
        printf("  质量: %.6f\n", system->particles[i].mass);
    }
}

// 释放系统内存
void free_system(NBodySystem* system) {
    if (system->particles != NULL) {
        free(system->particles);
        system->particles = NULL;
    }
}