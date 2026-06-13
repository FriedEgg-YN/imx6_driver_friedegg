#!/bin/bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
WORKSPACE_DIR=$(dirname "$SCRIPT_DIR")
BSP_DIR="$WORKSPACE_DIR/bsp"
BUILDROOT_DIR="$WORKSPACE_DIR/buildroot"

# linux-menuconfig 修改后先保存到:
#   buildroot/output/build/linux-custom/.config
#
# 固化到 BSP 持久配置:
#   ./buildscripts/build_and_deploy.sh config save linux
# 固化后的文件:
#   bsp/configs/linux.config
#
# 重编并部署内核镜像:
#   ./buildscripts/build_and_deploy.sh zimage

make -C "$BUILDROOT_DIR" \
  BR2_EXTERNAL="$BSP_DIR" \
  BR2_PACKAGE_OVERRIDE_FILE="$BSP_DIR/local.mk" \
  linux-menuconfig
