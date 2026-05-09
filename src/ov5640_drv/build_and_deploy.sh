#!/bin/bash
set -euo pipefail

WORKSPACE_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$WORKSPACE_DIR")"
SRC_KERNEL_DIR="$PROJECT_DIR/src/linux-friedegg"

# Default paths — override with env vars
KERNELDIR="${KERNELDIR:-$SRC_KERNEL_DIR}"
TFTP_DIR="${TFTP_DIR:-$HOME/linux/tftp}"
NFS_MODULES_DIR="${NFS_MODULES_DIR:-$HOME/linux/nfs/rootfs/lib/modules/4.1.15}"
NFS_BIN_DIR="${NFS_BIN_DIR:-$HOME/linux/nfs/rootfs/usr/bin}"

CC="${CC:-arm-linux-gnueabihf-gcc}"
ARCH="${ARCH:-arm}"
CROSS_COMPILE="${CROSS_COMPILE:-arm-linux-gnueabihf-}"
MAKE_JOBS="${MAKE_JOBS:-$(nproc)}"

MODULE_NAMES="ov5640.ko mx6s_capture.ko"
APP_NAME="ov5640_test"

log(){ echo "[INFO] $*"; }
ok(){ echo "[OK] $*"; }
err(){ echo "[ERR] $*" >&2; exit 1; }
require_dir(){ [ -d "$1" ] || err "Directory not found: $1"; }
require_file(){ [ -f "$1" ] || err "File not found: $1"; }
check_cmd(){ command -v "$1" >/dev/null 2>&1 || err "Command not found: $1"; }

log "Validating environment"
check_cmd make
check_cmd "${CROSS_COMPILE}gcc"
check_cmd "$CC"
require_dir "$WORKSPACE_DIR"
require_dir "$KERNELDIR"

# Build kernel modules
log "Building kernel modules..."
make -C "$WORKSPACE_DIR" \
    KERNELDIR="$KERNELDIR" \
    ARCH="$ARCH" \
    CROSS_COMPILE="$CROSS_COMPILE" \
    -j"$MAKE_JOBS" kernel_modules

for m in $MODULE_NAMES; do
    require_file "${WORKSPACE_DIR}/${m}"
    modinfo "${WORKSPACE_DIR}/${m}" | grep vermagic || true
done
ok "Modules built: $MODULE_NAMES"

# Build test app
log "Building test app..."
make -C "$WORKSPACE_DIR" \
    CC="$CC" \
    ARCH="$ARCH" \
    CROSS_COMPILE="$CROSS_COMPILE" \
    app
require_file "${WORKSPACE_DIR}/${APP_NAME}"
ok "App built: ${APP_NAME}"

# Deploy modules to NFS
log "Deploying modules to NFS..."
mkdir -p "$NFS_MODULES_DIR"
for m in $MODULE_NAMES; do
    cp -f "${WORKSPACE_DIR}/${m}" "${NFS_MODULES_DIR}/"
    ok "Deployed ${m} -> ${NFS_MODULES_DIR}"
done

# Deploy app to NFS
mkdir -p "$NFS_BIN_DIR"
cp -f "${WORKSPACE_DIR}/${APP_NAME}" "${NFS_BIN_DIR}/"
chmod +x "${NFS_BIN_DIR}/${APP_NAME}"
ok "Deployed ${APP_NAME} -> ${NFS_BIN_DIR}"

cat <<EOF

============================================
Deployment complete!
============================================

Next on the board:
  # Load modules (order matters!)
  insmod /lib/modules/4.1.15/mx6s_capture.ko
  insmod /lib/modules/4.1.15/ov5640.ko

  # Verify
  dmesg | grep -E 'ov5640|mx6s'
  ls -l /dev/video0
  v4l2-ctl --device /dev/video0 --all

  # Launch preview (requires fb0 LCD)
  ov5640_test /dev/video0

EOF
