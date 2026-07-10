# 使用 `compile_commands.json` 转换完整 C 项目

本文说明如何使用 Auto-C-To-CUDA 的 `--compile-db` 模式一次转换整个 C
项目，包括 `compile_commands.json` 的生成方式、路径解析规则、输出目录结构、
容器内使用方法，以及当前实现的限制。

## 1. 适用场景

当项目包含多个 `.c` 文件、头文件目录、预处理宏或比较复杂的编译参数时，推荐
使用 `compile_commands.json`，而不是在命令行中手动列出所有源文件和 `-I`、
`-D` 参数。

转换器会从编译数据库中：

1. 收集所有扩展名为 `.c` 的源文件；
2. 读取每条编译命令中的 include 路径；
3. 读取 `-D` 宏定义；
4. 将源文件和编译参数交给 ROSE frontend；
5. 为每个源文件生成对应的 `.cu` 文件。

当前只收集 `.c` 文件，不处理 `.cc`、`.cpp` 或 `.cxx` 文件。

## 2. 基本命令

```bash
./build/bin/translate.out \
    --compile-db ./build/compile_commands.json \
    --output-dir ./cuda_out \
    -rose:skipfinalCompileStep
```

参数含义：

| 参数 | 简写 | 作用 |
|---|---|---|
| `--compile-db <file>` | `-p <file>` | 指定 `compile_commands.json` |
| `--output-dir <dir>` | `-O <dir>` | 指定生成 `.cu` 文件的输出根目录 |
| `--include <dir>` | `-I <dir>` | 额外增加一个 include 路径 |
| `--verbose` | `-v` | 输出最终传给 ROSE 的命令行 |
| `-rose:skipfinalCompileStep` | 无 | 只生成代码，不让 ROSE 执行最终编译 |

不指定 `--output-dir` 时，生成的 `.cu` 文件仍写入对应源文件所在目录，兼容
原来的行为。

## 3. 生成 `compile_commands.json`

### 3.1 CMake 项目

在项目根目录执行：

```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
```

生成文件通常位于：

```text
build/compile_commands.json
```

然后执行：

```bash
./build/bin/translate.out \
    --compile-db ./build/compile_commands.json \
    --output-dir ./cuda_out \
    -rose:skipfinalCompileStep
```

如果 Auto-C-To-CUDA 的可执行文件不在被转换项目中，应使用它的绝对路径：

```bash
/path/to/Auto-C-To-CUDA/build/bin/translate.out \
    --compile-db ./build/compile_commands.json \
    --output-dir ./cuda_out \
    -rose:skipfinalCompileStep
```

### 3.2 Makefile 项目

可使用 Bear 捕获真实编译命令：

```bash
bear -- make
```

某些 Bear 版本使用：

```bash
bear --output compile_commands.json -- make
```

生成完成后执行：

```bash
/path/to/Auto-C-To-CUDA/build/bin/translate.out \
    --compile-db ./compile_commands.json \
    --output-dir ./cuda_out \
    -rose:skipfinalCompileStep
```

如果项目已经编译完成，Bear 可能捕获不到命令。此时先清理构建产物，再重新捕获：

```bash
make clean
bear -- make
```

## 4. `compile_commands.json` 示例

标准条目通常使用 `arguments` 或 `command`，两种形式都受支持。

使用 `arguments`：

```json
[
  {
    "directory": "/workspace/example/build",
    "file": "/workspace/example/src/main.c",
    "arguments": [
      "gcc",
      "-I/workspace/example/include",
      "-DENABLE_FAST_PATH=1",
      "-c",
      "/workspace/example/src/main.c",
      "-o",
      "CMakeFiles/example.dir/src/main.c.o"
    ]
  }
]
```

使用 `command`：

```json
[
  {
    "directory": "/workspace/example/build",
    "file": "../src/main.c",
    "command": "gcc -I../include -DENABLE_FAST_PATH=1 -c ../src/main.c"
  }
]
```

当条目同时包含 `arguments` 和 `command` 时，转换器优先使用 `arguments`；只有
缺少 `arguments` 时才对 `command` 做简单命令行分词。

## 5. 源文件路径解析规则

### 5.1 绝对 `file` 路径

如果 `file` 已经是绝对路径，转换器直接使用：

```json
{
  "directory": "/workspace/example/build",
  "file": "/workspace/example/src/main.c"
}
```

输入文件为：

```text
/workspace/example/src/main.c
```

### 5.2 相对 `file` 路径

如果 `file` 是相对路径，转换器会将它拼接到该条目的 `directory` 后面：

```json
{
  "directory": "/workspace/example/build",
  "file": "../src/main.c"
}
```

得到：

```text
/workspace/example/build/../src/main.c
```

操作系统访问文件时会将其等价解析为：

```text
/workspace/example/src/main.c
```

因此，`file` 字段可以使用相对路径，但建议 `directory` 使用绝对路径。

### 5.3 `directory` 也是相对路径

当前实现不会将相对 `directory` 自动转换为相对于
`compile_commands.json` 所在目录的路径。此类路径最终相对于启动
`translate.out` 时的当前工作目录。

例如：

```json
{
  "directory": "build",
  "file": "../src/main.c"
}
```

只有在项目根目录启动转换器时才能稳定工作：

```bash
cd /workspace/example
/path/to/translate.out \
    --compile-db ./build/compile_commands.json \
    --output-dir ./cuda_out \
    -rose:skipfinalCompileStep
```

为了避免当前工作目录造成歧义，推荐让构建系统生成绝对 `directory`。

## 6. 输出路径规则

`--compile-db` 模式会计算全部源文件的最长公共父目录，并以它作为输入根目录。
每个输出文件相对于该公共父目录保持原目录结构，然后将扩展名替换为 `.cu`。

例如编译数据库包含：

```text
/workspace/example/src/main.c
/workspace/example/src/math/add.c
/workspace/example/lib/helper.c
```

公共父目录为：

```text
/workspace/example
```

执行：

```bash
./build/bin/translate.out \
    --compile-db ./build/compile_commands.json \
    --output-dir ./cuda_out \
    -rose:skipfinalCompileStep
```

生成：

```text
cuda_out/src/main.cu
cuda_out/src/math/add.cu
cuda_out/lib/helper.cu
```

如果所有源文件都位于 `/workspace/example/src` 内，公共父目录可能是
`/workspace/example/src`，此时输出会从 `src` 内部开始：

```text
cuda_out/main.cu
cuda_out/math/add.cu
```

当前没有单独的 `--base-dir` 参数覆盖自动计算的公共父目录。

## 7. 在 Docker 容器中使用

### 7.1 推荐：在容器内生成编译数据库

如果项目在容器中的路径是 `/workspace`，应在同一个路径和同一个容器环境中
配置、编译并生成 `compile_commands.json`：

```bash
cd /workspace
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build

./build/bin/translate.out \
    --compile-db ./build/compile_commands.json \
    --output-dir ./cuda_out \
    -rose:skipfinalCompileStep
```

这样编译数据库中的绝对路径和转换时看到的文件路径一致。

### 7.2 宿主机和容器保持相同绝对路径

如果编译数据库已经在宿主机生成，里面可能记录：

```text
/home/user/project/src/main.c
```

若容器将项目挂载到 `/workspace`，上述绝对路径在容器内不存在。可以把宿主机
项目挂载到容器内相同的绝对路径：

```bash
docker run --rm --gpus all \
    -v "$PWD":"$PWD" \
    -w "$PWD" \
    c2cuda-dev \
    /path/to/translate.out \
    --compile-db ./build/compile_commands.json \
    --output-dir ./cuda_out \
    -rose:skipfinalCompileStep
```

这里的关键是：

```text
宿主机绝对路径 == 容器内绝对路径
```

### 7.3 不推荐：直接使用路径不匹配的编译数据库

下面这种组合通常会失败：

```text
compile_commands.json: /home/user/project/src/main.c
容器实际路径:          /workspace/src/main.c
```

转换器当前不会自动把 `/home/user/project` 重写为 `/workspace`。应重新生成
编译数据库，或者调整容器挂载路径。

## 8. include 路径和宏定义

### 8.1 绝对 include 路径

推荐编译数据库使用绝对 include 路径：

```text
-I/workspace/example/include
```

这种形式可以直接交给 ROSE。

### 8.2 相对 include 路径的当前限制

当前实现会提取 `-Iinclude` 或 `-I include`，但不会按每条编译命令的
`directory` 将相对 include 路径转成绝对路径。

例如条目为：

```json
{
  "directory": "/workspace/example/build",
  "command": "gcc -I../include -c ../src/main.c",
  "file": "../src/main.c"
}
```

转换器当前传给 ROSE 的仍是：

```text
-I../include
```

它相对于启动 `translate.out` 的当前工作目录，而不一定相对于
`/workspace/example/build`。因此，整项目转换时推荐：

1. 让编译数据库生成绝对 `-I`；或
2. 从能够正确解析这些相对路径的目录启动转换器；或
3. 使用 `--include` 额外补充一个绝对 include 路径。

例如：

```bash
./build/bin/translate.out \
    --compile-db ./build/compile_commands.json \
    --include /workspace/example/include \
    --output-dir ./cuda_out \
    -rose:skipfinalCompileStep
```

目前 `--include` 重复指定时只保留最后一个值。如果项目需要多个额外 include
目录，优先让它们进入 `compile_commands.json`。

### 8.3 宏定义冲突

转换器会合并所有条目中的 `-D`。如果不同编译单元对同一个宏给出不同值，当前
实现保留先遇到的值并打印警告：

```text
-DFEATURE_LEVEL=1
-DFEATURE_LEVEL=2
```

这种项目具有编译单元专属配置，而当前实现将全部源文件放入一次 ROSE frontend
调用，因此不能完全复现每个编译单元的独立编译环境。

## 9. 使用目录扫描模式作为替代

如果项目没有 `compile_commands.json`，并且所有源文件使用大致相同的 include
路径和宏定义，可以使用：

```bash
./build/bin/translate.out \
    --scan-dir ./src \
    --include ./include \
    --output-dir ./cuda_out \
    -rose:skipfinalCompileStep
```

目录扫描模式会：

- 递归查找扫描目录中的 `.c` 文件；
- 将扫描目录作为固定输入根目录；
- 保持扫描目录以下的相对输出结构。

它不会自动获得构建系统中的全部 `-I`、`-D` 和其他编译参数，因此复杂项目仍
应优先使用 `--compile-db`。

## 10. 推荐的完整项目转换流程

以容器中的 CMake 项目为例：

```bash
# 1. 进入项目在容器中的真实路径
cd /workspace/example

# 2. 在同一环境生成编译数据库
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build

# 3. 检查编译数据库中的路径
head -n 30 build/compile_commands.json

# 4. 执行转换
/workspace/Auto-C-To-CUDA/build/bin/translate.out \
    --verbose \
    --compile-db ./build/compile_commands.json \
    --output-dir ./cuda_out \
    -rose:skipfinalCompileStep

# 5. 检查生成文件
find ./cuda_out -type f -name '*.cu' -print
```

首次使用建议保留 `--verbose`，确认打印出的源文件、`-I` 和 `-D` 均指向容器
内实际存在的位置。

## 11. 常见问题

### 11.1 `failed to load compile_commands.json`

检查：

```bash
ls -l ./build/compile_commands.json
python3 -m json.tool ./build/compile_commands.json >/dev/null
```

确认文件存在、是合法 JSON，并且顶层结构为数组。

### 11.2 找不到源文件

检查编译数据库中的 `directory` 和 `file`：

```bash
head -n 30 ./build/compile_commands.json
```

如果在容器中运行，确认其中的绝对路径在容器内也存在。

### 11.3 找不到头文件

使用：

```bash
./build/bin/translate.out \
    --verbose \
    --compile-db ./build/compile_commands.json \
    --output-dir ./cuda_out \
    -rose:skipfinalCompileStep
```

检查输出中的 `-I` 路径。相对 `-I` 应重点检查其实际解析目录，必要时通过
`--include` 补充绝对路径。

### 11.4 输出目录中缺少最外层 `src`

这是公共父目录自动计算的结果。例如全部输入都在 `project/src` 中时，
`project/src` 会成为公共父目录，所以输出直接从其内部结构开始。当前没有
`--base-dir` 参数。

### 11.5 编译数据库中有 C++ 文件但没有生成 `.cu`

当前 `--compile-db` 模式只接受文件名以 `.c` 结尾的条目，C++ 文件会被过滤。

## 12. 当前能力边界

使用 `compile_commands.json` 转换完整项目时，应了解以下边界：

- 支持绝对 `file` 路径；
- 支持相对于 `directory` 的 `file` 路径；
- 不自动重写宿主机与容器之间的绝对路径前缀；
- 相对 `-I` 尚未按每条命令的 `directory` 规范化；
- 所有编译单元的 include 路径和宏会合并；
- 同名但不同值的宏只保留第一个；
- 只转换 `.c` 文件；
- 输出根目录可通过 `--output-dir`/`-O` 指定；
- 输出结构相对于源文件的自动公共父目录；
- 当前不能通过命令行覆盖这个公共父目录。

对于编译参数一致、路径在同一环境内有效的普通多文件 C 项目，推荐直接使用
`--compile-db`。对于不同源文件使用大量互相冲突的宏或编译选项的项目，当前实现
还不能完全替代逐编译单元翻译。
