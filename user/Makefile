CC=gcc
CFLAGS=-I. -I../include
TARGS=bin/launcher

.PHONY:all
all: $(TARGS)

bin/launcher: launcher.o hashtable.o | bin
	$(CC) -o $@ $^ $(CFLAGS)
bin:
	mkdir bin

.PHONY:clean
clean:
	rm *.o
	rm bin/*
