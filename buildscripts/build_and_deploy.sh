#!/bin/bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

usage() {
    cat <<EOF
Usage:
  ./buildscripts/build_and_deploy.sh all
  ./buildscripts/build_and_deploy.sh dtb
  ./buildscripts/build_and_deploy.sh zimage|linux-rebuild
  ./buildscripts/build_and_deploy.sh rootfs
  ./buildscripts/build_and_deploy.sh drv <pkg>
  ./buildscripts/build_and_deploy.sh drv add <pkg> <src-dir> [app-bin]
  ./buildscripts/build_and_deploy.sh drv list
  ./buildscripts/build_and_deploy.sh config status
  ./buildscripts/build_and_deploy.sh config save buildroot|busybox|linux|all
  ./buildscripts/build_and_deploy.sh config reset buildroot|busybox|linux|all

Direct entrypoints:
  ./buildscripts/all_rebuild.sh
  ./buildscripts/linux_rebuild.sh
  ./buildscripts/dtb.sh
  ./buildscripts/rootfs_redeploy.sh
  ./buildscripts/pkg_redeploy.sh <pkg>
  ./buildscripts/pkg_clean_stale.sh <pkg>
  ./buildscripts/config.sh status|save|reset ...
EOF
}

die() {
    echo "ERROR: $*" >&2
    exit 1
}

cmd="${1:-help}"
shift || true

case "$cmd" in
    all|-a)
        exec bash "$SCRIPT_DIR/all_rebuild.sh"
        ;;
    dtb)
        exec bash "$SCRIPT_DIR/dtb.sh"
        ;;
    zimage|zImage|linux-rebuild)
        exec bash "$SCRIPT_DIR/linux_rebuild.sh"
        ;;
    rootfs)
        exec bash "$SCRIPT_DIR/rootfs_redeploy.sh"
        ;;
    drv)
        action="${1:-}"
        case "$action" in
            add)
                [ -n "${2:-}" ] || die "drv add requires <pkg> <src-dir> [app-bin]"
                [ -n "${3:-}" ] || die "drv add requires <pkg> <src-dir> [app-bin]"
                # shellcheck source=buildscripts/lib/pkg.sh
                . "$SCRIPT_DIR/lib/pkg.sh"
                register_driver_package "$2" "$3" "${4:-}"
                ;;
            list)
                # shellcheck source=buildscripts/lib/pkg.sh
                . "$SCRIPT_DIR/lib/pkg.sh"
                list_driver_packages
                ;;
            ""|-h|--help|help)
                usage
                ;;
            *)
                exec bash "$SCRIPT_DIR/pkg_redeploy.sh" "$action"
                ;;
        esac
        ;;
    config)
        exec bash "$SCRIPT_DIR/config.sh" "$@"
        ;;
    help|-h|--help)
        usage
        ;;
    *)
        usage
        die "unknown command: $cmd"
        ;;
esac
