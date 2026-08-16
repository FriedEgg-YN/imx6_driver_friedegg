#!/bin/bash

set -e

PRO_FILE=$(realpath "$1")
WORKSPACE_DIR=$(cd "$(dirname "$0")/../../.." && pwd)
QMAKE="$WORKSPACE_DIR/buildroot/output/host/bin/qmake"
TARGET=$(sed -n 's/^[[:space:]]*TARGET[[:space:]]*=[[:space:]]*//p' "$PRO_FILE" | head -n 1)
BUILD_DIR="/tmp/${TARGET}-build"
NFS_DIR=${NFS_DIR:?set NFS_DIR to the NFS rootfs path}

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
"$QMAKE" -o "$BUILD_DIR/Makefile" "$PRO_FILE"
make -C "$BUILD_DIR" -j2
sudo install -D -m 0755 "$BUILD_DIR/$TARGET" "$NFS_DIR/usr/bin/$TARGET"

echo "Deployed $TARGET to $NFS_DIR/usr/bin/$TARGET"
