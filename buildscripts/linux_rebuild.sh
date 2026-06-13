#!/bin/bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=buildscripts/lib/common.sh
. "$SCRIPT_DIR/lib/common.sh"

ensure_config
require_kernel_toolchain
sync_linux_override_sources

note "running linux-rebuild"
br_make linux-rebuild -j"$MAKE_JOBS"
require_file "$KERNEL_FILE"
deploy_tftp_file "$KERNEL_FILE"
