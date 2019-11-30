CC=gcc
CFLAGS=-I. -I../include
TARGS= bin/test_mmap bin/main

.PHONY:all
all: $(TARGS)

bin/test_mmap: test_mmap.o | bin
	$(CC) -o $@ $^ $(CFLAGS)

bin/main: main.o | bin
	$(CC) -o $@ $^ $(CFLAGS)
bin:
	mkdir bin

.PHONY:clean
clean:
	rm *.o
	rm bin/*
