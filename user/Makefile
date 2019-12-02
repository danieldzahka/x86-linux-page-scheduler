CC=gcc
CFLAGS=-I. -I../include
TARGS=bin/test_heap bin/test_mmap bin/main bin/count_vmas bin/mulmat

.PHONY:all
all: $(TARGS)

bin/mulmat: mulmat.o | bin
	$(CC) -o $@ $^ $(CFLAGS)

bin/count_vmas: count_vmas.o | bin
	$(CC) -o $@ $^ $(CFLAGS)

bin/test_heap: test_heap.o | bin
	$(CC) -o $@ $^ $(CFLAGS)

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
