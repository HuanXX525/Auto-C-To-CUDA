# ============================================================
# 子项目配置：只需改 TARGET
# ============================================================
TARGET = playground

# 由父 Makefile 传入
BUILD_DIR      ?= ../../build/tools/$(TARGET)
BIN_DIR        ?= ../../build/bin/tools
CXX            ?= g++
CXXFLAGS       ?= -std=c++17 -O2
INCLUDES       ?=
PROJ_DEPS      ?=
ROSE_LIBS      ?=
BOOST_LD_FLAGS ?=
BOOST_LIBS     ?=

# ============================================================
# 源文件 -> 中间文件（.lo）放在 BUILD_DIR 下
# ============================================================
SRCS = $(wildcard *.cpp)
OBJS = $(patsubst %.cpp,$(BUILD_DIR)/%.lo,$(SRCS))

.PHONY: all clean

all: $(BIN_DIR)/$(TARGET).out

# 链接
$(BIN_DIR)/$(TARGET).out: $(OBJS) | $(BIN_DIR)
	libtool --mode=link $(CXX) $(CXXFLAGS) $(INCLUDES) \
		-o $@ $(PROJ_DEPS) $(OBJS) \
		$(ROSE_LIBS) $(BOOST_LD_FLAGS) $(BOOST_LIBS)

# 编译
$(BUILD_DIR)/%.lo: %.cpp | $(BUILD_DIR)
	libtool --mode=compile $(CXX) $(CXXFLAGS) $(INCLUDES) \
		-c $< -o $@

# 目录创建
$(BUILD_DIR):
	@mkdir -p $@

$(BIN_DIR):
	@mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(BIN_DIR)/$(TARGET).out
