## @file
# Makefile script for the osio io-scheduler kernel modules
#
# Copyright (C) 2015 Octagram Sun <octagram@qq.com>
#
# This file is part of osio io-scheduler, as available from 
# https://gitcafe.com/octagram/osio. This file is free software;
# you can redistribute it and/or modify it under the terms of the GNU
# General Public License (GPL) as published by the Free Software
# Foundation, in version 2 as it comes in the "LICENSE" file of the
# osio distribution. osio is distributed in the hope that 
# it will be useful, but WITHOUT ANY WARRANTY of any kind.
#


KVERSION ?= `uname -r`
KERNELDIR ?= /lib/modules/$(KVERSION)/build
CROSS_COMPILE ?=

# ******** ******** ******** ******** +++ config +++ ******** ******** ******** ******** #
CONFIG_OSIO := m
CONFIG_OSIO_DEBUG := n
# ******** ******** ******** ******** --- config --- ******** ******** ******** ******** #

EXTRA_CFLAGS := -DCONFIG_OSIO=1

ifeq ($(CONFIG_OSIO_DEBUG), y)
EXTRA_CFLAGS += -DCONFIG_OSIO_DEBUG=1
endif

MODNAME := osio
PWD := $(shell pwd)
CC := $(CROSS_COMPILE)gcc
STRIP := $(CROSS_COMPILE)strip

obj-$(CONFIG_OSIO) := $(MODNAME).o
$(MODNAME)-y := osio-iosched.o

all: modules

modules:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

strip: modules
	$(STRIP) $(MODNAME).ko --strip-unneeded

deb:
	./mkdeb.sh 

install: all
	-mkdir -p /lib/modules/$(KVERSION)/kernel/block
	install -m 0755 -o root -g root $(MODNAME).ko /lib/modules/$(KVERSION)/kernel/block
	depmod -a

uninstall:
	rm /lib/modules/$(KVERSION)/kernel/block/$(MODNAME).ko
	-rmdir /lib/modules/$(KVERSION)/kernel/block
	depmod -a

load:
	-rmmod $(MODNAME)
	insmod $(MODNAME).ko

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions *.markers *.symvers *.order
	[ -f mkdeb.sh ] && ./mkdeb.sh clean

.PHONY: clean install modules strip
