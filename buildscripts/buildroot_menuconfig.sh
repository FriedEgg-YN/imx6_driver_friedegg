#!/bin/bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
WORKSPACE_DIR=$(dirname "$SCRIPT_DIR")
BSP_DIR="$WORKSPACE_DIR/bsp"
BUILDROOT_DIR="$WORKSPACE_DIR/buildroot"

# Buildroot menuconfig 修改后先保存到:
#   buildroot/.config
#
# 当前 BSP 默认配置由 Buildroot defconfig 固化:
#   bsp/configs/imx6ull_friedegg_emmc_defconfig
#
# 固化当前 Buildroot 配置到 BSP 持久配置:
#   ./buildscripts/build_and_deploy.sh config save buildroot
# 或:
#   ./buildscripts/config.sh save buildroot
#
# 固化后的文件:
#   bsp/configs/imx6ull_friedegg_emmc_defconfig
#
# 从 BSP 持久配置重新加载到 buildroot/.config:
#   ./buildscripts/build_and_deploy.sh config reset buildroot
#
# 重编并部署 rootfs:
#   ./buildscripts/build_and_deploy.sh rootfs
#
# 说明:
#   Buildroot 总配置用于启用/关闭 rootfs 包、工具链选项、overlay、
#   Linux/BusyBox 配置文件路径等。若只修改 Linux 或 BusyBox 子配置，
#   优先使用 linux_menuconfig.sh 或 busybox_menuconfig.sh。

make -C "$BUILDROOT_DIR" \
  BR2_EXTERNAL="$BSP_DIR" \
  BR2_PACKAGE_OVERRIDE_FILE="$BSP_DIR/local.mk" \
  menuconfig
