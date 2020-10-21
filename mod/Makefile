LINUX_KERN   ?= /lib/modules/`uname -r`/build
EXTRA_CFLAGS += -DMODULE=1 -D__KERNEL__=1 -I$(PWD)/../include -I$(PWD)

pg_sched-y := \
	pg_sched_core.o \
	pg_sched_mem.o

pg_sched-objs := $(pg_sched-y)
obj-m := pg_sched.o


all:
	$(MAKE) -C $(LINUX_KERN) M=$(PWD) modules

clean:
	$(MAKE) -C $(LINUX_KERN) M=$(PWD) clean
