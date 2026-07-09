#!/bin/bash

# Common helpers for BSP Buildroot build/deploy scripts.

if [ -n "${BSP_COMMON_SH_LOADED:-}" ]; then
    return 0
fi
BSP_COMMON_SH_LOADED=1

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
WORKSPACE_DIR=$(cd "$SCRIPT_DIR/../.." && pwd)
BSP_DIR="$WORKSPACE_DIR/bsp"
BUILDROOT_DIR="$WORKSPACE_DIR/buildroot"

KERNEL_IMAGE="${KERNEL_IMAGE:-zImage}"
TARGET_DTB="${TARGET_DTB:-imx6ull-friedegg-emmc.dtb}"
BSP_KERNEL_TOOLCHAIN_PATH="${BSP_KERNEL_TOOLCHAIN_PATH:-/usr/local/arm/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf}"
BSP_KERNEL_CROSS_COMPILE="${BSP_KERNEL_CROSS_COMPILE:-$BSP_KERNEL_TOOLCHAIN_PATH/bin/arm-linux-gnueabihf-}"

if [ -z "${DEFCONFIG_NAME:-}" ]; then
    DEFCONFIG_NAME="${TARGET_DTB%.dtb}"
    DEFCONFIG_NAME="${DEFCONFIG_NAME//-/_}_defconfig"
fi

default_deploy_home() {
    local passwd_entry user_home

    # Keep deploy paths under the invoking user home even if the whole
    # script is launched through sudo.
    if [ -n "${SUDO_USER:-}" ] && [ "$SUDO_USER" != "root" ]; then
        if passwd_entry=$(getent passwd "$SUDO_USER" 2>/dev/null); then
            IFS=: read -r _ _ _ _ _ user_home _ <<< "$passwd_entry"
            if [ -n "$user_home" ]; then
                printf "%s\n" "$user_home"
                return 0
            fi
        fi
    fi

    printf "%s\n" "$HOME"
}

DEPLOY_HOME="${DEPLOY_HOME:-$(default_deploy_home)}"
NFS_DIR="${NFS_DIR:-$DEPLOY_HOME/linux/nfs/rootfs}"
TFTP_DIR="${TFTP_DIR:-$DEPLOY_HOME/linux/tftp}"
MAKE_JOBS="${MAKE_JOBS:-$(nproc)}"

OUTPUT_IMAGE_DIR="$BUILDROOT_DIR/output/images"
ROOTFS_FILE="$OUTPUT_IMAGE_DIR/rootfs.tar"
KERNEL_FILE="$OUTPUT_IMAGE_DIR/$KERNEL_IMAGE"
DTB_FILE="$OUTPUT_IMAGE_DIR/$TARGET_DTB"
DEPLOY_MANIFEST_DIR="$OUTPUT_IMAGE_DIR/deploy-manifests"

MAKE_OPTS=(
    BR2_EXTERNAL="$BSP_DIR"
    BR2_PACKAGE_OVERRIDE_FILE="$BSP_DIR/local.mk"
    BR2_JLEVEL="$MAKE_JOBS"
    BSP_KERNEL_TOOLCHAIN_PATH="$BSP_KERNEL_TOOLCHAIN_PATH"
    BSP_KERNEL_CROSS_COMPILE="$BSP_KERNEL_CROSS_COMPILE"
)

die() {
    echo "ERROR: $*" >&2
    exit 1
}

note() {
    echo ">>> $*"
}

ok() {
    echo "OK: $*"
}

br_make() {
    make "${MAKE_OPTS[@]}" "$@"
}

require_file() {
    [ -f "$1" ] || die "missing file: $1"
}

require_dir() {
    [ -d "$1" ] || die "missing directory: $1"
}

ensure_config() {
    require_dir "$BUILDROOT_DIR"
    cd "$BUILDROOT_DIR"

    if [ ! -f "$BUILDROOT_DIR/.config" ]; then
        note "Buildroot is not configured, applying $DEFCONFIG_NAME"
        br_make "$DEFCONFIG_NAME"
    elif ! grep -q '^BR2_LINUX_KERNEL_USE_CUSTOM_CONFIG=y' "$BUILDROOT_DIR/.config"; then
        note "Buildroot .config is not using bsp/configs/linux.config yet; run './buildscripts/config.sh reset buildroot' to adopt the BSP defconfig"
    fi
}

guard_nfs_dir() {
    [ -n "$NFS_DIR" ] || die "NFS_DIR is empty"
    [ "$NFS_DIR" != "/" ] || die "refusing to deploy to /"
}

guard_tftp_dir() {
    [ -n "$TFTP_DIR" ] || die "TFTP_DIR is empty"
    [ "$TFTP_DIR" != "/" ] || die "refusing to deploy to /"
}

require_kernel_toolchain() {
    [ -x "${BSP_KERNEL_CROSS_COMPILE}gcc" ] || \
        die "kernel toolchain not found: ${BSP_KERNEL_CROSS_COMPILE}gcc"
}

kernel_cross_prefix() {
    require_kernel_toolchain
    printf '%s\n' "$BSP_KERNEL_CROSS_COMPILE"
}

linux_build_dir() {
    if [ -d "$BUILDROOT_DIR/output/build/linux-custom" ]; then
        printf '%s\n' "$BUILDROOT_DIR/output/build/linux-custom"
        return 0
    fi

    find "$BUILDROOT_DIR/output/build" -maxdepth 1 -type d \
        \( -name 'linux-custom' -o -name 'linux-[0-9]*' \) \
        2>/dev/null | sort | head -n1
}

linux_uses_override_srcdir() {
    [ -f "$BSP_DIR/local.mk" ] && \
        grep -q '^[[:space:]]*LINUX_OVERRIDE_SRCDIR[[:space:]]*=' "$BSP_DIR/local.mk"
}

sync_linux_override_sources() {
    local ldir

    linux_uses_override_srcdir || return 0

    ldir=$(linux_build_dir || true)
    [ -n "$ldir" ] || return 0

    note "syncing Linux override source"
    rm -f "$ldir/.stamp_rsynced"
    br_make linux-rsync
}

busybox_build_dir() {
    find "$BUILDROOT_DIR/output/build" -maxdepth 1 -type d -name 'busybox-*' \
        2>/dev/null | sort | head -n1
}

normalize_kconfig() {
    grep -vE '^# (Mon|Tue|Wed|Thu|Fri|Sat|Sun) ' "$1"
}

deploy_tftp_file() {
    local src="$1"
    local name

    require_file "$src"
    guard_tftp_dir
    name=$(basename "$src")
    mkdir -p "$TFTP_DIR"
    cp "$src" "$TFTP_DIR/"
    ok "deployed $name to $TFTP_DIR"
}

deploy_tftp_all() {
    deploy_tftp_file "$KERNEL_FILE"
    deploy_tftp_file "$DTB_FILE"
}

deploy_rootfs_full() {
    require_file "$ROOTFS_FILE"
    guard_nfs_dir

    note "full NFS rootfs deploy to $NFS_DIR"
    sudo mkdir -p "$NFS_DIR"
    sudo find "$NFS_DIR" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} +
    sudo tar -xf "$ROOTFS_FILE" -C "$NFS_DIR"
    ok "NFS rootfs refreshed from $ROOTFS_FILE"
}

kernel_release() {
    local ldir
    ldir=$(linux_build_dir)
    [ -n "$ldir" ] || die "Linux build directory not found; run './buildscripts/linux_rebuild.sh' or './buildscripts/all_rebuild.sh' first"

    make --no-print-directory -s -C "$ldir" ARCH=arm CROSS_COMPILE="$(kernel_cross_prefix)" kernelrelease
}
