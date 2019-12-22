CC=gcc
CFLAGS=-I. -I../include
TARGS=bin/launcher
pg_sched_tmp_dir = /tmp/pg_sched

.PHONY:all
all: $(TARGS)

bin/launcher: launcher.o hashtable.o memory_map.o | bin $(pg_sched_tmp_dir)
	$(CC) -o $@ $^ $(CFLAGS)
bin:
	mkdir bin

$(pg_sched_tmp_dir):
	mkdir -p $(pg_sched_tmp_dir)

.PHONY:clean
clean:
	rm *.o
	rm bin/*
	rm $(pg_sched_tmp_dir)/*
