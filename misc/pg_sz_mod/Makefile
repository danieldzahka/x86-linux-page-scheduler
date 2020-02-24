LINUX_KERN   ?= /home/dzahka3/experimental/linux-5.4.18
EXTRA_CFLAGS += -DMODULE=1 -D__KERNEL__=1

obj-m := pg_size.o

all:
	$(MAKE) -C $(LINUX_KERN) M=$(PWD) modules

clean:
	$(MAKE) -C $(LINUX_KERN) M=$(PWD) clean
