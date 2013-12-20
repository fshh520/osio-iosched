#
#  osio i/o scheduler.
#
#  Copyright (C) Octagram Sun <octagram@qq.com>
#

KERNELDIR ?= /lib/modules/`uname -r`/build
KERNEL_VERSION ?= `uname -r`
CROSS_COMPILER ?=
INSTALLDIR ?= out

PWD := $(shell pwd)
CC ?= $(CROSS_COMPILER)gcc

obj-m := osio.o
osio-objs := osio-iosched.o

all: osio-iosched.c
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

install: all
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules_install
	depmod

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions *.markers *.symvers *.order

.PHONY: clean install
