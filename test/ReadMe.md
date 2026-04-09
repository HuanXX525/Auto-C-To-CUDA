# 此文件夹的文件用于测试转化后执行结果的正确性

由于原项目的`tests`文件夹下的文件仅是测试语法，逻辑上存在大量的漏洞，无法用于具体测试，因此新开一个文件夹用于测试正确性，并使用脚本。

测试逻辑：将测试目标数组保存为二进制文件，对比两个版本输出的二进制文件是否相同

- 测试使用方法:在workspace运行 `make test_translate && make test_run`，若CUDA不在容器中可以先在容器运行`make test_translate`，然后在宿主机运行`make test_run`
- 测试编写方法:在`test`文件夹下任意新建文件夹，放入待测试的项目程序，一个文件夹只能放一个主文件。务必将待比较的数据文件按下列方式输出，第一个参数(待比对的文件名)在Makefile里写死了，不要改第一个参数：
    ```c
    #ifdef AUTOC2CUDATEST
        save_binary("bin/cuda_out.bin", c, size);
    #else
        save_binary("bin/c_out.bin", c, size);
    ```
