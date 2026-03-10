#ifndef NBODY_H
#define NBODY_H

// 粒子结构体
typedef struct {
    double x, y, z;      // 位置坐标
    double vx, vy, vz;   // 速度分量
    double mass;         // 质量
} Particle;

// 系统结构体
typedef struct {
    Particle* particles; // 粒子数组
    int n;               // 粒子数量
    double G;            // 万有引力常数
    double dt;           // 时间步长
} NBodySystem;

// 初始化粒子系统
void init_system(NBodySystem* system, int n, double G, double dt);

// 初始化粒子的随机位置和速度
void init_particles(NBodySystem* system);

// 计算粒子间的引力并更新速度
void compute_forces(NBodySystem* system);

// 更新粒子位置
void update_positions(NBodySystem* system);

// 模拟一步
void step(NBodySystem* system);

// 输出粒子信息
void print_particles(NBodySystem* system);

// 释放系统内存
void free_system(NBodySystem* system);

#endif // NBODY_H