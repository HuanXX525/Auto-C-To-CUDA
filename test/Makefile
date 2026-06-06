WORKSPACE_DIR = ../
TRANSLATE := $(WORKSPACE_DIR)/build/bin/translate.out

# ─── 目录发现 ───────────────────────────────────────────────
# DIR=xxx     : 单独测试某个目录
# START_DIR=x : 从字母序 >= x 的目录开始 (续跑用)
# FORCE=1     : 跳过全部增量检查，强制全量重跑
ifeq ($(DIR),)
  RAW_DIRS := $(wildcard */)
  # 显式字母序排序 (wildcard 返回顺序依赖文件系统, 各平台可能不同)
  DIRS := $(shell echo "$(RAW_DIRS)" | tr ' ' '\n' | sort | tr '\n' ' ')
  ifneq ($(START_DIR),)
    DIRS := $(shell echo "$(DIRS)" | tr ' ' '\n' | awk -v s="$(START_DIR)/" '$$0 >= s' | tr '\n' ' ')
  endif
else
  DIRS := $(DIR)/
endif

TEST_DIRS   := $(addprefix test-, $(DIRS))
C2CUDA_DIRS := $(addprefix c2cuda-, $(DIRS))

.PHONY: all c2cuda test $(TEST_DIRS) $(C2CUDA_DIRS)

# ─── 时间戳比较辅助函数 ─────────────────────────────────────
# 用法: $(call newer_than, target, deps...)
# 返回 "1" 当 target 不存在 或 任意 dep 比 target 新
define newer_than
$(shell \
  t=$(1); \
  if [ ! -f "$$t" ]; then echo 1; exit 0; fi; \
  for d in $(2); do \
    if [ "$$d" -nt "$$t" ]; then echo 1; exit 0; fi; \
  done; \
  echo 0)
endef

# ─── C2CUDA: 翻译 .c → .cu ─────────────────────────────────
c2cuda: $(C2CUDA_DIRS)
	@echo "All .c files in test are translated successfully"

$(C2CUDA_DIRS):
	$(eval TARGET_DIR := $(patsubst c2cuda-%, %, $@))
	@C_FILES=$$(find $(TARGET_DIR) -name "*.c"); \
	if [ -z "$$C_FILES" ]; then \
		echo "No .c files found in $(TARGET_DIR)"; \
		exit 0; \
	fi; \
	CU_FILE="$(TARGET_DIR)/main.cu"; \
	NEED=0; \
	if [ "$(FORCE)" = "1" ] || [ ! -f "$$CU_FILE" ]; then NEED=1; \
	else \
		for c in $$C_FILES; do \
			[ "$$c" -nt "$$CU_FILE" ] && { NEED=1; break; }; \
		done; \
	fi; \
	if [ "$$NEED" = "1" ]; then \
		echo "Translating: $$C_FILES"; \
		$(TRANSLATE) -DAUTOC2CUDATEST $$C_FILES; \
	else \
		echo "Skipping translate $(TARGET_DIR) (main.cu is up to date)"; \
	fi

# ─── clean_bin ──────────────────────────────────────────────
clean_bin:
# 	find . -type f -name "*.bin" -delete

# ─── TEST: 编译 + 运行 + 比对 ────────────────────────────────
test: $(TEST_DIRS) clean_bin
	@echo "------------------------------------------------"
	@echo "\033[32mAll Test Passed\033[0m"

$(TEST_DIRS):
	$(eval TARGET_DIR := $(patsubst test-%, %, $@))
	@echo "------------------------------------------------"; \
	echo "正在测试目录: $(TARGET_DIR)"; \
	mkdir -p $(TARGET_DIR)/bin; \
	T="$(TARGET_DIR)"; \
	C_FILES=$$(find $$T -name "*.c" 2>/dev/null); \
	CU_FILES=$$(find $$T -name "*.cu" 2>/dev/null); \
	TOOL_CPP="$$(pwd)/tool.cpp"; \
	C_OUT="$$T/bin/C.out";  C_BIN="$$T/bin/c_out.bin"; \
	CU_OUT="$$T/bin/CU.out"; CU_BIN="$$T/bin/cuda_out.bin"; \
	NEED_C=0; NEED_CU=0; \
	if [ -z "$$C_FILES" ]; then \
		echo "  (no .c files, skipping C)"; \
	else \
		if [ "$(FORCE)" = "1" ] || [ ! -f "$$C_OUT" ]; then NEED_C=1; \
		else \
			for f in $$C_FILES "$$TOOL_CPP"; do \
				[ "$$f" -nt "$$C_OUT" ] && { NEED_C=1; break; }; \
			done; \
		fi; \
	fi; \
	if [ -z "$$CU_FILES" ]; then \
		echo "  (no .cu files, skipping CUDA)"; \
	else \
		if [ "$(FORCE)" = "1" ] || [ ! -f "$$CU_OUT" ]; then NEED_CU=1; \
		else \
			for f in $$CU_FILES "$$TOOL_CPP"; do \
				[ "$$f" -nt "$$CU_OUT" ] && { NEED_CU=1; break; }; \
			done; \
		fi; \
	fi; \
	if [ "$$NEED_C" = "1" ]; then \
		echo "  [rebuild] C 程序: $$C_FILES"; \
		g++ $$C_FILES "$$TOOL_CPP" -o "$$C_OUT" && (cd $$T && ./bin/C.out) || exit 1; \
	elif [ -f "$$C_OUT" ] && [ ! -f "$$C_BIN" ]; then \
		echo "  [re-run]  C 程序 (binary up to date, output missing)"; \
		(cd $$T && ./bin/C.out) || exit 1; \
	fi; \
	if [ "$$NEED_CU" = "1" ]; then \
		echo "  [rebuild] CUDA 程序: $$CU_FILES"; \
		nvcc $$CU_FILES "$$TOOL_CPP" -o "$$CU_OUT" && (cd $$T && ./bin/CU.out) || exit 1; \
	elif [ -f "$$CU_OUT" ] && [ ! -f "$$CU_BIN" ]; then \
		echo "  [re-run]  CUDA 程序 (binary up to date, output missing)"; \
		(cd $$T && ./bin/CU.out) || exit 1; \
	fi; \
	if [ ! -f "$$C_BIN" ]; then echo "  ERROR: $$C_BIN not found"; exit 1; fi; \
	if [ ! -f "$$CU_BIN" ]; then echo "  ERROR: $$CU_BIN not found"; exit 1; fi; \
	cd $$T && cmp bin/c_out.bin bin/cuda_out.bin; \
	echo "\033[32m$(TARGET_DIR) PASSED\033[0m"
