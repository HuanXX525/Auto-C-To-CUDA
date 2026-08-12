# 对当前仿真实例测试的记录

测试对象：仿真实例(external/Aircraft-Simu)

## 测试流程

### 正确性测试

1. 复制原项目到output文件夹

```bash
cp -r ./external/Aircraft-Simu/ ./output/
```

2. 使用当前的编译器(build/bin/translate.out)编译这个项目，转化为cu

```bash
./build/bin/translate.out -p external/Aircraft-Simu/build/compile_commands.json -O output/Aircraft-Simu
```

3. 手动更改原项目的cmake文件，目标针对cu编译为可执行文件

```bash
CCACHE_DISABLE=1 cmake -S . -B build && CCACHE_DISABLE=1 cmake --build build
```

4. 分别运行CPU和GPU编译后的实例，比对结果正确性

```bash
./build/tools/instance_manager/instance_manager --runtime ./configs/test/AccuracyTests/runtime_64.json
```

比对目录`external/Aircraft-Simu/runs/accuracy_64_combinations`和`output/Aircraft-Simu/runs/accuracy_64_combinations`

```bash
python3 /workspace/compare_runs.py /workspace/external/Aircraft-Simu/runs/accuracy_64_combinations /workspace/output/Aircraft-Simu/runs/accuracy_64_combinations --verbose
```

### 性能测试

在正确性测试可以通过后，更改cmake将目标编译为静态库，链接线程管理程序(pthreadManager)

分别对CPU运行100多个实例和线程管理程序运行100多个实例进行时间测量
