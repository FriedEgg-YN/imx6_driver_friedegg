#!/bin/bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=buildscripts/lib/common.sh
. "$SCRIPT_DIR/lib/common.sh"

ensure_config
require_kernel_toolchain

note "reloading persisted Buildroot config from bsp/configs/$DEFCONFIG_NAME"
br_make "$DEFCONFIG_NAME"

note "running make clean"
br_make clean

note "building full system"
br_make -j"$MAKE_JOBS"
require_file "$KERNEL_FILE"
require_file "$DTB_FILE"
deploy_tftp_all
deploy_rootfs_full
