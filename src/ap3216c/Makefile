obj-m += ap3216c.o

APP_NAME := ap3216c_test
APP_SRC := ap3216c_test.c
APP_CFLAGS ?= -Wall -O2

.PHONY: all app kernel_modules clean

all: kernel_modules app

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