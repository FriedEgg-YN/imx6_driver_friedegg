#!/bin/bash
export PATH=$PATH:/usr/local/arm/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf/bin
export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabihf-

make distclean
# 注意：配置名字换成了正点原子的内核默认配置
make imx_friedegg_emmc_defconfig
# 编译内核主体、设备树和全部模块
# make -j4 all
# make menuconfig
make zImage modules imx6ull-friedegg-emmc.dtb -j4