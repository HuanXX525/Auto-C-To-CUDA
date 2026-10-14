# Aircraft-Simu GPU 迁移项目结构设计

> 原则：原项目（external/Aircraft-Simu/）不改一行代码。GPU 侧所有代码独立存放。

---

## 1. 整体目录树

```
Auto-C-To-CUDA/                          # 转换器主项目
├── build/bin/translate.out              # 转换器可执行
├── src/                                 # 转换器源码（不改）
├── external/Aircraft-Simu/             # 原项目（不改，CPU 基线）
│
├── gpu_workspace/                      # ★ GPU 迁移工作区（全部新增）
│   ├── converted/                       #   转换器生成的 .cu 文件
│   ├── staging/                         #   预处理后的 .c 副本（剥离依赖）
│   ├── gpu_campaign/                    #   手写调度层
│   ├── output/                          #   运行结果输出
│   └── Makefile                         #   GPU 项目构建
│
├── Parallel_test.md                    # 需求说明
├── planA.md                            # 加速方案
└── planA_modify.md                     # 方案修改建议
```

## 2. 详细结构

### 2.1 转换器输出：gpu_workspace/converted/

转换器对 staging/ 里的每个 .c 生成对应的 .cu：

```
gpu_workspace/converted/
├── common/                             # 来自 common/src/ 的转换结果
│   ├── vec3.cu                         #   vec3_add/sub/mul/dot/cross/norm...
│   ├── matrix3.cu                      #   matrix3_mul/transpose/inverse...
│   ├── quaternion.cu                    #   quaternion_mul/normalize/to_matrix...
│   └── random.cu                       #   xorshift64*/box_muller
│
├── environment/                        # 来自 environment_sim/src/ 的转换结果
│   ├── vec3.cu                         #   向量运算（已内联）
│   ├── matrix3.cu                      #   矩阵运算（已内联）
│   ├── missile_plant_6dof.cu          #   6DOF 刚体积分（内层循环已转换）
│   ├── actuator_model.cu              #   执行机构模型
│   ├── aero_model.cu                  #   气动力/力矩计算
│   ├── aero_database.cu              #   气动表查表循环
│   ├── mass_model.cu                  #   质量/推进剂消耗
│   ├── atmosphere_model.cu            #   大气模型
│   ├── gravity_model.cu               #   重力模型
│   ├── geo_coordinate.cu             #   坐标转换
│   ├── sensor_noise.cu                #   传感器噪声生成
│   ├── sensor_imu.cu                  #   IMU 传感器模型
│   ├── sensor_accel.cu               #   加速度计模型
│   ├── sensor_seeker.cu             #   导引头模型
│   ├── sensor_speed.cu              #   速度传感器模型
│   ├── environment_force_model.cu    #   环境力模型
│   ├── propulsion_model.cu           #   推进模型
│   ├── terrain_model.cu             #   地形查询（如可转换）
│   └── fault_injection.cu            #   故障注入逻辑
│
├── flight_control/                     # 来自 flight_control_sim/src/ 的转换结果
│   ├── estimator.cu                   #   状态估计
│   ├── navigation.cu                  #   导航计算
│   ├── guidance_png.cu               #   PNG 制导律
│   ├── guidance_manager.cu           #   制导管理
│   ├── autopilot.cu                  #   自动驾驶仪
│   ├── command_manager.cu            #   指令构建
│   ├── safety_monitor.cu             #   安全监控
│   └── fc_modes.cu                   #   模式管理
│
└── NOT_CONVERTED.md                    # 转换器跳过的文件清单及原因
```

### 2.2 预处理暂存：gpu_workspace/staging/

原文件复制到这里并做最小预处理（剥离 host 依赖），**不改原项目**：

```
gpu_workspace/staging/
├── common/
│   ├── vec3.c                          # 从原项目复制，删除 #include <math.h>，
│   │                                   #   替换为内置函数或自定义声明
│   ├── matrix3.c
│   ├── quaternion.c
│   └── random.c
│
├── environment/
│   ├── missile_plant_6dof.c           # 删除 printf/fprintf 调用
│   ├── actuator_model.c               # 删除 logger_info 调用
│   ├── aero_model.c
│   ├── aero_database.c                # 文件加载部分用 #ifdef GPU_BUILD 隔离
│   ├── mass_model.c
│   ├── atmosphere_model.c
│   ├── gravity_model.c
│   ├── geo_coordinate.c
│   ├── sensor_noise.c
│   ├── sensor_imu.c
│   ├── sensor_accel.c
│   ├── sensor_seeker.c
│   ├── sensor_speed.c
│   ├── environment_force_model.c
│   ├── propulsion_model.c
│   ├── terrain_model.c               # 文件加载隔离，查询逻辑保留
│   └── fault_injection.c
│
├── flight_control/
│   ├── estimator.c
│   ├── navigation.c
│   ├── guidance_png.c
│   ├── guidance_manager.c
│   ├── autopilot.c
│   ├── command_manager.c
│   ├── safety_monitor.c
│   └── fc_modes.c
│
└── headers/                           # 预处理后的头文件副本
    ├── common/
    ├── environment/
    └── flight_control/
```

### 2.3 手写调度层：gpu_workspace/gpu_campaign/

**这是唯一需要手写的代码**，不碰原项目：

```
gpu_workspace/gpu_campaign/
├── include/
│   ├── gpu_types.cuh                  # GPU 端状态结构体（DeviceInstanceState 等）
│   ├── gpu_config.cuh                 # 配置加载结构体
│   ├── gpu_trace.cuh                  # 轨迹缓冲结构体
│   └── gpu_constants.cuh              # GPU 端常量定义
│
├── src/
│   ├── main.cu                        # 入口：读配置、分配显存、启动 kernel、回传结果
│   ├── gpu_campaign.cu                # 调度器：N 个 block 并行、chunk 循环
│   ├── simulate_step.cu               # 单步逻辑：env_sensor→fc_step→env_truth
│   ├── trace_buffer.cu                # 轨迹缓冲写入和批量回传
│   ├── config_loader.c                # JSON 解析→DeviceConfig（CPU 端，用 cJSON）
│   ├── terrain_loader.c              # 地形数据加载→device buffer（CPU 端）
│   └── result_writer.c               # 回传结果→CSV/BIN/JSON（CPU 端）
│
├── CMakeLists.txt                     # 构建脚本
└── README.md                          # 构建和运行说明
```

### 2.4 输出目录：gpu_workspace/output/

运行 GPU campaign 后的结果，与 CPU 基线对比：

```
gpu_workspace/output/
├── 2026-08-04_128instances/          # 按运行时间命名
│   ├── instance_000/
│   │   ├── trajectory.csv            # 轨迹（与 CPU 格式一致）
│   │   ├── trajectory_diagnostics.csv
│   │   ├── event_log.txt
│   │   └── summary.json
│   ├── instance_001/
│   │   └── ...
│   ├── ...
│   ├── instance_127/
│   │   └── ...
│   ├── campaign_summary.json         # 整批汇总：总耗时、命中率、实例统计
│   └── timing.json                   # 性能数据：GPU 时间、回传时间、落盘时间
│
└── cpu_baseline/                     # CPU 基线结果（用于对比）
    ├── instance_000/
    │   └── ...
    └── ...
```

## 3. 数据流

```
                    CPU 端（主机）
┌──────────────────────────────────────────────┐
│ 1. config_loader.c                            │
│    读 runtime.json + scenario.json            │
│    → DeviceConfig[128]                        │
│                                               │
│ 2. terrain_loader.c                           │
│    读地形 tile 文件                           │
│    → device buffer                             │
│                                               │
│ 3. main.cu                                    │
│    分配显存、上传配置和地形                    │
│    启动 kernel                                 │
└──────────────┬───────────────────────────────┘
               │ cudaMemcpyHostToDevice
               ▼
┌──────────────────────────────────────────────┐
│              GPU 端（设备）                  │
│                                               │
│  simulate_chunk<<<128, 1, 0, stream>>>(       │
│      device_states,                           │
│      device_configs,                          │
│      device_trace,                            │
│      first_step, chunk_steps)                  │
│                                               │
│  每个 block（=1个实例）内部：                 │
│  for step in chunk:                           │
│    build_sensor_frame(...)   ← 调用 converted/ │
│    flight_controller_step(...)← 调用 converted/│
│    update_truth(...)         ← 调用 converted/ │
│    write_trace(...)          ← 写 trace_buffer │
└──────────────┬───────────────────────────────┘
               │ cudaMemcpyDeviceToHost
               ▼
┌──────────────────────────────────────────────┐
│  CPU 端（主机）                               │
│  4. result_writer.c                           │
│     trace buffer → trajectory.csv             │
│     trace buffer → diagnostics.csv            │
│     events → event_log.txt                    │
│     final state → summary.json                │
│                                               │
│  5. compare_runs.py                            │
│     GPU output/ vs CPU baseline/              │
│     → diff_report.txt                         │
└──────────────────────────────────────────────┘
```

## 4. 构建流程

```
# 1. 预处理：复制原文件到 staging/，剥离 host 依赖
cd gpu_workspace
./preprocess.sh

# 2. 转换：对 staging/ 里每个 .c 运行转换器
./convert.sh
# 等价于：
#   ../../build/bin/translate.out staging/common/vec3.c -rose:o converted/common/vec3.cu -rose:C99
#   ../../build/bin/translate.out staging/environment/missile_plant_6dof.c -rose:o converted/environment/missile_plant_6dof.cu -rose:C99
#   ...

# 3. 编译：nvcc 编译 converted/*.cu + gpu_campaign/*.cu
make

# 4. 运行
./gpu_campaign --config ../external/Aircraft-Simu/configs/baseline/runtime_128.json \
               --output output/$(date +%Y-%m-%d)_128instances

# 5. 对比
python3 compare_runs.py \
    --gpu output/2026-08-04_128instances \
    --cpu output/cpu_baseline \
    --rtol 1e-10 --atol 1e-12
```

## 5. 原项目不变约束

```
external/Aircraft-Simu/
└── （所有文件保持原样，不做任何修改）
    ├── common/          ← 不改
    ├── environment_sim/ ← 不改
    ├── flight_control_sim/ ← 不改
    ├── configs/         ← 不改（GPU 端只读）
    ├── tools/           ← 不改
    └── tests/           ← 不改
```

原项目的作用：
- CPU 基线：用 gcc 编译运行，产出 `output/cpu_baseline/`
- 转换器输入源：转换器读 `staging/` 里的副本（从原项目复制），不直接读原项目
- 配置文件来源：GPU 端读取原项目的 `configs/baseline/*.json`

## 6. 估算工作量

| 部分 | 文件数 | 来源 | 手写量 |
|---|---|---|---|
| staging/ 预处理 | ~25 个 .c | 从原项目复制 + 剥离 | 每文件改 ~5 行 |
| converted/ 转换 | ~25 个 .cu | 转换器生成 | 0 |
| gpu_campaign/ 调度层 | ~8 个 .cu/.c | 手写 | ~530 行 |
| 构建脚本 | 3 个 | 手写 | ~100 行 |
| **合计** | ~56 个文件 | | ~630 行手写 |
```
