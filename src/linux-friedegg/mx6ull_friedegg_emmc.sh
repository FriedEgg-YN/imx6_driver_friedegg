#!/bin/bash
export PATH=$PATH:/usr/local/arm/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf/bin
export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabihf-

# 清除编译产物，准备全新编译
make distclean
# 加载自定义配置到.config
# 注意通过make menuconfig修改的配置可以保存到.config，注意需要指定ARCH和CROSS_COMPILE，否则可能会保存错误的配置
# 想要固化可以make savedefconfig，生成一个新的defconfig文件，再粘贴到./arch/arm/configs/目录下
make imx_friedegg_emmc_defconfig
# 编译内核主体、设备树和全部模块
# make -j4 all
# make menuconfig
make zImage modules imx6ull-friedegg-emmc.dtb -j4