# 修复报告：DCE 误删 `output_index` 声明及 SSA 时序问题

## 问题

```bash
cd output/Aircraft-Simu/build && cmake .. && make
```

编译 `tools/map_preprocess/main.cu` 时报错：

```
main.cu(1082): error: identifier "output_index" is undefined
main.cu(1084): error: identifier "output_index" is undefined
```

原始 C 代码 `read_samples()` 中：

```c
for (index = 0u; index < count; ++index) {
    size_t output_index = index;          // ← 声明被翻译工具丢掉
    ...
    samples[output_index] = (int16_t)rounded;
}
```

翻译后的 `.cu` 文件缺少 `size_t output_index = index;` 声明。

---

## 定位过程

### 第一层：排除 AstPostProcessing

怀疑 ROSE 内建 `AstPostProcessing()` 自带 DCE 误删。去掉后单文件翻译成功，但全量翻译崩溃：

```
Rose[FATAL]: virtualCFG/memberFunctions.C:174
             getNodeJustAfterInContainer: parent == null
```

→ `AstPostProcessing` 修 parent 指针不可省略。

### 第二层：SSA 时序错误

原始代码中全局 SSA 在归一化**之前**创建（`translate.cpp:272-274`），但 DCE 在归一化**之后**运行（`translate.cpp:501`）。归一化修改了循环体 AST（index 引用替换，表达式树变化），导致 SSA 内部节点映射过时。

修复：将 SSA 创建移到归一化之后，每个项目只算 2 次（Phase 1: 归纳变量暴露，Phase 2: DCE）。

### 第三层：SSA VarName 版本后缀匹配失败

ROSE SSA 的 `VarName` 可能带版本后缀（如 `{decl, version}`），但 DCE 的 `makeVarName()` 只创建单元素 `{decl}`。`defMap.find(vn)` 精确匹配失败，导致声明标记不到 def-use 链。

修复：查找时只取 `vn[0]`（基础声明）做 key。

### 第四层（根因）：ROSE SSA 不追踪数组下标 use

添加 debug 日志后发现：`samples[output_index] = ...` 这个数组写语句，ROSE SSA 只报 `rounded` 的 use，**不报 `output_index`**。

```log
DCE uses: useful stmt has 1 SSA vars
DCE uses:   var=rounded vn_sz=1
```

数组下标引用（`output_index` 在 `samples[output_index]` 里）不被 ROSE SSA 视为 use——这是 ROSE 内部的设计限制。DCE 反向传播永远看不到 `output_index` 有下游 use，于是将其标记为非 useful 并删除。

修复：在 SSA 查找之外增加直接 AST 扫描——对每个 useful 语句，遍历其 `SgVarRefExp` 子节点，直接用 `SgInitializedName` 查 defMap。数组下标变量由此被捕获。

---

## 根因总结

| 层次 | 根因 | 位置 |
|------|------|------|
| 1 | `AstPostProcessing` 内建 DCE 误删 | `normalize.cpp:39` |
| 2 | SSA 在归一化前计算、DCE 在归一化后运行 | `translate.cpp` |
| 3 | SSA VarName 版本后缀不匹配 | `deadCodeElim.cpp:226` |
| 4 | ROSE SSA 不报告数组下标 use | `deadCodeElim.cpp` |

---

## 修改文件清单

| 文件 | 修改 | 类型 |
|------|------|------|
| `translate.cpp` | SSA 从归一化前提出来，双 pass 架构：Pass1(收集→归一化) → Phase1(SSA→inductVar) → Phase2(SSA→DCE) → Pass2(affine/dep/codeGen) | **BUG FIX** |
| `src/preprocess/deadCodeElim.cpp` | SSA VarName 只取 `vn[0]` 去版本；增加直接 AST fallback 扫描捕获数组下标 use | **BUG FIX** |
| `src/normalize/normalize.cpp` | `for(;;)` null-test 提前捡测；`AstPostProcessing` 后恢复被误删声明 | **BUG FIX** |

---

## 验证

```bash
make debug
./build/bin/translate.out -p external/Aircraft-Simu/build/compile_commands.json -O output/Aircraft-Simu/
cd output/Aircraft-Simu/build && cmake .. && make
```

- 翻译正常完成，522s
- 输出编译 100% 成功，无 `output_index` undefined 错误
- `main.cu:1043` 保留 `size_t output_index = 1 * index + (((size_t )0u) - 1);`
