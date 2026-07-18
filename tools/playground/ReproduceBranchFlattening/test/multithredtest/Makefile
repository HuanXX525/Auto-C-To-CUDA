NVCC       ?= nvcc
NVCCFLAGS  ?= -std=c++17 -O2 -arch=native
TARGET      = benchmark

.PHONY: all clean run

all: $(TARGET)

$(TARGET): benchmark.cu heat3d_cuda.cu heat3d_cpu.cpp heat3d_common.h heat3d_cpu.h heat3d_cuda.h
	$(NVCC) $(NVCCFLAGS) -o $@ benchmark.cu heat3d_cuda.cu heat3d_cpu.cpp

run: $(TARGET)
	./$(TARGET)

run-verify: $(TARGET)
	./$(TARGET) --verify

test-small: $(TARGET)
	./$(TARGET) -N 4 -S 32 -T 10 --verify

test-medium: $(TARGET)
	./$(TARGET) -N 8 -S 64 -T 50 --verify

test-large: $(TARGET)
	./$(TARGET) -N 8 -S 120 -T 500

clean:
	rm -f $(TARGET)
