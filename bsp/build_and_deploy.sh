#!/bin/bash

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
exec bash "$SCRIPT_DIR/build_and_deploy_impl.sh" "$@"
