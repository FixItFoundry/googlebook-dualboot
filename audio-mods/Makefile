KDIR ?= /lib/modules/$(shell uname -r)/build
ccflags-y += -I$(src)/sdw_utils -I$(src)/intel/boards

obj-m += sdw_utils/
obj-m += intel/boards/

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

install: all
	mkdir -p /lib/modules/$(shell uname -r)/updates
	cp -f sdw_utils/snd-soc-sdw-utils.ko /lib/modules/$(shell uname -r)/updates/
	cp -f intel/boards/snd-soc-sof-sdw.ko /lib/modules/$(shell uname -r)/updates/
	depmod -a
