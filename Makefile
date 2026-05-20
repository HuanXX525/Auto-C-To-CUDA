default: translate

all: translate tools

# ROSE install path:
# 1) honor explicit override: `make ROSE_INSTALL=/path/to/rose`
# 2) auto-detect CI path
# 3) auto-detect common local path
ROSE_INSTALL ?=
ifeq ($(strip $(ROSE_INSTALL)),)
ifneq ("$(wildcard /workspace/3rdpart/rose_build/include/rose/rose.h)","")
ROSE_INSTALL := /workspace/3rdpart/rose_build
else ifneq ("$(wildcard /usr/rose/include/rose/rose.h)","")
ROSE_INSTALL := /usr/rose
else
$(error ROSE not found. Please set ROSE_INSTALL, e.g. make ROSE_INSTALL=/usr/rose)
endif
endif

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
INCLUDE_DIR = $(CURDIR)/include
BUILD_DIR = build
# store binary files
BIN_DIR = $(BUILD_DIR)/bin

# Include paths
INCLUDES = -I$(INCLUDE_DIR) -I$(ROSE_INCLUDE_DIR) $(BOOST_CPPFLAGS)

# Auto-discover all .cpp files under src/
SRC_FILES = $(shell find $(SRC_DIR) -name '*.cpp')
PROJ_DEPS = $(patsubst $(SRC_DIR)/%.cpp,$(CURDIR)/$(BUILD_DIR)/%.lo,$(SRC_FILES))

# Debug target
debug: CXXFLAGS = -g -DC2CUDEBUG -O0 -Wall -std=c++17
debug: translate

# Main target
translate: $(PROJ_DEPS)
	@mkdir -p $(BIN_DIR)
	@cp config.json $(BIN_DIR)/config.json
	libtool --mode=link $(CXX) $(CXXFLAGS) $(INCLUDES) -o $(BIN_DIR)/translate.out $(PROJ_DEPS) translate.cpp $(ROSE_LIBS) $(BOOST_LD_FLAGS) $(BOOST_LIBS)

# Generic pattern rule: src/**/*.cpp -> build/**/*.lo
$(CURDIR)/$(BUILD_DIR)/%.lo: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	libtool --mode=compile $(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# Tools test
tools: $(PROJ_DEPS)
	$(MAKE) -C tools \
		BUILD_DIR=$(CURDIR)/$(BUILD_DIR)/tools \
		BIN_DIR=$(CURDIR)/$(BIN_DIR)/tools \
		PROJ_DEPS="$(PROJ_DEPS)" \
		CXX="$(CXX)" \
		CXXFLAGS="$(CXXFLAGS)" \
		INCLUDES="$(INCLUDES)" \
		ROSE_LIBS="$(ROSE_LIBS)" \
		BOOST_LD_FLAGS="$(BOOST_LD_FLAGS)" \
		BOOST_LIBS="$(BOOST_LIBS)"


clean:
	rm -rf $(BUILD_DIR)

# Run deubg
run_debug:
	make debug -j$(shell nproc)
	cd tmp && \
	gdb -x ../scripts/gdb_script/common.gdb ../$(BIN_DIR)/translate.out

TEST_DIR = test
test_translate:
	make -C ${TEST_DIR} c2cuda DIR=$(DIR)

test_run:
	make -C ${TEST_DIR} test DIR=$(DIR)





.PHONY: default debug clean tools run_debug


