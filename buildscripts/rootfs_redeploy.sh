#!/bin/bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=buildscripts/lib/common.sh
. "$SCRIPT_DIR/lib/common.sh"

ensure_config
require_kernel_toolchain

note "building Buildroot rootfs"
br_make -j"$MAKE_JOBS"
deploy_rootfs_full
