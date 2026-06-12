BSP_KERNEL_TOOLCHAIN_PATH ?= /usr/local/arm/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf
BSP_KERNEL_CROSS_COMPILE ?= $(BSP_KERNEL_TOOLCHAIN_PATH)/bin/arm-linux-gnueabihf-

# Buildroot's target toolchain is intentionally free to move forward for
# rootfs/Qt/user programs. The Linux 4.1 kernel and external .ko packages stay
# on the known-good Linaro 4.9 toolchain.
override LINUX_MAKE_FLAGS := $(filter-out CROSS_COMPILE=%,$(LINUX_MAKE_FLAGS)) CROSS_COMPILE="$(BSP_KERNEL_CROSS_COMPILE)"

define BSP_GCC_FINAL_CREATE_STAGING_USR_LIB
	mkdir -p $(STAGING_DIR)/usr/lib
endef

GCC_FINAL_PRE_INSTALL_STAGING_HOOKS += BSP_GCC_FINAL_CREATE_STAGING_USR_LIB
