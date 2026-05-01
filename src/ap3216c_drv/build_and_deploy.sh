#!/bin/bash
set -euo pipefail

WORKSPACE_DIR="$(cd "$(dirname "$0")" && pwd)"

KERNELDIR="${KERNELDIR:-/home/alientek/Desktop/linux_NXP_mychange/linux-imx-rel_imx_4.1.15_2.1.0_ga_friedegg}"
TFTP_DIR="${TFTP_DIR:-/home/alientek/linux/tftp}"
NFS_DIR="${NFS_DIR:-/home/alientek/linux/nfs/rootfs/lib/modules/4.1.15}"

CC="${CC:-arm-linux-gnueabihf-gcc}"
APP_CFLAGS="${APP_CFLAGS:--Wall -O2}"
MAKE_JOBS="${MAKE_JOBS:-$(nproc)}"

# 重点：明确内核架构和交叉工具链前缀
ARCH="${ARCH:-arm}"
CROSS_COMPILE="${CROSS_COMPILE:-arm-linux-gnueabihf-}"

# 你的内核 defconfig（按你的内核实际名字改）
KERNEL_DEFCONFIG="${KERNEL_DEFCONFIG:-imx_v7_defconfig}"

DTS_NAME="imx6ull-friedegg-emmc.dts"
DTB_NAME="imx6ull-friedegg-emmc.dtb"
MODULE_NAME="ap3216c.ko"
APP_NAME="ap3216cApp"

KERNEL_DTS_DIR="${KERNELDIR}/arch/arm/boot/dts"
KERNEL_DTB_PATH="${KERNEL_DTS_DIR}/${DTB_NAME}"

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
check_cmd modinfo
require_dir "$WORKSPACE_DIR"
require_dir "$KERNELDIR"
require_dir "$KERNEL_DTS_DIR"
require_dir "$TFTP_DIR"
require_dir "$NFS_DIR"
require_file "${WORKSPACE_DIR}/${DTS_NAME}"
require_file "${WORKSPACE_DIR}/Makefile"

log "Stage 0/4: Prepare kernel config (must match running kernel)"
# 如果你板子上 /proc/config.gz 可用，最稳妥是先导出它覆盖 .config
# zcat /proc/config.gz > ${KERNELDIR}/.config
make -C "$KERNELDIR" ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" "$KERNEL_DEFCONFIG"
make -C "$KERNELDIR" ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" olddefconfig

# 校验是否 ARMv7
grep -q '^CONFIG_CPU_V7=y' "${KERNELDIR}/.config" || err "Kernel .config is not ARMv7 (CONFIG_CPU_V7!=y)"

log "Stage 1/4: Copy DTS to kernel tree"
cp -f "${WORKSPACE_DIR}/${DTS_NAME}" "${KERNEL_DTS_DIR}/"
ok "Copied ${DTS_NAME} -> ${KERNEL_DTS_DIR}"

log "Stage 2/4: Build DTBs"
make -C "$KERNELDIR" ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" -j"$MAKE_JOBS" dtbs
require_file "$KERNEL_DTB_PATH"
cp -f "$KERNEL_DTB_PATH" "$TFTP_DIR/"
ok "Copied ${DTB_NAME} -> ${TFTP_DIR}"

log "Stage 3/4: Build module and app"
make -C "$WORKSPACE_DIR" KERNELDIR="$KERNELDIR" kernel_modules
make -C "$WORKSPACE_DIR" CC="$CC" APP_CFLAGS="$APP_CFLAGS" app
require_file "${WORKSPACE_DIR}/${MODULE_NAME}"
require_file "${WORKSPACE_DIR}/${APP_NAME}"

log "Check module vermagic"
modinfo "${WORKSPACE_DIR}/${MODULE_NAME}" | grep vermagic
ok "Build complete: ${MODULE_NAME}, ${APP_NAME}"

log "Stage 4/4: Deploy module and app to NFS"
cp -f "${WORKSPACE_DIR}/${MODULE_NAME}" "${NFS_DIR}/"
cp -f "${WORKSPACE_DIR}/${APP_NAME}" "${NFS_DIR}/"
chmod +x "${NFS_DIR}/${APP_NAME}"
ok "Copied ${MODULE_NAME}, ${APP_NAME} -> ${NFS_DIR}"

cat <<EOF

Deployment done.
Kernel dir     : ${KERNELDIR}
TFTP dir       : ${TFTP_DIR}
NFS dir        : ${NFS_DIR}
Compiler(app)  : ${CC}
Kernel CC      : ${CROSS_COMPILE}gcc
ARCH           : ${ARCH}

Next on board:
  uname -r
  modinfo /lib/modules/4.1.15/ap3216c.ko | grep vermagic
  depmod -a
  modprobe ap3216c
  ./ap3216cApp /dev/ap3216c
EOF