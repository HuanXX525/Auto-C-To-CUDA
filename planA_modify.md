# planA.md 修改建议

> 基于 planA.md 的审查结论。整体架构合理，分阶段策略扎实，以下为需要调整的要点。

---

## 1. 核心修改：bitwise 一致性降级为容差比较

### 问题

planA.md Section 4 要求 GPU 与 CPU 逐位一致（bitwise），并为此设计了：

- 自定义 `sim_math_reproducible.cuh`（自实现 sin/cos/sqrt/log/exp）
- 禁止 fast-math、禁止隐式 FMA
- 与"共享确定性 CPU reference build"比较（需额外写第三个实现）

### 为什么过度

1. **工作量巨大**：自实现一套跨平台 bitwise 一致的 sin/cos/sqrt/log/exp，可能比整个 GPU 迁移本身还费工。每个函数要固定算法、阶数、系数、舍入方式，并在 CPU/GPU 上逐 bit 验证。
2. **FMA 无法真正禁止**：GPU 硬件原生支持 FMA，`--fmad=false` 只影响编译器是否主动生成，某些情况硬件隐式使用。CPU 的 FMA 支持取决于架构。实践上无法完全保证。
3. **参考对象混乱**：不和现有 CPU 程序比，而是和"共享确定性 CPU reference build"比，需要先写第三个实现并验证它和现有 CPU 程序行为一致，再验证 GPU 和它 bitwise 一致。三重验证链。

### 建议

改为容差比较：

```
比较方式：相对误差 < 1e-10（double 精度容差）

验收标准：
- 逐时间步对比 trajectory.csv 每个字段
- |GPU - CPU| / max(|CPU|, epsilon) < 1e-10
- summary.json 的 hit_flag、exit_reason 必须完全一致
- miss_distance 相对误差 < 1e-8
```

效果：
- 省去自写数学库的巨大工作量
- 用现有 CPU 程序直接作为基线（不用写第三个 reference）
- 仍然能抓到所有真正的 bug（算法错误、索引错误、符号错误）
- `compare_runs.py --exact` 改为 `compare_runs.py --tolerance 1e-10`

### 对应文档修改

| planA.md 原文 | 修改为 |
|---|---|
| Section 4 标题"位级一致性策略" | "数值一致性策略" |
| 4.1 共享数学库 | 删除自实现数学库，改用编译器原生 math 函数，仅固定编译参数（`-fno-fast-math`） |
| 4.1"禁止隐式 FMA" | 删除，改为"允许 FMA，但编译参数固定" |
| 4.3"轨迹 binary 文件按字节比较" | "轨迹字段按相对误差 < 1e-10 比较" |
| 4.3"数值字段使用十六进制 bit pattern 比较" | "数值字段使用 double 相对误差比较" |
| Phase 1"逐字段 bitwise 对比" | "逐字段容差对比（1e-10）" |
| Phase 2"bitwise mismatch 定位" | "容差 mismatch 定位" |
| Phase 3"不得改变单实例 bit pattern" | "不得改变单实例输出（1e-10 容差内）" |
| 验收标准"bitwise 一致" | "1e-10 容差内一致" |
| 风险"CPU/GPU 数学函数逐位差异" | 删除此项（改用容差比较后不再阻塞） |

---

## 2. 次要修改：warp 分工标记为可选

### 问题

planA.md Section 1.1 设计了"固定 lane 分工处理三维向量和矩阵"（x→lane 0, y→lane 1, z→lane 2），并在 Phase 5 列为优化项。

### 为什么可能不必要

- 单步计算量约 170 FLOPs（env ~100 + fc ~50 + sensor ~20）
- 用 32 线程处理 170 FLOPs，31 个线程大部分空闲
- `__syncwarp()` 和 `__shfl_sync()` 的同步开销可能超过 3D 向量拆分的收益
- 128 实例 × 1 线程 = 128 个活跃线程，并行度偏低但**瓶颈原本不是计算**（是 UDP 和进程调度）
- 同一 warp 内不同实例走不同分支会导致 warp divergence（`flight_controller_step` 有条件分支和模式切换）

### 建议

| planA.md 原文 | 修改为 |
|---|---|
| 1.1"通过固定 lane 分工处理三维向量、矩阵和独立标量" | "（可选）warp 内 lane 分工，仅在 profiling 显示计算成为瓶颈时启用" |
| 1.1"所有条件分支由固定 lane 计算后广播" | 删除（lane 0 单线程版不需要） |
| Phase 5 第 4 项"warp lane 分工" | 标注"仅在 Nsight 显示计算瓶颈时执行" |
| 风险"warp divergence" | 补充："lane 0 单线程版无此问题；仅在启用 warp 分工时需关注" |

---

## 3. 补充：状态机分支分歧说明

### 问题

planA.md Phase 4 提到"完整 flight-control 状态机"，但未说明 GPU 上如何处理条件分支。

### 建议

在 Section 1.1 补充一段：

```
flight_controller_step 含条件分支和模式切换（安全评估→导航→制导→
控制律→指令构建，每步可能走不同路径）。lane 0 单线程版中，不同实例
走不同分支不是问题（每个 block 独立执行）。若将来启用 warp 分工，
同一 warp 内不同实例走不同路径会导致 warp divergence，需按场景/
故障类型分批调度以减少路径差异。
```

---

## 4. 不需要修改的部分

以下部分经审查确认正确，无需改动：

- 双路径架构（CPU 基线 + GPU 新路径）
- block = instance 映射
- chunk kernel 设计（128-512 步/次）
- 多 stream + 双缓冲 + pinned memory
- `DeviceInstanceState` POD 设计（已验证 EnvTruthState/PlantState6Dof 无指针）
- `SIM_HOST_DEVICE` 双端共用策略
- 迁移边界（Auto-C-To-CUDA 仅管局部仿射循环）
- 地形预加载策略（先 FLAT_FILL）
- Phase 0-5 分阶段门控
- `backend = CPU/CUDA_BATCH` 配置切换
- 日志策略（GPU 缓冲二进制，CPU 批量落盘）
- 子模块 gitlink 风险提示
