#!/bin/bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=buildscripts/lib/common.sh
. "$SCRIPT_DIR/lib/common.sh"

usage() {
    cat <<EOF
Usage:
  ./buildscripts/config.sh status
  ./buildscripts/config.sh save buildroot|busybox|linux|all
  ./buildscripts/config.sh reset buildroot|busybox|linux|all
EOF
}

config_buildroot_status() {
    local tmp base_norm tmp_norm

    if [ ! -f "$BUILDROOT_DIR/.config" ]; then
        echo "Buildroot: buildroot/.config missing"
        return
    fi

    tmp=$(mktemp)
    (
        cd "$BUILDROOT_DIR"
        br_make BR2_DEFCONFIG="$tmp" savedefconfig >/dev/null
    )

    base_norm=$(mktemp)
    tmp_norm=$(mktemp)
    normalize_kconfig "$BSP_DIR/configs/$DEFCONFIG_NAME" > "$base_norm"
    normalize_kconfig "$tmp" > "$tmp_norm"

    if diff -q "$base_norm" "$tmp_norm" >/dev/null; then
        echo "Buildroot: clean"
    else
        echo "Buildroot: differs from bsp/configs/$DEFCONFIG_NAME"
        diff -u "$base_norm" "$tmp_norm" || true
    fi
    rm -f "$tmp" "$base_norm" "$tmp_norm"
}

config_busybox_status() {
    local bdir base_norm tmp_norm
    bdir=$(busybox_build_dir)

    if [ -z "$bdir" ] || [ ! -f "$bdir/.config" ]; then
        echo "BusyBox: build config missing"
        return
    fi

    base_norm=$(mktemp)
    tmp_norm=$(mktemp)
    normalize_kconfig "$BSP_DIR/configs/busybox.config" > "$base_norm"
    normalize_kconfig "$bdir/.config" > "$tmp_norm"

    if diff -q "$base_norm" "$tmp_norm" >/dev/null; then
        echo "BusyBox: clean"
    else
        echo "BusyBox: differs from bsp/configs/busybox.config"
        diff -u "$base_norm" "$tmp_norm" || true
    fi
    rm -f "$base_norm" "$tmp_norm"
}

config_linux_status() {
    local ldir base_norm tmp_norm
    ldir=$(linux_build_dir)

    if [ -z "$ldir" ] || [ ! -f "$ldir/.config" ]; then
        echo "Linux: build config missing"
        return
    fi

    make --no-print-directory -s -C "$ldir" ARCH=arm CROSS_COMPILE="$(kernel_cross_prefix)" savedefconfig >/dev/null

    base_norm=$(mktemp)
    tmp_norm=$(mktemp)
    normalize_kconfig "$BSP_DIR/configs/linux.config" > "$base_norm"
    normalize_kconfig "$ldir/defconfig" > "$tmp_norm"

    if diff -q "$base_norm" "$tmp_norm" >/dev/null; then
        echo "Linux: clean"
    else
        echo "Linux: differs from bsp/configs/linux.config"
        diff -u "$base_norm" "$tmp_norm" || true
    fi
    rm -f "$base_norm" "$tmp_norm"
}

config_status() {
    ensure_config
    config_buildroot_status
    config_busybox_status
    config_linux_status
}

config_save_one() {
    local target="$1"
    local ldir

    ensure_config
    case "$target" in
        buildroot)
            br_make savedefconfig
            ok "saved Buildroot config to bsp/configs/$DEFCONFIG_NAME"
            ;;
        busybox)
            br_make busybox-update-config
            ok "saved BusyBox config to bsp/configs/busybox.config"
            ;;
        linux)
            if grep -q '^BR2_LINUX_KERNEL_USE_CUSTOM_CONFIG=y' "$BUILDROOT_DIR/.config"; then
                br_make linux-update-defconfig
            else
                ldir=$(linux_build_dir)
                [ -n "$ldir" ] || die "Linux build directory not found; run './buildscripts/linux_rebuild.sh' or './buildscripts/all_rebuild.sh' first"
                make --no-print-directory -s -C "$ldir" ARCH=arm CROSS_COMPILE="$(kernel_cross_prefix)" savedefconfig
                cp "$ldir/defconfig" "$BSP_DIR/configs/linux.config"
            fi
            ok "saved Linux config to bsp/configs/linux.config"
            ;;
        all)
            config_save_one buildroot
            config_save_one busybox
            config_save_one linux
            ;;
        *) die "unknown config save target: $target" ;;
    esac
}

config_reset_one() {
    local target="$1"

    ensure_config
    case "$target" in
        buildroot)
            br_make "$DEFCONFIG_NAME"
            ok "reloaded Buildroot config from bsp/configs/$DEFCONFIG_NAME"
            ;;
        busybox)
            br_make busybox-dirclean
            br_make busybox-configure
            ok "reloaded BusyBox config from bsp/configs/busybox.config"
            ;;
        linux)
            if ! grep -q '^BR2_LINUX_KERNEL_USE_CUSTOM_CONFIG=y' "$BUILDROOT_DIR/.config"; then
                br_make "$DEFCONFIG_NAME"
            fi
            br_make linux-dirclean
            br_make linux-configure
            ok "reloaded Linux config from bsp/configs/linux.config"
            ;;
        all)
            config_reset_one buildroot
            config_reset_one busybox
            config_reset_one linux
            ;;
        *) die "unknown config reset target: $target" ;;
    esac
}

action="${1:-}"
target="${2:-}"

case "$action" in
    status)
        config_status
        ;;
    save)
        [ -n "$target" ] || die "config save requires buildroot|busybox|linux|all"
        config_save_one "$target"
        ;;
    reset)
        [ -n "$target" ] || die "config reset requires buildroot|busybox|linux|all"
        config_reset_one "$target"
        ;;
    ""|-h|--help|help)
        usage
        ;;
    *)
        die "unknown config action: $action"
        ;;
esac
