# 正确性测试框架

`test/` 目录用于验证转化后的 CUDA 程序运行结果与原始 C 程序一致。修改代码后应运行测试，确保算法逻辑未被破坏。

## 测试逻辑

编译 C 和 CUDA 两个版本 → 运行 → 将关键数组保存为二进制文件 → `cmp` 比对。完全一致则通过。

## 命令

```bash
# 翻译全部 .c → .cu（先在容器内运行）
make test_translate

# 编译运行 + 比对（若 CUDA 不在容器中，在宿主机运行）
make test_run

# 单目录
make test_translate DIR=atax
make test_run DIR=atax

# 从某个目录续跑（跳过字母序之前的已通过目录）
make test_run START_DIR=correlation

# 强制全量重跑（跳过所有增量检查）
make test_run FORCE=1
make test_run START_DIR=gemm FORCE=1
```

## 新增测试

在 `test/` 下新建目录，放入 `.c` 源文件（支持多文件，但仅限一个 main）。输出数组时遵守：

```c
#ifdef AUTOC2CUDATEST
    save_binary("bin/cuda_out.bin", array, byte_size);
#else
    save_binary("bin/c_out.bin", array, byte_size);
#endif
```

`tool.h`/`tool.cpp` 提供 `save_binary()` 和 `now_ms()`，已在 `test/` 根目录，所有子目录通过 `#include "../tool.h"` 引用。

## 增量检测

时间戳比较避免重复编译，三类提示：

| 输出信息 | 含义 |
|---------|------|
| `[rebuild]` | 源文件变更，重新编译 + 运行 |
| `[re-run]` | 二进制未变但输出缺失（如 bin 被手动删除），仅重新运行 |
| 无额外输出 | 全部 up-to-date，仅做 `cmp` 比对 |

**跳过规则**：

| 步骤 | 跳过条件 |
|------|---------|
| translate (`.c` → `.cu`) | `main.cu` 存在且比所有 `.c` 新 |
| C 编译 | `C.out` 存在且比所有 `.c` + `tool.cpp` 新 |
| CUDA 编译 | `CU.out` 存在且比所有 `.cu` + `tool.cpp` 新 |
| 运行 | 二进制未重编且 `.bin` 输出文件存在 |

## 续跑

大范围测试中某个目录失败时，`make` 立即停止，后续目录不执行，`clean_bin` 也不触发（已通过目录的 `.bin` 保留）。修复后可用 `START_DIR=` 从失败处续跑：

```bash
# 假设 gemver 失败了
make test_run START_DIR=gemver FORCE=1
```

`START_DIR` 目录搜索顺序为**字母序**（与 `make test_run` 全量遍历顺序一致）：

```
2mm → 3mm → atax → bicg → complexTest → conv1d → correlation → 
covariance → declarationCopy → doitgen → fdtd-2d → fir16 → 
functionCall → gemm → gemver → gesummv → heat-3d → heat2d → 
jacobi-1d → jacobi-2d → mvt → poly10 → saxpy → seidel-2d → 
simpleCondition → simpleLoop → simple_2mm → symm → syr2k → 
syrk → test → trmm → vectorAdd
```

`DIR=xxx` 和 `START_DIR=xxx` 互斥（前者仅跑单目录，忽略 `START_DIR`）。

## 参数速查

| 参数 | 作用 |
|------|------|
| `DIR=xxx` | 仅测试指定目录 |
| `START_DIR=xxx` | 从指定目录开始续跑（字母序 >=） |
| `FORCE=1` | 跳过所有增量检查，强制全量重编译重跑 |
