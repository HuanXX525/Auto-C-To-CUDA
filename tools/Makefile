# ============================================================
# 变量由根目录传入，这里只设默认值（方便单独调试）
# ============================================================
BUILD_DIR     ?= ../build/tools
BIN_DIR       ?= ../build/bin/tools
CXX           ?= g++
CXXFLAGS      ?= -std=c++17 -O2
INCLUDES      ?=
PROJ_DEPS     ?=
ROSE_LIBS     ?=
BOOST_LD_FLAGS?=
BOOST_LIBS    ?=

# ============================================================
# 自动发现所有子项目（含 Makefile 的子目录）
# ============================================================
SUBDIRS := $(patsubst %/Makefile,%,$(wildcard */Makefile))

.PHONY: all clean $(SUBDIRS)

all: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@ \
		BUILD_DIR=$(BUILD_DIR)/$@ \
		BIN_DIR=$(BIN_DIR) \
		PROJ_DEPS="$(PROJ_DEPS)" \
		CXX="$(CXX)" \
		CXXFLAGS="$(CXXFLAGS)" \
		INCLUDES="$(INCLUDES)" \
		ROSE_LIBS="$(ROSE_LIBS)" \
		BOOST_LD_FLAGS="$(BOOST_LD_FLAGS)" \
		BOOST_LIBS="$(BOOST_LIBS)"

clean:
	for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean BUILD_DIR=$(BUILD_DIR)/$$dir; \
	done
