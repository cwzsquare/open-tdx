obj-m += verify_pte.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install:
	insmod verify_pte.ko

uninstall:
	rmmod verify_pte

.PHONY: all clean install uninstall

