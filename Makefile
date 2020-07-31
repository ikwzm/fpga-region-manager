HOST_ARCH       ?= $(shell uname -m | sed -e s/arm.*/arm/ -e s/aarch64.*/arm64/)
ARCH            ?= $(shell uname -m | sed -e s/arm.*/arm/ -e s/aarch64.*/arm64/)
KERNEL_SRC_DIR  ?= /lib/modules/$(shell uname -r)/build

ifeq ($(ARCH), arm)
 ifneq ($(HOST_ARCH), arm)
   CROSS_COMPILE  ?= arm-linux-gnueabihf-
 endif
endif
ifeq ($(ARCH), arm64)
 ifneq ($(HOST_ARCH), arm64)
   CROSS_COMPILE  ?= aarch64-linux-gnu-
 endif
endif

fpga-region-interface-obj  := fpga-region-interface.o
fpga-region-manager-obj    := fpga-region-manager.o
fpga-region-clock-obj      := fpga-region-clock.o

all: fpga-region-interface.ko fpga-region-manager.ko fpga-region-clock.ko

fpga-region-interface.ko:
	make -C $(KERNEL_SRC_DIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) obj-m=fpga-region-interface.o fpga-region-interface.ko

fpga-region-manager.ko:
	make -C $(KERNEL_SRC_DIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) obj-m=fpga-region-manager.o fpga-region-manager.ko

fpga-region-clock.ko:
	make -C $(KERNEL_SRC_DIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) obj-m=fpga-region-clock.o fpga-region-clock.ko

clean:
	make -C $(KERNEL_SRC_DIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) clean

