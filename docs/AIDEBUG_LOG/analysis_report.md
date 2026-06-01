# test.c 翻译为 test.cu 的错误分析报告

## 问题描述

输入 `test/test.c`：
```c
int arr[10];
int main()
{
    int t = 10;
    for (int i = 0; i < 10; i++)
    {
        t = t + 1;
        arr[t] = t;
    }
}
```

输出 `test/test.cu`（核心部分）：
```c
__global__ void _auto_kernel_0(int arr[10],int t)
{
    int thread_x_id;thread_x_id = blockIdx.x * blockDim.x + threadIdx.x + 1;
    if (thread_x_id)
        if (thread_x_id <= 10) {
            arr[t + 1] = t + 1;
        }
}
```

**错误**：`t = t + 1` 是一个归纳变量，迭代间有数据依赖（循环进位依赖 loop-carried dependency），该循环不能被直接并行化。翻译器错误地生成了并行 kernel，且 `arr[t + 1] = t + 1` 中所有线程都使用初始值 `t=10`，语义完全错误（正确行为应是 `arr[11]=11, arr[12]=12, ..., arr[20]=20`）。

---

## 流水线故障链追踪

翻译流水线（`translate.cpp:456-476`）依次执行：
1. 循环归一化 (`normalizeLoopNest`)
2. 归纳变量替换 (`inductionVariableExposure`)
3. 死代码消除 (`eliminateDeadCode`)
4. 仿射测试 (`affineTest`)
5. 依赖测试 (`dependencyExists`)
6. 代码生成/并行性提取

### 故障根因：`forwardSub` 未正确处理自引用归纳变量

**文件**：`src/preprocess/InductionVariableExposure.cpp:184-250`

#### forwardSub 的逻辑

`forwardSub` 对语句 `t = t + 1` 的处理过程：

```cpp
// stmt = "t = t + 1", defVar = t, rhs = "t + 1"
std::set<SgInitializedName*> usedVars = allVarRefsIn(rhs);  // usedVars = {t}

for (SgInitializedName* var : usedVars) {
    if (var == defVar) continue;  // ← 关键：t == defVar，直接跳过！
    // ... 对其他变量的检查
}
// for循环结束后，没有任何 return true
// 然后错误地进入 Step 2 前向替换
```

**问题**：当 `var == defVar`（自引用）时，代码直接 `continue`，跳过了对该变量的 reaching definition 分析。但在 `t = t + 1` 中，RHS 的 `t` 是循环携带的（从上一轮迭代的 phi 节点来），它并不是一个不变量。由于跳过了检查，`forwardSub` 认为没有发现"循环变体输入"，错误地返回 `false`，导致：

1. Step 2：将 `arr[t] = t` 中所有 `t` 替换为 RHS 的拷贝 `t + 1` → `arr[t + 1] = t + 1`
2. Step 3：由于 `allUsesGone && allLoopUsesGone` 为 true，删除 `t = t + 1`

#### 本应调用的 ivSub 未被调用

驱动函数 `ivDrive`（`InductionVariableExposure.cpp:454-463`）：
```cpp
bool fsNotDone = forwardSub(stmt, loop, loopIndex, ssa);
if (fsNotDone) {
    ivSub(stmt, loop, loopIndex, ssa);  // ← 永远不会被调用！
}
```

由于 `forwardSub` 错误返回 `false`，`ivSub` 从未执行。`ivSub` 本来应该检测到 `isIV(t = t + 1, ...)` 返回 true（增量为 1），并将使用替换为 `t + i`（初始值 + 迭代次数），而非简单的 RHS 拷贝。

### 小节：归纳变量替换的 bug 位置

| 位置 | 问题 |
|---|---|
| `src/preprocess/InductionVariableExposure.cpp:196` | 自引用变量（`var == defVar`）被直接跳过，没有检查其 reaching definition 是否来自循环内部 |
| `src/preprocess/InductionVariableExposure.cpp:458-462` | `forwardSub` 返回 `false` 时不再调用 `ivSub`，但本例中本应调用 |

**正确的行为**：当 `defVar` 出现在 RHS 的 `usedVars` 中，且其 reaching definition 来自循环内部时，`forwardSub` 应返回 `true`，触发 `ivSub`。

---

### 依赖测试为何通过

在 IV 前向替换之后，循环体变为：

```
arr[t + 1] = t + 1;
```

然后 `eliminateDeadCode` 运行——但对 `t + 1` 中的 `t` 引用是读操作，不产生死代码。

**仿射测试**：`arr[t + 1]` 的下标 `t + 1` 是符号常量 `t`（来自循环外）的线性表达式，通过仿射测试。

**依赖测试**（`src/dependency/dependency.cpp:19-197`）：

1. `collectReadWriteRefs` 收集：写引用为 `arr`（下标 `t+1`），读引用为空
2. `collectReadWriteVariables` 收集：写变量为 `{arr}`，读变量为 `{t}`
3. `dep_vars` 仅包含数组写变量：`{arr}`
4. 在 `dependencyTests` 中：
   - `write_refs` 大小为 1（只有一条写引用）
   - 第一个 `for` 循环：`w_it` 从 0 到 `write_refs.size() - 1 = 0`，不执行
   - 第二个 `for` 循环：`read_refs` 为空，不执行
   - `test_flag` 保持 0（无依赖）

**此外**：代码中有对非数组写变量的保护（`src/dependency/dependency.cpp:44-47`）：
```cpp
else
    if((*it)->get_scope() != isSgScopeStatement(body))
        return 2;
```
该检查本应拦住 `t` 的写操作——但 `t = t + 1` 已被 `forwardSub` 删除，此时 `write_vars` 中已无 `t`，因此此保护失效。

---

## 完整的故障传播链

```
原始 for 循环：t = t + 1; arr[t] = t;
    ↓
归一化：i → 1..10（不影响 t）
    ↓
forwardSub(t = t + 1)：
  ├─ 跳过自引用检查（var == defVar）
  ├─ 错误执行前向替换 → arr[t+1] = t+1
  └─ 删除 t = t + 1
    ↓
死代码消除：arr[t+1] = t+1 有用，保留
    ↓
仿射测试：arr[t+1] 的 t 是符号常量 → 通过
    ↓
依赖测试：只有一条数组写引用 → 无依赖 → 返回 0
    ↓
kernelCodeGenSimple：生成并行 kernel
    ↓
结果：所有线程都用 t=10，arr[t+1]=t+1 并行写 arr[11]
```

---

## 修正方案

在 `src/preprocess/InductionVariableExposure.cpp` 的 `forwardSub` 函数中，在 `for` 循环结束后，检查 `defVar` 是否出现在 `usedVars` 中且其 reaching definition 来自循环内部：

```cpp
// for 循环之后，Step 2 之前添加：
if (usedVars.count(defVar)) {
    // defVar 自引用：检查其 reaching definition 是否来自循环内部
    StaticSingleAssignment::ReachingDefPtr def =
        getReachingDefAtStmt(ssa, stmt, defVar);
    if (def && defIsInLoop(def, loop)) {
        return true;  // 循环携带依赖，需要 IVSub
    }
}
```

这样，`ivSub` 会被正确调用，识别 `t = t + 1` 为归纳变量，将 `arr[t] = t` 替换为 `arr[t + i] = t + i`（其中 `i` 是起始为 1 的归一化循环索引），使得每个迭代/线程计算正确的值。
