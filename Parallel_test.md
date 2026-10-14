# Aircraft-Simu 飞行实例 GPU 并行加速 — 项目需求说明

> 本文档供 AI Agent 阅读，提供项目背景、架构现状、加速需求和关键约束，以便 Agent 制定具体的 GPU 并行加速方案。

---

## 1. 需求概述

### 1.1 核心目标

将 Aircraft-Simu 飞行模拟项目的仿真实例从 CPU 端迁移到 GPU 端并行执行。项目需要大量飞行实例数据，CPU 逐个实例计算太慢，GPU 可批量并行处理多个实例，大幅缩短总运行时间。

### 1.2 具体要求

- N 个飞行实例在 GPU 上并行执行，实例间数据独立
- 单实例内的时间步迭代保持串行（步间有数据依赖）
- 转换后的输出结果必须与 CPU 基线一致
- 目标：相同数量实例在 GPU 端的运行时间显著低于 CPU 端

### 1.3 项目所在位置

- **Auto-C-To-CUDA（C→CUDA 转换器）**：`/home/yzypromax/.document/project/c2cuda/Auto-C-To-CUDA/`
- **Aircraft-Simu（飞行模拟项目）**：`external/Aircraft-Simu/`（git 子模块，GitHub: `https://github.com/hulintian/Aircraft-Simu`）
- **转换器用法**：`./build/bin/translate.out input.c -rose:o output.cu -rose:C99`
- **结果比对脚本**：`compare_runs.py`（项目根目录）

---

## 2. Aircraft-Simu 项目架构

### 2.1 整体结构

```
external/Aircraft-Simu/
├── common/               # 公共库（数学、日志、配置、协议、时间）
├── environment_sim/       # 环境仿真（弹体6DOF动力学、传感器、大气、地形）
├── flight_control_sim/   # 飞控仿真（导航、制导、控制律）
├── configs/baseline/     # 运行配置
├── tools/                # 工具集（批次运行、实例管理、日志对比、绘图）
└── tests/                # 测试用例
```

### 2.2 当前 CPU 运行架构

采用 **双进程 + UDP 通信** 模型：

```
每个飞行实例 = 2 个进程
  environment_sim     — 物理仿真（弹体运动、传感器测量、大气环境、地形碰撞）
  flight_control_sim  — 飞控仿真（状态估计、制导律、自动驾驶仪、指令构建）

进程间通过 UDP 交换数据：
  env → fc: SensorFrame  (IMU测量、位置、距离、视线方向、闭合速度)
  fc → env: ControlCommand (加速度指令、姿态指令、执行机构角度)
```

- `instance_manager` 读取 `runtime.json`，为每个实例分配 `instance_id`、UDP 端口、配置路径、随机种子
- 可配置 `PARALLEL`（多实例同时运行）或 `SEQUENTIAL`（逐个运行）
- `runtime_128.json` 配置了 128 个实例，`max_parallel_instances: 1`（实际串行）

### 2.3 环境仿真主循环

入口：`environment_sim/src/main.c` → `env_app_run()` → 主循环在 `env_app.c:2990`

```c
while (status == SIM_OK && state.time <= scenario.max_time) {
    terrain_is_surface_collision(...)       // 地形碰撞检测
    range = vec3_norm(vec3_sub(...))        // 命中判定
    fault_injection_update(...)             // 故障注入
    build_sensor_frame(&state, &sensor)     // 构建传感器帧
    send_sensor_frame(sock, &fc_addr, ...)  // ★ UDP 发送到飞控
    receive_control_command(sock, ...)      // ★ UDP 接收控制指令
    update_truth(&state, &command, ...)     // ★ 物理状态更新（计算密集）
    update_geodetic_state(&state, ...)      // ECEF → LLA 坐标转换
    write_trajectory_row(...)               // 日志
    seq++;
}
```

`update_truth`（`env_app.c:1680`）每步执行：
- `actuator_model_step × 3`（3 轴执行机构）
- `environment_force_model_evaluate`（气动力/力矩）
- `missile_plant_step`（6DOF 刚体运动方程积分，含 3×3 矩阵运算）
- `mass_model_step`（推进剂消耗）
- 3×3 惯量矩阵缩放循环（`for(row=0;row<3) for(col=0;col<3)`）

### 2.4 飞控仿真主循环

入口：`flight_control_sim/src/main.c` → `fc_app_run()` → 主循环在 `fc_app.c`

```c
for (;;) {
    receive_sensor_frame(sock, ...)          // ★ UDP 接收传感器帧
    flight_controller_step(&controller, &sensor, &command)  // ★ 飞控单步推进
    send_control_command(sock, ...)         // ★ UDP 发送控制指令
}
```

`flight_controller_step`（`fc_state.c:113`）每步执行：
- `safety_monitor_check_sensor`（安全评估）
- `estimator_update`（状态估计/导航）
- `guidance_manager_update`（制导律）
- `autopilot_update`（控制律）
- `command_manager_build`（指令构建）
- 含条件分支、状态机模式切换

### 2.5 进程间通信数据结构

定义在 `common/include/common/protocol.h`：

```c
typedef struct SensorFrame {        // env → fc
    uint32_t seq;
    double sim_time;
    double dt;
    Vec3 missile_vel_ecef_meas;    // 导弹速度
    Vec3 missile_accel_ecef_meas;  // 导弹加速度
    Vec3 missile_gyro_b_meas;      // 陀螺仪
    double missile_lat_rad_meas;   // 经纬高
    double missile_lon_rad_meas;
    double missile_height_m_meas;
    double target_range_meas;      // 目标距离
    Vec3 target_los_unit_ecef_meas; // 视线方向
    Vec3 target_los_rate_ecef_meas; // 视线角速度
    double target_closing_velocity_meas;
    uint32_t sensor_valid_flags;
    uint32_t sensor_fault_flags;
} SensorFrame;

typedef struct ControlCommand {    // fc → env
    uint32_t seq;
    double sim_time;
    Vec3 accel_cmd_ecef;           // 加速度指令
    Vec3 attitude_cmd;             // 姿态指令
    Vec3 body_rate_cmd;            // 角速度指令
    double actuator_cmd[SIM_MAX_ACTUATORS]; // 执行机构
    uint32_t command_mode;
    uint32_t command_status;
} ControlCommand;
```

### 2.6 配置文件

位于 `configs/baseline/`：

| 文件 | 内容 |
|---|---|
| `runtime.json` | 实例数量、调度模式、端口、输出目录、每实例配置 |
| `runtime_128.json` | 128 实例配置 |
| `scenario.json` | 仿真场景（最大时间、步长、命中半径、初始条件） |
| `flight_control.json` | 飞控参数（制导增益、控制律参数） |
| `faults.json` | 故障注入脚本 |

### 2.7 现有工具

| 工具 | 路径 | 功能 |
|---|---|---|
| `instance_manager` | `tools/instance_manager/src/main.c` | 启动/管理多实例进程对 |
| `batch_runner` | `tools/batch_runner/src/main.c` | 批次清单生成和运行 |
| `compare_logs` | `tools/compare_logs/src/main.c` | 日志对比 |
| `batch_stats` | `tools/batch_stats/src/main.c` | 批次统计 |
| `compare_runs.py` | 项目根目录 | CPU/GPU 运行结果比对脚本 |

---

## 3. Auto-C-To-CUDA 转换器

### 3.1 功能

基于 ROSE 编译器框架，将满足仿射约束的 C 程序自动转换为 CUDA 程序。

### 3.2 转换流程

```
参数解析 → ROSE frontend (AST) → WhileToFor → Inline → NestCollect → SSA/DCE → CodeGen → unparse(.cu)
```

### 3.3 转换能力

- 能处理：仿射循环（循环边界和数组下标为循环变量的线性函数）、无数据依赖的循环、已内联的函数调用
- 限制：非仿射循环跳过、不分析指针别名、跨函数 def-use 未分析
- 源码路径不能含 `include`（ROSE SSA 的 FunctionFilter 会跳过）

### 3.4 当前状态

- 可正常编译运行（`make debug` / `make release`）
- 已验证简单仿射 C 程序可正确转换
- 已修复 DCE 误删问题（加了 SSA 未覆盖即跳过的兜底）

---

## 4. 关键约束

1. **实例间数据独立**：不同实例的场景、种子、弹道互不依赖，天然可并行
2. **单实例内时间步串行**：步 t+1 依赖步 t 的状态，不可跨步并行
3. **正确性优先**：GPU 输出必须与 CPU 基线一致，不确定时保守处理
4. **C99 标准**：Aircraft-Simu 源码为 C99
5. **ROSE 框架**：AST 操作依赖 ROSE，SSA 分析依赖 ROSE 的 StaticSingleAssignment
6. **子模块状态**：Aircraft-Simu 当前 checkout 在远程 HEAD（`970bd...`），团队提交记录的 `76f39829...` 未推送到远程，AccuracyTests 配置缺失

---

## 5. 希望由方案解决的问题

以下问题需要方案制定者分析并给出回答：

1. **实例并行映射**：N 个飞行实例如何映射到 GPU 线程/块？
2. **UDP 通信消除**：env 和 fc 之间的 UDP 通信如何在 GPU 端替换为零开销的数据传递？
3. **计算模块转换**：Aircraft-Simu 中哪些循环适合用 Auto-C-To-CUDA 自动转换？哪些需要手动 GPU 化？为什么？
4. **数据布局**：实例状态数据如何组织以适配 GPU 内存模型？
5. **配置/日志适配**：JSON 配置加载和文件日志写入如何在 GPU 端处理？
6. **性能预期**：相比 CPU 方案，预期加速比是多少？加速来源是什么？
