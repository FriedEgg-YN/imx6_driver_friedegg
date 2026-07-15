obj-m += ld2410c.o

APP_NAMES := ld2410c_attach ld2410c_rawdump
APP_CFLAGS ?= -Wall -O2 -Iinclude
ccflags-y += -I$(src)/include

.PHONY: all app kernel_modules clean

all: kernel_modules app

app:
	$(CC) $(CPPFLAGS) $(CFLAGS) $(APP_CFLAGS) ld2410c_attach.c -o ld2410c_attach $(LDFLAGS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(APP_CFLAGS) ld2410c_rawdump.c -o ld2410c_rawdump $(LDFLAGS)

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
	rm -f $(APP_NAMES)
