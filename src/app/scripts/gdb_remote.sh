#!/bin/bash

set -euo pipefail

# Host-side remote debugging helper for the i.MX6ULL application.
#
# Before running this script, perform the following on the target board:
#
#   1. Make sure gdbserver is installed, for example:
#        gdbserver --version
#   2. Stop the normally started app_entrance, if it is already running.
#   3. Start the target program under gdbserver:
#        gdbserver :2345 /usr/bin/app_entrance
#      If the application needs environment variables, export them before
#      the gdbserver command. For an already running process, use instead:
#        gdbserver :2345 --attach <pid>
#   4. Ensure the host can reach <board-ip>:2345. The port number is arbitrary,
#      but it must match the port passed to this script.
#
# Usage:
#   ./gdb_remote.sh <board-ip> [port] [host-elf]
#
# Examples:
#   ./gdb_remote.sh <board-ip>
#   ./gdb_remote.sh <board-ip> 2345 /tmp/app_entrance-build/app_entrance

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
WORKSPACE_DIR=$(cd "$SCRIPT_DIR/../../.." && pwd)
BUILDROOT_DIR="$WORKSPACE_DIR/buildroot"
GDB="$BUILDROOT_DIR/output/host/bin/arm-buildroot-linux-gnueabihf-gdb"
SYSROOT="$BUILDROOT_DIR/output/host/arm-buildroot-linux-gnueabihf/sysroot"
DEFAULT_ELF="/tmp/app_entrance-build/app_entrance"

usage() {
    printf 'Usage: %s <board-ip> [port] [host-elf]\n' "$(basename "$0")"
}

if [ "$#" -lt 1 ] || [ "$#" -gt 3 ]; then
    usage >&2
    exit 2
fi

BOARD_IP="$1"
PORT="${2:-2345}"
HOST_ELF="${3:-$DEFAULT_ELF}"

[ -x "$GDB" ] || {
    printf 'ERROR: cross GDB not found: %s\n' "$GDB" >&2
    printf '       Enable BR2_PACKAGE_HOST_GDB and rebuild Buildroot.\n' >&2
    exit 1
}

[ -f "$SYSROOT"/lib/ld-linux-armhf.so.3 ] || {
    printf 'ERROR: target sysroot not found or incomplete: %s\n' "$SYSROOT" >&2
    exit 1
}

[ -f "$HOST_ELF" ] || {
    printf 'ERROR: host ELF not found: %s\n' "$HOST_ELF" >&2
    exit 1
}

exec "$GDB" "$HOST_ELF" \
    -ex "set sysroot $SYSROOT" \
    -ex "target remote $BOARD_IP:$PORT"
