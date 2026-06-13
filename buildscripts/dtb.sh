#!/bin/bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=buildscripts/lib/common.sh
. "$SCRIPT_DIR/lib/common.sh"

ensure_config
require_kernel_toolchain
sync_linux_override_sources

ldir=$(linux_build_dir || true)
[ -n "$ldir" ] || die "Linux build directory not found; run './buildscripts/linux_rebuild.sh' or './buildscripts/all_rebuild.sh' once first"

cross=$(kernel_cross_prefix)
target="${TARGET_DTB%.dtb}.dtb"
note "building DTB only: $target"

if ! make -C "$ldir" ARCH=arm CROSS_COMPILE="$cross" "$target"; then
    make -C "$ldir" ARCH=arm CROSS_COMPILE="$cross" dtbs
fi

built="$ldir/arch/arm/boot/dts/$TARGET_DTB"
if [ ! -f "$built" ]; then
    built=$(find "$ldir/arch/arm/boot/dts" -type f -name "$TARGET_DTB" -print -quit)
fi

require_file "$built"
mkdir -p "$OUTPUT_IMAGE_DIR"
cp "$built" "$DTB_FILE"
touch "$ldir/.stamp_images_installed"
deploy_tftp_file "$DTB_FILE"
