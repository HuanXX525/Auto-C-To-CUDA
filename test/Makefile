WORKSPACE_DIR = ../
TRANSLATE := $(WORKSPACE_DIR)/build/bin/translate.out
# 1. 获取子目录
DIRS := $(wildcard */)

# 2. 定义前缀目标
TEST_DIRS := $(addprefix test-, $(DIRS))
C2CUDA_DIRS := $(addprefix c2cuda-, $(DIRS))

.PHONY: all c2cuda test $(TEST_DIRS) $(C2CUDA_DIRS)

# --- C2CUDA 入口 ---
c2cuda: $(C2CUDA_DIRS)
	@echo "All .c files in test are translated successfully"

$(C2CUDA_DIRS):
	$(eval TARGET_DIR := $(patsubst c2cuda-%, %, $@))
	@C_FILES=$$(find $(TARGET_DIR) -name "*.c"); \
	echo "Found files: $$C_FILES"; \
	if [ -n "$$C_FILES" ]; then \
		$(TRANSLATE) -DAUTOC2CUDATEST $$C_FILES; \
	else \
		echo "No .c files found in $(TARGET_DIR)"; \
	fi


# --- TEST 入口 ---
test: $(TEST_DIRS)
	@echo "------------------------------------------------"
	@echo "\033[32mAll Test Passed\033[0m"

$(TEST_DIRS):
	$(eval TARGET_DIR := $(patsubst test-%, %, $@))
	@echo "------------------------------------------------"
	@echo "正在测试目录: $(TARGET_DIR)"
	@mkdir -p $(TARGET_DIR)/bin
	
	@C_FILES=$$(find $(TARGET_DIR) -name "*.c"); \
	if [ -n "$$C_FILES" ]; then \
		echo "编译 C 程序: $$C_FILES"; \
		g++ $$C_FILES tool.cpp -o $(TARGET_DIR)/bin/C.out && \
		(cd $(TARGET_DIR) && ./bin/C.out); \
	else \
		echo "跳过 C 编译: 未找到 .c 文件"; \
	fi
	
	@CU_FILES=$$(find $(TARGET_DIR) -name "*.cu"); \
	if [ -n "$$CU_FILES" ]; then \
		echo "编译 CUDA 程序: $$CU_FILES"; \
		nvcc $$CU_FILES tool.cpp -o $(TARGET_DIR)/bin/CU.out && \
		(cd $(TARGET_DIR) && ./bin/CU.out); \
	else \
		echo "跳过 CUDA 编译: 未找到 .cu 文件"; \
	fi

	@(cd $(TARGET_DIR) && cmp bin/c_out.bin bin/cuda_out.bin)
	@echo ${TARGET_DIR} PASSED

