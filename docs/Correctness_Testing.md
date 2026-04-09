# 相关文件在Test文件夹

test文件夹用于测试转化后运行的结果正确性，理应在更改代码后运行测试，以验证所更改的代码没有引起程序算法正确性出现逻辑性问题

测试逻辑：将测试目标数组保存为二进制文件，对比两个版本输出的二进制文件是否相同

- 测试使用方法:在workspace下运行 `make test_translate && make test_run`，若CUDA不在容器中可以先在容器运行`make test_translate`，然后在宿主机运行`make test_run`
- 测试编写方法:在`test`文件夹下任意新建文件夹，放入待测试的项目程序（可以是单文件或多文件，但仅仅能放一个主文件）。务必将待比较的数据按下列方式输出，第一个参数(待比对的文件名)在`Test/Makefile`里写死了：
    ```c
    #ifdef AUTOC2CUDATEST
        save_binary("bin/cuda_out.bin", c, size);
    #else
        save_binary("bin/c_out.bin", c, size);
    ```
