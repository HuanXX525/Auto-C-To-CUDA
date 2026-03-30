# CTOCUDA container env

> 我的配置 os: fedora42, hw: 3050

## Docker

> 之前用的是 nvidia 的，太大了，nv相关的工具链host上就有，反正生成的是 cu 代码，直接在host上跑就行了

docker 用的 base image ubuntu:20.04 镜像，具体内容是 [Dockerfile](Dockerfile)。

build docker file:

```sh
docker build -t c2cuda-env .
# 看看有没有 image
docker images
```

build 完镜像后，用 [create_container.sh](create_container.sh) 创建容器。

例如：
```sh
sh create_container.sh --name c2cuda-dev ../
```

## ROSE

> build rose from src

需要 `automake`、`libtool`、`boost`、`flex`、`bison`。

源码编译 rose:

```sh
# 先运行
./build

# 创建编译目录，并进入
cd .. && mkdir -p build && cd build

$ROSE_PATH/src/configure --prefix=/path/for/ROSE/install \
    --enable-languages=c,c++ \
    --with-boost=/path/to/boost/install
make -j${NUM_PROCESSORS}
make install -j${NUM_PROCESSORS}
make check -j${NUM_PROCESSORS}
```

> 需要注意的是
> 1. boost 的版本要小于 1.69，具体的 boost 版本范围在 `$(ROSR_PATH)/configure` 的 1200-1339。`configure` 是在执行 `./build` 后生成的。

## Auto-C-To-CUDA

```sh
# env 里有 ROSE_INSTALL 和 BOOST_PATH
make -j8

# env 里没有 ROSE_INSTALL 和 BOOST_PATH
make ROSE_INSTALL=/path/rose/install BOOST_PATH=/path/boost/install -j8
```
