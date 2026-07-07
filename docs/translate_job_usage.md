# `translate_job.h` 使用说明

`include/utils/translate_job.h` 的作用是把命令行解析结果收敛成一个统一的“翻译任务”对象，供 `translate.cpp` 这类入口程序使用。

它解决两件事：

1. 过滤掉项目自己的命令行参数。
2. 按单文件、多文件 `compile_commands.json`、多文件目录扫描三种模式，构造传给 ROSE 的参数列表。

## 1. 头文件里的核心类型

### `TranslateMode`

用于区分当前运行模式：

```cpp
enum class TranslateMode {
    SingleFile,
    CompileDB,
    ScanDir
};
```

含义：

- `SingleFile`：单文件模式，保留原有 ROSE 调用方式。
- `CompileDB`：从 `compile_commands.json` 收集 `.c` 文件、`-I`、`-D`。
- `ScanDir`：递归扫描目录收集 `.c` 文件。

### `TranslateJob`

`TranslateJob` 是最终产物，描述这次翻译任务：

```cpp
struct TranslateJob {
    TranslateMode mode;
    std::vector<std::string> source_files;
    std::vector<std::string> include_paths;
    std::vector<std::string> defines;
    std::vector<std::string> extra_rose_args;
    std::string output_dir;
    std::string base_dir;
    bool verbose;
};
```

几个关键字段：

- `source_files`：多文件模式下待翻译的 `.c` 文件列表，当前实现里是绝对路径。
- `include_paths`：最终合并后的 `-I`。
- `defines`：最终合并后的 `-D`。
- `extra_rose_args`：透传给 ROSE 的其他参数，比如 `-rose:skipfinalCompileStep`。
- `output_dir`：多文件模式输出根目录。
- `base_dir`：多文件模式的公共前缀目录，用于保持原目录结构。

### `TranslateJobBuilder::Result`

`build()` 不只返回 `TranslateJob`，还会返回完整的解析结果和退出标志：

```cpp
struct Result {
    TranslateJob job;
    ParsedArgs args;
    bool should_exit = false;
    int exit_code = 0;
};
```

用途：

- `job`：模式化之后的任务描述。
- `args`：原始命令行经过自定义参数过滤后的完整结果。
- `should_exit`：遇到 `--help` 或参数错误时为 `true`。
- `exit_code`：主程序可直接返回。

## 2. 标准用法

最常见的接法就是在 `main()` 入口处先调用 `build()`，再根据模式选择 ROSE 参数来源。

```cpp
#include "include/utils/translate_job.h"

int main(int argc, char** argv) {
    auto build_result = c2cuda::TranslateJobBuilder::build(argc, argv);
    if (build_result.should_exit) {
        return build_result.exit_code;
    }

    std::vector<std::string> rose_args_storage;

    if (build_result.job.mode == c2cuda::TranslateMode::SingleFile) {
        rose_args_storage = build_result.args.remaining;
    } else {
        const std::string program_name = argc > 0 ? argv[0] : "translate.out";
        rose_args_storage = build_result.job.buildRoseArgv(program_name);
    }

    auto rose_argv = c2cuda::CmdLineParser::toArgv(rose_args_storage);
    int rose_argc = static_cast<int>(rose_argv.size());

    ROSE_INITIALIZE;
    SgProject* project = frontend(rose_argc, rose_argv.data());
}
```

这里有两个要点：

- 单文件模式使用 `build_result.args.remaining`。
- 多文件模式使用 `build_result.job.buildRoseArgv(program_name)`。

## 3. 三种模式分别怎么工作

### 3.1 单文件模式

如果没有传 `--compile-db` 和 `--scan-dir`，就走单文件模式。

特点：

- `job.mode == SingleFile`
- `args.remaining` 里保留了要传给 ROSE 的原始参数
- `job.source_files`、`job.include_paths` 等字段不作为主输入使用

适合原来的调用方式：

```bash
./translate.out input.c -rose:o output.cu
```

### 3.2 `compile_commands.json` 模式

如果传了 `--compile-db=<path>`，就会：

1. 加载 `compile_commands.json`
2. 过滤其中的 `.c` 文件
3. 把文件绝对路径放入 `job.source_files`
4. 合并各条目的 `-I`
5. 合并各条目的 `-D`
6. 从 `remaining` 中提取额外 ROSE 参数到 `job.extra_rose_args`

然后通过：

```cpp
auto rose_args = job.buildRoseArgv(argv[0]);
```

得到一组新的 ROSE 参数：

```text
[argv0, -I..., -D..., extra_rose_args..., source1.c, source2.c, ...]
```

### 3.3 目录扫描模式

如果传了 `--scan-dir=<path>`，就会：

1. 递归扫描目录下所有 `.c` 文件
2. 跳过匹配 `exclude` 的文件
3. 把绝对路径放入 `job.source_files`
4. 把扫描目录本身加入 `job.include_paths`
5. 再追加命令行给的 `--include`

适合没有 `compile_commands.json`，但想一次处理一批 `.c` 文件的场景。

## 4. `TranslateJob` 里两个常用方法

### `buildRoseArgv(program)`

作用：把 `TranslateJob` 转成 ROSE 可直接消费的参数数组。

```cpp
std::vector<std::string> rose_args = job.buildRoseArgv(argv[0]);
```

再配合：

```cpp
auto rose_argv = c2cuda::CmdLineParser::toArgv(rose_args);
```

注意：

- `toArgv()` 返回的是指向 `std::string` 内部缓冲区的指针。
- `rose_args` 生命周期必须覆盖 `frontend()` 调用。

### `getOutputPath(source_file)`

作用：在多文件模式下，为某个源文件计算输出路径。

```cpp
std::string out = job.getOutputPath(source_file);
```

计算规则：

```text
output_dir / relative_path(source_file, base_dir)
```

然后把扩展名从 `.c` 改成 `.cu`。

例如：

```text
base_dir   = /work/src
output_dir = ./cuda_out
source     = /work/src/math/add.c
result     = ./cuda_out/math/add.cu
```

这里的 `source_file` 应该传当前任务里的源文件路径，当前实现下通常是绝对路径。

## 5. 命令行示例

### 单文件

```bash
./translate.out input.c -rose:o output.cu
```

### compile_commands.json

```bash
./translate.out --compile-db=build/compile_commands.json -rose:skipfinalCompileStep
```

### 扫描目录

```bash
./translate.out --scan-dir=./src --include=./include -rose:skipfinalCompileStep
```

## 6. 当前实现的限制和注意事项

这部分是按当前代码实际状态写的，不是设计目标。

### `--compile-db` 和 `--scan-dir` 互斥

这两个参数不能同时传，同时传会直接报错退出。

### `--output-dir` 目前在代码里还没有真正注册进 parser

虽然 `build()` 里会读：

```cpp
args.getOption("output-dir", "./c2cuda_out")
```

但 `createParser()` 里这一行现在还是注释状态：

```cpp
// .addOption("output-dir", "O", "Output directory for multi-file mode", "./c2cuda_out")
```

所以按当前代码，命令行传 `--output-dir` 还不会被 `CmdLineParser` 正式识别。

### `--include` 和 `--exclude` 当前只保留一个值

`CmdLineParser` 的 `options` 是 `unordered_map<string, string>`，所以当前 `--include`、`--exclude` 即使多次出现，也只会保留最后一次解析结果。

如果后续需要“可多次指定”，要先扩展 `CmdLineParser` 的数据结构。

### `extractRoseArgs()` 只会跳过 `.c` 文件

当前逻辑是：从 `remaining` 里跳过 `argv[0]` 和 `.c` 文件，其余内容都视为 ROSE 额外参数。

这意味着：

- 传给多文件模式的非 `.c` 参数会进入 `extra_rose_args`
- 是否应再过滤别的项目自定义参数，要看 `CmdLineParser` 是否已经消费掉它们

## 7. 推荐使用方式

如果你是在 `translate.cpp` 这类驱动入口里接入它，建议固定采用下面这套顺序：

1. `TranslateJobBuilder::build(argc, argv)`
2. 检查 `should_exit`
3. 单文件模式取 `args.remaining`
4. 多文件模式取 `job.buildRoseArgv(argv[0])`
5. `CmdLineParser::toArgv(...)`
6. `frontend(...)`

这样最接近当前实现，也最不容易把“项目自定义参数”和“ROSE 参数”混在一起。
