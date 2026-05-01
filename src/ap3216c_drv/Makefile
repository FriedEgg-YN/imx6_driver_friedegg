# KERNELDIR ?= /home/alientek/Desktop/linux_NXP_mychange/linux-imx-rel_imx_4.1.15_2.1.0_ga_friedegg
# CURRENT_PATH := $(shell pwd)

# ARCH ?= arm
# CROSS_COMPILE ?= arm-linux-gnueabihf-

# CC ?= arm-linux-gnueabihf-gcc
# APP_CFLAGS ?= -Wall -O2
# APP_NAME := ap3216cApp
# APP_SRC := ap3216cApp.c

# obj-m := ap3216c.o

# .PHONY: build app kernel_modules clean vermagic

# build: kernel_modules

# app:
# 	$(CC) $(APP_CFLAGS) $(APP_SRC) -o $(APP_NAME) -pthread

# kernel_modules:
# 	$(MAKE) -C $(KERNELDIR) \
# 		ARCH=$(ARCH) \
# 		CROSS_COMPILE=$(CROSS_COMPILE) \
# 		M=$(CURRENT_PATH) modules

# vermagic:
# 	modinfo ap3216c.ko | grep vermagic || true

# clean:
# 	$(MAKE) -C $(KERNELDIR) \
# 		ARCH=$(ARCH) \
# 		CROSS_COMPILE=$(CROSS_COMPILE) \
# 		M=$(CURRENT_PATH) clean
# 	rm -f $(APP_NAME)
obj-m += ap3216c.o