# 1. 获取目录并强制去空格
DIRS := $(strip $(patsubst %/, %, $(wildcard */)))

# 2. 定义前缀目标
TEST_DIRS := $(addprefix test-, $(DIRS))
C2CUDA_DIRS := $(addprefix c2cuda-, $(DIRS))

.PHONY: all c2cuda test $(TEST_DIRS) $(C2CUDA_DIRS)

# --- C2CUDA 入口 ---
c2cuda: $(C2CUDA_DIRS)
	@echo "所有目录的 c2cuda 任务处理完毕。"

$(C2CUDA_DIRS):
	$(eval TARGET_DIR := $(strip $(patsubst c2cuda-%, %, $@)))
	@echo "正在尝试进入目录: [$(TARGET_DIR)]"
	@if [ -f "$(TARGET_DIR)/Makefile" ]; then \
		$(MAKE) -C $(TARGET_DIR) c2cuda; \
	else \
		echo "跳过: $(TARGET_DIR) (找不到 Makefile)"; \
	fi

# --- TEST 入口 ---
test: $(TEST_DIRS)
	@echo "所有目录的测试任务处理完毕。"

$(TEST_DIRS):
	$(eval TARGET_DIR := $(strip $(patsubst test-%, %, $@)))
	@if [ -f "$(TARGET_DIR)/Makefile" ]; then \
		$(MAKE) -C $(TARGET_DIR) test; \
	fi