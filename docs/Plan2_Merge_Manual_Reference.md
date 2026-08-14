# 方案2 合并步骤：手写参考版实验记录

> 目标：将 Aircraft-Simu 的双进程 UDP 仿真（environment_sim + flight_control_sim）
> 合并为单进程单实例程序，作为方案2（整个实例迁移 GPU、一个线程跑一个实例）
> 的第一步。本实验手写合并版并验证其与双进程基线**逐字节一致**，
> 重点记录后续自动化合并 Pass（InstanceMergePass）所需的**接口**与**先验知识**。

## 1. 实验环境与结论

- 实验副本：`output/merged_sim/`（`external/Aircraft-Simu` 的拷贝，submodule 未改动）
- 构建：`cmake -S . -B build && cmake --build build`
- 合并产物：`build/environment_sim/merged_sim`
- 验证配置：`configs/baseline/`（2 实例，固定种子 12345/12346）

**结论：合并版与双进程基线的全部 14 个输出文件逐字节一致**（2 实例 ×
sensor_log.bin / command_log.bin / fc_internal_log.bin / trajectory.csv /
trajectory_diagnostics.csv / summary.json / event_log.txt）。

已知差异（仅 3 处，均在 `run_manifest.json`，属预期）：

| 字段 | 基线 | 合并版 | 说明 |
|---|---|---|---|
| `runtime_path` | `./configs/baseline/runtime.json` | `configs/merged/runtime.json` | 实验配置路径不同 |
| `environment_port` | 54000 | 0 | 合并版无 UDP |
| `flight_control_port` | 54001 | 0 | 合并版无 UDP |

## 2. 合并语义定义

### 2.1 原双进程锁步结构

- **env 主循环**（`env_app_run`，environment_sim/src/env_app.c）：
  `while (status == SIM_OK && state.time <= scenario.max_time)`，是锁步的宿主。
- **fc 从循环**（`fc_app_run`，flight_control_sim/src/fc_app.c）：
  `for (;;)`，`recvfrom` 超时（2s，SO_RCVTIMEO）= 对端（env）结束的信号。
- **帧交换**（common/protocol.h，UDP 线格式经 common/packet.c 序列化）：
  - `SensorFrame`：env → fc（`send_sensor_frame` / fc 侧 recvfrom+decode）
  - `ControlCommand`：fc → env（fc 侧 encode+`send_control_command` / `receive_control_command`）
- env 循环单步顺序：
  1. 地形碰撞 / 命中 / 故障更新（fault_injection_update）
  2. `build_sensor_frame` → LOS 遮挡 → comm 乱序/延迟故障
  3. **[UDP] send_sensor_frame** → `write_sensor_log`
  4. **[UDP] receive_control_command** → `write_command_log`
  5. `update_truth`（6-DOF RK4 + 力模型）→ `update_geodetic_state`
  6. 轨迹/诊断日志 → `seq++`

### 2.2 合并后单循环（一个线程内锁步）

```
while (status == SIM_OK && state.time <= scenario.max_time) {
    // [env] 碰撞/命中/故障 → build_sensor_frame → LOS/comm 故障
    // [fc ] flight_controller_step(&controller, &sensor, &command)   ← 替换 send+recv
    // [fc ] mode 变化打印 / protection 打印 / fc_runner_log_step
    // [env] write_sensor_log → write_command_log
    // [env] update_truth(&command) → update_geodetic_state → 轨迹/诊断日志
}
```

UDP 收发点消失，`SensorFrame`/`ControlCommand` 直接传指针，序列化代码删除。
`FlightController` 状态成为合并函数局部变量；fc 的初始化并入 env init 段；
fc 的 cleanup（fclose internal_log）并入 env cleanup 段。

## 3. 自动化流程所需接口（InstanceMergePass 输入/输出）

### 3.1 需配置声明的识别点

```json
"plan2_merge": {
  "env_runner": "env_app_run",
  "fc_runner": "fc_app_run",
  "send_fn": "send_sensor_frame",        // env 循环内的帧发送点
  "recv_fn": "receive_control_command",  // env 循环内的帧接收点
  "fc_step": "flight_controller_step",   // fc 单步推进函数（sensor→command）
  "fc_init_fn": "fc_runner_init",        // 生成的初始化接口名
  "fc_log_fn": "fc_runner_log_step"      // 生成的日志接口名
}
```

### 3.2 手写版新增的接口函数（即自动化 Pass 的生成物）

`flight_control_sim/include/fc/fc_app.h`：

```c
/* 内容 = 原 fc_app_run 中 UDP 绑定之前的全部初始化（配置加载/schema 校验/
 * controller init/打开内部日志）。与 fc_app_run 的差异：
 *   1) 不创建 Logger（合并程序中 env 侧负责日志）；
 *   2) 错误路径释放已加载的 ConfigTree（原 fc_app_run 泄漏）；
 *   3) 成功后自行 config_free，调用方无需关心配置树生命周期。 */
SimStatus fc_runner_init(const FcContext *ctx,
                         FlightControllerConfig *controller_cfg,
                         FlightController *controller,
                         FILE **internal_log_out,
                         uint32_t *flush_every_steps_out);

/* 内容 = 原 fc 循环内的 write_internal_log + 帧计数 + 周期刷新。 */
SimStatus fc_runner_log_step(FILE *internal_log,
                             const ControlCommand *command,
                             uint32_t *logged_frames,
                             uint32_t flush_every_steps);
```

### 3.3 env_app_run 签名与局部变量变更

```c
/* 旧：SimStatus env_app_run(const EnvContext *ctx);
 * 新：SimStatus env_app_run(const EnvContext *env_ctx, const FcContext *fc_ctx);
 * 约束：env_ctx->instance_id == fc_ctx->instance_id，否则返回 SIM_ERR_INVALID_ARG */
```

- **删除**的局部变量：`sock`、`env_port`、`fc_port`、`fc_addr`
- **新增**的局部变量：`controller_cfg`、`controller`、`last_reported_mode`
  （= FC_POWER_ON）、`internal_log`、`logged_frames`、`fc_flush_every_steps`
- init 顺序：env init（配置/传感器/地形/气动/状态/日志文件）→ `fc_runner_init`
  → 循环；fc 初始化失败时复用 env 既有 cleanup 路径返回
- cleanup：env 原有 cleanup 之后、unload 之前追加 `fclose(internal_log)`

### 3.4 删除清单（自动化 Pass 的删除规则）

env_app.c：
- 静态函数 `bind_udp_socket`、`send_sensor_frame`、`receive_control_command`
- 端口计算（`env_port/fc_port`）、`bind_udp_socket` 调用与全部 `close(sock)`
- `write_run_manifest(..., env_port, fc_port)` 端口实参改为 0
- `ENV_PACKET_BUFFER_SIZE` 宏（仅 recvfrom 缓冲使用）

fc_app.c（fc 循环整体被吸收）：
- socket 生命周期（bind / close / SO_RCVTIMEO）、`send_ready_heartbeat`
- `recvfrom` 超时退出（即 fc 循环唯一正常出口）
- 报文长度校验 / decode 失败丢包 `continue`（合并后不可能发生，直接删除）

## 4. 先验知识（自动化 Pass 必须知道的启发式）

1. **锁步宿主**：env 循环是主循环，fc 循环被整体吸收进 env 循环体；
   合并点 = env 循环内的 send/recv 函数调用处，fc 循环体替换 send 调用，
   recv 调用删除（command 由 fc step 产生）。
2. **结束条件**：fc 的 recvfrom 超时（2s）本质是"env 已结束"信号，合并后
   不需要——env 循环的 while 条件与 break 即整个实例的结束条件。
3. **随机种子**：env 侧种子 = `runtime.base_random_seed + instance_id`，或由
   `--random-seed` 显式覆盖；fc 侧无独立随机源（估计器等为确定性计算）。
   合并版种子语义不变。
4. **帧序列化是二进制无损拷贝**（packet.c 直接 memcpy 结构体字段），因此
   合并版浮点运算序列与双进程版完全一致 → 逐字节一致可达成，且是合理验收标准。
5. **runtime.json 被双进程各自加载**：EnvRuntimeConfig 与 FcRuntimeConfig 是
   两个不同的结构体（fc 版只有 env 版字段的子集）。合并后由 env 侧加载一次；
   fc 侧实际需要的仅 output_dir / binary_logs / flush_every_steps。
6. **跨文件 static 冲突**：env/fc 各有同名 static 函数（如 `load_runtime_config`），
   合并时 fc 侧 helper 需"去 static + 改名"（手写版采用新增 fc_runner_init 封装，
   而非逐个暴露 helper）。
7. **输出目录一致**：fc 的 `open_internal_log` 与 env 的 `make_run_dirs` 使用
   相同的目录格式（`mkdir("runs")` + `output_dir/instance_%04u`），合并后
   fc_internal_log.bin 与其余日志写入同一实例目录，行为不变。
8. **exit_reason 映射**：fc 步骤失败需映射为 env 侧退出原因——手写版采用
   `controller_step_failed` / `internal_log_failed` + write_event + break。
9. **stdout 顺序不影响比对**：每个输出文件只由一侧写入，跨进程打印顺序
   无关紧要；仅需保证每个文件内部的字节序列一致。
10. **构建调整**：fc_app.c 需从可执行目标移入静态库（手写版移入
    `missile_flight_control`），否则合并目标无法链接 fc_runner_init；
    双进程测试（closed_loop_test / instance_manager_test）因架构变更移除。

## 5. 验证方法复现

```bash
# 1) 基线（双进程，instance_manager 调度）
./build/tools/instance_manager/instance_manager --runtime ./configs/baseline/runtime.json
# 输出 → runs/baseline_dev_001/

# 2) 合并版（每个实例一个进程）
./build/environment_sim/merged_sim --instance-id 0 \
    --scenario configs/baseline/scenario.json \
    --runtime configs/merged/runtime.json \
    --faults configs/baseline/faults.json \
    --config configs/baseline/flight_control.json \
    --random-seed 12345
./build/environment_sim/merged_sim --instance-id 1 \
    --scenario configs/baseline/scenario.json \
    --runtime configs/merged/runtime.json \
    --faults configs/baseline/faults.json \
    --config configs/baseline/flight_control.json \
    --random-seed 12346
# 输出 → runs/merged_dev_001/

# 3) 逐字节比对（除 run_manifest.json 的 3 处已知差异外全部一致）
for f in sensor_log.bin command_log.bin fc_internal_log.bin trajectory.csv \
         trajectory_diagnostics.csv summary.json event_log.txt; do
    cmp runs/baseline_dev_001/instance_0000/$f runs/merged_dev_001/instance_0000/$f
done
```

## 6. 手写版改动的文件清单（自动化 Pass 的对照物）

| 文件 | 改动 |
|---|---|
| `flight_control_sim/include/fc/fc_app.h` | 新增 fc_runner_init / fc_runner_log_step 声明 |
| `flight_control_sim/src/fc_app.c` | 新增两个接口函数（fc_app_run 保持不变） |
| `environment_sim/include/env/env_app.h` | env_app_run 签名改为双 Context |
| `environment_sim/src/env_app.c` | 签名/局部变量/init/循环/cleanup 合并，删除网络代码 |
| `environment_sim/src/merged_main.c` | 新增：合并两个 main 的参数解析 |
| `environment_sim/CMakeLists.txt` | 新增 merged_sim 目标（替代 environment_sim） |
| `flight_control_sim/CMakeLists.txt` | fc_app.c 移入 missile_flight_control 库 |
| `tests/CMakeLists.txt` | 移除双进程测试（closed_loop/instance_manager） |
| `configs/merged/runtime.json` | 实验专用：output_dir/campaign_id 改名 |

## 7. 下一步（自动化 InstanceMergePass）

1. config.json 增加 `plan2_merge` 声明（见 3.1）
2. 新 Pass 按 3.2/3.3/3.4 规则对 AST 实施合并
3. 产物与手写版（output/merged_sim）输出比对逐字节一致
4. 之后再进入方案2 的 GPU 化步骤（文件 I/O、线程映射）——本实验不涉及