#!/bin/bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
WORKSPACE_DIR=$(dirname "$SCRIPT_DIR")
BSP_DIR="$WORKSPACE_DIR/bsp"
BUILDROOT_DIR="$WORKSPACE_DIR/buildroot"

# busybox-menuconfig 修改后先保存到:
#   buildroot/output/build/busybox-*/.config
#
# 当前 Buildroot 配置指定的 BusyBox 持久配置路径:
#   BR2_PACKAGE_BUSYBOX_CONFIG="$(BR2_EXTERNAL_BSP_PATH)/configs/busybox.config"
#
# 固化当前 BusyBox 配置到 BSP 持久配置:
#   ./buildscripts/build_and_deploy.sh config save busybox
# 或:
#   ./buildscripts/config.sh save busybox
#
# 固化后的文件:
#   bsp/configs/busybox.config
#
# 从 BSP 持久配置重新加载到 BusyBox build 目录:
#   ./buildscripts/build_and_deploy.sh config reset busybox
#
# 重编并部署 rootfs:
#   ./buildscripts/build_and_deploy.sh rootfs
#
# 说明:
#   BusyBox 子配置只控制 BusyBox applet 和 BusyBox 自身特性。
#   如果要启用 v4l2-ctl 等独立 Buildroot 包，应使用
#   buildroot_menuconfig.sh，而不是 BusyBox menuconfig。

make -C "$BUILDROOT_DIR" \
  BR2_EXTERNAL="$BSP_DIR" \
  BR2_PACKAGE_OVERRIDE_FILE="$BSP_DIR/local.mk" \
  busybox-menuconfig
