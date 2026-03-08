CC = gcc
CFLAGS = -Wall -g

TARGET = multifiles
OBJS = main.o head.o

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

main.o: main.c head.h
	$(CC) $(CFLAGS) -c main.c

head.o: head.c head.h
	$(CC) $(CFLAGS) -c head.c

.PHONY: clean
clean:
	rm -f $(OBJS) $(TARGET)