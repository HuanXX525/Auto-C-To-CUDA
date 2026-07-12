# pthreadManager — 通用线程池管理器

可复用的 C 线程池，并行执行任意任务。任务通过 `int argc, char** argv` 传递参数（与 `main()` 相同的惯例），任何已有程序都可轻松适配。

## 快速开始

```bash
make          # 编译，生成 bin/test
bin/test      # 并行执行 2 个图像处理任务
```

## 架构概览

```
 外部（可替换）              管理器核心                     输出
 ┌────────────────┐    ┌──────────────────────┐    ┌────────────┐
 │ 任务函数       │    │  thread_pool_run     │    │ results[]  │
 │ int (*)(int,**)│───▶│  thread_pool_execute │───▶│ 汇总摘要   │
 │ args.json      │    │  create/submit/wait  │    │ 日志文件   │
 └────────────────┘    │  FIFO队列 + N 个工作线程│   └────────────┘
                        └──────────────────────┘
```

详见 `docs/architecture.png`（架构总览）和 `docs/integration.png`（对接流程）。

## 目录结构

```
pthreadManager/
├── src/              # 管理器源码（thread_manager.c/h, log.c/h, main.c）
├── third_party/      # cJSON 解析器
├── program/          # 示例：图像处理管线（编译为静态库）
│   ├── image_proc.c  #   任务函数 main_task(int argc, char** argv)
│   ├── stb/          #   stb_image / stb_image_write
│   ├── input/        #   2 张测试图片
│   └── output/       #   处理结果输出目录
├── lib/              # 静态库（libimage_proc.a）
├── bin/              # 可执行文件（test.exe）
├── build/            # 编译中间文件
├── args.json         # 任务参数（2 条）
├── Makefile
└── docs/
    ├── architecture.mmd / .png   # 架构图
    └── integration.mmd / .png    # 对接示意图
```

## 如何对接你自己的任务

### 第一步：改造函数签名

将程序入口改为以下签名：

```c
int my_task(int argc, char** argv) {
    // argv[0] = 程序名（自动设置）
    // argv[1] = JSON 中的第一个参数
    // argv[2] = JSON 中的第二个参数
    // ...
    return 0;  // 返回值会被收集到 results[] 中
}
```

### 第二步：编译为静态库

```makefile
libyour_project.a: your_project.c
	gcc -O2 -Wall -Wextra -c your_project.c -o your_project.o
	ar rcs libyour_project.a your_project.o
```

### 第三步：链接管理器

```makefile
TARGET = bin/test
SRCS = $(wildcard src/*.c third_party/*.c)
CFLAGS = -Wall -Wextra -pthread -Isrc -Ithird_party -Iprogram

$(TARGET): $(SRCS) libyour_project.a
	gcc $(CFLAGS) $(SRCS) -L. -lyour_project -lm -o $(TARGET)
```

## 任务参数（args.json）

JSON 格式：外层数组的每个元素对应一个任务，内层数组的字符串依次作为 `argv[1..N]`。

```json
[
    ["input/01.jpg", "output/01.jpg"],
    ["input/02.jpg", "output/02.jpg"]
]
```

`argv[0]` 自动设为 `"./program"`。

## 三层 API

### 1. 高级接口：`thread_pool_run`

```c
int thread_pool_run(int (*func)(int, char**),
                    const char* json_path,
                    const char* log_path);
```

解析 JSON → 执行全部任务 → 打印汇总 → 写日志。一次调用完成全部流程。

### 2. 中级接口：`thread_pool_execute`

```c
int* thread_pool_execute(int (*func)(int, char**),
                         task_param_t* args[], int num_tasks,
                         int num_threads);  // 0 表示自动
```

自动管理完整生命周期：创建池 → 提交全部 → 等待 → 销毁。返回 `results[]` 数组（调用者负责 free）。

### 3. 底层接口：手动控制

```c
thread_pool_t* pool = thread_pool_create(0);   // 0 表示自动计算线程数
thread_pool_submit(pool, my_func, id, argc, argv);
thread_pool_submit(pool, my_func, id, argc, argv);
// ...
thread_pool_wait_all(pool);
thread_pool_destroy(pool);
```

### 查询接口

```c
int n = thread_pool_queue_size(pool);    // 排队中的任务数
int n = thread_pool_active_count(pool);  // 正在执行的任务数
```

## 线程数计算公式

```
线程数 = CPU核心数 × (1 + (1 - ratio) / ratio)
```

| `set_cpu_ratio()` | 适用场景         | 线程数与核心数关系 |
|-------------------|------------------|-------------------|
| 1.0（默认）       | 纯计算密集型     | 线程数 = 核心数   |
| 0.5               | 计算+IO 混合     | 线程数 = 2×核心数 |
| 0.25              | IO 密集型        | 线程数 = 4×核心数 |

在 `create` 前调用：

```c
thread_pool_set_cpu_ratio(0.5);
thread_pool_t* pool = thread_pool_create(0);
```

或直接指定固定线程数：

```c
thread_pool_t* pool = thread_pool_create(8);
```

## 日志系统

- 三级日志：`LOG_ERROR`、`LOG_INFO`、`LOG_TRACE`
- `thread_pool_run` 传路径则写文件，传 NULL 则输出到控制台
- 每条日志包含时间戳：`HH:MM:SS [级别] 消息`

## 线程安全

- 所有公有 API 内部均已做完整同步
- 任务函数应避免访问未保护的全局/静态变量
- 任务间需共享数据时，请自行加锁（`pthread_mutex_t`）

## 平台支持

- Windows（通过 winpthreads / MinGW 的 pthread 实现）
- Linux / macOS（POSIX 线程）
- 跨平台 CPU 核心数检测：`GetSystemInfo` / `sysconf`
