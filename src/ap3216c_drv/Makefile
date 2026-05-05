obj-m += ap3216c.o

APP_NAME := ap3216cApp
APP_SRC := ap3216cApp.c
APP_CFLAGS ?= -Wall -O2
APP_LDFLAGS ?= -pthread

.PHONY: all app kernel_modules clean vermagic

all: modules app

app:
	$(CC) $(CPPFLAGS) $(CFLAGS) $(APP_CFLAGS) $(APP_SRC) -o $(APP_NAME) $(LDFLAGS) $(APP_LDFLAGS)

kernel_modules:
	$(MAKE) -C $(KERNELDIR) \
		ARCH=$(ARCH) \
		CROSS_COMPILE=$(CROSS_COMPILE) \
		M=$(CURDIR) modules

vermagic:
	modinfo ap3216c.ko | grep vermagic || true

clean:
	$(MAKE) -C $(KERNELDIR) \
		ARCH=$(ARCH) \
		CROSS_COMPILE=$(CROSS_COMPILE) \
		M=$(CURDIR) clean
	rm -f $(APP_NAME)
