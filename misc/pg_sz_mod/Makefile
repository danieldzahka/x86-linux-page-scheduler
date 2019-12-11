LINUX_KERN   ?= /home/dzahka3/thermo-kernel/thermostat_asplos_2017-release_v1
EXTRA_CFLAGS += -DMODULE=1 -D__KERNEL__=1

obj-m := pg_size.o

all:
	$(MAKE) -C $(LINUX_KERN) M=$(PWD) modules

clean:
	$(MAKE) -C $(LINUX_KERN) M=$(PWD) clean
