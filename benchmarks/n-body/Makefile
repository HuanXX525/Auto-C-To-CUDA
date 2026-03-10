# Makefile for N-Body Simulation

# 编译器
CC = gcc

# 编译选项
# CFLAGS = 
CFLAGS = -Wall

# 链接选项
LDFLAGS = -lm

# 目标文件
TARGET = nbody

# 源文件
SRC = main.c nbody.c

# 目标文件（.o）
OBJ = $(SRC:.c=.o)

# 头文件
HEADERS = nbody.h

# 默认目标
all: $(TARGET)

# 链接规则
$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "编译成功: $(TARGET)"

# 编译规则
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# 清理规则
clean:
	rm -f $(OBJ) $(TARGET)
	@echo "清理完成"

# 运行规则
run: $(TARGET)
	./$(TARGET)

# 运行带参数
run-100: $(TARGET)
	./$(TARGET) 100

run-1000: $(TARGET)
	./$(TARGET) 1000

# 调试版本
debug: CFLAGS += -g -DDEBUG
debug: clean $(TARGET)

# 帮助信息
help:
	@echo "可用的 Make 目标:"
	@echo "  make          - 编译程序"
	@echo "  make clean    - 清理生成的文件"
	@echo "  make run      - 编译并运行程序（默认参数）"
	@echo "  make run-100  - 编译并运行程序（100个粒子）"
	@echo "  make run-1000 - 编译并运行程序（1000个粒子）"
	@echo "  make debug    - 编译调试版本"
	@echo "  make help     - 显示此帮助信息"

.PHONY: all clean run run-100 run-1000 debug help