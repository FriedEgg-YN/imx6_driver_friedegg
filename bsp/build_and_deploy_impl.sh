#!/bin/bash

set -euo pipefail

KERNEL_IMAGE="${KERNEL_IMAGE:-zImage}"
TARGET_DTB="${TARGET_DTB:-imx6ull-friedegg-emmc.dtb}"
BSP_KERNEL_TOOLCHAIN_PATH="${BSP_KERNEL_TOOLCHAIN_PATH:-/usr/local/arm/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf}"
BSP_KERNEL_CROSS_COMPILE="${BSP_KERNEL_CROSS_COMPILE:-$BSP_KERNEL_TOOLCHAIN_PATH/bin/arm-linux-gnueabihf-}"

BSP_DIR=$(cd "$(dirname "$0")" && pwd)
WORKSPACE_DIR=$(dirname "$BSP_DIR")
BUILDROOT_DIR="$WORKSPACE_DIR/buildroot"

if [ -z "${DEFCONFIG_NAME:-}" ]; then
    DEFCONFIG_NAME="${TARGET_DTB%.dtb}"
    DEFCONFIG_NAME="${DEFCONFIG_NAME//-/_}_defconfig"
fi

NFS_DIR="${NFS_DIR:-$HOME/linux/nfs/rootfs}"
TFTP_DIR="${TFTP_DIR:-$HOME/linux/tftp}"
MAKE_JOBS="${MAKE_JOBS:-$(nproc)}"

OUTPUT_IMAGE_DIR="$BUILDROOT_DIR/output/images"
ROOTFS_FILE="$OUTPUT_IMAGE_DIR/rootfs.tar"
KERNEL_FILE="$OUTPUT_IMAGE_DIR/$KERNEL_IMAGE"
DTB_FILE="$OUTPUT_IMAGE_DIR/$TARGET_DTB"
MANIFEST_DIR="$OUTPUT_IMAGE_DIR/manifests"

MAKE_OPTS=(
    BR2_EXTERNAL="$BSP_DIR"
    BR2_PACKAGE_OVERRIDE_FILE="$BSP_DIR/local.mk"
    BR2_JLEVEL="$MAKE_JOBS"
    BSP_KERNEL_TOOLCHAIN_PATH="$BSP_KERNEL_TOOLCHAIN_PATH"
    BSP_KERNEL_CROSS_COMPILE="$BSP_KERNEL_CROSS_COMPILE"
)
SCRIPT_NAME="./bsp/build_and_deploy.sh"

usage() {
    cat <<EOF
Usage:
  $SCRIPT_NAME all
      Full rebuild: make clean, build all, deploy zImage/DTB and full NFS rootfs.

  $SCRIPT_NAME dtb
      Build only $TARGET_DTB from the existing Linux build tree and deploy it to TFTP.

  $SCRIPT_NAME zimage
      Run linux-rebuild and deploy only $KERNEL_IMAGE to TFTP.

  $SCRIPT_NAME rootfs
      Build the current Buildroot rootfs image and fully redeploy NFS rootfs.

  $SCRIPT_NAME drv <ap3216c|ov5640_drv|local-pkg>
      Rebuild one Buildroot external local driver package and incrementally deploy only its files to NFS.

  $SCRIPT_NAME drv add <pkg> <src-dir> [app-bin]
      Register a src/*_drv directory as a Buildroot external local kernel-module package.
      If app-bin is provided, the package also runs 'make app' and installs that binary to /usr/bin.

  $SCRIPT_NAME drv list
      List registered BSP local driver packages.

  $SCRIPT_NAME verify all|dtb|zimage|nfs-pkg <pkg>|ko <module-or-pkg>
      Compare built artifacts with deployed TFTP/NFS files and check module metadata.

  $SCRIPT_NAME config status
      Show whether Buildroot, BusyBox, and Linux temporary configs differ from BSP-persisted configs.

  $SCRIPT_NAME config save buildroot|busybox|linux|all
      Persist temporary configs back into bsp/configs.

  $SCRIPT_NAME config reset buildroot|busybox|linux|all
      Drop temporary config changes and reload BSP-persisted configs.

Environment overrides:
  NFS_DIR=<path> TFTP_DIR=<path> MAKE_JOBS=<n> KERNEL_IMAGE=<name> TARGET_DTB=<name>
  BSP_KERNEL_CROSS_COMPILE=<old-kernel-toolchain-prefix>
  MAKE_JOBS also sets Buildroot BR2_JLEVEL, including CMake/Qt sub-builds.
EOF
}

die() {
    echo "ERROR: $*" >&2
    exit 1
}

note() {
    echo ">>> $*"
}

ok() {
    echo "OK: $*"
}

br_make() {
    make "${MAKE_OPTS[@]}" "$@"
}

require_file() {
    [ -f "$1" ] || die "missing file: $1"
}

require_dir() {
    [ -d "$1" ] || die "missing directory: $1"
}

ensure_config() {
    require_dir "$BUILDROOT_DIR"
    cd "$BUILDROOT_DIR"

    if [ ! -f "$BUILDROOT_DIR/.config" ]; then
        note "Buildroot is not configured, applying $DEFCONFIG_NAME"
        br_make "$DEFCONFIG_NAME"
    elif ! grep -q '^BR2_LINUX_KERNEL_USE_CUSTOM_CONFIG=y' "$BUILDROOT_DIR/.config"; then
        note "Buildroot .config is not using bsp/configs/linux.config yet; run '$SCRIPT_NAME config reset buildroot' to adopt the BSP defconfig"
    fi
}

guard_nfs_dir() {
    [ -n "$NFS_DIR" ] || die "NFS_DIR is empty"
    [ "$NFS_DIR" != "/" ] || die "refusing to deploy to /"
}

kernel_cross_prefix() {
    require_kernel_toolchain
    printf '%s\n' "$BSP_KERNEL_CROSS_COMPILE"
}

require_kernel_toolchain() {
    [ -x "${BSP_KERNEL_CROSS_COMPILE}gcc" ] || \
        die "kernel toolchain not found: ${BSP_KERNEL_CROSS_COMPILE}gcc"
}

linux_build_dir() {
    if [ -d "$BUILDROOT_DIR/output/build/linux-custom" ]; then
        printf '%s\n' "$BUILDROOT_DIR/output/build/linux-custom"
        return 0
    fi

    find "$BUILDROOT_DIR/output/build" -maxdepth 1 -type d \
        \( -name 'linux-custom' -o -name 'linux-[0-9]*' \) \
        2>/dev/null | sort | head -n1
}

busybox_build_dir() {
    find "$BUILDROOT_DIR/output/build" -maxdepth 1 -type d -name 'busybox-*' \
        2>/dev/null | sort | head -n1
}

pkg_symbol() {
    printf '%s\n' "$1" | tr '[:lower:]-' '[:upper:]_'
}

pkg_mk_path() {
    local pkg="$1"
    local mk="$BSP_DIR/package/$pkg/$pkg.mk"

    [ -f "$mk" ] || die "unknown package '$pkg' (expected $mk)"
    printf '%s\n' "$mk"
}

pkg_enabled() {
    local pkg="$1"
    local sym
    sym=$(pkg_symbol "$pkg")

    grep -q "^BR2_PACKAGE_${sym}=y" "$BUILDROOT_DIR/.config"
}

pkg_site_dir() {
    local pkg="$1"
    local mk pkg_var site_raw site_path

    mk=$(pkg_mk_path "$pkg")
    pkg_var=$(sed -n 's/^\([A-Za-z0-9_][A-Za-z0-9_]*\)_SITE_METHOD[[:space:]]*=[[:space:]]*local[[:space:]]*$/\1/p' "$mk" | head -n1)
    [ -n "$pkg_var" ] || die "$pkg is not a SITE_METHOD = local package"

    site_raw=$(sed -n "s/^${pkg_var}_SITE[[:space:]]*=[[:space:]]*//p" "$mk" | head -n1)
    site_raw=${site_raw%%#*}
    site_raw=$(printf '%s\n' "$site_raw" | xargs)
    [ -n "$site_raw" ] || die "$pkg has no ${pkg_var}_SITE"

    site_path=$(printf '%s\n' "$site_raw" \
        | sed -e "s|\$(TOPDIR)|$BUILDROOT_DIR|g" \
              -e "s|\$(BR2_EXTERNAL_BSP_PATH)|$BSP_DIR|g")

    if printf '%s\n' "$site_path" | grep -q '\$('; then
        die "$pkg SITE path still has unresolved variables: $site_raw"
    fi

    if [ "${site_path#/}" = "$site_path" ]; then
        site_path="$BUILDROOT_DIR/$site_path"
    fi

    require_dir "$site_path"
    printf '%s\n' "$site_path"
}

pkg_build_dir() {
    local pkg="$1"
    find "$BUILDROOT_DIR/output/build" -maxdepth 1 -type d -name "$pkg-*" \
        2>/dev/null | sort | head -n1
}

pkg_file_list() {
    local pkg="$1"
    local dir

    dir=$(pkg_build_dir "$pkg")
    [ -n "$dir" ] || die "$pkg has no build directory under buildroot/output/build"
    require_file "$dir/.files-list.txt"
    printf '%s\n' "$dir/.files-list.txt"
}

validate_pkg_name() {
    local pkg="$1"

    if ! printf '%s\n' "$pkg" | grep -Eq '^[a-z0-9][a-z0-9_-]*$'; then
        die "invalid package name '$pkg'; use lowercase letters, digits, '_' or '-', starting with a letter/digit"
    fi
}

site_expr_for_src_dir() {
    local src_dir="$1"
    local abs_src rel

    if [ "${src_dir#/}" = "$src_dir" ]; then
        abs_src="$WORKSPACE_DIR/$src_dir"
    else
        abs_src="$src_dir"
    fi

    abs_src=$(cd "$abs_src" && pwd)
    case "$abs_src" in
        "$WORKSPACE_DIR"/*)
            rel=${abs_src#"$WORKSPACE_DIR"/}
            printf '$(TOPDIR)/../%s\n' "$rel"
            ;;
        *)
            printf '%s\n' "$abs_src"
            ;;
    esac
}

write_driver_pkg_mk() {
    local pkg="$1"
    local src_dir="$2"
    local app_bin="$3"
    local sym site_expr mk

    sym=$(pkg_symbol "$pkg")
    site_expr=$(site_expr_for_src_dir "$src_dir")
    mk="$BSP_DIR/package/$pkg/$pkg.mk"

    {
        printf '%s_VERSION = 1.0\n' "$sym"
        printf '%s_SITE = %s\n' "$sym" "$site_expr"
        printf '%s_SITE_METHOD = local\n\n' "$sym"

        if [ -n "$app_bin" ]; then
            printf 'define %s_BUILD_CMDS\n' "$sym"
            printf '\t$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) app \\\n'
            printf '\t\tCC="$(TARGET_CC)" \\\n'
            printf '\t\tCFLAGS="$(TARGET_CFLAGS)" \\\n'
            printf '\t\tLDFLAGS="$(TARGET_LDFLAGS)"\n'
            printf 'endef\n\n'
            printf 'define %s_INSTALL_TARGET_CMDS\n' "$sym"
            printf '\t$(INSTALL) -D -m 0755 $(@D)/%s $(TARGET_DIR)/usr/bin/%s\n' "$app_bin" "$app_bin"
            printf 'endef\n\n'
        fi

        printf '$(eval $(kernel-module))\n'
        printf '$(eval $(generic-package))\n'
    } > "$mk"
}

write_driver_config_in() {
    local pkg="$1"
    local sym="$2"
    local config_file="$BSP_DIR/package/$pkg/Config.in"

    cat > "$config_file" <<EOF
config BR2_PACKAGE_$sym
    bool "$pkg local driver"
    depends on BR2_LINUX_KERNEL
    help
      Local kernel module package from src for $pkg.
EOF
}

register_driver_package() {
    local pkg="$1"
    local src_dir="$2"
    local app_bin="${3:-}"
    local sym pkg_dir source_line

    validate_pkg_name "$pkg"
    require_dir "$src_dir"

    sym=$(pkg_symbol "$pkg")
    pkg_dir="$BSP_DIR/package/$pkg"
    [ ! -e "$pkg_dir" ] || die "$pkg already exists at $pkg_dir"

    mkdir -p "$pkg_dir"
    write_driver_pkg_mk "$pkg" "$src_dir" "$app_bin"
    write_driver_config_in "$pkg" "$sym"

    source_line="source \"\$BR2_EXTERNAL_BSP_PATH/package/$pkg/Config.in\""
    if ! grep -Fxq "$source_line" "$BSP_DIR/Config.in"; then
        printf '%s\n' "$source_line" >> "$BSP_DIR/Config.in"
    fi

    if ! grep -q "^BR2_PACKAGE_${sym}=y$" "$BSP_DIR/configs/$DEFCONFIG_NAME"; then
        printf 'BR2_PACKAGE_%s=y\n' "$sym" >> "$BSP_DIR/configs/$DEFCONFIG_NAME"
    fi

    ok "registered $pkg from $src_dir"
    note "run '$SCRIPT_NAME config reset buildroot' to enable it in buildroot/.config"
    note "then run '$SCRIPT_NAME drv $pkg' to build and incrementally deploy it"
}

list_driver_packages() {
    local pkg mk status site

    ensure_config
    for mk in "$BSP_DIR"/package/*/*.mk; do
        [ -f "$mk" ] || continue
        pkg=$(basename "$(dirname "$mk")")
        if pkg_enabled "$pkg"; then
            status="enabled"
        else
            status="disabled"
        fi
        site=$(pkg_site_dir "$pkg" 2>/dev/null || printf '%s\n' "unknown")
        printf '%-20s %-8s %s\n' "$pkg" "$status" "$site"
    done
}

sha_file() {
    sha256sum "$1" | awk '{print $1}'
}

normalize_kconfig() {
    grep -vE '^# (Mon|Tue|Wed|Thu|Fri|Sat|Sun) ' "$1"
}

source_tree_hash() {
    local dir="$1"

    if ! find "$dir" -type f \
        -not -path '*/.git/*' \
        -not -path '*/.tmp_versions/*' \
        -not -name '*.o' \
        -not -name '*.ko' \
        -not -name '*.mod.c' \
        -not -name '*.cmd' \
        -not -name '.*.cmd' \
        -not -name 'Module.symvers' \
        -not -name 'modules.order' \
        -not -perm /111 \
        -print -quit | grep -q .; then
        printf '%s\n' "empty"
        return 0
    fi

    find "$dir" -type f \
        -not -path '*/.git/*' \
        -not -path '*/.tmp_versions/*' \
        -not -name '*.o' \
        -not -name '*.ko' \
        -not -name '*.mod.c' \
        -not -name '*.cmd' \
        -not -name '.*.cmd' \
        -not -name 'Module.symvers' \
        -not -name 'modules.order' \
        -not -perm /111 \
        -print0 \
        | sort -z \
        | xargs -0 sha256sum \
        | sha256sum \
        | awk '{print $1}'
}

git_commit() {
    local dir="$1"

    if git -C "$dir" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        git -C "$dir" rev-parse --short HEAD
    else
        printf '%s\n' "unknown"
    fi
}

git_dirty_count() {
    local dir="$1"

    if git -C "$dir" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        git -C "$dir" status --short -- . | wc -l | tr -d ' '
    else
        printf '%s\n' "unknown"
    fi
}

git_dirty_hash() {
    local dir="$1"

    if git -C "$dir" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        {
            git -C "$dir" status --porcelain=v1 -- .
            git -C "$dir" diff -- .
            git -C "$dir" diff --cached -- .
            git -C "$dir" ls-files --others --exclude-standard -z -- . \
                | (cd "$dir" && xargs -0 -r sha256sum)
        } | sha256sum | awk '{print $1}'
    else
        printf '%s\n' "unknown"
    fi
}

ko_field() {
    local field="$1"
    local ko="$2"

    if command -v modinfo >/dev/null 2>&1; then
        modinfo -F "$field" "$ko" 2>/dev/null || true
    fi
}

kernel_release() {
    local ldir
    ldir=$(linux_build_dir)
    [ -n "$ldir" ] || die "Linux build directory not found; run '$SCRIPT_NAME all' or '$SCRIPT_NAME zimage' first"

    make --no-print-directory -s -C "$ldir" ARCH=arm CROSS_COMPILE="$(kernel_cross_prefix)" kernelrelease
}

deploy_tftp_file() {
    local src="$1"
    local name

    require_file "$src"
    name=$(basename "$src")
    mkdir -p "$TFTP_DIR"
    cp "$src" "$TFTP_DIR/"
    ok "deployed $name to $TFTP_DIR"
}

deploy_tftp_all() {
    deploy_tftp_file "$KERNEL_FILE"
    deploy_tftp_file "$DTB_FILE"
}

deploy_rootfs_full() {
    require_file "$ROOTFS_FILE"
    guard_nfs_dir

    note "full NFS rootfs deploy to $NFS_DIR"
    sudo mkdir -p "$NFS_DIR"
    sudo find "$NFS_DIR" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} +
    sudo tar -xf "$ROOTFS_FILE" -C "$NFS_DIR"
    ok "NFS rootfs refreshed from $ROOTFS_FILE"
}

deploy_pkg_to_nfs() {
    local pkg="$1"
    local list rel src dst count

    list=$(pkg_file_list "$pkg")
    guard_nfs_dir
    sudo mkdir -p "$NFS_DIR"

    count=0
    while IFS=, read -r owner rel; do
        [ "$owner" = "$pkg" ] || continue
        [ -n "$rel" ] || continue

        rel=${rel#./}
        src="$BUILDROOT_DIR/output/target/$rel"
        dst="$NFS_DIR/$rel"

        require_file "$src"
        sudo mkdir -p "$(dirname "$dst")"
        sudo cp -a "$src" "$dst"
        count=$((count + 1))
        echo "  $rel"
    done < "$list"

    [ "$count" -gt 0 ] || die "$pkg file list is empty"
    ok "incrementally deployed $count files for $pkg to $NFS_DIR"
}

write_pkg_manifest() {
    local pkg="$1"
    local srcdir list manifest rel target_file release

    srcdir=$(pkg_site_dir "$pkg")
    list=$(pkg_file_list "$pkg")
    release=$(kernel_release)
    mkdir -p "$MANIFEST_DIR"
    manifest="$MANIFEST_DIR/$pkg.manifest"

    {
        printf 'type=package\n'
        printf 'pkg=%s\n' "$pkg"
        printf 'time_utc=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
        printf 'workspace=%s\n' "$WORKSPACE_DIR"
        printf 'source_dir=%s\n' "$srcdir"
        printf 'source_hash=%s\n' "$(source_tree_hash "$srcdir")"
        printf 'git_commit=%s\n' "$(git_commit "$srcdir")"
        printf 'git_dirty_count=%s\n' "$(git_dirty_count "$srcdir")"
        printf 'kernelrelease=%s\n' "$release"

        while IFS=, read -r owner rel; do
            [ "$owner" = "$pkg" ] || continue
            rel=${rel#./}
            target_file="$BUILDROOT_DIR/output/target/$rel"
            [ -f "$target_file" ] || continue
            printf 'file=%s sha256=%s\n' "$rel" "$(sha_file "$target_file")"
            if [ "${target_file%.ko}" != "$target_file" ]; then
                printf 'ko=%s srcversion=%s vermagic=%s\n' \
                    "$rel" "$(ko_field srcversion "$target_file")" "$(ko_field vermagic "$target_file")"
            fi
        done < "$list"
    } > "$manifest"

    ok "wrote $manifest"
}

target_dts_hash() {
    local dts_dir="$WORKSPACE_DIR/src/linux-friedegg/arch/arm/boot/dts"
    local base="${TARGET_DTB%.dtb}"
    local files=()

    [ -f "$dts_dir/$base.dts" ] && files+=("$dts_dir/$base.dts")
    [ -f "$dts_dir/$base.dtsi" ] && files+=("$dts_dir/$base.dtsi")

    if [ "${#files[@]}" -eq 0 ]; then
        printf '%s\n' "unknown"
        return 0
    fi

    sha256sum "${files[@]}" | sha256sum | awk '{print $1}'
}

write_kernel_manifest() {
    local kind="$1"
    local manifest="$MANIFEST_DIR/$kind.manifest"
    local release="unknown"
    local kernel_src="$WORKSPACE_DIR/src/linux-friedegg"

    mkdir -p "$MANIFEST_DIR"
    if ldir=$(linux_build_dir) && [ -n "$ldir" ]; then
        release=$(kernel_release)
    fi

    {
        printf 'type=kernel\n'
        printf 'artifact=%s\n' "$kind"
        printf 'time_utc=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
        printf 'kernel_src=%s\n' "$kernel_src"
        printf 'kernel_git_commit=%s\n' "$(git_commit "$kernel_src")"
        printf 'kernel_git_dirty_count=%s\n' "$(git_dirty_count "$kernel_src")"
        printf 'kernel_git_dirty_hash=%s\n' "$(git_dirty_hash "$kernel_src")"
        printf 'kernelrelease=%s\n' "$release"
        case "$kind" in
            zimage)
                [ -f "$KERNEL_FILE" ] && printf 'zimage_sha256=%s\n' "$(sha_file "$KERNEL_FILE")"
                ;;
            dtb)
                [ -f "$DTB_FILE" ] && printf 'dtb_sha256=%s\n' "$(sha_file "$DTB_FILE")"
                printf 'target_dts_hash=%s\n' "$(target_dts_hash)"
                ;;
            all|kernel)
                [ -f "$KERNEL_FILE" ] && printf 'zimage_sha256=%s\n' "$(sha_file "$KERNEL_FILE")"
                [ -f "$DTB_FILE" ] && printf 'dtb_sha256=%s\n' "$(sha_file "$DTB_FILE")"
                printf 'target_dts_hash=%s\n' "$(target_dts_hash)"
                ;;
        esac
    } > "$manifest"

    ok "wrote $manifest"
}

build_dtb_only() {
    local ldir built target cross

    ensure_config
    require_kernel_toolchain
    ldir=$(linux_build_dir)
    [ -n "$ldir" ] || die "Linux build directory not found; run '$SCRIPT_NAME all' or '$SCRIPT_NAME zimage' once first"

    cross=$(kernel_cross_prefix)
    target="${TARGET_DTB%.dtb}.dtb"
    note "building DTB only: $target"

    if ! make -C "$ldir" ARCH=arm CROSS_COMPILE="$cross" "$target"; then
        make -C "$ldir" ARCH=arm CROSS_COMPILE="$cross" dtbs
    fi

    built="$ldir/arch/arm/boot/dts/$TARGET_DTB"
    if [ ! -f "$built" ]; then
        built=$(find "$ldir/arch/arm/boot/dts" -type f -name "$TARGET_DTB" -print -quit)
    fi

    require_file "$built"
    mkdir -p "$OUTPUT_IMAGE_DIR"
    cp "$built" "$DTB_FILE"
    touch "$ldir/.stamp_images_installed"
    deploy_tftp_file "$DTB_FILE"
    write_kernel_manifest dtb
}

build_zimage() {
    ensure_config
    require_kernel_toolchain
    note "running linux-rebuild"
    br_make linux-rebuild -j"$MAKE_JOBS"
    require_file "$KERNEL_FILE"
    deploy_tftp_file "$KERNEL_FILE"
    write_kernel_manifest zimage
}

build_rootfs() {
    ensure_config
    require_kernel_toolchain
    note "building Buildroot rootfs"
    br_make -j"$MAKE_JOBS"
    deploy_rootfs_full
}

build_all() {
    ensure_config
    require_kernel_toolchain
    note "running make clean"
    br_make clean
    note "building full system"
    br_make -j"$MAKE_JOBS"
    require_file "$KERNEL_FILE"
    require_file "$DTB_FILE"
    deploy_tftp_all
    deploy_rootfs_full
    write_kernel_manifest zimage
    write_kernel_manifest dtb
}

build_drv() {
    local pkg="$1"

    [ -n "$pkg" ] || die "drv requires a package name"
    ensure_config
    require_kernel_toolchain
    pkg_mk_path "$pkg" >/dev/null
    pkg_enabled "$pkg" || die "$pkg is not enabled in buildroot/.config"

    note "rebuilding driver package: $pkg"
    br_make "${pkg}-dirclean"
    br_make "$pkg" -j"$MAKE_JOBS"
    deploy_pkg_to_nfs "$pkg"
    write_pkg_manifest "$pkg"
}

run_drv() {
    local action="${1:-}"

    case "$action" in
        add)
            [ -n "${2:-}" ] || die "drv add requires <pkg> <src-dir> [app-bin]"
            [ -n "${3:-}" ] || die "drv add requires <pkg> <src-dir> [app-bin]"
            register_driver_package "$2" "$3" "${4:-}"
            ;;
        list)
            list_driver_packages
            ;;
        ""|-h|help)
            usage
            ;;
        *)
            build_drv "$action"
            ;;
    esac
}

mark_fail() {
    VERIFY_FAILS=$((VERIFY_FAILS + 1))
    echo "FAIL: $*" >&2
}

mark_warn() {
    echo "WARN: $*" >&2
}

verify_same_file() {
    local label="$1"
    local src="$2"
    local dst="$3"
    local src_hash dst_hash

    if [ ! -f "$src" ]; then
        mark_fail "$label source missing: $src"
        return
    fi
    if [ ! -f "$dst" ]; then
        mark_fail "$label deployed file missing: $dst"
        return
    fi

    src_hash=$(sha_file "$src")
    dst_hash=$(sha_file "$dst")
    if [ "$src_hash" = "$dst_hash" ]; then
        ok "$label hash matches ($src_hash)"
    else
        mark_fail "$label hash mismatch: build=$src_hash deployed=$dst_hash"
    fi
}

verify_kernel_manifest() {
    local kind="$1"
    local manifest="$MANIFEST_DIR/$kind.manifest"
    local kernel_src expected_commit expected_dirty expected_dirty_hash current_commit current_dirty current_dirty_hash

    if [ ! -f "$manifest" ]; then
        mark_warn "no $kind manifest; rebuild with '$SCRIPT_NAME $kind' or '$SCRIPT_NAME all'"
        return
    fi

    kernel_src=$(sed -n 's/^kernel_src=//p' "$manifest" | head -n1)
    expected_commit=$(sed -n 's/^kernel_git_commit=//p' "$manifest" | head -n1)
    expected_dirty=$(sed -n 's/^kernel_git_dirty_count=//p' "$manifest" | head -n1)
    expected_dirty_hash=$(sed -n 's/^kernel_git_dirty_hash=//p' "$manifest" | head -n1)

    [ -n "$kernel_src" ] && [ -d "$kernel_src" ] || {
        mark_fail "kernel manifest source_dir is invalid"
        return
    }

    current_commit=$(git_commit "$kernel_src")
    current_dirty=$(git_dirty_count "$kernel_src")
    current_dirty_hash=$(git_dirty_hash "$kernel_src")
    if [ -n "$expected_dirty_hash" ]; then
        if [ "$current_commit" = "$expected_commit" ] && [ "$current_dirty_hash" = "$expected_dirty_hash" ]; then
            ok "kernel source git state matches manifest ($current_commit dirty=$current_dirty)"
        else
            mark_fail "kernel source changed after last kernel build: manifest=$expected_commit dirty_hash=$expected_dirty_hash current=$current_commit dirty_hash=$current_dirty_hash"
        fi
    elif [ "$current_commit" = "$expected_commit" ] && [ "$current_dirty" = "$expected_dirty" ]; then
        ok "kernel source git state matches manifest ($current_commit dirty=$current_dirty)"
    else
        mark_fail "kernel source changed after last kernel build: manifest=$expected_commit dirty=$expected_dirty current=$current_commit dirty=$current_dirty"
    fi
}

verify_dtb_manifest() {
    local manifest="$MANIFEST_DIR/dtb.manifest"
    local expected current

    if [ ! -f "$manifest" ]; then
        mark_warn "no kernel manifest; rebuild with '$SCRIPT_NAME dtb', '$SCRIPT_NAME zimage' or '$SCRIPT_NAME all'"
        return
    fi

    expected=$(sed -n 's/^target_dts_hash=//p' "$manifest" | head -n1)
    current=$(target_dts_hash)
    if [ "$current" = "$expected" ]; then
        ok "$TARGET_DTB source DTS hash matches manifest"
    else
        mark_fail "$TARGET_DTB source DTS changed after last DTB build: manifest=$expected current=$current"
    fi
}

verify_zimage() {
    verify_same_file "$KERNEL_IMAGE" "$KERNEL_FILE" "$TFTP_DIR/$KERNEL_IMAGE"
    verify_kernel_manifest zimage
}

verify_dtb() {
    verify_same_file "$TARGET_DTB" "$DTB_FILE" "$TFTP_DIR/$TARGET_DTB"
    verify_dtb_manifest
}

verify_nfs_pkg() {
    local pkg="$1"
    local list rel src dst count

    [ -n "$pkg" ] || die "nfs-pkg requires a package name"
    list=$(pkg_file_list "$pkg")
    count=0

    while IFS=, read -r owner rel; do
        [ "$owner" = "$pkg" ] || continue
        rel=${rel#./}
        src="$BUILDROOT_DIR/output/target/$rel"
        dst="$NFS_DIR/$rel"
        verify_same_file "$pkg:$rel" "$src" "$dst"
        count=$((count + 1))
    done < "$list"

    [ "$count" -gt 0 ] || mark_fail "$pkg file list is empty"
}

pkg_for_relpath() {
    local rel="$1"
    local list pkg

    for list in "$BUILDROOT_DIR"/output/build/*/.files-list.txt; do
        [ -f "$list" ] || continue
        pkg=$(awk -F, -v path="./$rel" '$2 == path { print $1; exit }' "$list")
        if [ -n "$pkg" ]; then
            printf '%s\n' "$pkg"
            return 0
        fi
    done

    return 1
}

verify_ko_source_manifest() {
    local pkg="$1"
    local manifest="$MANIFEST_DIR/$pkg.manifest"
    local srcdir expected current

    if [ ! -f "$manifest" ]; then
        mark_warn "no manifest for $pkg; rebuild with '$SCRIPT_NAME drv $pkg' to enable source-hash verification"
        return
    fi

    srcdir=$(sed -n 's/^source_dir=//p' "$manifest" | head -n1)
    expected=$(sed -n 's/^source_hash=//p' "$manifest" | head -n1)
    [ -n "$srcdir" ] && [ -d "$srcdir" ] || {
        mark_fail "$pkg manifest source_dir is invalid"
        return
    }

    current=$(source_tree_hash "$srcdir")
    if [ "$current" = "$expected" ]; then
        ok "$pkg source hash matches manifest"
    else
        mark_fail "$pkg source changed after last driver build: manifest=$expected current=$current"
    fi
}

verify_ko_file() {
    local ko="$1"
    local rel release version srcversion vermagic pkg

    require_file "$ko"
    rel=${ko#"$BUILDROOT_DIR/output/target/"}
    release=$(kernel_release)
    version=$(printf '%s\n' "$rel" | awk -F/ '{print $3}')
    srcversion=$(ko_field srcversion "$ko")
    vermagic=$(ko_field vermagic "$ko")

    [ "$version" = "$release" ] || mark_fail "$rel is under module dir $version but kernelrelease is $release"

    if printf '%s\n' "$vermagic" | grep -q "^$release"; then
        ok "$rel vermagic starts with $release"
    else
        mark_fail "$rel vermagic mismatch: $vermagic"
    fi

    if [ -n "$srcversion" ]; then
        ok "$rel srcversion=$srcversion"
    else
        mark_warn "$rel has no srcversion; check CONFIG_MODULE_SRCVERSION_ALL"
    fi

    verify_same_file "$rel" "$ko" "$NFS_DIR/$rel"

    if pkg=$(pkg_for_relpath "$rel"); then
        verify_ko_source_manifest "$pkg"
    else
        mark_warn "could not map $rel to a Buildroot package"
    fi
}

verify_ko() {
    local name="$1"
    local pkg list rel ko found

    [ -n "$name" ] || die "ko requires a module or package name"
    found=0

    if [ -f "$BSP_DIR/package/$name/$name.mk" ]; then
        pkg="$name"
        list=$(pkg_file_list "$pkg")
        while IFS=, read -r owner rel; do
            [ "$owner" = "$pkg" ] || continue
            [ "${rel%.ko}" != "$rel" ] || continue
            rel=${rel#./}
            ko="$BUILDROOT_DIR/output/target/$rel"
            verify_ko_file "$ko"
            found=1
        done < "$list"
    else
        while IFS= read -r ko; do
            verify_ko_file "$ko"
            found=1
        done < <(find "$BUILDROOT_DIR/output/target/lib/modules" -type f -name "${name%.ko}.ko" 2>/dev/null | sort)
    fi

    [ "$found" -eq 1 ] || mark_fail "no .ko found for $name"
}

verify_all() {
    local pkg

    verify_zimage
    verify_dtb

    for pkg in "$BSP_DIR"/package/*; do
        [ -d "$pkg" ] || continue
        pkg=$(basename "$pkg")
        if [ -f "$BUILDROOT_DIR/.config" ] && pkg_enabled "$pkg"; then
            verify_nfs_pkg "$pkg"
        fi
    done
}

run_verify() {
    local what="${1:-}"
    shift || true
    VERIFY_FAILS=0

    ensure_config

    case "$what" in
        all) verify_all ;;
        dtb) verify_dtb ;;
        zimage) verify_zimage ;;
        nfs-pkg) verify_nfs_pkg "${1:-}" ;;
        ko) verify_ko "${1:-}" ;;
        ""|-h|help) usage; return 0 ;;
        *) die "unknown verify target: $what" ;;
    esac

    if [ "$VERIFY_FAILS" -ne 0 ]; then
        die "verification failed with $VERIFY_FAILS issue(s)"
    fi
    ok "verification passed"
}

config_buildroot_status() {
    local tmp base_norm tmp_norm

    if [ ! -f "$BUILDROOT_DIR/.config" ]; then
        echo "Buildroot: buildroot/.config missing"
        return
    fi

    tmp=$(mktemp)
    (
        cd "$BUILDROOT_DIR"
        br_make BR2_DEFCONFIG="$tmp" savedefconfig >/dev/null
    )

    base_norm=$(mktemp)
    tmp_norm=$(mktemp)
    normalize_kconfig "$BSP_DIR/configs/$DEFCONFIG_NAME" > "$base_norm"
    normalize_kconfig "$tmp" > "$tmp_norm"

    if diff -q "$base_norm" "$tmp_norm" >/dev/null; then
        echo "Buildroot: clean"
    else
        echo "Buildroot: differs from bsp/configs/$DEFCONFIG_NAME"
        diff -u "$base_norm" "$tmp_norm" || true
    fi
    rm -f "$tmp" "$base_norm" "$tmp_norm"
}

config_busybox_status() {
    local bdir base_norm tmp_norm
    bdir=$(busybox_build_dir)

    if [ -z "$bdir" ] || [ ! -f "$bdir/.config" ]; then
        echo "BusyBox: build config missing"
        return
    fi

    base_norm=$(mktemp)
    tmp_norm=$(mktemp)
    normalize_kconfig "$BSP_DIR/configs/busybox.config" > "$base_norm"
    normalize_kconfig "$bdir/.config" > "$tmp_norm"

    if diff -q "$base_norm" "$tmp_norm" >/dev/null; then
        echo "BusyBox: clean"
    else
        echo "BusyBox: differs from bsp/configs/busybox.config"
        diff -u "$base_norm" "$tmp_norm" || true
    fi
    rm -f "$base_norm" "$tmp_norm"
}

config_linux_status() {
    local ldir base_norm tmp_norm
    ldir=$(linux_build_dir)

    if [ -z "$ldir" ] || [ ! -f "$ldir/.config" ]; then
        echo "Linux: build config missing"
        return
    fi

    make --no-print-directory -s -C "$ldir" ARCH=arm CROSS_COMPILE="$(kernel_cross_prefix)" savedefconfig >/dev/null

    base_norm=$(mktemp)
    tmp_norm=$(mktemp)
    normalize_kconfig "$BSP_DIR/configs/linux.config" > "$base_norm"
    normalize_kconfig "$ldir/defconfig" > "$tmp_norm"

    if diff -q "$base_norm" "$tmp_norm" >/dev/null; then
        echo "Linux: clean"
    else
        echo "Linux: differs from bsp/configs/linux.config"
        diff -u "$base_norm" "$tmp_norm" || true
    fi
    rm -f "$base_norm" "$tmp_norm"
}

config_status() {
    ensure_config
    config_buildroot_status
    config_busybox_status
    config_linux_status
}

config_save_one() {
    local target="$1"
    local ldir

    ensure_config
    case "$target" in
        buildroot)
            br_make savedefconfig
            ok "saved Buildroot config to bsp/configs/$DEFCONFIG_NAME"
            ;;
        busybox)
            br_make busybox-update-config
            ok "saved BusyBox config to bsp/configs/busybox.config"
            ;;
        linux)
            if grep -q '^BR2_LINUX_KERNEL_USE_CUSTOM_CONFIG=y' "$BUILDROOT_DIR/.config"; then
                br_make linux-update-defconfig
            else
                ldir=$(linux_build_dir)
                [ -n "$ldir" ] || die "Linux build directory not found; run '$SCRIPT_NAME zimage' or '$SCRIPT_NAME all' first"
                make --no-print-directory -s -C "$ldir" ARCH=arm CROSS_COMPILE="$(kernel_cross_prefix)" savedefconfig
                cp "$ldir/defconfig" "$BSP_DIR/configs/linux.config"
            fi
            ok "saved Linux config to bsp/configs/linux.config"
            ;;
        all)
            config_save_one buildroot
            config_save_one busybox
            config_save_one linux
            ;;
        *) die "unknown config save target: $target" ;;
    esac
}

config_reset_one() {
    local target="$1"

    ensure_config
    case "$target" in
        buildroot)
            br_make "$DEFCONFIG_NAME"
            ok "reloaded Buildroot config from bsp/configs/$DEFCONFIG_NAME"
            ;;
        busybox)
            br_make busybox-dirclean
            br_make busybox-configure
            ok "reloaded BusyBox config from bsp/configs/busybox.config"
            ;;
        linux)
            if ! grep -q '^BR2_LINUX_KERNEL_USE_CUSTOM_CONFIG=y' "$BUILDROOT_DIR/.config"; then
                br_make "$DEFCONFIG_NAME"
            fi
            br_make linux-dirclean
            br_make linux-configure
            ok "reloaded Linux config from bsp/configs/linux.config"
            ;;
        all)
            config_reset_one buildroot
            config_reset_one busybox
            config_reset_one linux
            ;;
        *) die "unknown config reset target: $target" ;;
    esac
}

run_config() {
    local action="${1:-}"
    local target="${2:-}"

    case "$action" in
        status) config_status ;;
        save) [ -n "$target" ] || die "config save requires buildroot|busybox|linux|all"; config_save_one "$target" ;;
        reset) [ -n "$target" ] || die "config reset requires buildroot|busybox|linux|all"; config_reset_one "$target" ;;
        ""|-h|help) usage ;;
        *) die "unknown config action: $action" ;;
    esac
}

main() {
    local cmd="${1:-help}"
    shift || true

    case "$cmd" in
        all|-a) build_all ;;
        dtb) build_dtb_only ;;
        zimage|zImage) build_zimage ;;
        rootfs) build_rootfs ;;
        drv) run_drv "$@" ;;
        verify) run_verify "$@" ;;
        config) run_config "$@" ;;
        help|-h|--help) usage ;;
        *) usage; die "unknown command: $cmd" ;;
    esac
}

main "$@"
