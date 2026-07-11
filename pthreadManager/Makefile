CC = gcc
CFLAGS = -Wall -Wextra -pthread
TARGET = test
IMG_PROC = program/image_proc

.PHONY: all clean imgproc

all: $(TARGET)

$(TARGET): main.c thread_manager.c thread_manager.h
	$(CC) $(CFLAGS) main.c thread_manager.c -o $(TARGET)

imgproc: $(IMG_PROC).exe

$(IMG_PROC).exe: $(IMG_PROC).c
	$(CC) -O2 -Wall -Wextra $(IMG_PROC).c -lm -o $(IMG_PROC).exe

clean:
	rm -f $(TARGET) $(IMG_PROC).exe
