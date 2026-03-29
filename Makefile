default: translate

# ROSE install path (override with env var or `make ROSE_INSTALL=/path/to/rose`)
ROSE_INSTALL ?= /usr/rose
ROSE_INCLUDE_DIR = $(ROSE_INSTALL)/include/rose
ROSE_LIB_DIR = $(ROSE_INSTALL)/lib
ROSE_LIBS = $(ROSE_LIB_DIR)/librose.la

# Boost
BOOST_PATH ?= /usr
BOOST_CPPFLAGS = -pthread -I$(BOOST_PATH)/include
BOOST_LD_FLAGS = -L$(BOOST_PATH)/lib
BOOST_LIBS = -lboost_system

# Compiler
CC = gcc
CXX = g++
CXXFLAGS = -g -O2 -Wall -std=c++17

# Directories
SRC_DIR = src
INCLUDE_DIR = include
BUILD_DIR = build

# Include paths
INCLUDES = -I$(INCLUDE_DIR) -I$(ROSE_INCLUDE_DIR) $(BOOST_CPPFLAGS)

# Source modules (each is src/<module>/<file>.cpp)
MODULES = normalize affine dependency parallel kernel preprocess fileio
SRC_FILES = $(SRC_DIR)/normalize/normalize.cpp \
            $(SRC_DIR)/affine/affine.cpp \
            $(SRC_DIR)/dependency/dependency.cpp \
            $(SRC_DIR)/parallel/parallel.cpp \
            $(SRC_DIR)/kernel/kernel.cpp \
            $(SRC_DIR)/preprocess/preprocess.cpp \
            $(SRC_DIR)/fileio/io.cpp

PROJ_DEPS = $(BUILD_DIR)/normalize.lo \
            $(BUILD_DIR)/affine.lo \
            $(BUILD_DIR)/dependency.lo \
            $(BUILD_DIR)/parallel.lo \
            $(BUILD_DIR)/kernel.lo \
            $(BUILD_DIR)/preprocess.lo \
            $(BUILD_DIR)/io.lo

# Debug target
debug: CXXFLAGS += -g -DC2CUDEBUG
debug: translate

# Main target
translate: $(PROJ_DEPS)
	libtool --mode=link $(CXX) $(CXXFLAGS) $(INCLUDES) -o translate.out $(PROJ_DEPS) translate.cpp $(ROSE_LIBS) $(BOOST_LD_FLAGS) $(BOOST_LIBS)

# Pattern rules for compiling src modules
$(BUILD_DIR)/%.lo: $(SRC_DIR)/normalize/%.cpp
	libtool --mode=compile $(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/%.lo: $(SRC_DIR)/affine/%.cpp
	libtool --mode=compile $(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/%.lo: $(SRC_DIR)/dependency/%.cpp
	libtool --mode=compile $(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/%.lo: $(SRC_DIR)/parallel/%.cpp
	libtool --mode=compile $(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/%.lo: $(SRC_DIR)/kernel/%.cpp
	libtool --mode=compile $(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/%.lo: $(SRC_DIR)/preprocess/%.cpp
	libtool --mode=compile $(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/%.lo: $(SRC_DIR)/fileio/%.cpp
	libtool --mode=compile $(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# Tools test
tools_test: $(PROJ_DEPS)
	libtool --mode=link $(CXX) $(CXXFLAGS) $(INCLUDES) -o tools/playground.out tools/test.cpp tools/playground.cpp $(ROSE_LIBS)

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Ensure build dir exists before compiling
$(PROJ_DEPS): | $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)/*

.PHONY: default debug clean tools_test
