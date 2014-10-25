##
# elevator osio
#
# Date: Dec. 20, 2013
# Copyright (C) 2014 Octagram Sun <octagram@qq.com>
# License: GPL v2
#

KERNELDIR ?= /lib/modules/`uname -r`/build
KERNEL_VERSION ?= `uname -r`
CROSS_COMPILE ?=

########################## +++ config +++ ############################
CONFIG_OSIO := m
CONFIG_OSIO_DEBUG := y
########################## --- config --- ############################

ifeq ($(CONFIG_OSIO_DEBUG), y)
EXTRA_CFLAGS := -DCONFIG_OSIO_DEBUG=1
endif

MODULE_NAME := osio
PWD := $(shell pwd)
CC := $(CROSS_COMPILE)gcc
STRIP := $(CROSS_COMPILE)strip

obj-$(CONFIG_OSIO) := $(MODULE_NAME).o
$(MODULE_NAME)-y := osio-iosched.o

all: strip

modules:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

strip: modules
	$(STRIP) $(MODULE_NAME).ko --strip-unneeded

install: all
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules_install
	depmod

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions *.markers *.symvers *.order

.PHONY: clean install modules strip
