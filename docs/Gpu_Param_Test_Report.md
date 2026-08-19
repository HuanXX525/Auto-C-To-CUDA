# GPU 迁移参数化测试报告

- 日期：2026-08-17
- 被测对象：`merged_gpu_sim`（GPU 迁移版）vs `merged_sim`（CPU 参考）
- 环境：NVIDIA RTX 3050 Laptop（4 GB, sm_86），CUDA 13.3，nvcc `-arch=sm_86 -std=c++17`
- 验收标准：**容差比对**（GPU/CPU 浮点末位 ULP 差异不计，不要求逐字节）

## 1. 测试方法

自动化脚本：`experiment/param_test.py`（可重复执行，`python3 param_test.py [过滤词]`）。

每个用例的执行流程：

1. 生成临时 runtime（覆盖 `output_dir` 到 `/tmp/opencode/paramtest/`，避免 9p 慢盘干扰正确性对比）；
2. CPU 参考：`merged_sim --scenario ... --runtime ... --faults ... --config ... --instance-id N`；
3. GPU：`merged_gpu_sim` 同参数 + `--instances N --output ...`；
4. 对比输出：
   - `summary.json`：`hit_flag / exit_reason / simulation_steps / fault_* 统计 / model_degradation_flags_or / time_of_closest_approach` 逐字段相等；`miss_distance` 绝对容差 < 1e-3 m；其余字段 < 1e-9；
   - `trajectory.csv`：逐字段相对容差 < 1e-6（排除 seq/step/time_s）；
   - `sensor_log.bin / command_log.bin / fc_internal_log.bin`：文件大小一致；
   - `event_log.txt`：**逐字节一致**。

## 2. 正确性测试矩阵（12/12 PASS）

### 2.1 场景变化（发射条件/目标位置/高度）

| 用例 | 场景参数 | miss (m) | 步数 | 结果 |
|---|---|---|---|---|
| scene_01_baseline_clean | 基线（高 1000 m，前向） | 0.352643 | 978 | PASS |
| scene_02_high_altitude_clean | 高 10000 m，目标 12000 m | 0.588930 | 1314 | PASS |
| scene_03_low_altitude_clean | 低空 300 m | 2.824020 | 688 | PASS |
| scene_04_tail_chase_clean | 尾追（同向 150/260 m/s） | 19300.27 | 3266 | PASS（未命中，超时退出一致） |

### 2.2 故障注入（基线场景）

| 用例 | 故障 | miss (m) | 步数 | 结果 |
|---|---|---|---|---|
| fault_02_seeker_bias | seeker LOS 速率偏置 0.002 rad/s（t=2.5s，0.5s） | 1.944582 | 978 | PASS |
| fault_03_imu_drift | IMU 三轴偏置 0.002（t=0.5s，10s） | 0.352643 | 978 | PASS |
| fault_04_combined | seeker 偏置 + IMU 偏置组合 | 1.538730 | 978 | PASS |

（fault_01_clean = 无故障，见 2.1 首行。）

### 2.3 制导参数变化（覆盖层合并进 scene_01_baseline）

| 用例 | PNG N / 最大加速度 / 加速度率 | miss (m) | 步数 | 结果 |
|---|---|---|---|---|
| guide_01_baseline | 4.0 / 350 / 2000 | 0.352643 | 978 | PASS |
| guide_02_aggressive | 5.0 / 500 / 3000 | 0.352643 | 978 | PASS |
| guide_03_conservative | 3.0 / 200 / 1000 | 0.352643 | 978 | PASS |
| guide_04_slow_response | 4.0 / 350 / 1500 | 0.352643 | 978 | PASS |

### 2.4 多 seed（runtime_64：base_random_seed=50000）

GPU `--instances 64` 全量正确；CPU 参考抽 4 个实例（id 0/17/33/63）逐一对比：

| 实例 id | miss (m) | 步数 | 结果 |
|---|---|---|---|
| 0 | 0.352313 | 978 | PASS |
| 17 / 33 / 63 | 一致 | 978 | PASS |

- 64/64 实例 summary 全部 hit/978 步。
- 此前已验证 1024 实例（seed 12345+id）1024/1024 正确。

## 3. 性能测试

### 3.1 实例数扫描（kernel 时间，输出到本地快盘 /tmp）

| 实例数 | kernel | 每实例 | 说明 |
|---|---|---|---|
| 2 | 313 ms | 157 ms | 固定启动开销占比高 |
| 8 | 320 ms | 40 ms | |
| 32 | 477 ms | 15 ms | |
| 128 | 958 ms | 7.5 ms | |
| 256 | 1948 ms | 7.6 ms | 每 SM 1 block × 256 线程 |
| 512 | 3795 ms | 7.4 ms | 2 block |
| 1024 | 7769 ms | 7.6 ms | 4 block（分批 256/批） |

256 起**线性扩展**（block 数 = 实例数/256，受寄存器 255/线程限制每 SM 1 block）。

### 3.2 计算 vs 写盘分解（重要发现）

`/workspace` 挂载为 **9p（virtio drvfs）慢盘**，日志写盘是两端共同瓶颈：

| 口径 | CPU（merged_sim） | GPU（merged_gpu_sim） | 加速比 |
|---|---|---|---|
| 计算（CPU user / GPU kernel） | 18–40 ms/实例 | 7.6 ms/实例 | **~3–5×** |
| 单实例端到端（9p 输出） | ~1.2 s | 128 实例 0.87 s/实例（111.7 s 总） | ~1.4× |
| 1024 实例端到端（9p） | ~1229 s（1.2 s × 1024） | **1079 s（实测，rc=0，1024/1024 hit）** | ~1.14× |
| 1024 实例（GPU 输出到快盘） | — | 46.9 s（kernel 7.8 s + 快盘写出 39 s） | — |

要点：

- GPU 的**计算**优势 ~3–5×，且在 256+ 实例时线性扩展；
- 端到端收益主要来自**并行摊薄**（CPU 串行进程 × 1024 vs GPU 一次 launch 4 block）；
- **9p 写盘**（每实例 ~1 MB 日志，逐实例 fopen/fclose + flush）使 GPU 1024 实例端到端从 46.9 s（/tmp 快盘）膨胀到 **1079 s（9p 实测）**——写盘是两端共同瓶颈，后续优化方向：写盘降频/合并输出/异步写。

## 4. 关于"空文件"的说明

`experiment/runs/`、`merged_sim/runs/baseline_dev_001` 下存在 **0 字节 `fc_internal_log.bin`** 的历史残留，为 9p 慢盘上旧运行（写入未完成/进程被杀）所致；**本次全部 13 组测试输出均非空且内容正确**（字节大小与 CPU 参考一致，见 2.1–2.4）。

## 5. 结论

1. **迁移正确性确认**：12/12 参数化用例 PASS（场景 ×4、故障 ×3、制导 ×4、多 seed ×1），全部在容差内与 CPU 一致；event_log 逐字节一致；
2. **性能确认**：kernel 计算 ~3–5× 加速且随实例数线性扩展；大规模场景下 GPU 并行收益显著；
3. **遗留**：9p 慢盘写盘是端到端瓶颈（建议输出到本地盘或批量写盘）；`param_test.py` 留在 `experiment/` 可作回归工具。