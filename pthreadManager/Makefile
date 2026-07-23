CC = gcc
AR = ar
ARFLAGS = rcs
CFLAGS = -Wall -Wextra -pthread -Isrc -Ithird_party -Iprogram
TARGET = bin/test
LIB = lib/libimage_proc.a

SRCS = $(wildcard src/*.c third_party/*.c)
OBJS = $(patsubst %.c,build/%.o,$(notdir $(SRCS)))

.PHONY: all clean test_target

all: $(LIB) $(TARGET)

$(LIB): program/image_proc.c
	$(CC) -O2 -Wall -Wextra -Iprogram -c program/image_proc.c -o build/image_proc.o
	$(AR) $(ARFLAGS) $(LIB) build/image_proc.o

$(TARGET): $(SRCS) $(LIB)
	$(CC) $(CFLAGS) $(SRCS) -Llib -limage_proc -lm -o $(TARGET)

test_target: bin/test_app
bin/test_app: test/test.c
	$(CC) $(CFLAGS) test/test.c -o bin/test_app

clean:
	rm -f $(TARGET) $(TARGET).exe $(LIB) bin/test_app bin/test_app.exe build/*.o
