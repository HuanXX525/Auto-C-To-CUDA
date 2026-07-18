TARGET = branch_flatten

BUILD_DIR      ?= ../../../build/tools/$(TARGET)
BIN_DIR        ?= ../../../build/bin/tools
CXX            ?= g++
CXXFLAGS       ?= -std=c++17 -O2
INCLUDES       ?=
PROJ_DEPS      ?=
ROSE_LIBS      ?=
BOOST_LD_FLAGS ?=
BOOST_LIBS     ?=

LOCAL_DEPS = $(filter-out %/preprocess.lo, $(PROJ_DEPS))

SRCS = $(wildcard *.cpp)
OBJS = $(patsubst %.cpp,$(BUILD_DIR)/%.lo,$(SRCS))

.PHONY: all clean

all: $(BIN_DIR)/$(TARGET).out

$(BIN_DIR)/$(TARGET).out: $(OBJS) | $(BIN_DIR)
	libtool --mode=link $(CXX) $(CXXFLAGS) $(INCLUDES) \
		-o $@ $(LOCAL_DEPS) $(OBJS) \
		$(ROSE_LIBS) $(BOOST_LD_FLAGS) $(BOOST_LIBS)

$(BUILD_DIR)/%.lo: %.cpp | $(BUILD_DIR)
	libtool --mode=compile $(CXX) $(CXXFLAGS) $(INCLUDES) \
		-c $< -o $@

$(BUILD_DIR):
	@mkdir -p $@

$(BIN_DIR):
	@mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(BIN_DIR)/$(TARGET).out
