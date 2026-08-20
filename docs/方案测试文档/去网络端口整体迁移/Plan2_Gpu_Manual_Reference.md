# 方案2 GPU 化：手写参考版实验计划

> 目标：验证"一个 GPU 线程跑一个完整仿真实例"是否可行。
> 输入：`output/merged_sim`（已合并、无 CPU UDP 网络通信的飞控+仿真）。
> 映射：**整个 merged_sim（while 锁步循环 + 内部飞控 step）作为一个整体，
> 串行地跑在一个 kernel 线程里**；实例内部不做任何并行化，GPU 只把 N 个
> 这样的线程并行跑（threadIdx.x = instance_id）。
> 验收标准：**容差比对**（CUDA libm 与 glibc 最后一位 ulp 有差异，不追求逐字节）。

## 1. 现状盘点（GPU 友好度）

| 项 | 状态 |
|---|---|
| 飞控 11 个模块（estimator/guidance/autopilot/safety/scheduler…） | 纯计算，0 malloc / 0 文件 I/O，无全局可变 static |
| env 热路径（update_truth RK4、build_sensor_frame、sensor_*、fault_injection、通信延迟/乱序） | 纯计算 |
| RNG | xorshift64*（common/src/random.c），每实例一个状态即可移植 |
| 数学函数 | 292 处 sin/cos/sqrt/exp/… 全在 config.json safe_functions |
| malloc/fopen | 只在 init 期加载器（config.c / map_tile.c / aero_database.c / aero_surrogate.c） |
| 循环外收尾（summary.json / run_manifest.json / 表头） | 本就 host-only，保留 |

## 2. 需要的操作（按工作量排序）

### 2.1 热路径文件 I/O → 设备缓冲 + host 回放（最大工作量）

主循环 env_app.c:2910-3121 内每步写 5 条流：
write_sensor_log / write_command_log / fc_runner_log_step(internal_log) /
write_trajectory_row / write_diagnostics_row + 周期 fflush + 异常路径 write_event。

方案（二进制日志可逐字节无损）：
- 二进制日志：packet_encode_sensor_frame / packet_encode_control_command /
  write_internal_log（fc_app.c:391，92 字节记录）是纯编码函数 →
  设备端编码进 [instances][max_steps] 字节缓冲，host 端 fwrite 原字节。
  max_steps = max_time/dt（baseline：0.01s × 120s = 12000 步），可预分配。
- CSV：缓冲原始 double（每步 ~28 个 ≈ 224B）→ host 格式化；
  或设备端 snprintf（CUDA 支持，%.6f/%.17g 与 glibc 一致性需验证）。
- event_log / stdout：设备端记 (time, event, detail) 三元组 → host 回放。
  stdout 不影响比对（文档 §4.9）。
- fclose / fflush / 表头 / 行格式：全部 host 侧。

### 2.2 地形懒加载 LRU 缓存改造（隐藏的热路径磁盘 I/O！）

terrain_get_height_from_cache（terrain_model.c:353）在仿真循环内调用
cache_load_path → map_tile_load_file（fopen/fread），是唯一进入热路径的磁盘读。
- host 端 kernel 前预热：所有瓦片一次读入 → 拷到设备只读区。
- 设备端只留"直接索引查询"（去掉路径数组和 LRU 逻辑）。
- 验证：CPU 懒加载计数 load_count/eviction_count 与预热版最终值应一致
  （capacity≥瓦片数时无淘汰），在 summary.json 比对中确认。

### 2.3 每实例状态收集 + 共享只读数据上台

- 收集全部可变状态：EnvTruthState、EnvSensorState、fault 状态、通信延迟/
  乱序、地形缓存、FlightController、diagnostic/fault stats、seq、hit、exit_reason。
- 陷阱：AeroDatabase 非纯只读——last_valid/have_last_valid
  （HOLD_LAST_VALID 策略，aero_database.h:60-61）是每实例可变状态；
  样本数组单独拎出共享。
- 只读共享（瓦片采样、气动样本、surrogate、earth 模型、两份 config）→
  __constant__ / 只读全局，一次性 H2D。

### 2.4 主循环 → kernel（thread = instance）

- while (status==SIM_OK && time<=max_time) 变成 kernel 体，各线程独立
  结束（hit/碰撞 break）——线程发散无碍，无 shared memory、无 syncthreads。
- 整条调用链标 __device__：env_app.c ~15 个 static 函数 + fc 全部模块。
- 栈/寄存器压力是主要性能风险：build_sensor_frame、update_truth 等局部量
  大 → 寄存器溢出 local memory。对策：__launch_bounds__、把大局部量收进
  per-instance state。

### 2.5 Init/cleanup 留在 host

配置解析、schema 校验、地形/气动表加载、fc_runner_init、make_run_dirs 全在
host；kernel 结束后 host 写 summary/manifest/回放日志。

### 2.6 数值一致性

- 关 -use_fast_math，保留双精度。
- 验收 = 容差比对（已确认），不与 CPU 版逐字节比较。

## 3. 实施步骤（顺序执行）

1. **编译通过性实验**：机械地把全部调用图标 __device__，nvcc 编译，
   确认整个调用图可编成 device 代码（不做任何功能改造）。
2. **日志缓冲化 + 地形预热**：改造 env_app.c 主循环 + terrain_model.c
   （改动最集中的两处）。
3. **kernel 化**：merged_gpu.cu（host init/launch/flush + kernel）。
4. **容差比对验证**：对 merged_sim CPU 输出。
5. **性能测量**：占用率、local memory 溢出量、单实例耗时 vs CPU 核、
   多实例扩展性。

## 4. 产物布局（规划）

```
output/merged_sim_gpu/
├─ merged_gpu.cu        # host: 参数解析/init/H2D/launch/flush/summary
│                       # + __global__ sim_instance_kernel(threadIdx.x=instance_id)
└─ 复用 merged_sim 的 .c 源码（标 __device__ 后由 nvcc 编）
```

比对基准：`output/merged_sim` 的 CPU 输出（runs/merged_dev_001 等）。

## 5. 执行日志

### M1 编译通过性实验（完成 2026-08-14）

**结论：整个实例的循环调用图可编译、链接并在 GPU 上运行。**

- 产物：`output/merged_sim_gpu/experiment/`
  - `mark_device.py`：把清单内 .c 文件的函数标 `__host__ __device__`
    （deny 列表 = host 侧 I/O/配置加载函数，全部剥离成 stub；跨文件重名
    static 自动改名；多行签名函数体按括号配平整段替换）。
  - `dev/`：38 个标注后的设备源码 + 2 个手写设备变体
    （`map_tile_dev.c` 直接索引查询、`terrain_dev.c` 无懒加载直查）。
  - `smoke.cu`：单 TU 合并全部设备源码，dummy kernel 实例化完整调用图：
    terrain 碰撞/AGL/LOS → fault_injection → build_sensor_frame →
    flight_controller_step → update_truth → update_geodetic_state →
    诊断统计 → packet_encode（sensor/command）。
- 构建：`nvcc -arch=sm_86 -std=c++17`，运行输出 `smoke OK: status=2 seq=0 mode=0`
  （status=2 为全零初始状态的预期失败，kernel 本身成功执行）。
- 资源占用：164 寄存器/线程，0 spill，624B 栈帧（单步版本）。
- 过程中发现并修复：
  - host 加载器调用共享助手（vec3/crc32/validate 等）→ 助手标 shared 即可。
  - 调用图缺口：`propulsion_model.c`、`ring_buffer.c` 未在首批清单。
  - 设备端 strcmp/snprintf 走 host 声明（#20011 warning，待 M2 验证
    运行时行为；CUDA 设备端支持两者）。
  - 多行函数签名导致 body 剥离失败 → 括号配平需等 in_body 后 depth==0。

### M2 完成（2026-08-15）热路径日志缓冲化 + 地形预热

**结论：merged_gpu_sim 与 CPU 参考容差级一致（hit=1 / 978 步 / miss=0.353），
M2a/M2b/M2c 全部落地。**

- 现状：experiment/ 已有完整 M2/M3 骨架（merged_gpu_kernel.cu 单 TU 含
  dev/ 全部设备源码；merged_gpu_main.cu host 初始化/回放；gpu_sim.h
  共享缓冲结构）。日志缓冲、92B 内部记录编码、地形预热
  host→H2D→terrain_dev 直查均已实现。
- 排障（按序修复 3 个设备端 bug）：
  1. **设备端 strcmp 静默删除**：`fc_scheduler_task_due_name` 等
     `__host__ __device__` 函数内调用 host-only `strcmp`，nvcc 设备版
     静默删除调用（#20011），恒返回 0 → 调度器永不 due → 制导/估计只跑
     第一帧 → 模式卡 COMMAND_HOLD、指令全零、287 步直线飞行撞地。
     修复：`gpu_strcmp`（手写循环，kernel 顶部定义）。
  2. **设备端 snprintf 破坏寄存器**：`fault_injection_update` 的
     transition 记录用 snprintf 写 id/target/type（host-only），设备版
     UB：transition->active 写成功但 `++(*transition_count)`、
     `fault->was_active`、`accumulate_fault` 全部失效（调试 printf 参数
     求值也被破坏——表现为 DBGU act=1 但行为 act=0）。
     修复：`gpu_strncpy`。
  3. **terrain warning_flags 未回读**：设备端 terrain_dev 直查产生的
     `warning_flags`（TERRAIN_WARNING_MISSING_TILE）只写设备内存，
     host 端 summary 用独立加载的 terrain_host（warning_flags=0）
     → `model_degradation_flags_or` 缺 TERRAIN_WARNING。
     修复：SimInstanceState 加 `terrain_warning_flags`，kernel 结束时
     回写，host 写 summary 前赋给 terrain_host。
  - 附带修复：Makefile 的 kernel.o 依赖缺失 dev/ 源码（改 dev 文件不
    触发重编，排障中造成假象）。
- 验证（vs `runs/merged_dev_001` instance_0000 + `--instance-id 1`
  独立 CPU 参考）：
  - summary：全字段一致（仅 max_dcm_orthogonality_error 1e-15 ULP 级）
  - event_log.txt：字节级一致
  - trajectory.csv：一致（仅 force_b_y_n 1e-6 N 末位）
  - sensor/command/internal 日志：double 尾数末位（ULP）级差异
    （sensor 619/978 条、command accel 字段、internal 4.6e-05 相对差）
  - 性能：2 实例 333ms（kernel 部分）
- 遗留：fc_scheduler_add_task 的 snprintf 仅 host 路径调用（安全）；
  glibc 位精确移植已按用户决定搁置（dev/glibc_port 存档）。

### M3-M5 完成（2026-08-15）kernel 化 + 容差比对 + 性能测量

**结论：merged_gpu_sim 已是完整可用的 GPU 版本；1024 实例 100%
正确（hit/978 步），kernel 加速 ~124×。**

- kernel 化：host init/H2D/launch/flush/summary 全流程在
  merged_gpu_main.cu（分批启动，见下）。
- 性能（RTX 3050 Laptop 4GB, sm_86）：

  | 实例数 | kernel | wall | 每实例 |
  |---|---|---|---|
  | 2 | 313 ms | 1.4 s | 157 ms |
  | 8 | 320 ms | 0.9 s | 40 ms |
  | 32 | 477 ms | 1.9 s | 15 ms |
  | 128 | 958 ms | 5.2 s | 7.5 ms |
  | 256 | 1948 ms | 9.7 s | 7.6 ms |
  | 512 | 3795 ms | 22.2 s | 7.4 ms |
  | 1024 | 7769 ms | 46.9 s | 7.6 ms |

  - 256+ 后线性扩展（每 SM 1 block × 256 线程，block 数 = SM 数）。
  - CPU 单实例 ~0.94 s（978 步）→ 1024 实例串行 ~962 s：
    **kernel 加速 ~124×**；wall 加速 ~20×（host 回放写出 ~39 s 是
    当前瓶颈，kernel 仅 7.8 s）。
  - 资源：255 寄存器/线程，8840 B spill stores（state 局部过大），
    7504 B 栈帧；每 SM 256 线程（1 block）。
- 显存适配（4 GB 卡 1024 实例）：
  - `GPU_MAX_EVENTS` 8192→512（事件缓冲 -2 GB）。
  - launch 改 `<<<(n+255)/256, 256>>>`（原 1 block 上限 256 线程，
    512+ 启动失败）。
  - 分批执行：每批 ≤256 实例（truth_states 缓冲 11.7 MB/实例 × 1002
    步，256 批 ≈ 3 GB），批间复用 inst_dev/buffers_dev。
  - 每批独立 分配→H2D→launch→D2H→staging→释放；buffers_dev 每批
    重拷（含 dev 指针域）。
- 遗留：host 写出（文件 IO + D2H）占 wall 39/47 s，可后续用流
  （cudaMemcpyAsync + 双缓冲）或并行写出优化。

### M6 参数化测试完成（2026-08-17）→ 详见 [Gpu_Param_Test_Report.md](Gpu_Param_Test_Report.md)

- 12/12 参数化用例全 PASS（场景 ×4、故障 ×3、制导 ×4、多 seed ×1），
  容差内与 CPU 一致；event_log 逐字节一致；回归脚本 `experiment/param_test.py`。
- 重要发现：/workspace 为 9p 慢盘，CPU 单实例 ~1.2 s 中约 1.18 s 是写盘
  （user 仅 20–40 ms）；真实计算加速 ~3–5×，端到端同盘 ~1.4×。
  输出到本地快盘 /tmp 时 1024 实例 GPU wall 46.9 s。

### 遗留记录（2026-08-15）
- kernel 化：host init/H2D/launch/flush + summary；容差比对；性能测量。
