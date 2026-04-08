
WORKSPACE_DIR = ../../
TRANSLATE := $(WORKSPACE_DIR)/build/bin/translate.out
INCLUDE_DIR = ../

c2cuda:
	${TRANSLATE} 1.c -DAUTOC2CUDATEST
	${TRANSLATE} 2.c -DAUTOC2CUDATEST
# 	其他affine的测试

test:
# 	g++ 1.c ../tool.cpp -o ./bin/1.out && ./bin/1.out
# 	nvcc 1.cu ../tool.cpp -o ./bin/1.cu.out && ./bin/1.cu.out
# 	@echo "======================="
# 	cmp bin/1.bin bin/1.cu.bin
	g++ 2.c ../tool.cpp -o ./bin/2.out && ./bin/2.out
	nvcc 2.cu ../tool.cpp -o ./bin/2.cu.out && ./bin/2.cu.out
	@echo "======================="
	cmp bin/2.bin bin/2.cu.bin

	@echo "======================="