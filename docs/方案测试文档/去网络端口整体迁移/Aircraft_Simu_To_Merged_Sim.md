# Aircraft-Simu → merged_sim 合并迁移详解

- 日期：2026-08-20
- 范围：`output/merged_sim`（单进程锁步 CPU 版）是如何从 Aircraft-Simu 项目
  迁移出来的：动机、文件级差异、逐文件改动、保留的语义、与后续 GPU 迁移的关系。
- 前置阅读：[Gpu_Migration_Guide.md](Gpu_Migration_Guide.md)（merged_sim → merged_gpu_sim）

---

## 0. 背景：为什么要合并

Aircraft-Simu 原架构是**双进程 UDP**：

```
environment_sim ──SensorFrame/UDP──> flight_control_sim
      ^                                  |
      └──────── ControlCommand/UDP <─────┘
```

每个仿真实例需要两个进程 + 两套 UDP 端口 + 一个"心跳/锁步"握手才能闭环。
这对后续 GPU 迁移（`merged_gpu_sim`）是根本性障碍：**kernel 线程无法创建
进程、无法绑定端口、无法 recvfrom**。因此第一步必须是"**去网络化**"：
把飞控逻辑以库的形式并入环境进程，在同一个进程内完成整个闭环。

**术语澄清**：`external/Aircraft-Simu` 是原始 C 源码（git 子模块）；
`output/Aircraft-Simu` 是 Auto-C-To-CUDA 工具链的输出（`.c → .cu` 重命名 +
宏注入的翻译产物，仍无 kernel）。`output/merged_sim` 的源文件是 `.c`，
**直接派生自原始 C 源码（external/Aircraft-Simu）**，与 `.cu` 翻译产物无关。

## 1. 文件级差异总览

对 `external/Aircraft-Simu` 与 `output/merged_sim` 全树逐文件比较
（排除 build/runs/文档）：

| 类别 | 文件 | 变化 |
|---|---|---|
| 改动 | `environment_sim/src/env_app.c` | 核心：UDP 循环 → 锁步循环（-141/+76 行） |
| 改动 | `environment_sim/include/env/env_app.h` | `env_app_run` 签名 +fc_ctx |
| **新增** | `environment_sim/src/merged_main.c` | 合并入口（82 行） |
| 改动 | `environment_sim/CMakeLists.txt` | 可执行目标 → `merged_sim`，链 fc 库 |
| 改动 | `flight_control_sim/src/fc_app.c` | +131 行：抽出 `fc_runner_init` / `fc_runner_log_step` |
| 改动 | `flight_control_sim/include/fc/fc_app.h` | 新增两个 runner 接口声明 |
| 改动 | `flight_control_sim/CMakeLists.txt` | `fc_app.c` 移入静态库 |
| 改动 | `tests/CMakeLists.txt` | UDP 相关测试注释掉 |
| **新增** | `configs/merged/runtime.json` | merged 运行配置 |
| 其余 | 45 个模型模块（common 12 + env 22 + fc 11） | **零改动** |
| 其余 | 全部模型头文件、protocol/config/packet | **零改动** |

> 注：merged_main.c、configs/merged/ 等是工作区文件，未提交进 git
> （merged_sim 目录的 .git 指向子模块 external/Aircraft-Simu 的仓库，
> 合并改动只存在于工作树中）。

## 2. 架构变化

### 2.1 双进程 UDP（迁移前）

```
environment_sim 进程                          flight_control_sim 进程
┌──────────────────────────┐                 ┌──────────────────────────┐
│ env_app_run(ctx)         │                 │ fc_app_run(ctx)          │
│  while(...) {            │   send_sensor_  │  for(;;) {               │
│    build_sensor_frame    │──frame (UDP)──> │    recvfrom → decode     │
│    send_sensor_frame ────┼────────────────>│    flight_controller_step│
│    write_sensor_log      │                 │    write_internal_log    │
│    receive_control_command<──command───────┤    send_control_command  │
│    write_command_log     │  (UDP)          │  }                       │
│    update_truth ...      │                 │  (bind fc_port + 2*iid)  │
│  }                       │                 └──────────────────────────┘
│  (bind env_port + 2*iid) │
└──────────────────────────┘
```

要点：每步一次 `sendto` + 一次 `recvfrom`；SensorFrame/ControlCommand 经过
`packet_encode/decode`（线协议 CRC、字节序、版本校验）；飞控进程持有
`internal_log` 文件句柄；环境进程持有 sensor/command/trajectory 等日志。

### 2.2 单进程锁步（迁移后）

```
merged_sim 进程（单进程单实例）
┌──────────────────────────────────────────────┐
│ merged_main.c: 解析两套参数 → env_app_run(env_ctx, fc_ctx) │
│   env_app_run:                               │
│     ... 环境初始化（不变） ...               │
│     fc_runner_init(fc_ctx, ...)   ← 飞控初始化内联      │
│     while (status==SIM_OK && ...) {          │
│       build_sensor_frame                    │
│       flight_controller_step(&controller,    │
│                              &sensor, &command)  ← 直接函数调用       │
│       fc_runner_log_step(internal_log, ...)  ← 环境侧代写内部日志     │
│       write_sensor_log / write_command_log   │
│       update_truth ...                       │
│     }                                        │
└──────────────────────────────────────────────┘
```

**本质变化：UDP 收发替换为直接函数调用**。`send_sensor_frame` + 
`receive_control_command` 两个 socket 步骤变成一次 `flight_controller_step`
内存调用——数据不落线、不编码解码，其余计算语义完全不变。

## 3. 逐文件改动明细

### 3.1 environment_sim/src/env_app.c（核心，-141/+76 行）

**删除**（全部与网络相关）：

| 删除项 | 位置（原文件） | 说明 |
|---|---|---|
| `#define ENV_PACKET_BUFFER_SIZE 1024u` | 头区 | 仅 recv 缓冲用 |
| `bind_udp_socket()` 约 66 行 | ~1305-1370 | socket/bind/SO_REUSEADDR/1s 超时 |
| `send_sensor_frame()` 约 45 行 | ~1372-1417 | encode + sendto + 大小校验 |
| `receive_control_command()` 约 36 行 | ~1483-1518 | recvfrom + decode + EAGAIN 判定 |
| 全部 `close(sock)` 调用 | 14 处 | 各错误路径与收尾 |
| `env_port`/`fc_port` 计算与 `inet_pton` | init 段 | `runtime.env_base_port + 2*iid` 等 |
| 端口相关日志 | 2 处 | 启动打印、错误打印 |

**新增**：

| 新增项 | 说明 |
|---|---|
| `#include "fc/fc_app.h" / "fc/fc_modes.h" / "fc/fc_state.h"` | 飞控接口 |
| `env_app_run(const EnvContext *ctx, const FcContext *fc_ctx)` | 签名变更，见 3.3 |
| `ctx->instance_id != fc_ctx->instance_id` 校验 | 双上下文一致性检查 |
| `fc_runner_init(...)` 调用块（约 30 行） | 在环境初始化完成后、循环前 |
| `flight_controller_step(&controller, &sensor, &command)` | 替换 send+recv 两步 |
| 模式切换打印（`mode %s -> %s at t=...`） | **从 fc 侧搬过来**，行为保留 |
| 保护打印（`protection seq=%u mode=%s status=0x%08x`） | 同上 |
| `fc_runner_log_step(...)` 调用 | 环境侧代写飞控内部日志 |
| `internal_log` 句柄的 fclose | 收尾处（替代 close(sock)） |
| `flight_control_sim:` 前缀的错误打印 | 失败路径信息保留 |

**关键位置对照**（原 UDP 循环 vs 合并锁步循环）：

```
原:  build_sensor_frame → send_sensor_frame(sock,fc_addr) → write_sensor_log
     → receive_control_command(sock) → write_command_log → update_truth
新:  build_sensor_frame → flight_controller_step(&controller,&sensor,&command)
     → fc_runner_log_step → write_sensor_log → write_command_log → update_truth
```

注意顺序细节：合并版把 `flight_controller_step` 放在 `write_sensor_log`
之前（原版 send_sensor_frame 也在 write_sensor_log 之前），
`write_command_log` 仍在 update_truth 之前——**日志顺序与原版一致**。

### 3.2 flight_control_sim/src/fc_app.c（+131 行）

新增两个接口，把原 `fc_app_run` 拆成"初始化 + 每步"两部分，供环境进程内联调用：

- **`fc_runner_init(ctx, &controller_cfg, &controller, &internal_log_out, &flush_every_steps_out)`**：
  封装原 fc_app_run 中 UDP 绑定**之前**的全部逻辑——加载 flight_control.json、
  runtime.json、schema 校验、`load_guidance/autopilot/safety/scheduler_config`、
  `flight_controller_init`、`open_internal_log`。与 fc_app_run 的差异：
  **不创建 Logger**（合并程序中环境侧负责日志）、错误路径释放已加载配置。
- **`fc_runner_log_step(internal_log, &command, &logged_frames, flush_every_steps)`**：
  封装原循环内的 `write_internal_log` + 计数 + 周期 fflush。

原 `fc_app_run`（563 行起）**保留未删**，独立 `flight_control_sim` 可执行文件
仍可构建运行（双进程模式未被破坏）。

### 3.3 头文件

`env/env_app.h`：

```c
/* 原 */ SimStatus env_app_run(const EnvContext *ctx);
/* 新 */ SimStatus env_app_run(const EnvContext *env_ctx, const FcContext *fc_ctx);
// 文档注释：合并模式，调用方必须保证 env_ctx 与 fc_ctx 的 instance_id 一致
```

`fc/fc_app.h`：新增 `fc_runner_init` / `fc_runner_log_step` 声明 +
`#include "fc/fc_state.h"`、`<stdio.h>`。

### 3.4 environment_sim/src/merged_main.c（新增，82 行）

合并入口：同时解析环境参数（`--scenario/--runtime/--faults/--random-seed`）
与飞控参数（`--config`），`--instance-id`/`--runtime` 双写两个 ctx，
最后 `env_app_run(&env_ctx, &fc_ctx)`。原 `environment_sim/src/main.c`
（仅环境参数）保留未删但不再参与构建。

### 3.5 CMake 变更

`environment_sim/CMakeLists.txt`：

```cmake
set(ENV_SOURCES
    src/env_app.c
    src/merged_main.c        # 原 src/main.c
)
add_executable(merged_sim ...)      # 原 environment_sim
target_link_libraries(merged_sim PRIVATE
    missile_environment missile_flight_control missile_common m)  # +fc 库
```

`flight_control_sim/CMakeLists.txt`：`src/fc_app.c` 从"独立可执行文件专用源"
移入 `missile_flight_control` 静态库（供 merged_sim 链接），同时保留
`flight_control_sim` 独立可执行目标。

### 3.6 配置

- **`configs/merged/runtime.json`（新增）**：与 baseline 相比仅
  `campaign_id: "merged_dev_001"`、`output_dir: "runs/merged_dev_001"`。
  `network` 段**原样保留**（schema 校验强制要求该段，虽然合并版不再使用）。
- `configs/baseline/runtime.json`：未改动。
- `configs/baseline/runtime_128.json`：128 实例批量运行配置（后续加入）。

### 3.7 测试

`tests/CMakeLists.txt`：`closed_loop_test`、`instance_manager_test`、
`long_campaign_test` 全部注释（它们依赖双进程 UDP 可执行文件参数）。
其余单元测试（common/environment/flight_control）不受影响。

## 4. 保留不变的语义

| 项目 | 状态 |
|---|---|
| 全部 45 个模型模块计算逻辑 | 零改动 |
| sensor/command/trajectory/diagnostics/event 日志格式 | 不变（同源写出函数） |
| 内部日志记录格式（92 字节） | 不变（`write_internal_log` 复用） |
| 模式切换/保护 stdout 打印 | 保留（从 fc 侧移入 env 循环） |
| 每实例 seed 派生 `base_random_seed + instance_id` | 不变 |
| summary.json / run_manifest.json | 不变（manifest 端口字段传 0） |
| 双进程模式（独立 environment_sim/flight_control_sim） | 代码保留，仍可构建 |

## 5. 与 GPU 迁移的关系

merged_sim 是 GPU 化的**直接输入**，其意义：

1. **锁步循环 1:1 映射 kernel 线程**：`while` 循环体（merged_gpu_kernel.cu
   的 `sim_instance_kernel`）就是从这里照搬的——对比两文件可看到循环体
   逐行对应；
2. **状态已集中**：`FlightController controller` 成为 env_app_run 的局部变量，
   GPU 版把它收进 `SimInstanceState`；
3. **无网络依赖**：kernel 内不需要任何 socket/进程能力；
4. **输出格式已验证**：合并时保留的日志格式成为 GPU 回放逐字节对齐的基准。

## 6. 验证方式

- `make` 构建 merged_sim + 两个独立可执行文件；
- 双进程 UDP 版输出（runs/baseline_dev_001）与 merged 版输出
  （runs/merged_dev_001）同 seed 对比：summary/event_log 一致
  （merged 版即后续所有 CPU 参考输出的来源）；
- 单元测试（common/environment/flight_control_tests）继续通过。