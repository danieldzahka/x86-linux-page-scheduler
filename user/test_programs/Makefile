CC=gcc
CFLAGS=-I. -I../../include
CXX=g++
CXXFLAGS=-std=c++11 -I. -I../include
TARGS=bin/mulmat

.PHONY:all
all: $(TARGS)

bin/mulmat: mulmat.o | bin
	$(CC) -o $@ $^ $(CFLAGS)

bin:
	mkdir bin

.PHONY:clean
clean:
	rm *.o
	rm bin/*
