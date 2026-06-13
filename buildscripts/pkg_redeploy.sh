#!/bin/bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=buildscripts/lib/pkg.sh
. "$SCRIPT_DIR/lib/pkg.sh"

usage() {
    cat <<EOF
Usage:
  ./buildscripts/pkg_redeploy.sh <pkg>

Rebuild one enabled Buildroot external local package with:
  make <pkg>-dirclean
  make <pkg>

Then copy only the package-owned files from buildroot/output/target to NFS_DIR.
EOF
}

case "${1:-}" in
    ""|-h|--help|help)
        usage
        ;;
    *)
        rebuild_and_deploy_pkg "$1"
        ;;
esac
