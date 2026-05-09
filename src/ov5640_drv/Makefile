# OV5640 camera driver modules + test app

obj-m += ov5640.o mx6s_capture.o

APP_NAME := ov5640_test
APP_SRC := ov5640_test.c
APP_CFLAGS ?= -Wall -O2

.PHONY: all app kernel_modules clean

all: modules app

app:
	$(CC) $(CPPFLAGS) $(CFLAGS) $(APP_CFLAGS) $(APP_SRC) -o $(APP_NAME) $(LDFLAGS)

kernel_modules:
	$(MAKE) -C $(KERNELDIR) \
		ARCH=$(ARCH) \
		CROSS_COMPILE=$(CROSS_COMPILE) \
		M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KERNELDIR) \
		ARCH=$(ARCH) \
		CROSS_COMPILE=$(CROSS_COMPILE) \
		M=$(CURDIR) clean
	rm -f $(APP_NAME)
