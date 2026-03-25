# C/C++ Development Environment
# Base: Ubuntu 20.04
FROM nvidia/cuda:12.2.0-devel-ubuntu20.04

# Avoid interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Shanghai

# Update and install development tools
RUN apt-get update && apt-get install -y \
    build-essential \
    gcc \
    g++ \
    gdb \
    make \
    cmake \
    git \
    wget \
    curl \
    vim \
    nano \
    pkg-config \
    valgrind \
    strace \
    ltrace \
    tree \
    automake \
    libtool \
    libtool-bin \
    bison \
    flex \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Build and install Boost 1.67.0 from source into /usr for ROSE.
RUN cd /tmp \
    && wget -O boost_1_67_0.tar.gz https://archives.boost.io/release/1.67.0/source/boost_1_67_0.tar.gz \
    && tar -xzf boost_1_67_0.tar.gz \
    && cd boost_1_67_0 \
    && ./bootstrap.sh --prefix=/usr \
    && ./b2 install -j"$(nproc)" \
    && cd /tmp \
    && rm -rf /tmp/boost_1_67_0 /tmp/boost_1_67_0.tar.gz

# Set working directory
WORKDIR /workspace

# Default command
CMD ["/bin/bash"]
