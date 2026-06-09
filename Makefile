# Makefile — sysinfo_plus kernel module
#
# Usage:
#   make                      build sysinfo_plus.ko
#   make clean                remove build artefacts
#   sudo insmod sysinfo_plus.ko
#   cat /proc/sysinfo_plus
#   sudo rmmod sysinfo_plus

obj-m += sysinfo_plus.o

KDIR  := /lib/modules/$(shell uname -r)/build
PWD   := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
