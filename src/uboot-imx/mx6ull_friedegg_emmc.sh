#!/bin/bash

# 给脚本声明绝对路径，防止子终端找不到编译器
export PATH=$PATH:/usr/local/arm/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf/bin

# 指定架构和编译器前缀
export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabihf-

# 1. 彻底清理
make distclean

# 2. 生成配置
make mx6ull_friedegg_emmc_defconfig

# 3. 多核编译
make V=1 -j4

# 图形化配置（根据.config)
# make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- menuconfig