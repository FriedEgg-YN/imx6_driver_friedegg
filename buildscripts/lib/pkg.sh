#!/bin/bash

# Buildroot local package helpers and incremental NFS deployment.

if [ -n "${BSP_PKG_SH_LOADED:-}" ]; then
    return 0
fi
BSP_PKG_SH_LOADED=1

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=buildscripts/lib/common.sh
. "$SCRIPT_DIR/common.sh"

pkg_symbol() {
    printf '%s\n' "$1" | tr '[:lower:]-' '[:upper:]_'
}

validate_pkg_name() {
    local pkg="$1"

    if ! printf '%s\n' "$pkg" | grep -Eq '^[a-z0-9][a-z0-9_-]*$'; then
        die "invalid package name '$pkg'; use lowercase letters, digits, '_' or '-', starting with a letter/digit"
    fi
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
    local dir

    dir=$(find "$BUILDROOT_DIR/output/build" -maxdepth 1 -type d -name "$pkg-*" \
        2>/dev/null | sort | head -n1)

    if [ -n "$dir" ]; then
        printf '%s\n' "$dir"
    fi
}

pkg_file_list() {
    local pkg="$1"
    local dir

    dir=$(pkg_build_dir "$pkg")
    [ -n "$dir" ] || die "$pkg has no build directory under buildroot/output/build"
    require_file "$dir/.files-list.txt"
    printf '%s\n' "$dir/.files-list.txt"
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
    note "run './buildscripts/config.sh reset buildroot' to enable it in buildroot/.config"
    note "then run './buildscripts/pkg_redeploy.sh $pkg' to build and incrementally deploy it"
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

current_pkg_files() {
    local pkg="$1"
    local list rel owner

    list=$(pkg_file_list "$pkg")
    while IFS=, read -r owner rel; do
        [ "$owner" = "$pkg" ] || continue
        [ -n "$rel" ] || continue
        rel=${rel#./}
        printf '%s\n' "$rel"
    done < "$list" | sort -u
}

current_pkg_files_if_available() {
    local pkg="$1"
    local dir list rel owner

    [ -f "$BSP_DIR/package/$pkg/$pkg.mk" ] || return 0
    if [ -f "$BUILDROOT_DIR/.config" ] && ! pkg_enabled "$pkg"; then
        return 0
    fi

    dir=$(pkg_build_dir "$pkg")
    [ -n "$dir" ] || return 0
    list="$dir/.files-list.txt"
    [ -f "$list" ] || return 0

    while IFS=, read -r owner rel; do
        [ "$owner" = "$pkg" ] || continue
        [ -n "$rel" ] || continue
        rel=${rel#./}
        printf '%s\n' "$rel"
    done < "$list" | sort -u
}

pkg_deploy_manifest() {
    local pkg="$1"

    printf '%s/%s.files\n' "$DEPLOY_MANIFEST_DIR" "$pkg"
}

write_pkg_deploy_manifest_from_file() {
    local pkg="$1"
    local files="$2"
    local manifest tmp rel

    manifest=$(pkg_deploy_manifest "$pkg")
    tmp=$(mktemp)
    mkdir -p "$DEPLOY_MANIFEST_DIR"

    {
        printf '# type=package-deploy-files\n'
        printf '# pkg=%s\n' "$pkg"
        printf '# time_utc=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
        while IFS= read -r rel; do
            [ -n "$rel" ] && printf '%s\n' "$rel"
        done < "$files"
    } > "$tmp"

    mv "$tmp" "$manifest"
    ok "wrote deploy manifest $manifest"
}

write_pkg_deploy_manifest() {
    local pkg="$1"
    local files

    files=$(mktemp)
    current_pkg_files "$pkg" > "$files"
    write_pkg_deploy_manifest_from_file "$pkg" "$files"
    rm -f "$files"
}

read_pkg_deploy_manifest_files() {
    local manifest="$1"

    sed '/^[[:space:]]*#/d;/^[[:space:]]*$/d' "$manifest" | sort -u
}

deploy_pkg_to_nfs() {
    local pkg="$1"
    local rel src dst count

    guard_nfs_dir
    sudo mkdir -p "$NFS_DIR"

    count=0
    while IFS= read -r rel; do
        [ -n "$rel" ] || continue
        src="$BUILDROOT_DIR/output/target/$rel"
        dst="$NFS_DIR/$rel"

        require_file "$src"
        sudo mkdir -p "$(dirname "$dst")"
        sudo cp -a "$src" "$dst"
        count=$((count + 1))
        echo "  $rel"
    done < <(current_pkg_files "$pkg")

    [ "$count" -gt 0 ] || die "$pkg file list is empty"
    write_pkg_deploy_manifest "$pkg"
    ok "incrementally deployed $count files for $pkg to $NFS_DIR"
}

clean_pkg_stale_from_nfs() {
    local pkg="$1"
    local manifest old current stale rel dst removed

    manifest=$(pkg_deploy_manifest "$pkg")
    require_file "$manifest"
    guard_nfs_dir

    old=$(mktemp)
    current=$(mktemp)
    stale=$(mktemp)
    read_pkg_deploy_manifest_files "$manifest" > "$old"
    current_pkg_files_if_available "$pkg" > "$current"
    comm -23 "$old" "$current" > "$stale"

    removed=0
    while IFS= read -r rel; do
        [ -n "$rel" ] || continue
        dst="$NFS_DIR/$rel"
        case "$dst" in
            "$NFS_DIR"/*) ;;
            *) die "refusing to remove path outside NFS_DIR: $dst" ;;
        esac
        if [ -e "$dst" ] || [ -L "$dst" ]; then
            sudo rm -f -- "$dst"
            removed=$((removed + 1))
            echo "  removed $rel"
        fi
    done < "$stale"

    rm -f "$old" "$current" "$stale"
    current=$(mktemp)
    current_pkg_files_if_available "$pkg" > "$current"
    write_pkg_deploy_manifest_from_file "$pkg" "$current"
    rm -f "$current"
    ok "removed $removed stale files for $pkg from $NFS_DIR"
}

rebuild_and_deploy_pkg() {
    local pkg="$1"

    [ -n "$pkg" ] || die "package name is required"
    validate_pkg_name "$pkg"
    ensure_config
    require_kernel_toolchain
    pkg_mk_path "$pkg" >/dev/null
    pkg_enabled "$pkg" || die "$pkg is not enabled in buildroot/.config"

    note "rebuilding package: $pkg"
    br_make "${pkg}-dirclean"
    br_make "$pkg" -j"$MAKE_JOBS"
    deploy_pkg_to_nfs "$pkg"
}
