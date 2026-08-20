# merged_sim → merged_gpu_sim 迁移详解 + 自动迁移方法论

- 日期：2026-08-20
- 范围：本文件分两部分：
  1. **迁移全记录**——GPU 版本（`output/merged_sim_gpu/experiment/`）在 CPU 版本（`output/merged_sim/`）基础上做了哪些改动、为什么这么做；
  2. **自动化方法论**——若对"类似 `output/Aircraft-Simu` 结构、不能直接阅读源代码、只能由自动化程序间接修改"的项目做自动 GPU 迁移，应该如何做。
- 前置阅读：[Aircraft_Simu_To_Merged_Sim.md](Aircraft_Simu_To_Merged_Sim.md)（Aircraft-Simu → merged_sim 的合并迁移）

---

# 第一部分：迁移全记录（CPU → GPU 改动清单）

## 0. 迁移目标与验收标准

- 目标：验证"**一个 GPU 线程跑一个完整仿真实例**"是否可行。N 个实例并行，实例内部不做任何并行化。
- 验收标准：**容差比对**，不是逐字节。原因：CUDA libm 与 glibc 在最后一位 ulp 有差异（sin/cos/exp 等），逐字节比对永远不可能通过。容差按字段类别设定（`miss_distance` 绝对 1e-3 m、轨迹字段相对 1e-6、event_log 逐字节）。
- 硬约束：目标卡 RTX 3050 Laptop **4 GB 显存**，需要支持 1024 实例。

## 1. 项目结构（迁移前）

```
output/merged_sim/                  # CPU 版（已合并 env+fc，无 UDP 网络通信）
├─ common/            src/*.c       # 12 个通用模块：config/crc32/logger/matrix3/
│                                   #   packet/protocol/quaternion/random/ring_buffer/
│                                   #   sim_time/status/vec3
├─ environment_sim/   src/*.c       # 22 个模块：env_app.c 主循环、actuator/aero/
│                                   #   atmosphere/earth/gravity/geo/mass/propulsion/
│                                   #   missile_plant_6dof/environment_force/
│                                   #   fault_injection/sensor_*/map_tile/terrain_model
└─ flight_control_sim/ src/*.c      # 11 个模块：fc_app.c、fc_scheduler/estimator/
                                    #   guidance_png/guidance_manager/autopilot/
                                    #   command_manager/safety_monitor/fc_modes/
                                    #   fc_health/fc_state/navigation
```

关键特征（决定迁移工作量）：

| 特征 | 对 GPU 迁移的意义 |
|---|---|
| 飞控 11 模块：纯计算，0 malloc / 0 文件 I/O，无全局可变 static | 直接可标 `__device__` |
| env 热路径（`update_truth` RK4、`build_sensor_frame`、sensor_*、fault、通信延迟/乱序）：纯计算 | 同上 |
| RNG：xorshift64*（`common/src/random.c`），每实例一个状态 | 每实例 seed 独立，天然并行 |
| 292 处 sin/cos/sqrt/exp 等数学函数 | 在 config.json `safe_functions` 清单里，设备端可用 |
| malloc/fopen 只在 init 期（config.c / map_tile.c / aero_database.c / aero_surrogate.c） | 全部留在 host |
| 循环内每步写 5 条日志流 | **最大工作量**：改设备缓冲 + host 回放 |
| 地形查询 `terrain_get_height_from_cache` 在循环内做磁盘懒加载 | **隐藏热路径**：host 预热 + 设备直查 |

## 2. 迁移策略总览

```
CPU 版:  env_app_run() 单实例进程:
         加载配置 → make_run_dirs → 初始化 → while 锁步循环(每步写 5 条日志) → 写 summary

GPU 版:  merged_gpu_main.cu (host):
         加载配置(复用原函数) → host 模型加载 → H2D → launch kernel(分批) → D2H → 回放写出

         merged_gpu_kernel.cu (device):
         __global__ sim_instance_kernel: 线程 = 实例，循环体原样串行执行，
         日志写入改为写设备缓冲（GpuBuffers），不碰任何文件。
```

三层结构：
1. **host 驱动** `merged_gpu_main.cu`（963 行）：CLI 解析、配置加载、显存分配、分批 launch、回读、回放写出。
2. **设备内核** `merged_gpu_kernel.cu`（549 行）：`sim_instance_kernel` + 设备端编码/字符串助手。
3. **设备源码树** `experiment/dev/`：41 个被机械标注的 `.c` + 2 个手写变体（`map_tile_dev.c`、`terrain_dev.c`）。

## 3. 对 CPU 源码的具体改动清单

### 3.1 函数标注（机械，`mark_device.py`，167 行）

对 41 个 `.c` 文件逐行扫描函数定义行（正则 `^(static )?(类型)( *)(名字)(\()`），
在函数名前插入 `__host__ __device__`。三类控制：

- **`"all"` 模式**（37 个文件）：全部函数标注。例：`common/src/matrix3.c`、`environment_sim/src/gravity_model.c`、`flight_control_sim/src/guidance_png.c`。
- **`("deny", [函数名...])` 模式**（4 个文件）：除名单外全部标注。deny 名单 = host-only 函数：
  - `env_app.c`：全部加载器（`load_scenario_config`/`load_runtime_config`/`load_terrain_tile_index`…）、全部写出函数（`write_*`）、`make_run_dirs`、`env_app_run`；
  - `aero_database.c`：`aero_database_load_file`/`write_file`/`unload` 及文件编解码助手；
  - `aero_surrogate.c`：`aero_surrogate_load_file`；
  - `fault_injection.c`：配置解析函数（`parse_target`/`parse_type`/`load_config`…）。
- **`STRIP`**：`env_app_run` 被整段剥离成 stub（括号配平算法定位函数体），因为 M3 时它被拆成 host init + device loop 两部分。

**跨文件重名 static 修复**（`RENAMES` 表）：单 TU 合并后同名的 static 函数冲突，
`clamp_value → clamp_value_2`、`vector_component/sample_truth → *_2`（定义和调用点一起改）。

### 3.2 手写设备变体（2 个文件，非机械）

| 文件 | CPU 原版 | 设备版 |
|---|---|---|
| `dev/environment_sim/src/map_tile_dev.c` | `map_tile.c`：`map_tile_load_file`（fopen/fread + CRC 校验） | 只保留 `map_tile_contains` / `map_tile_get_height`（双线性插值），去掉加载；host 在 kernel 前把所有瓦片读入设备内存 |
| `dev/environment_sim/src/terrain_dev.c` | `terrain_model.c`：`terrain_get_height_from_cache` 带 LRU 懒加载（`cache_load_path` → fopen/fread，循环内磁盘 I/O） | 无懒加载直接索引查询（瓦片已全部驻留），保留 warning_flags 语义 |

### 3.3 热路径文件 I/O → 设备缓冲 + host 回放（最大改动）

CPU 主循环（`env_app.c:2910-3121`）每步写 5 条流；GPU 版全部改为写设备缓冲：

| 输出流 | CPU 写出方式 | GPU 设备端 | host 回放 |
|---|---|---|---|
| `sensor_log.bin` | `write_sensor_log`（编码 SensorFrame） | `memcpy` 原始 `SensorFrame` 到缓冲 | `write_sensor_log` 逐条写出（复用原函数，字节一致） |
| `command_log.bin` | `write_command_log` | `memcpy` 原始 `ControlCommand` | `write_command_log` |
| `fc_internal_log.bin` | `write_internal_log`（92 字节记录，fc_app.c:389） | `gpu_encode_internal_record`：手写小端 `gpu_u32_le`/`gpu_f64_le` 逐字段编码 92 字节，与 CPU 布局逐字节一致 | 原样 `fwrite` |
| `trajectory.csv` | `write_trajectory_row`（格式化） | 缓冲原始 `EnvTruthState`（`gpu_store_truth_row`） | `write_trajectory_row` 格式化 |
| `trajectory_diagnostics.csv` | `write_diagnostics_row` | 同上（同一缓冲） | `write_diagnostics_row`（含诊断统计） |
| `event_log.txt` | `write_event`（时间 + 事件 + 详情） | `GpuEventRecord{time, event[40], detail[192]}`，`gpu_push_event`，`gpu_str_append` 手工拷贝字符串 | `write_event` 回放 |
| stdout `summary:` | `printf` | — | 保留 host 侧 |

关键设计（保证输出逐字节一致）：**host 侧 `merged_gpu_main.cu` 直接 `#include` 原始 `env_app.c`**，
从而复用全部 static 格式化函数（`write_event`/`write_trajectory_row`/`write_diagnostics_row`/`write_summary`/`make_run_dirs`/`write_run_manifest`…），
CSV/日志格式与 CPU 版来自同一份代码，不存在格式漂移。

事件缓冲溢出策略：`event_count >= max_events` 时置 `event_truncated=1` 丢弃（故障跳变极稀疏，实际不会发生）。

### 3.4 主循环拆分（env_app_run → host init + kernel loop）

CPU `env_app_run`（env_app.c:2424，约 700 行）被拆成：

**host 保留（merged_gpu_main.cu 镜像原逻辑）**：
- CLI 解析（新增 `--instances N`、`--output DIR`）
- `config_load_file` + `config_validate_schema` + `load_scenario_config` / `load_runtime_config`
- `fault_injection_load_config`
- `earth_model_wgs84()`、`aero_database_load_file`、`aero_surrogate_load_file`、`map_tile_load_file`
- `fc_runner_init`（每实例，从 `runtime.base_random_seed + instance_id` 派生 seed）
- kernel 后：`make_run_dirs` + 全部写出 + `write_summary`（复用原函数）

**设备端（kernel 内镜像 CPU 循环 2720-2846 初始化 + 2914-3120 锁步循环）**：
- 初始化：LLA→ECEF、mass_model_init、惯量/执行机构配置、`update_geodetic_state`、初始轨迹行
- `while (status==SIM_OK && time<=max_time)`：地形碰撞/AGL → hit 判定 → `fault_injection_update` →
  故障跳变事件 → `build_sensor_frame` → LOS 遮挡 → `apply_communication_reorder/delay` →
  `flight_controller_step` → 日志入缓冲 → `update_truth` → `update_geodetic_state` → 轨迹行
- 退出时把 `hit/seq/exit_id/terrain_warning_flags/fault_stats/state` 写回 `SimInstanceState`

**状态收集**（`gpu_sim.h` 的 `SimInstanceState`，187 KB/实例）：
`EnvTruthState + EnvSensorState + FlightController + FaultInjection + 通信延迟/乱序状态 + FaultRunStats + instance_seed + hit/seq/exit_id/terrain_warning_flags`。
这是"每实例可变状态"全集——CPU 版散落在局部变量/静态变量里，GPU 版集中成 struct，kernel 从 `inst[instance]` 取。

**只读共享数据上台**（一次性 H2D）：
- `EnvScenarioConfig × N`（每实例一份，因为内部指针指向设备端 aero 数据）
- `EarthModel`、`TerrainModel`（tiles 指针指向设备瓦片数组）
- `AeroDatabase × N`（**陷阱**：`last_valid/have_last_valid` 是每实例可变状态，HOLD_LAST_VALID 策略要用；样本数组单独拎出共享）
- `AeroSurrogateModel`、瓦片采样数组（int16_t 连续区，每瓦片 offset 索引）

### 3.5 显存适配（4 GB 卡，1024 实例）

- **分批执行**：每次 launch 最多 256 实例（`batch_size=256`），批间复用 `inst_dev/buffers_dev`，日志缓冲每批独立分配→H2D→launch→D2H→释放。
- 缓冲容量：`max_steps = max_time/dt + 2 = 12002`（baseline 场景 120 s / 0.01 s）。
  每实例缓冲：sensor 200 B×12002 ≈ 2.4 MB、command 160 B×12002 ≈ 1.9 MB、truth 976 B×12002 ≈ 11.7 MB、
  internal 92 B×12002 ≈ 1.1 MB、events 512×240 B ≈ 122 KB → 每实例约 17 MB。
- **`GPU_MAX_EVENTS` 8192→512**：事件记录 240 B/条，8192 条/实例 ≈ 2 MB×1024 实例 ≈ 2 GB，砍到 512（1024 实例事件区共 122 MB）。实际运行最多 5 条事件，512 富余。
- **launch 配置**：`<<<(n+255)/256, 256>>>`（原单 block 256 线程上限，512+ 实例启动失败）。
- **寄存器压力**：255 寄存器/线程、8840 B spill stores、7504 B 栈帧 → 每 SM 只能驻 1 block × 256 线程。这是性能上限来源（256+ 实例线性扩展）。

### 3.6 设备端 bug 修复（M2 排障，3 个关键修复）

| bug | 现象 | 根因 | 修复 |
|---|---|---|---|
| strcmp 静默删除 | 调度器任务永不 due → 制导/估计只跑第一帧 → 模式卡 COMMAND_HOLD、指令全零、287 步直线撞地 | `<string.h>` 的 `strcmp` 是 host-only 声明；在 `__host__ __device__` 函数里调用被 nvcc 静默删除（warning #20011），恒返回 0 | `gpu_strcmp`（手写循环，kernel TU 顶部） |
| snprintf 破坏寄存器 | fault transition 记录写成功但 `++(*transition_count)`、`was_active`、accumulate 全失效（调试 printf 参数也被破坏） | 设备端 `snprintf` 是 host-only，设备版 UB | `gpu_strncpy`（带 cap 的手写拷贝） |
| terrain warning_flags 未回读 | summary `model_degradation_flags_or` 缺 TERRAIN_WARNING | 设备端直查写设备内存的 warning_flags，host summary 用独立加载的 terrain_host（flags=0） | `SimInstanceState` 加 `terrain_warning_flags` 字段，kernel 结束回写，host 写 summary 前赋给 terrain_host |

附带修复：Makefile 的 `kernel.o` 依赖缺失 dev/ 源码（`$(wildcard $(ENV_DIR)/src/*.c)` 等），改 dev 文件不触发重编，排障中造成假象。

### 3.7 构建系统

```
Makefile: HOST_SRCS（36 个 .c，gcc -std=c11 -O2 编译）+ KERNEL_OBJ + MAIN_OBJ
          → nvcc -arch=sm_86 -std=c++17 链接
dev/ 源码由 mark_device.py 从 merged_sim_gpu 根目录生成，不进 git 语义（可重生成）
```

## 4. 验证方法（容差比对闭环）

1. **单实例对照**：CPU 跑 `--instance-id 1`（独立参考）与 GPU 1024 实例里的 instance 0/1 对比。
2. **比什么**：
   - `summary.json`：逐字段相等（除 `max_dcm_orthogonality_error` 1e-15 ULP 级）；
   - `event_log.txt`：字节级一致；
   - `trajectory.csv`：一致（仅 `force_b_y_n` 1e-6 N 末位）；
   - `sensor/command/internal` 二进制日志：double 尾数末位（ULP）级差异。
3. **多参数回归**：`experiment/param_test.py`（12 用例全 PASS，见 `Gpu_Param_Test_Report.md`）：
   场景 ×4、故障 ×3、制导 ×4、多 seed ×1，全自动 CPU/GPU 双跑 + 容差判定。

## 5. 性能结果（RTX 3050 Laptop 4 GB, sm_86）

| 实例数 | kernel | 每实例 |
|---|---|---|
| 2 | 313 ms | 157 ms |
| 8 | 320 ms | 40 ms |
| 32 | 477 ms | 15 ms |
| 128 | 958 ms | 7.5 ms |
| 256 | 1948 ms | 7.6 ms |
| 512 | 3795 ms | 7.4 ms |
| 1024 | 7769 ms | 7.6 ms |

- 256+ 线性扩展；CPU 计算 ~20–40 ms/实例（user 口径）→ 计算加速 ~3–5×。
- 注意：/workspace 是 9p 慢盘，写盘 ~1.2 s/实例 是端到端瓶颈（详见测试报告 §3.2）。

---

# 第二部分：类似 Aircraft-Simu 结构项目的自动 GPU 迁移方法论

## 0. 约束与前提

**场景设定**：
- 项目结构与 `output/Aircraft-Simu` 类似：`common/ + environment_sim/ + flight_control_sim/`，
  纯 C，配置 JSON 驱动，每实例 = 一个进程跑完整个仿真（一个 `while` 锁步主循环 + 循环内每步文件日志）。
- **不能直接阅读源代码**：人类不看代码，所有分析必须程序化。
- **只能间接修改**：通过自动化程序（脚本/工具）对源码做机械变换。

**因此方法论核心** = 可程序化分析的信号 + 机械变换 + 黑盒差分验证闭环。三个支柱缺一不可。

## 1. 总体流水线（10 步）

```
┌─ 静态分析层（可全自动）─────────────────────────────┐
│ 1. 结构探测：目录/文件清单/构建命令/入口定位          │
│ 2. 调用图与热路径分析：主循环定位、每步 I/O 调用点    │
│ 3. 函数分类：纯计算 / 加载器 / 格式化写出 / 主循环    │
├─ 变换层（可全自动，启发式+模板）────────────────────┤
│ 4. 机械标注 __host__ __device__（deny/strip/rename） │
│ 5. 单 TU 合并 + 编译闭环（错误驱动修复）              │
│ 6. 热路径 I/O → 设备缓冲 + host 回放改造             │
│ 7. kernel + host 驱动模板生成                        │
├─ 验证层（可全自动）─────────────────────────────────┤
│ 8. 差分验证闭环（同 seed 双跑 + 容差判定）           │
│ 9. 性能调优循环（寄存器/显存/分批扫描）              │
│ 10. 回归固化（多参数用例自动生成）                   │
└────────────────────────────────────────────────────┘
```

## 2. 各步的程序化做法

### 2.1 结构探测（自动）

- 扫描目录树：`*.c` 文件清单、顶层 `common/` 疑似共享库（出现频率最高）。
- 构建命令提取：`make -n` / `compile_commands.json`（本项目已有工具支持，见
  `docs/compile_commands_project_usage.md`），得到每个 .c 的编译参数 → include 路径。
- 入口定位：`main()` 定义文件；`main` → 依次调用 → 定位"主循环函数"（见 2.2）。
- 可执行文件黑盒探针：`--help` 输出、配置文件加载行为（喂合法/非法 JSON 看报错）。

### 2.2 调用图与热路径分析（自动，复用现有工具链）

现有工具链基础（Auto-C-To-CUDA，ROSE 前端）：`translate.out` 已能做
`.c → .cu` 重命名、`CUDA_BLOCK_X/Y/Z` 宏注入、`isfinite → __builtin_isfinite` 替换
（`output/Aircraft-Simu` 就是它的产物）。在此基础上增加分析 pass：

- **主循环定位启发式**：函数体内存在 `while` 且循环体内调用了 ≥2 个"写文件"符号
  （`fwrite/fprintf/fopen/fflush`），且调用点按 `sim_time`/`seq` 递增 → 主循环。
  循环体 = kernel 体候选。
- **每步 I/O 调用点识别**：在主循环体内收集全部 `fopen/fwrite/fprintf/fclose/fflush` 调用 →
  每个调用点的参数结构（写入类型）→ 生成"设备缓冲 + host 回放"映射表。
- **函数分类**（依赖调用图，全部程序化）：
  - 被主循环传递调用的 → 标 `__device__` 候选；
  - 含 `fopen/fread/malloc` 且被 init 路径调用 → host 加载器（deny）；
  - 含 `fwrite/fprintf` 且只在循环外 → host 写出（deny，供回放复用）。
  - 这个分类 ≈ `mark_device.py` 的 MANIFEST，由分析器自动生成，替代手写清单。

### 2.3 机械标注（自动）

直接复用 `mark_device.py` 的模式（本迁移已验证 38 文件 100% 可行）：

- 定义行正则标注 `__host__ __device__`；
- deny 列表由 2.2 的分类器生成；
- 跨文件重名 static：全项目符号表扫描 → 重名组内除一个外全部改名（定义 + 调用点）；
- `strip` 主循环函数本体 → 之后由模板替换为 kernel 体；
- 多行签名处理：括号配平算法（已有实现）。

### 2.4 单 TU 合并 + 编译闭环（自动）

- 把全部设备源码 `#include` 进一个 `.cu`（顺序 = 依赖拓扑，由 include 图 + 未定义符号
  解析自动排序；本迁移手工排序一次，可程序化）。
- 编译错误驱动循环：nvcc 报错 → 分类处理：
  - `#20011` host-only 函数调用（strcmp/snprintf/strcpy…）→ 自动生成设备替代
    （手写循环版 `gpu_strcmp/gpu_strncpy/gpu_str_append`，替换调用点）；
  - 未标函数/缺失头 → 加入标注/补 include；
  - 内存操作符（memcpy/memset）→ 设备端可用，无需处理。
- 终止条件：编译通过 + 一个 dummy kernel 实例化全部调用图（`smoke.cu` 模式）能运行。

### 2.5 热路径 I/O 改造（半自动，模板化）

对 2.2 生成的"每步 I/O 映射表"逐条套模板：

| CPU 模式 | 自动替换为 |
|---|---|
| `write_xxx(file, &struct)`（编码函数是纯函数） | 设备端 memcpy 原始 struct 到缓冲 + host 回放调用原编码函数 |
| `fprintf(file, "%.17g", ...)`（CSV/文本行） | 缓冲原始 struct，host 格式化（**推荐**，避免设备 snprintf 与 glibc 格式差异） |
| `write_event(file, t, str1, str2)` | 设备端 `{time, event[40], detail[192]}` 三元组 + host 回放 |
| 循环内 `fflush` | 删除（host 回放一次性写） |
| 循环内懒加载（如地形 LRU 读盘） | host 预热全部数据 H2D，设备端直查变体（模板：去掉加载逻辑，保留查询语义） |

判断"编码函数是纯函数"：函数体内无 fopen/fwrite/fprintf/全局写 → 纯函数，可设备端执行或 host 回放。

### 2.6 kernel + host 驱动模板生成（自动）

- kernel 模板：`sim_instance_kernel<<<(n+255)/256, 256>>>`，线程 = 实例；
  实例状态 = 主循环函数里全部可变状态的结构化（由调用图分析收集每个被写 struct/局部变量 → 合并为 per-instance state 模板）。
- host 驱动模板（本迁移 `merged_gpu_main.cu` 可作蓝本，约 300 行固定模式）：
  参数解析 → 配置加载（原函数）→ 模型加载（原函数）→ 每实例 seed 派生
  （`base_random_seed + instance_id`）→ H2D → 分批 launch → D2H → 回放写出（原写出函数）。
- 关键模板技巧：host 驱动 `#include` 原始主循环 .c，直接复用全部 static 格式化函数 → 输出格式零漂移。

### 2.7 显存与容量自适应（自动）

- `max_steps = max_time/dt + 2`（从配置 JSON 读出）；
- 每实例缓冲大小 = 各日志 struct 的 sizeof（编译期探针程序自动生成）；
- 分批大小 = f(可用显存)（`cudaDeviceGetMemInfo`）；
- 事件缓冲上限：先 8192，运行后查"截断标志"自动下调（本迁移 8192→512）。

### 2.8 差分验证闭环（自动，最关键）

黑盒差分（不需要读代码）：
1. 同 seed、同配置，CPU 版跑 `--instance-id N`，GPU 版跑 `--instances M`；
2. 输出目录逐文件比较：二进制日志比大小（先）+ 抽样逐字节（后）；
   CSV 比字段相对容差；summary.json 比字段绝对/相对容差；文本日志逐字节；
3. 失败 → 输出差异样本（首个不一致字段/行号/值）→ 驱动修复（回到 2.4/2.5/3.x 修复清单）；
4. 通过 → 换参数（多 seed、故障注入、边界场景）回归，用例自动生成
   （本迁移 `param_test.py`：12 用例自动双跑 + 容差判定，可作模板）。

### 2.9 性能调优循环（自动）

- 寄存器/spill 报告（nvcc `-Xptxas -v` 解析）→ 超阈值提示（255 寄存器是硬上限）；
- 每 SM block 数 = 255/占用 → 决定 256 线程 block 配置；
- 分批大小扫描（2/8/32/128/256/512/1024）→ 自动产出扩展性曲线。

## 3. 自动化程度汇总

| 步骤 | 自动化程度 | 依赖 |
|---|---|---|
| 结构探测 | 全自动 | 无 |
| 调用图/分类 | 全自动 | ROSE 前端（现有 translate.out） |
| 机械标注 | 全自动 | 分类器输出 |
| 单 TU 合并 | 全自动 | 依赖拓扑排序 |
| 编译闭环 | 全自动 | nvcc 错误分类表（strcmp/snprintf 等已知模式） |
| 热路径改造 | 模板化（90% 自动） | 每步 I/O 映射表 |
| kernel/驱动生成 | 模板化（90% 自动） | 状态收集模板 |
| 差分验证 | 全自动 | 容差表 |
| 性能调优 | 全自动扫描 + 人工确认 | — |

**必须人工确认的点（很少）**：
- 设备端替代函数的行为等价性（strcmp 等，一次确认永久复用）；
- 容差阈值合理性（每类字段的量级，如 miss_distance 1e-3 vs 轨迹 1e-6）；
- 新增手写设备变体（如 terrain_dev.c 去 LRU）的语义等价（可用"最终 LRU 状态一致"断言替代人工）。

## 4. 关键风险与对策（本迁移踩过的坑，自动工具需内置）

| 风险 | 表现 | 对策（内置到工具） |
|---|---|---|
| host-only 函数静默删除（strcmp） | 行为错误但不报错，最危险 | 编译期检测：#20011 警告 → 强制替代；替代表内置 strcmp/strcpy/snprintf/strtok |
| 设备端 snprintf UB | 相邻变量被破坏，调试打印也失真 | 一律不用 snprintf，用 gpu_str_append/gpu_strncpy |
| 跨文件 static 重名 | 单 TU 合并后语义被覆盖 | 全项目符号表去重改名 |
| 加载器/写出函数误标注 | 设备端编译不过或语义错 | 分类器 deny 列表 + strip |
| 只读数据里的可变字段（AeroDatabase.last_valid） | 实例间串扰 | 分类器识别"被写字段"→ 挪进 per-instance state |
| 循环内磁盘懒加载 | 性能灾难 | 检测 fopen 在循环内 → 强制预热 |
| 显存超限 | launch 失败 | 分批 + 容量自适应 |
| 端序（小端编码日志） | 二进制日志不一致 | 显式小端编码函数（gpu_u32_le/gpu_f64_le），host 回放原样写 |
| 慢盘写盘 | 端到端被 IO 拖死 | 输出到快盘目录；回放写盘降频（后续优化方向） |

## 5. 与现有工具链的衔接建议

现有 Auto-C-To-CUDA（`translate.out` + `config.json` safe_functions）已覆盖：
`.c → .cu` 重命名、宏注入、数学函数白名单。本方法论在其**输出上继续**：
1. 新增 3 个 pass：主循环定位、I/O 调用点识别、函数分类；
2. 新增 1 个生成器：device kernel + host 驱动模板（本迁移的 `merged_gpu_*.cu` 做蓝本）；
3. 新增 1 个验证器：差分闭环（`param_test.py` 做蓝本）。
4. 已知设备端替代函数（gpu_strcmp/gpu_strncpy/gpu_str_append）沉淀为内置库。

## 6. 结论

- 手动迁移证明：**"一个 GPU 线程 = 一个实例"的迁移路径是可行的**，
  改动集中在 5 个点：函数标注（机械）、热路径日志缓冲化、主循环拆分、
  host 驱动模板、显存分批。其中前 3 点全部可程序化，后 2 点模板化。
- 自动迁移的可行性：静态分析（ROSE）+ 机械变换（mark_device.py 模式）+
  差分验证闭环（黑盒容差比对）三个支柱都有现成验证过的实现；
  真正需要"智能"的部分（主循环识别、分类、替代函数）都可以用启发式 +
  编译错误驱动循环收敛，**不需要人类阅读源码**。
- 风险最大的环节是"静默行为错误"（strcmp/snprintf 类），对策是编译期
  拦截 + 差分验证兜底——后者是黑盒约束下唯一可靠的正确性证明手段。