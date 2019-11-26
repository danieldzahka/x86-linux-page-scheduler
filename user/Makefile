CC=gcc
CFLAGS=-I. -I../include

bin/main: main.o | bin
	$(CC) -o $@ $^ $(CFLAGS)
bin:
	mkdir bin

.PHONY:clean
clean:
	rm *.o
	rm bin/*
