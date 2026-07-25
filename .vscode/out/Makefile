# Makefile for compiling translated .cu files and linking with pthreadManager
# Pattern: matching the original CMake Project structure

NVCC = nvcc
CC = gcc
AR = ar
ARFLAGS = rcs

# --- Flags ---
# _Bool is C99 type, nvcc uses C++ host compiler → map to bool
NVCC_FLAGS = -D_Bool=bool -O2

# Include paths from original compile_commands.json
COMMON_INC = -I/workspace/external/Aircraft-Simu/common/include
ENV_INC    = -I/workspace/external/Aircraft-Simu/environment_sim/include
FC_INC     = -I/workspace/external/Aircraft-Simu/flight_control_sim/include

# pthreadManager
PM_DIR    = /workspace/pthreadManager
PM_SRCS   = $(PM_DIR)/src/main.c $(PM_DIR)/src/thread_manager.c $(PM_DIR)/src/log.c $(PM_DIR)/third_party/cJSON.c
PM_INC    = -I$(PM_DIR)/src -I$(PM_DIR)/third_party
PM_FLAGS  = -pthread -Wall

# --- Source files (translated .cu) ---
OUT_DIR   = outputTest

COMMON_SRCS   = $(wildcard $(OUT_DIR)/common/src/*.cu)
ENV_SRCS      = $(wildcard $(OUT_DIR)/environment_sim/src/*.cu)
FC_SRCS       = $(wildcard $(OUT_DIR)/flight_control_sim/src/*.cu)
INST_MGR_SRC  = $(OUT_DIR)/tools/instance_manager/src/main.cu

COMMON_OBJS   = $(COMMON_SRCS:.cu=.o)
ENV_OBJS      = $(ENV_SRCS:.cu=.o)
FC_OBJS       = $(FC_SRCS:.cu=.o)
INST_MGR_OBJ  = $(INST_MGR_SRC:.cu=.o)

# --- Targets ---
all: bin/instance_manager bin/environment_sim bin/flight_control_sim

# --- Libraries ---
lib/libcommon.a: $(COMMON_OBJS)
	@mkdir -p lib
	$(AR) $(ARFLAGS) $@ $^

# --- pthreadManager (pre-compile to .o) ---
PM_OBJS = $(PM_DIR)/src/main.o $(PM_DIR)/src/thread_manager.o $(PM_DIR)/src/log.o $(PM_DIR)/third_party/cJSON.o

$(PM_DIR)/src/%.o: $(PM_DIR)/src/%.c
	$(CC) -c $(PM_FLAGS) $(PM_INC) $< -o $@

$(PM_DIR)/third_party/%.o: $(PM_DIR)/third_party/%.c
	$(CC) -c $(PM_FLAGS) $(PM_INC) $< -o $@

# --- Binaries ---

# instance_manager: common + pthreadManager (nvcc link for CUDA symbols)
bin/instance_manager: lib/libcommon.a $(INST_MGR_OBJ) $(PM_OBJS)
	@mkdir -p bin
	$(NVCC) $(NVCC_FLAGS) $(PM_INC) $(COMMON_INC) $(INST_MGR_OBJ) $(PM_OBJS) -Llib -lcommon -lstdc++ -lm -o $@

# environment_sim: common + env (nvcc link for CUDA symbols)
bin/environment_sim: lib/libcommon.a $(ENV_OBJS)
	@mkdir -p bin
	$(NVCC) $(NVCC_FLAGS) $(COMMON_INC) $(ENV_INC) $(ENV_OBJS) -Llib -lcommon -lstdc++ -lm -o $@

# flight_control_sim: common + fc (nvcc link for CUDA symbols)
bin/flight_control_sim: lib/libcommon.a $(FC_OBJS)
	@mkdir -p bin
	$(NVCC) $(NVCC_FLAGS) $(COMMON_INC) $(FC_INC) $(FC_OBJS) -Llib -lcommon -lstdc++ -lm -o $@

# --- Compile rules ---
$(OUT_DIR)/common/src/%.o: $(OUT_DIR)/common/src/%.cu
	$(NVCC) $(NVCC_FLAGS) -c $< $(COMMON_INC) -o $@

$(OUT_DIR)/environment_sim/src/%.o: $(OUT_DIR)/environment_sim/src/%.cu
	$(NVCC) $(NVCC_FLAGS) -c $< $(COMMON_INC) $(ENV_INC) -o $@

$(OUT_DIR)/flight_control_sim/src/%.o: $(OUT_DIR)/flight_control_sim/src/%.cu
	$(NVCC) $(NVCC_FLAGS) -c $< $(COMMON_INC) $(FC_INC) -o $@

$(OUT_DIR)/tools/instance_manager/src/%.o: $(OUT_DIR)/tools/instance_manager/src/%.cu
	$(NVCC) $(NVCC_FLAGS) -c $< $(COMMON_INC) -o $@

clean:
	rm -rf bin lib $(OUT_DIR)/*/src/*.o
