#!/bin/bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=buildscripts/lib/pkg.sh
. "$SCRIPT_DIR/lib/pkg.sh"

usage() {
    cat <<EOF
Usage:
  ./buildscripts/pkg_clean_stale.sh <pkg>

Remove NFS files that belonged to this package in the previous deploy manifest
but no longer appear in the current Buildroot .files-list.txt.
EOF
}

case "${1:-}" in
    ""|-h|--help|help)
        usage
        ;;
    *)
        validate_pkg_name "$1"
        ensure_config
        clean_pkg_stale_from_nfs "$1"
        ;;
esac
