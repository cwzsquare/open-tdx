#obj-m := main.o
#KVERSION = $(shell uname -r)
#all:
#	make -C /lib/modules/$(KVERSION)/build M=$(PWD) modules
#clean:
#	make -C /lib/modules/$(KVERSION)/build M=$(PWD) clean
ifeq ($(KERNELRELEASE),)
		KERNELDIR ?= /home/cwz/open-tdx/linux-l1
		PWD := $(shell pwd)

modules:
		$(MAKE) -C $(KERNELDIR) M=$(PWD) modules EXTRA_CFLAGS="-g -DDEBUG"
modules_install:
		$(MAKE) -C $(KERNELDIR) M=$(PWD) modules_install

clean:
		rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions

.PHONY: modules modules_install clean

else
        # called from kernel build system: just declare what our modules are
		obj-m := proto.o
		proto-y := proto_asm.o protovirt.o
endif
