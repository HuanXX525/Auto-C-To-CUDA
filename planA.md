# Aircraft-Simu 多实例 CUDA 加速方案

## 总体方案

采用"批量 GPU 执行器 + 保留 CPU 基线"的双路径架构：

```text
runtime.json
    |
    +--> CPU backend：现有 instance_manager + env/fc 双进程 + UDP（原项目不改）
    |
    +--> CUDA backend：单进程 GPU campaign runner
                         |
                         +--> N 个实例状态驻留 GPU
                         +--> 每个实例内部时间步串行
                         +--> 实例之间 GPU 并行
                         +--> GPU 缓冲完整轨迹
                         +--> 主机统一写日志
```

当前 Aircraft-Simu 的并行性主要来自实例独立性；单实例内部存在严格的 lockstep 时间依赖，因此不能将时间步直接跨线程并行。CUDA 的 block 之间没有执行顺序保证，也不应存在跨 block 数据依赖，这与"一个实例对应一个独立 block"的模型相符。[CUDA Programming Model](https://docs.nvidia.com/cuda/cuda-programming-guide/01-introduction/programming-model.html)

首期不改造现有 `environment_sim`/`flight_control_sim` 双进程，而新增独立的 `gpu_campaign` 执行器。这样可以保留当前 CPU 结果作为回归基线，并避免在 GPU 路径继续使用 UDP、`fork`、JSON 解析和文件 I/O。

### 核心约束：原项目不改

**`external/Aircraft-Simu/` 下的所有源码不做任何修改。** 原项目的作用仅限于：

- CPU 基线：用 gcc 编译运行，产出回归对比数据；
- 转换器输入源：从原项目复制到 `gpu_workspace/staging/`，在副本上做预处理和转换；
- 配置文件来源：GPU 端只读 `configs/baseline/*.json`。

原项目的 `common/`、`environment_sim/`、`flight_control_sim/`、`tools/`、`tests/` 目录保持原样。

### 当前里程碑状态（2026-10-08）

- M0 已完成：父仓库 gitlink 与 CPU 基线固定在 Aircraft-Simu
  `970bdfb`，迁移工作区、忽略规则、工具链和验收配置已固化；
- M1 已完成：RTX 5070 上的地形飞越、碰撞（第 33 步）和
  N=1/8/32/128 隔离通过，见 `gpu_workspace/validation/terrain_m1.json`；
- M2 已完成：不改变原 JSON schema 的 campaign/instance 配置加载、
  POD 设备数据模型、资源索引去重和 kernel 启动前错误检查通过，见
  `gpu_workspace/validation/config_m2.json`；
- M3 已完成：生产入口已经接入
  `sensor → flight_control → actuator → environment force → plant → trace`，
  包含 RK4/Euler、气动优先级、重力、地球旋转、推进剂消耗、质量与惯量
  更新、目标运动及诊断；CPU/GPU baseline 在第 978 步同时命中，并完成
  执行机构、重力、推进/质量/惯量三个关闭地形和故障的独立 golden，见
  `gpu_workspace/validation/physics_m3.json`；
- M3 修复了转换后 `command_manager` 的执行机构数组 1-based 下标问题，
  并把修正规则固化进 sanitizer，重新转换后不会丢失；
- M4 已完成：生产路径实现 16 个内部 ECEF→LLA LOS 采样点、按 CPU 顺序
  加载文本/二进制多瓦片索引、`FLAT_FILL`/`ERROR` 缺瓦片策略，以及全部
  传感器、执行机构和锁步通信延迟/乱序/恢复语义；CPU/GPU 轨迹、诊断、摘要、
  事件和协议标志通过 clear/occluded/borderline、缺瓦片和重叠故障矩阵，见
  `gpu_workspace/validation/los_fault_m4.json`。N=1/8/32/128 实例隔离也通过。
- M5 已完成并通过运行时验收：`DeviceInstanceState` 跨 chunk 保留全局步号和终止状态，
  chunk 元数据包含序号/实例范围/首步/有效步数；full/summary 共用同一条
  fault event 历史，summary 不生成 full trace 文件；生产 runner 使用显式
  nonblocking compute/copy stream、两份 device snapshot/trace 和两份 pinned
  host buffer，通过 CUDA event 完成安全复用与 D2H 重叠，并实现
  `STOP_ON_FAILURE` 的 `skipped_after_failure` 语义。验收脚本为
  `gpu_workspace/tools/run_m5_chunk_verification.py`，场景定义为
  `gpu_workspace/configs/m5/cases.json`，状态摘要见
  `gpu_workspace/validation/chunk_m5.json`；在 NVIDIA GeForce RTX 5070
  Laptop GPU（CUDA runtime 13.2）上，chunk=1/64/256/all 的 full/summary、
  RNG/故障/通信连续性、确定性重复和混合提前终止隔离矩阵均 PASS。
- M6 已完成并通过生产接口验收：`--config` 参数互斥、`auto/serial/packed`
  映射记录、full/summary 输出目录、CPU 兼容 campaign summary、协议日志
  语义比较和 full/summary 最终状态一致性均 PASS；验收脚本为
  `gpu_workspace/tools/run_m6_verification.py`，结果见
  `gpu_workspace/validation/production_m6.json`。
- M7 已完成。M7-A 已冻结分段验收框架、矩阵定义和字段级比较规则；
  M7-B 已正式通过 16 个 64 步功能场景和 3 条错误路径；M7-C 已正式通过
  N=1/128 的 serial、packed/4、packed/32 映射不变性，以及
  chunk=1/64/256/67(all) 的 full/summary 和确定性重复；M7-D 已正式通过
  N=1/2/8/32/64/128 的混合场景与实例隔离；M7-E 已正式通过
  quiet/noise/dropout/timeout 的 12000 步、120.0 s 长回归，以及 quiet
  full/summary 和完整 trace 检查。四个当前 tracked 凭据分别为
  `gpu_workspace/validation/m7_receipts/m7_b_functional.json`、
  `gpu_workspace/validation/m7_receipts/m7_c_invariance.json`、
  `gpu_workspace/validation/m7_receipts/m7_d_batch.json` 和
  `gpu_workspace/validation/m7_receipts/m7_e_long.json`。M7-E 针对
  `long_quiet` 的 `SensorFrame.missile_vel_ecef_meas.y` 增加了仅限该字段的
  `atol=1e-10` 校准；全局 `rtol=1e-10, atol=1e-12` 不变。由于共享
  runner、matrix 和 comparator 哈希随校准变化，M7-B～D 已在父提交
  `f168c9d57525b8c776aa62981f73bd208e2148f6` 下重跑并通过，旧回执仅作为
  `*_pre_velocity_calibration.json` 历史归档。M7-F 已验证四份回执的
  section 覆盖、冻结哈希、输出指纹和严格线性祖先链，并在 RTX 5070
  Laptop GPU 上复跑含 CPU 对照的 M5/M6 回归；最终报告为
  `gpu_workspace/validation/correctness_m7.json`，SHA-256 为
  `6068d5bb44c3fdc4bde11ea05aabfc7cffbde8a165db1f88bce4450ac2b90627`。
  M8 已完成 M8-A1、M8-A2 和 M8-A3：A1 冻结 12000 步多实例输入契约，生成 uniform
  N=64/128 与 heterogeneous N=128 配置，通过生产 loader、POD ABI、确定性
  重建和精确资源预算检查；A2 在 RTX 5070 Laptop GPU 上完成 uniform
  N=64/128 的 12000 步 CPU full/GPU summary、GPU 重复和内存观测。A2 回执为
  `gpu_workspace/validation/m8_receipts/m8_a2_uniform.json`，其中明确记录
  instance 10/seed 12515 的一项量化边界校准：第 979 帧 seeker
  `LOS-rate.z` 只差一个 `1e-7` 量化档，随后命令保持放大为 1.5593378 m
  的末端水平位置差；其余 N=64 的 63 个、N=128 的 127 个实例仍通过默认
  容差。全局 FMA 关闭实验不改变该分支，正式构建保留默认 FMA。A2 不声称
  full-trace 或完整末态等价，也不作性能声明。后续高精度复查已将触发源定位为
  延迟样本时刻导弹 ECEF `x` 的一个 double 末位差异，RNG 状态一致；报告见
  `gpu_workspace/validation/m8_a2_numeric_root_cause.md`。A3 使用 128 个独立物理
  场景、8 种 profile 各 16 实例，在 12000 步 CPU full/GPU summary 下通过默认
  容差的语义与末态 x/y 比较；GPU 重跑、packed/32 和 chunk=64 的逐实例摘要、事件
  与末态投影均精确一致。76 个实例地表碰撞、52 个超时；A2 的固定输入校准不适用，
  A3 没有校准项。回执为 `gpu_workspace/validation/m8_receipts/m8_a3_heterogeneous.json`
  （SHA-256 `e0c77fc674444461857e7a56a9c821c62333dabd37aae00218ae82d51405898d`）。
  本段只提供正确性证据，不将诊断 wall time 用作性能结论。M8-B1 已固化
  `gpu_workspace/configs/m8/benchmark_contract.json` 与
  `gpu_workspace/tools/run_m8_benchmark.py`：CPU 顺序/进程并行及 GPU full/summary
  的端到端指标由进程外单调时钟计量；GPU compute-only 使用生产 kernel 的 CUDA
  event 总时长，不误用探针 kernel 或把重叠阶段求和。插值 P95、预热/重复次数、
  硬件/工具链/源码/二进制/输入哈希、命令顺序及不覆盖旧证据的续跑规则已冻结；
  B2 预检暴露两个测试框架问题：CPU 环境程序不会递归创建输出目录；原测试端口
  `55000` 段落在系统 UDP 临时端口范围内，N=128 并行轮次发生一次绑定失败。
  B1 因此以 v3 合同重新冻结：预建输出目录的父目录，仅在 CPU 基准的派生 runtime
  中使用 `62000–62255` 端口，并在启动前检查其不与本机临时端口范围重叠；场景、
  种子、物理参数及 12000 步工作负载保持不变。v1/v2 回执及失败输出均保留，
  不并入正式统计。13 项合成自检、N=8 CPU 嵌套输出预检通过；当前 B1 回执为
  `gpu_workspace/validation/m8_receipts/m8_b1_contract_v3.json`
  （SHA-256 `22c8de2f3a05635e3752c1bfa1a905b26f9b3344c0744ec5a7d7a03794236e21`）。
  B2 已按该合同完成 N=64/128 的 CPU sequential/process-parallel 各 1 次预热、
  5 次测量，共 24 轮；逐实例 `summary.json` 与事件日志的语义哈希在重复轮次
  与调度方式之间精确一致（本段未逐字节比较完整轨迹）。外部 wall
  中位数分别为 N=64 顺序 `237.744 s`、并行 `37.861 s`，N=128 顺序
  `490.051 s`、并行 `73.661 s`；这是 CPU 参考数据，不是 GPU 加速结论。
  B2 回执为 `gpu_workspace/validation/m8_receipts/m8_b2_cpu.json`
  （SHA-256 `8c8f8745d56d830e656930af7ead750fbdd0222daa4f2d0da95f57bb0e329bb7`），
  已独立复核全部输出、原始样本和统计。下一段为 M8-B3；持久状态见
  `gpu_workspace/validation/performance_m8_progress.json`。

### M7 分段执行计划（每段完成后立即停止）

M7 严格按 `A → B → C → D → E → F` 推进。M7-A 以 tracked 框架检查点
验收；可执行矩阵段 B～E 必须满足本段门槛、同时生成 build 内凭据和
`validation/m7_receipts/` 下的 tracked PASS 凭据，再更新进度文件并独立
提交；完成后不得自动进入下一段。M7-F 聚合这些 tracked 凭据并关闭阶段。
任一共享 runner、矩阵定义、比较器、执行二进制或 Aircraft-Simu gitlink
发生变化时，哈希不一致的旧凭据作废，必须重跑受影响段。分段进度提交会使
父仓库 commit 正常前进；后续聚合必须确认这些 commit 构成祖先链，并审计
其间差异只涉及验收记录或文档，否则旧凭据同样作废。

1. **M7-A：验收框架冻结（已完成）**
   - 固化 `configs/m7/cases.json`、分段 runner、碰撞 AGL 字段级容差及
     原子 PASS 凭据；校验父仓库和 Aircraft-Simu commit；
   - 通过 Python/JSON 静态检查、CPU/GPU 构建和 M6 生产接口回归；
   - 记录已有 short/mapping/chunk 探测证据，但不把它们当作新规则下的
     正式分段凭据。
2. **M7-B：64 步功能语义与错误路径（已完成）**
   - 命令：`python3 gpu_workspace/tools/run_m7_correctness_matrix.py --skip-build --sections short,errors --output gpu_workspace/build/m7_b_functional --receipt gpu_workspace/validation/m7_receipts/m7_b_functional.json`；
   - 覆盖 16 个 quiet/noise/dropout/fault/terrain/LOS/hit/timeout 场景及
     missing reference、非法 dt、缺瓦片 ERROR；
   - 门槛：CPU/GPU 默认 `rtol=1e-10, atol=1e-12`，碰撞仅 SensorFrame AGL
     使用已记录的 `atol=1e-9`；退出原因、步数、flags 和事件严格一致。
3. **M7-C：执行参数不变性（已完成）**
   - 命令：`python3 gpu_workspace/tools/run_m7_correctness_matrix.py --skip-build --sections mapping,chunk --output gpu_workspace/build/m7_c_invariance --receipt gpu_workspace/validation/m7_receipts/m7_c_invariance.json`；
   - 覆盖 N=1/128 的 serial、packed/4、packed/32，以及 chunk=1/64/256/all、
     full/summary 和确定性重复；CPU/GPU、映射间及 chunk 间必须一致。
4. **M7-D：多实例混合与隔离（已完成）**
   - 命令：`python3 gpu_workspace/tools/run_m7_correctness_matrix.py --skip-build --sections batch --output gpu_workspace/build/m7_d_batch --receipt gpu_workspace/validation/m7_receipts/m7_d_batch.json`；
   - 覆盖 N=1/2/8/32/64/128 和不同终止时间；每个 batch 的代表实例必须
     与相同 instance ID/seed 的独立运行一致，提前终止不得污染其他实例。
5. **M7-E：12000 步长回归（已完成）**
   - 命令：`python3 gpu_workspace/tools/run_m7_correctness_matrix.py --skip-build --sections long --output gpu_workspace/build/m7_e_long --receipt gpu_workspace/validation/m7_receipts/m7_e_long.json`；
   - quiet/noise/dropout/timeout 均执行 CPU/GPU summary；quiet 额外生成 full，
     已校验 12000 步、120.0 s、完整 trace 以及 full/summary 一致性；
   - CPU/CUDA 在 near-zero velocity Y 窗口（步骤 1523～1548）的最大差异为
     `9.71e-11`，因此仅 `long_quiet` 的
     `SensorFrame.missile_vel_ecef_meas.y` 使用 `atol=1e-10`；其他传感器字段、
     整数语义和全局容差不变。校准后 M7-B～D 已用相同冻结哈希重跑通过。
6. **M7-F：证据聚合与阶段关闭（已完成）**
   - 命令：`python3 gpu_workspace/tools/run_m7_closure.py --output gpu_workspace/build/m7_f_closure --skip-build`；
   - B～E 凭据中的 gitlink、runner、matrix、comparator、二进制哈希和输出
     指纹一致，六个 section 精确覆盖一次；`635c5e2 → 900ed4c` 为严格
     线性提交链，校准后的差异仅包含允许的 M7 文档、进度与回执；
   - M5 chunk/full/summary/RNG/故障/通信/提前终止回归和 M6 生产接口、
     CPU/GPU full、full/summary 回归均 PASS；
   - 最终 `validation/correctness_m7.json` 已发布，M7 正确性阶段关闭。
     默认全量命令仍保留供 clean checkout 复验；M8 后续工作不得修改这份
     M7 最终凭据。

### M8 分段性能验收计划（每个子段完成后停止）

M8 使用 `gpu_workspace/validation/performance_m8_progress.json` 作为活动状态，
每次只推进一个子段。正确性前置基线未通过时不得开始性能优化；任何优化后都
必须重跑受保护的 M7 与 M8 长回归。安全的构建、测试和 profiling 命令可自动
执行，但不得自动删除或覆盖既有回执和结果。

1. **M8-A：长期多实例正确性前置基线**
   - **A1（已完成）**：`prepare_m8_cases.py --write` 生成并冻结 138 个 JSON：
     uniform N=64/128 共享场景、heterogeneous N=128 独立物理场景、六个故障
     文件和三个 runtime。八种 quiet/noise/dropout/fault profile 各 16 个实例；
     N=64 是 N=128 的精确实例前缀；host loader/ABI 与资源预算均 PASS。
   - **A2（已完成，带一项窄范围校准）**：N=64/128 × 12000 步，CPU full、
     GPU summary，GPU primary/repeat 均完成；summary 语义、终止步/原因、
     事件、GPU 重复、N64/N128 前缀和内存观测通过。instance 10/seed 12515
     的第 979 帧 `SensorFrame.target_los_rate_ecef_meas.z` 首次落在 CPU/GPU
     `1e-7` 量化相邻档（CPU `-0.0105284`、GPU `-0.0105285`），round 输入
     分别距同一半档边界 `+0.0110461564` 和 `-0.00445655835` 个量化格；
     第 980～11999 帧 CPU/GPU 各自保持不同的 seq979 命令，造成末端水平
     位置差 1.5593378 m。验收仅允许该冻结输入的一项有符号终点差向量，
     N=64/128 各一个实例；其他 190 个实例仍使用全局
     `rtol=1e-10, atol=1e-12`。全局 `--fmad=false` 诊断与默认构建的关键
     量化值和轨迹一致，因此正式构建未加入该选项。该凭据不声称 full trace
     等价或完整末态等价。2026-10-11 的高精度诊断确认随机流一致，近距离
     LOS 计算将导弹 ECEF `x` 的一个 ULP 差异放大至量化边界；探针撤回后，
     单实例 GPU 传感器、指令和轨迹文件与原运行逐字节相同。复查报告为
     `gpu_workspace/validation/m8_a2_numeric_root_cause.md`。策略与回执分别为
     `gpu_workspace/validation/m8_a2_quantization_calibration.json` 和
     `gpu_workspace/validation/m8_receipts/m8_a2_uniform.json`；回执 SHA-256 为
     `73674f9df5378792c7659b4a6436ea9be61df1c383282a2e6f6209fd34c8354d`。
     不作性能声明；完成后停止。
   - **A3（已通过并停止）**：heterogeneous N=128 × 12000 步，CPU full 与 GPU
     summary、精确重跑、packed/32、chunk=64 全部通过。八种 profile 各 16 个实例；
     CPU/GPU 终止步、原因、故障计数、事件、摘要和末态 x/y 在默认容差下匹配，
     GPU 各映射/chunk 的逐实例摘要与事件逐字节相同。随机连续性通过输出级
     证据间接覆盖，不声称直接观察设备内部 PRNG 状态或完整 trace 等价。
     A2 的单实例校准因输入指纹不匹配而未应用；完成后停止，M8-B1 下一段。
2. **M8-B：冻结计时工具并建立未优化基线**
   - **B1（v3 已通过并停止）**：固化计时 schema、环境/二进制/输入哈希、插值 P95
     算法、确定性命令顺序和原子回执；13 项合成自检、N=8 CPU 实际预检与本地/仓库
     回执复核通过。CPU 测试输出父目录预建，派生 runtime 使用与系统临时端口不重叠的
     `62000–62255`；v1/v2 回执和两次失败输出保留、不混用样本。
     `gpu_execution.json` 的 `wall_ms` 排除最终写盘，`d2h_ms` 主要是等待时长，
     因此外部 wall 才是端到端指标；内部阶段允许重叠，不可相加。计划中的
     24 个 CPU、70 个 GPU campaign 均未在 B1 执行。
   - **B2（已通过并停止）**：按 v3 冻结协议完成 CPU sequential 与 process-parallel；
     每个 N=64/128、每个模式预热 1 次、测量 5 次，共 24 轮。20 个正式样本及
     median/P95/min/max、摘要/事件语义哈希与命令顺序均在回执中；各 N 的逐实例
     摘要和事件语义在重复轮次、调度方式之间精确一致，完整轨迹未在本段逐字节比较。
     尚未进行 CPU/GPU 性能阈值判定。
   - **B3（下一段）**：测未优化 GPU compute/full/summary；compute-only
     预热 3 次、测量 20 次，full/summary 各预热 1 次、测量 5 次；GPU 完整
     结果与 B2 CPU 的共享语义比较必须通过。
3. **M8-C：规模与端到端差距**
   - C1/C2 分别覆盖 N=1/2/8/32/64/128 的 compute-only 与端到端；C3 对
     N=64/128 给出相对 CPU 基线的验收差距，不以 kernel 时间替代总 wall time。
4. **M8-D：映射和 auto 阈值**
   - D1 扫描 serial 与 packed lanes=4/8/16/32；D2 专门扫描 N=640/768/896
     的 packed/4 与 packed/32；D3 仅按 median 结果冻结 `mapping=auto` 阈值。
5. **M8-E：profiling 与瓶颈排序**
   - E1 分解 H2D/kernel/D2H/host encode/I/O；E2 记录寄存器、occupancy、分支
     和内存指标；E3 发布按证据排序的优化决策。
6. **M8-F：只实施 profiling 证明必要的优化**
   - 顺序为寄存器/occupancy、状态布局/只读资源、chunk/双缓冲/写盘；仅当
     launch overhead 是瓶颈时才进入 CUDA Graph。每个优化单独验收并停止。
7. **M8-G：优化后正确性回归**
   - 重跑 M7、uniform/heterogeneous 长基线，以及 mapping/chunk/result
     invariance；任一失败都撤销该优化的性能结论。
8. **M8-H：最终性能矩阵与判定**
   - compute-only 预热 3 次、测量 20 次；端到端预热 1 次、测量 5 次；报告
     median/P95/min/max。目标为 GPU compute-only 相比 CPU sequential ≥5×，
     N=64/128 full 优于 CPU process-parallel，full 端到端目标 ≥2×。
9. **M8-I：固化与关闭**
   - 更新报告/README/manifest/结构文档，从 clean checkout 复验，并聚合全部
     回执。若正确性通过但性能门槛未达到，必须明确标记“正确性完成、性能未达标”。

M8-A1 的正式回执为
`gpu_workspace/validation/m8_receipts/m8_a1_inputs.json`，SHA-256 为
`c8273dc5e3211d0d6a8560a419ebf217e0ec7bd47a703c56dd78eed0fe1778eb`。
完整 raw trace 的 N=128 参考量为 1,265,769,472 B；这不是现有 chunk runner
的峰值 device 分配。chunk=256 时双 device trace 加主 state/双 snapshot 的
trace/state 工作集为 126,358,528 B，尚不含 config、fault 和 CUDA 管理开销。

### 历史实施进度（截至 2026-08-31）

已在 `gpu_workspace/` 完成 Phase 0/Phase 1 的最小垂直切片：

- `vec3.c`、`quaternion.c`、`random.c` 已生成 `.cu`，通过 host 数值探针、`nvcc -dc` 和 device-link 探针；
- `matrix3.c` 可生成和运行；转换器产生有符号/无符号索引警告，已通过派生 signed-index sanitizer 消除并通过公共 device-link 探针，但仍需更大范围数值回归；
- `missile_plant_6dof.c` 已完成派生适配，host Euler 单步和 device-link 探针通过；
- 多个低耦合环境模块已完成转换和 `nvcc -dc` 普查，具体状态见 `gpu_workspace/converted/PHASE1_STATUS.md`；
- `aero_surrogate.c` 的原始文件加载路径保持 host-only；已拆出 `aero_surrogate_device.c` 评估路径并通过 host/device 探针，原始完整文件仍不进入 device build；
- `gpu_workspace/gpu_campaign/` 已实现单实例 `simulate_chunk<<<1,1>>>` kernel、设备状态、完整 trace 回传和主机 launcher；已通过 nvcc 编译与 device-link；
- 单实例完整计算链已经接入：`sensor_imu/accel/speed/seeker → SensorFrame → flight_controller_step（安全监视、估计、PNG、自动驾驶仪、命令管理）→ missile_plant_step → trace`；传感器固定延迟线在 kernel 内初始化，避免环形缓冲区指向主机内存；
- 目标真值当前使用 GPU 侧确定性内存适配器 `(1000, 100, 0) m`，传感器误差/丢包/延迟关闭仅用于首轮正确性验证，真实传感器实现和随机状态仍在调用链中；
- trace 已增加传感器有效/故障位、飞控模式/状态和加速度指令，便于 CPU/GPU 对齐检查；`make -C gpu_workspace` 可完成整个链路的 nvcc 编译和 device-link；
- `tools/build_campaign_chain_probe.sh` 已提供同一套 sensor/control/plant 调用的 host probe；当前 8 步结果为 `mode=5`、`status=0`、`sensor_flags=0x1f`，可作为 GPU 运行后的首个 checkpoint；
- ring-buffer 视图已在 staging 适配为相对地址偏移句柄，`DeviceInstanceState` 不再包含裸指针字段；新增 `ring_buffer_probe` 验证覆盖/延迟语义，完整 sensor/control/plant 链路重新编译通过；该偏移 ABI 仍需在 GPU 长时运行中复核，并继续评估更严格的 offset/index 设计；
- 已开始 Phase 3 正确性基线：`simulate_chunk<<<instance_count, 1>>>` 现在按 `blockIdx.x` 映射实例，`--instances N` 分配 N 份状态和 trace，并为每个实例生成独立 seed/初始速度/目标偏移；当前只宣称实例隔离和可运行，不宣称性能收益；
- launcher 已加入 CUDA event kernel 计时和 `--repeats` 重复采样，并提供 `tools/run_multi_baseline.sh` 自动执行 N=1/2/8/32；计时只覆盖 kernel，不包含 host 分配和 PCIe 传输，真实结果需在匹配驱动环境记录；
- `tools/check_batch_isolation.py` 会比较 N=1/2/8/32 的 instance 0 全部结果字段，自动检查批量执行是否改变单实例基线；
- 已加入 `--threads-per-instance` 和 `tools/run_mapping_baseline.sh`，可在相同 N=32 工作负载下比较 1-thread/block 与 32-thread/block；当前 32-thread 只让 lane 0 执行完整串行实例，结果用于量化额外线程开销，不作为并行加速实现；
- 性能计时已增加 `--warmup` 和中位数统计；映射比较应优先使用 median/min，而不是被偶发 JIT/调度离群值污染的平均值；
- 预热后的映射结果显示 1-thread 与 32-thread 的 median 几乎相同（约 1.83 ms），32-thread 没有收益；已固定 1-thread 映射作为基线，并新增 `tools/run_scaling_baseline.sh` 覆盖 8/128/512/2048 步，避免仅凭 8 步小负载做性能结论；
- 当前 32 实例 scaling 结果：128 步 24.69 ms、512 步 103.71 ms、2048 步 399.90 ms；已补充 12000 步长时运行，结果保持线性并完成 instance-steps/s 记录；
- 已完成气动表/地形的首版设备侧扁平适配器：`DeviceAeroTable` 使用固定容量只读样本数组和显式外推策略，`DeviceTerrainGrid` 使用固定边界的行主序高度网格并提供双线性查询；两者均为无指针 POD，可由 host 初始化后整体上传；`tools/build_device_environment_probe.sh` 的 host 数值探针和 CUDA device-link 均通过；
- 设备适配器已通过真实二进制格式的 host packer 闭环：`host_environment_packer.cpp` 复用 `aero_database`/`map_tile` 解析并生成扁平缓冲，写入→读取→打包→查询与 host 数值回归通过；`gpu_campaign` 已支持 `--aero-file`/`--terrain-file` 上传并在 trace 中记录查询诊断位，但暂不改变 surrogate 力模型；原始文件读取、LRU/cache 和 tile 生命周期仍留在 host；
- 已在具备匹配驱动的 WSL/NVIDIA 环境完成真实上传路径验证：单实例和 2 实例、8 步运行均成功，`environment_flags=0x00000000`，且最终状态与默认 surrogate 路径一致（instance 0 `final_x=0.799988597849`）；原始输出记录于 `gpu_workspace/logs/environment_campaign_runtime_2026-08-31.log`，可用 `tools/run_environment_campaign_probe.sh` 重复执行；
- 已完成气动表力计算的独立设备适配：`device_force.cuh` 复刻 host `aero_model_evaluate` 的 table 分支，`device_force_probe` 对 host force/moment 逐字段回归并通过 CUDA device-link；该计算器尚未启用到默认 campaign，避免在场景参数尚未映射时改变轨迹；
- （历史记录）当时容器的 CUDA runtime 返回“driver version is insufficient”，
  因而不能代替用户环境的运行时结果；随后 M4 已完成 terrain/LOS 与故障语义，
  M5 已完成 chunk 多实例调度，并已在匹配驱动的 RTX 5070 Laptop GPU 环境
  通过 runtime 矩阵；随后 M6 生产输出与 CPU 兼容比较也已通过。M7 已进入
  分段正确性验收，现已完成 M7-A 框架冻结、M7-B 功能/错误路径验收、
  M7-C 映射/chunk 不变性验收、M7-D 多实例混合/隔离验收和 M7-E
  12000 步长回归，并由 M7-F 完成证据聚合、M5/M6 复验和阶段关闭；
  M8 现已开始；上述内容仅作为早期历史。当前以 M8 活动进度文件和分段回执为准。

### 可复现性与实现边界

“原项目不改”表示不修改 `external/Aircraft-Simu/` 的源文件；GPU 迁移仍会产生一个需要维护的 staging/设备侧派生版本，不能将其视为与原项目完全相同的实现。每次复制和转换必须记录：

- Aircraft-Simu 的实际 commit（包括 submodule gitlink）；
- staging 文件相对于源文件的路径和摘要；
- 预处理规则、转换器版本和转换参数；
- CUDA、host compiler、目标 GPU 架构和配置文件版本。

当前工程使用 C11，GPU 适配应以“C11-compatible subset + CUDA device 可编译接口”为目标，不能假设所有输入代码已经满足 C99。GPU backend 的配置不得要求修改原项目配置；应通过 `gpu_workspace/configs/` 或命令行覆盖提供独立的 GPU 配置。

## 1. GPU 执行模型

### 1.1 实例映射

目标映射：

```text
blockIdx.x = instance_id
一个 CUDA block = 一个飞行实例
正确性初版每个 block 使用一个线程；性能版本必须经过线程映射对比
```

每个 block 内：

- 负责一个实例的完整环境状态、飞控状态、随机数状态和轨迹缓冲；
- 时间步按原始顺序执行：

  ```text
  build_sensor_frame
  → flight_controller_step
  → update_truth
  → record trajectory/diagnostics
  ```

- 不同 block 之间无共享状态、无原子操作、无全局同步；
- 初版由 block 内单线程顺序执行完整实例逻辑，不引入额外 lane 同步；
- `flight_controller_step` 的安全评估、导航、制导、控制律和模式切换在单线程版本中直接按实例执行；
- 如果后续启用 warp 内 lane 分工，同一实例的不同 lane 可能走不同路径，需要通过固定分支、场景/故障分批和必要同步控制 divergence。

迁移初期使用“每个 block 一个线程”只作为正确性版本。对于当前约 12,000 步/实例的工作负载，128 个实例仅产生约 128 个活跃线程，不能预先宣称具有良好 GPU 利用率。Phase 3 必须比较至少两种映射（例如 1 thread/instance 与 1 warp/instance 或多个实例共享 block），并记录 occupancy、寄存器数、分支效率、kernel 时间和 instances/s。最终映射由实测结果决定。

### 1.2 时间步执行

采用"分块持久化 kernel"：

```cpp
simulate_chunk<<<instance_count, threads_per_instance, 0, stream>>>(
    device_states,
    device_configs,
    device_trace,
    first_step,
    chunk_steps);
```

每次 kernel 内部连续执行若干时间步，例如 128～512 步：

```text
for step in [first_step, first_step + chunk_steps):
    if instance_active:
        env_sensor_step()
        fc_step()
        env_truth_step()
        write_trace()
```

这样可以同时避免：

- 每个时间步单独启动 kernel；
- 单个超长 kernel 难以调试、无法分块失败恢复；
- 设备 watchdog 或长 kernel 调度风险。

后续如果将 env、fc、日志压缩等拆成多个 kernel，再考虑使用 CUDA Graph 将固定的 chunk 工作流实例化并重复执行。CUDA Graph 的设计目标就是减少重复 kernel 提交成本；图定义和实例化只做一次，之后可低开销重复启动。[CUDA Graphs](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/cuda-graphs.html)

### 1.3 Streams 与批量传输

使用：

- pinned host memory；
- 至少两个 CUDA stream；
- double-buffered trace chunk；
- `cudaMemcpyAsync()` 将已完成 chunk 回传主机；
- 一个 chunk 回传时，另一个 chunk 在 GPU 上继续计算。

双缓冲必须使用明确的所有权状态和 CUDA event：kernel 写入 buffer A 完成后，才允许拷贝流回传 A；主机写线程释放 A 后，A 才能被后续 kernel 重用。跨 stream 不得依赖隐式顺序。需要同时记录 chunk 序号、实例范围、首步和有效步数，避免短实例终止后覆盖或重复回传。

CUDA stream 保证同一 stream 内操作有序，并允许主机、内存传输和 GPU 计算重叠。[Asynchronous Execution](https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/asynchronous-execution.html)

GPU 路径禁止使用 legacy default stream，统一使用显式 non-blocking streams，避免隐式同步。

## 2. 数据与接口设计

### 2.1 项目目录结构

在 Auto-C-To-CUDA 根目录下新增 `gpu_workspace/`，所有 GPU 迁移工作在此完成，不侵入原项目：

```text
gpu_workspace/
├── staging/                  # 预处理暂存（从原项目复制，剥离 host 依赖）
│   ├── common/               #   vec3.c, matrix3.c, quaternion.c, random.c
│   ├── environment/          #   missile_plant_6dof.c, aero_model.c, ...
│   ├── flight_control/       #   estimator.c, navigation.c, autopilot.c, ...
│   └── headers/              #   预处理后的头文件副本
│
├── converted/                # 转换器生成的 .cu 文件
│   ├── common/               #   vec3.cu, matrix3.cu, quaternion.cu, random.cu
│   ├── environment/          #   missile_plant_6dof.cu, aero_model.cu, ...
│   ├── flight_control/       #   estimator.cu, navigation.cu, autopilot.cu, ...
│   └── NOT_CONVERTED.md      #   转换器跳过的文件清单及原因
│
├── gpu_campaign/             # GPU 适配、调度和主机桥接层（部分需手写）
│   ├── include/
│   │   ├── gpu_types.cuh     #   GPU 端状态结构体
│   │   ├── gpu_config.cuh    #   配置加载结构体
│   │   ├── gpu_trace.cuh     #   轨迹缓冲结构体
│   │   └── gpu_constants.cuh #   GPU 端常量定义
│   ├── src/
│   │   ├── main.cu           #   入口：读配置、分配显存、启动 kernel、回传
│   │   ├── gpu_campaign.cu   #   调度器：N 个 block 并行、chunk 循环
│   │   ├── simulate_step.cu  #   单步逻辑：env_sensor→fc_step→env_truth
│   │   ├── trace_buffer.cu   #   轨迹缓冲写入和批量回传
│   │   ├── config_loader.c   #   JSON 解析→DeviceConfig（CPU 端）
│   │   ├── terrain_loader.c  #   地形数据加载→device buffer（CPU 端）
│   │   └── result_writer.c   #   回传结果→CSV/BIN/JSON（CPU 端）
│   └── CMakeLists.txt
│
├── output/                   # 运行结果输出
│   ├── [timestamp]_Ninstances/
│   │   ├── instance_000/     #   trajectory.csv, diagnostics.csv, event_log.txt, summary.json
│   │   ├── instance_001/
│   │   ├── ...
│   │   ├── campaign_summary.json
│   │   └── timing.json
│   └── cpu_baseline/        #   CPU 基线结果（用于对比）
│
└── Makefile                  # GPU 项目构建
```

`staging/`、`converted/` 还必须生成 `manifest.json`，记录源文件、源 commit、转换状态、失败原因和编译参数。公共数学模块只能保留一份，环境和飞控模块通过统一的适配头文件引用，避免重复定义和链接冲突。

源文件映射必须明确标注原项目路径：公共模块来自 `common/src/`，环境模块来自 `environment_sim/src/`，飞控模块来自 `flight_control_sim/src/`；GPU 侧的 `environment/` 和 `flight_control/` 只是迁移工作区名称，不代表原项目目录名称。

### 2.2 设备端状态

所有设备状态必须是固定大小、无指针、可直接复制的设备专用 POD 类型。不能直接假设原项目运行时结构满足此条件；当前工程中的 `EnvContext`、`TerrainModel`、`AeroDatabase` 等类型包含路径、缓存或样本指针，必须留在 host 侧。

```cpp
struct DeviceInstanceState {
    DevicePlantState plant;
    DeviceFcState fc;
    DeviceSensorState sensors;
    DeviceFaultState faults;
    SimRandom random;
    ControlCommand command;
    SensorFrame sensor;
    uint32_t seq;
    uint32_t active;
    uint32_t exit_reason;
};
```

`DevicePlantState`、`DeviceFcState`、`DeviceSensorState` 和 `DeviceFaultState` 需要从实际字段审计后定义，而不是直接复用同名假设类型。每个设备结构必须通过固定大小、无指针、trivially-copyable、standard-layout 等编译期检查，并明确 host 结构到 device 结构的字段映射。

禁止在 kernel 内：

- `malloc/free`；
- `FILE*`；
- JSON/cJSON；
- UDP/socket；
- `printf` 日志；
- 动态容器；
- 指向主机内存的指针。

实例配置在主机完成 JSON 解析后，转成 `DeviceConfig` 一次性上传。场景中的地形 tile、气动表和故障脚本也必须预解析为扁平数组。

### 2.3 数据布局

采用混合布局：

- 实例状态：按实例组织的固定大小结构，便于一个 block 处理一个实例；
- 高频独立字段：必要时按字段 SoA 化，例如 `active[]`、`seq[]`、`status[]`；
- 地形/气动表：扁平化只读数组，优先放入只读缓存或纹理访问路径；
- 轨迹：使用固定 stride 的二维布局：

  ```text
  trace[instance_id * max_steps + step]
  ```

- 轨迹记录使用稳定的二进制结构，避免直接复制含 padding 的 C 结构体；
- host 端根据记录重新生成现有 `trajectory.csv`、`trajectory_diagnostics.csv`、binary logs 和 summary。

CUDA 的内存访问优化应以连续、合并访问为目标；具体布局在初版 kernel 完成后通过 Nsight Compute 的 global load/store 指标调优。[CUDA Best Practices Guide](https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/contents.html)

### 2.4 完整轨迹缓冲

由于当前选择保留完整轨迹，按下式估算显存：

```text
trace_bytes =
    instance_count × (max_steps + 1) × sizeof(TrajectoryRecord)
```

实现必须在启动前检查：

- 设备显存容量；
- 状态内存；
- 轨迹内存；
- 临时 workspace；
- 双缓冲传输空间。

若完整轨迹超过显存，透明切换为 chunked trace buffer：逻辑上仍保存全部轨迹，物理上分块回传和落盘，不改变输出格式和验证结果。

实现前必须给出 `TrajectoryRecord` 的实际字节数和显存预算，并分别报告 state、terrain、气动表、trace、临时 workspace 与双缓冲的占用。完整轨迹还必须提供 `FULL` 与 `SUMMARY_ONLY` 两种运行模式；端到端性能不得只报告 kernel 时间，必须单独计量 H2D/D2H、CPU 解码和落盘时间。

## 3. 转换器与手动分工

### 3.1 转换器负责的：纯计算模块

转换器只负责经过筛选的局部纯计算函数或仿射循环，不承诺对 `staging/` 中每个 `.c` 文件完整转换，也不承诺转换结果可以直接从 kernel 调用。每个候选模块必须先通过独立的 device 编译和 host 参考测试：

```text
能转换的（纯计算 + 仿射循环）：
  vec3.c, matrix3.c, quaternion.c
  missile_plant_6dof.c 的内层循环
  aero_model.c 的查表循环
  actuator_model.c 的计算循环
  sensor_noise.c, sensor_imu.c, sensor_accel.c, sensor_seeker.c
  mass_model.c, propulsion_model.c
  gravity_model.c, atmosphere_model.c, geo_coordinate.c
  environment_force_model.c
  estimator.c, navigation.c, guidance_png.c, autopilot.c
  → 逐模块试转；成功模块进入 converted，失败模块进入 NOT_CONVERTED.md
```

预处理流程（在 `staging/` 副本上操作，不改原项目）：

```text
原项目 .c → 复制到 staging/ → 剥离 host 依赖 → 转换器转换 → converted/.cu
                │                    │
                │ 不改原项目          │ 预处理工作量按模块评估，不假设每文件仅 5 行：
                │                    │   - 删除 printf/fprintf 调用
                │                    │   - 隔离 #include <math.h>
                │                    │   - 删除 logger_info 调用
                │                    │   - 文件加载部分用 #ifdef GPU_BUILD 隔离
```

转换器输入约束（在 staging/ 副本上必须满足）：

| 约束 | 处理方式 |
|---|---|
| 源文件路径不能含 `include` | staging/ 目录结构不含 include 路径 |
| 循环必须是仿射 for | WhileToFor pass 先转 |
| 设备调用图闭合 | ROSE doInline、设备适配声明或手动重写 |
| 无指针别名 | 手动消除或避开 |
| C11-compatible subset | 按 nvcc/device 编译结果处理 |

除语法转换外，还必须检查 `__device__` 调用链、静态全局、指针所有权、函数指针、动态内存、文件访问和 host-only 数学/库函数。复杂状态机、terrain cache、气动表加载和故障脚本解析不应默认交给转换器。

### 3.2 转换器不负责的：主循环 + 通信 + IO + 状态机

以下模块在 GPU 上无法执行，必须由手写调度层替代：

```text
不能转换的（在 GPU 上无法执行）：
  env_app_run() 主循环       → gpu_campaign.cu 替代（启动 N 个 block）
  UDP send/recv               → simulate_step.cu 替代（内存传递 sensor 结构体）
  JSON 配置解析               → config_loader.c 替代（CPU 端解析后上传）
  fprintf 日志                → trace_buffer.cu 替代（写显存缓冲，结束后回传）
  fork/waitpid                → 不需要（GPU 单进程）
  fc 状态机模式切换           → simulate_step.cu 手写（调用转换后的计算函数）
```

### 3.3 手写调度层清单

`gpu_campaign/` 是主要的 GPU 适配和调度层，不碰原项目；但设备状态、头文件适配、部分状态机和无法转换的计算边界可能需要手写。其职责包括：

1. 调用转换器生成的 `.cu` 里的计算函数（如 `vec3_add`、`matrix3_mul`）
2. 用内存传递替代 UDP（直接把 `sensor` 结构体传给 `fc`，不经过网络）
3. 用缓冲替代 `fprintf`（写显存，不写文件）

| 手写文件 | 行数 | 内容 |
|---|---|---|
| `main.cu` | ~100 行 | 入口：读配置、分配显存、启动 kernel、回传结果 |
| `gpu_campaign.cu` | ~150 行 | 调度器：N 个 block 并行、chunk 循环、多 stream 管理 |
| `simulate_step.cu` | ~200 行 | 单步逻辑：env_sensor→fc_step→env_truth（调用转换后的计算函数） |
| `trace_buffer.cu` | ~80 行 | 轨迹缓冲写入和批量回传 |
| `config_loader.c` | ~100 行 | 读 JSON→DeviceConfig（CPU 端，用 cJSON） |
| `terrain_loader.c` | ~50 行 | 地形数据加载→device buffer（CPU 端） |
| `result_writer.c` | ~80 行 | 回传结果→CSV/BIN/JSON（CPU 端） |
| **合计** | **初始估算，不作为承诺** | **需以 Phase 1 转换结果重新估算** |

原先约 630～760 行的估算仅覆盖调度草图，未包含设备结构、头文件适配、数据扁平化、错误处理、checkpoint、编译测试和未转换模块的人工实现。不得以该数字作为交付承诺；应在完成单实例垂直切片后按模块重新估算。6DOF、飞控、terrain 和查表逻辑是否能由转换器生成，必须以逐模块编译和数值测试结果为准。

### 3.4 地形和查表模型

当前 `scenario.json` 默认启用 terrain、LOS occlusion 和 bilinear interpolation。GPU 初始化阶段应：

1. 主机读取 tile index；
2. 加载本次 campaign 需要的 tile；
3. 将 tile 样本转为连续 device buffer；
4. 将经纬度范围、步长、tile offset 等元数据上传；
5. kernel 内只执行索引和插值，不访问文件系统。

当前原项目 terrain 支持懒加载和固定容量 LRU cache；GPU 侧不能照搬该 host cache，必须为一次 campaign 固定工作集并上传扁平 tile，或明确实现设备侧固定容量 cache。若 terrain 数据量过大，第一阶段可以固定测试场景为 `FLAT_FILL`，完成主物理链路验证后再启用完整 terrain。

## 4. 数值一致性策略

GPU 与现有 CPU 程序直接比较，不额外构造第三套 bitwise reference。由于主机 libc 与 CUDA device math 对 `sin/cos/sqrt/log` 等函数的实现可能不同，验收采用逐时间步、逐字段的 double 容差比较。

### 4.1 数学函数与编译参数

- 继续使用编译器/平台原生数学函数，不自实现跨平台的 `sin/cos/sqrt/log/exp` 数学库；
- CPU 和 GPU 均禁用 fast-math：CPU 使用 `-fno-fast-math`，CUDA 不使用 `--use_fast_math`；
- 固定 CUDA、host compiler、优化级别和目标架构，允许编译器按固定配置生成 FMA；
- 所有中间状态保持 `double`，避免无意的 `float` 降精度；
- 对数值误差设置明确阈值，发现误差随时间步持续放大时，优先定位算法/状态布局错误，而不是扩大容差。

### 4.2 随机数

当前 `xorshift64*` 生成器适合设备化：

- 每个实例独立保存 RNG state；
- seed 由 `instance_id` 和 runtime seed 明确派生；
- 不使用共享 RNG；
- 保持随机数调用顺序；
- GPU 与 CPU 共享整数位运算和 `[0,1)` 转换逻辑；
- Box-Muller 等涉及数学函数的路径使用与 CPU 基线一致的原生数学函数，并纳入容差验证。

### 4.3 容差比较

扩展 `compare_runs.py` 的现有 `--rtol/--atol` 接口，`1e-10/1e-12` 仅作为初始候选值，必须通过短场景、长场景和字段级误差曲线校准。不同字段可采用不同阈值，不得因为误差累积而无依据地放宽全局容差：

- `trajectory.csv` 和 `trajectory_diagnostics.csv` 按行、按字段比较 double 相对误差；
- 对接近零的值使用绝对误差 `1e-12`；
- binary 日志先解码为记录字段，再按数值容差比较；协议头、实例号、序列号、状态码等整数/枚举字段必须完全一致；
- JSON 中排除 wall time、设备名等非语义字段；
- `hit_flag`、`exit_reason`、实例数量和完成/失败计数必须完全一致；
- `miss_distance` 使用更严格的相对误差 `1e-8`；
- 对位置、速度、姿态、角速度和传感器字段分别记录误差包络；
- 命中、碰撞、LOS 等临界阈值增加 borderline 场景，不能只用“完全一致”覆盖数值临界情况；
- 发生差异时报告：

  ```text
  instance_id
  step
  field
  CPU value
  GPU value
  relative/absolute error
  前一个一致字段
  ```

- 对每个阶段设置 checkpoint：
  - 初始化；
  - sensor frame；
  - control command；
  - truth state；
  - diagnostics；
  - 最终 summary。

## 5. 分阶段实施

### Phase 0：基线和性能剖析

在修改 GPU 代码前，补齐以下指标：

- 固定主仓库和 Aircraft-Simu submodule commit；
- 固定并提交 N=1、N=2、N=128 的可复现配置；
- 记录当前 checkout 与 gitlink 的差异，禁止依赖未提交的 submodule 工作树；
- CPU 顺序执行时间；
- CPU `max_parallel_instances = 2/4/8/...` 时间；
- 单实例 wall time；
- env 计算时间；
- fc 计算时间；
- UDP send/recv 和等待时间；
- 进程启动与回收时间；
- 日志写入时间；
- 每实例步数；
- summary 生成时间。

使用 Nsight Systems 观察 CUDA API、kernel、内存传输、CPU/GPU 时间线，并使用 NVTX 标记 campaign、chunk、env、fc、trace copy 等区间。[Nsight Systems User Guide](https://docs.nvidia.com/nsight-systems/UserGuide/index.html)

基线矩阵至少包括：

```text
N = 1, 2, 8, 32, 64, 128
schedule = SEQUENTIAL / PARALLEL
logging = full / reduced
terrain = disabled / enabled
faults = disabled / enabled
```

目前仓库只有 `configs/baseline/runtime.json` 和 `runtime_128.json`，`Parallel_test.md` 中提到的 `AccuracyTests/runtime_64.json` 尚未出现在当前 checkout 中；应先提交一套可复现的测试配置，不能依赖外部路径。

### Phase 1：预处理与转换器验证

不改原项目，在 `gpu_workspace/staging/` 上操作：

1. 从原项目复制纯计算源文件到 `staging/`；
2. 剥离外部依赖（`printf`、`fprintf`、`logger_info`、`math.h` 的非兼容部分）；
3. 对每个模块用转换器试转，验证 `.cu` 可编译；
4. 编译验证：nvcc 编译 `converted/*.cu`，确认无语法错误；
5. 功能验证：编写 host 测试调用转换后的函数，与原 CPU 函数对比（`rtol=1e-10`）。

涉及的文件：

```text
common/:       vec3.c, matrix3.c, quaternion.c, random.c
environment/:  missile_plant_6dof.c, actuator_model.c, aero_model.c,
               aero_database.c, mass_model.c, atmosphere_model.c,
               gravity_model.c, geo_coordinate.c, sensor_noise.c,
               sensor_imu.c, sensor_accel.c, sensor_seeker.c,
               sensor_speed.c, environment_force_model.c,
               propulsion_model.c, fault_injection.c
flight_control/: estimator.c, navigation.c, guidance_png.c,
                 guidance_manager.c, autopilot.c, command_manager.c,
                 safety_monitor.c, fc_modes.c
```

这一阶段不要求转换器覆盖所有模块，必须产出逐模块分类：`converted-and-device-buildable`、`host-only`、`manual-port` 或 `blocked`。只有通过 device 编译、host 参考测试和调用链审计的模块，才允许进入 GPU 集成。

### Phase 2：单实例 GPU 正确性

先实现单实例最小垂直切片，再扩展完整调用链。建议首个切片只包含 Vec3/Quaternion、PlantState6Dof、简化 actuator 和简化 sensor，运行 100～500 步；不得一开始就承诺完整 terrain、fault 和全部飞控状态机。

手写 `gpu_campaign/` 调度层，实现单实例 GPU 运行：

```text
instance_count = 1
max_steps = 小规模
full trace = enabled
terrain = disabled
faults = disabled
```

`simulate_step.cu` 的单步逻辑：

```text
simulate_step(state, config, trace):
    1. build_sensor_frame(&state, &sensor)
       → 调用 converted/environment/sensor_*.cu
    2. flight_controller_step(&fc_state, &sensor, &command)
       → 调用 converted/flight_control/*.cu
    3. update_truth(&state, &command)
       → 调用 converted/environment/missile_plant_6dof.cu 等
    4. write_trace(trace, &state)
       → 写 trace_buffer（显存，不写文件）
```

验证内容：

- 初始化状态；
- 每一步 sensor/control/truth；
- 轨迹；
- summary；
- exit reason；
- RNG state；
- 容差 mismatch 定位。

此时不追求性能，只保证执行顺序和数据格式正确。

进入 Phase 3 前必须满足：所有被 kernel 直接或间接调用的函数均通过 device 编译；CPU/GPU 在初始化、sensor、control、truth 和 RNG checkpoint 上可定位比较；设备结构和 trace 编码已经固定。

### Phase 3：批量实例 GPU 化

扩展到：

```text
N = 2, 8, 32, 64, 128
```

实现：

- 正确性基线为一个 block 对应一个实例；性能实验允许替换实例与线程的映射；
- 独立状态；
- 独立 seed；
- inactive instance mask；
- 变量终止时间；
- 完整轨迹；
- 多 stream chunk copy；
- 主机统一落盘。

1 thread/instance 只作为正确性基线。批量阶段必须执行线程映射实验，并将 occupancy、register usage、branch efficiency、kernel time、传输时间和端到端 instances/s 纳入门槛。若 64/128 实例仍只有少量活跃 warp，则必须评估 1 warp/instance 或其他映射，不能直接进入“性能达标”结论。

批量结果必须满足实例不变性：

```text
GPU(N=1, instance=i) ≈ GPU(N=64, instance=i)
```

即改变批大小、block 调度和 chunk 大小，不得使单实例输出超出既定误差阈值。

### Phase 4：完整场景和故障

依次启用：

1. sensor noise；
2. dropout/delay；
3. fault injection；
4. terrain；
5. LOS occlusion；
6. 完整 flight-control 状态机；
7. 全部 AccuracyTests 场景。

只有上一阶段全部通过，才启用下一阶段。

### Phase 5：性能优化

按以下顺序优化：

1. 共享内存和寄存器使用；
2. 状态结构布局；
3. terrain/气动表缓存；
4. warp lane 分工（仅在 Nsight 显示计算瓶颈时执行）；
5. chunk 大小；
6. host/device 双缓冲；
7. CUDA Graph；
8. 多 GPU 分片。

性能优化不能改变算法步骤顺序、随机数调用顺序和日志记录顺序。

## 6. 运行接口和兼容性

由于 `external/Aircraft-Simu/` 保持不变，以下配置应放入 GPU workspace 或由命令行提供，不要求修改原项目的 `runtime.json`：

```json
{
  "campaign": {
    "backend": "CPU"
  },
  "gpu": {
    "device_id": 0,
    "instances_per_batch": 64,
    "chunk_steps": 256,
    "trace_mode": "FULL",
    "deterministic": true
  }
}
```

支持：

```text
backend = CPU
backend = CUDA_BATCH
```

行为约定：

- `CPU`：继续使用现有 instance_manager、env/fc 双进程和 UDP；
- `CUDA_BATCH`：启动 `gpu_campaign`，不启动 env/fc 子进程；
- GPU 不可用或初始化失败时默认返回错误，不静默切换，避免性能测试结果混淆；
- 可增加显式 `--fallback-cpu`，仅用于生产容错；
- CPU 路径始终保留，作为回归和无 GPU 环境的后备实现。

推荐运行接口：

```bash
cd gpu_workspace
./gpu_campaign --config configs/gpu_runtime_128.json \
               --output output/128instances
python3 ../compare_runs.py \
    output/128instances output/cpu_baseline \
    --rtol 1e-10 --atol 1e-12
```

`compare_runs.py` 使用两个结果目录位置参数，不支持 `--gpu` 和 `--cpu` 命名参数。

多 GPU 暂不引入 MPI/NCCL。后续可由一个 host manager 为每块 GPU 启动一个 `gpu_campaign`，按 instance ID 分区，最后合并结果；实例之间没有通信需求，因此不需要 GPU 间同步。

## 7. 验收标准

### 正确性

- 所有实例所有轨迹记录满足经校准的字段级容差；`rtol=1e-10, atol=1e-12` 仅作为初始默认值；
- `N=1/2/64/128` 的同一实例结果均在相同容差内一致；
- terrain、fault、noise、dropout、变量终止均通过；
- 失败实例不影响其他实例输出；
- 日志文件数量、目录结构和字段格式与 CPU 版本一致；
- 使用两个结果目录位置参数调用 `compare_runs.py`，并通过初始或校准后的 `--rtol/--atol` 检查；
- `hit_flag`、`exit_reason` 和实例完成状态完全一致。
- 所有设备结构通过固定大小、无指针和可复制性检查；所有 kernel 传递调用函数通过 device 编译；
- 所有生成文件具有源 commit、转换参数和失败模块清单，可重复生成。

### 性能

同时报告：

```text
CPU sequential campaign wall time
CPU process-parallel wall time
GPU compute-only wall time
GPU full-trace wall time
GPU end-to-end wall time
host trace write time
H2D/D2H transfer time
GPU occupancy
kernel duration
effective instances/s
```

建议阶段性目标：

- GPU 计算部分达到 CPU 单实例顺序执行的明显加速；
- 64/128 实例批量时，完整端到端时间至少优于现有 CPU 并发基线；
- 初步工作目标为计算部分 ≥5×、完整轨迹端到端 ≥2×；最终目标以真实 GPU 型号和 Nsight profiling 结果校准；
- 如果 full-trace I/O 成为瓶颈，必须同时给出 `FULL` 和 `SUMMARY_ONLY` 两种吞吐指标，不能只报告 GPU kernel 时间。

## 8. 主要风险与处理原则

- **CPU/GPU 数学函数差异**：通过固定编译参数和明确容差处理；若误差超过阈值，先定位数值传播或实现错误，不直接放宽验收标准。
- **warp divergence**：单线程 block 版本无 lane 分歧；只有启用 warp 分工后才需要按场景/故障类型分批，减少同一实例内部的路径差异。
- **terrain 随机访问**：先使用扁平只读 buffer，再根据 Nsight 指标决定纹理或缓存策略。
- **完整轨迹显存不足**：采用 chunked trace buffer，保持逻辑上的完整日志。
- **GPU kernel 太长**：使用 chunk kernel，而不是无限时长单 kernel。
- **日志成为瓶颈**：GPU 负责产生固定格式记录，CPU 负责批量落盘；性能测试必须区分计算和 I/O。
- **Auto-C-To-CUDA 误转换复杂控制流**：自动转换器仅用于局部仿射纯计算循环；复杂状态机和通信边界手工编写。
- **设备状态并非 POD**：host 的路径、缓存、文件和指针字段不得上传；建立独立 Device* 结构并进行编译期审计。
- **单线程 block 利用率不足**：将 1 thread/instance 限定为正确性版本，以 Nsight 指标触发备用线程映射实验。
- **trace 双缓冲竞态**：使用 chunk 元数据、CUDA event 和明确的 buffer ownership，禁止跨 stream 隐式复用。
- **容差过严或临界状态不稳定**：按字段校准误差包络，并增加命中、碰撞、LOS 的 borderline 测试。
- **转换器无法处理某文件**：该文件记入 `converted/NOT_CONVERTED.md`，由 `simulate_step.cu` 手动实现对应逻辑或跳过该功能。
- **子模块/测试配置不一致**：固定 Aircraft-Simu gitlink，提交 runtime 配置和 AccuracyTests，不依赖未提交的外部 checkout。
