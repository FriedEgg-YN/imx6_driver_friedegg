#!/bin/sh

set -eu

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
KERNEL_MODULE_DIR="/lib/modules/$(uname -r)/extra"

unload_module()
{
	module=$1

	if grep -q "^${module}[[:space:]]" /proc/modules; then
		echo "rmmod $module"
		rmmod "$module"
	else
		echo "skip rmmod $module (not loaded)"
	fi
}

load_module()
{
	module=$1
	module_path="$SCRIPT_DIR/$module.ko"

	if [ ! -f "$module_path" ]; then
		module_path="$KERNEL_MODULE_DIR/$module.ko"
	fi

	if [ ! -f "$module_path" ]; then
		echo "missing module: $module.ko" >&2
		echo "checked: $SCRIPT_DIR and $KERNEL_MODULE_DIR" >&2
		exit 1
	fi

	echo "insmod $module_path"
	insmod "$module_path"
}

unload_module mx6s_capture
unload_module ov5640

load_module mx6s_capture
load_module ov5640
