default: translate
# 请根据安装位置填写以下配置↓
ROSE_INSTALL ?= /usr/rose
# 请根据安装位置填写以上配置↑
# Location of include directory after "make install"
ROSE_INCLUDE_DIR = $(ROSE_INSTALL)/include/rose

# Location of Boost include directory
BOOST_CPPFLAGS = -pthread -I/usr/include

# Location of lib directory after "make install"
ROSE_LIB_DIR = $(ROSE_INSTALL)/lib

CC = gcc
CXX = g++
CXXFLAGS = -g -O2 -Wall -std=c++17

ROSE_LIBS = $(ROSE_LIB_DIR)/librose.la

BUILD_DIR = build

debug: CXXFLAGS += -g -DC2CUDEBUG
debug: translate

# PROJ_DEPS = normalize.lo affine.lo dependency.lo parallel.lo kernel.lo preprocess.lo
PROJ_DEPS =  $(BUILD_DIR)/normalize.lo $(BUILD_DIR)/affine.lo $(BUILD_DIR)/dependency.lo $(BUILD_DIR)/parallel.lo $(BUILD_DIR)/kernel.lo $(BUILD_DIR)/preprocess.lo $(BUILD_DIR)/io.lo

translate: $(PROJ_DEPS) ./include/loop_attr.hpp
	libtool --mode=link $(CXX) $(CXXFLAGS) -I$(ROSE_INCLUDE_DIR) $(BOOST_CPPFLAGS) -o translate.out $(PROJ_DEPS) translate.cpp $(ROSE_LIBS)

tools_test: $(PROJ_DEPS)
	libtool --mode=link $(CXX) $(CXXFLAGS) -I$(ROSE_INCLUDE_DIR) $(BOOST_CPPFLAGS) -o tools/playground.out tools/test.cpp tools/playground.cpp $(ROSE_LIBS)

$(BUILD_DIR)/io.lo: ./include/fileio/io.cpp ./include/fileio/io.h
	libtool --mode=compile $(CXX) $(CXXFLAGS) $(BOOST_CPPFLAGS) -c -o $(BUILD_DIR)/io.lo ./include/fileio/io.cpp

$(BUILD_DIR)/normalize.lo: ./include/normalize/normalize.cpp ./include/normalize/normalize.hpp 
	libtool --mode=compile $(CXX) $(CXXFLAGS) -I$(ROSE_INCLUDE_DIR) $(BOOST_CPPFLAGS) -c -o $(BUILD_DIR)/normalize.lo ./include/normalize/normalize.cpp $(ROSE_LIBS) 

$(BUILD_DIR)/affine.lo: ./include/affine/affine.cpp ./include/affine/affine.hpp ./include/loop_attr.hpp 
	libtool --mode=compile $(CXX) $(CXXFLAGS) -I$(ROSE_INCLUDE_DIR) $(BOOST_CPPFLAGS) -c -o $(BUILD_DIR)/affine.lo ./include/affine/affine.cpp $(ROSE_LIBS) 

$(BUILD_DIR)/dependency.lo: ./include/dependency/dependency.cpp ./include/dependency/dependency.hpp ./include/loop_attr.hpp
	libtool --mode=compile $(CXX) $(CXXFLAGS) -I$(ROSE_INCLUDE_DIR) $(BOOST_CPPFLAGS) -c -o $(BUILD_DIR)/dependency.lo ./include/dependency/dependency.cpp $(ROSE_LIBS)

$(BUILD_DIR)/parallel.lo: ./include/parallel/parallel.cpp ./include/parallel/parallel.hpp ./include/loop_attr.hpp $(BUILD_DIR)/dependency.lo $(BUILD_DIR)/kernel.lo 
	libtool --mode=compile $(CXX) $(CXXFLAGS) -I$(ROSE_INCLUDE_DIR) $(BOOST_CPPFLAGS) -c -o $(BUILD_DIR)/parallel.lo ./include/parallel/parallel.cpp $(ROSE_LIBS) 

$(BUILD_DIR)/kernel.lo: ./include/kernel/kernel.cpp ./include/kernel/kernel.hpp ./include/loop_attr.hpp
	libtool --mode=compile $(CXX) $(CXXFLAGS) -I$(ROSE_INCLUDE_DIR) $(BOOST_CPPFLAGS) -c -o $(BUILD_DIR)/kernel.lo ./include/kernel/kernel.cpp $(ROSE_LIBS)

$(BUILD_DIR)/preprocess.lo: ./include/preprocess/preprocess.cpp ./include/preprocess/preprocess.hpp 
	libtool --mode=compile $(CXX) $(CXXFLAGS) -I$(ROSE_INCLUDE_DIR) $(BOOST_CPPFLAGS) -c -o $(BUILD_DIR)/preprocess.lo ./include/preprocess/preprocess.cpp $(ROSE_LIBS)

clean:
	rm build/*
