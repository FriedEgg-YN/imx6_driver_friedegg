#!/bin/bash

# ========================================================
# Buildroot 综合一键编译与部署脚本 (Rootfs + Kernel + DTB)
# ========================================================
# 
# 【使用说明】
# 本脚本旨在实现嵌入式 Linux 系统的一键化交叉编译与网络部署。
# 
# 1. 编译前置要求：
#    - 确保 Buildroot 的 menuconfig 中已经开启 Kernel 编译 (BR2_LINUX_KERNEL=y)
#    - 确保 Kernel 的配置中指定了正确的设备树名称，避免编译冗余 DTB。
#
# 2. 脚本运行方式：
#    - ./build_and_deploy.sh            # 默认增量编译 (仅重编改动过的模块，速度最快)
#    - ./build_and_deploy.sh all        # 全量重编 (执行 make clean 后完全重新编译)
#    - ./build_and_deploy.sh help       # 打印此帮助信息
#
# 3. 部署动作说明：
#    - rootfs.tar 会被使用 sudo 权限解压到设定的 NFS 目录中（保留设备节点权限）
#    - 指定的 Kernel 和 DTB 会被复制到设定的 TFTP 目录中
# ========================================================

# ----------------- [ 配置区：请根据实际情况修改 ] -----------------
# 目标文件名配置（按需精准指定，多余的 DTB 就算生成了也不会被拷贝）
KERNEL_IMAGE="zImage"                                   # 内核文件名，一般为 zImage 或 Image
TARGET_DTB="imx6ull-friedegg-emmc.dtb"                  # 你的目标设备树文件名（请换成你实际的板子dtb名称）

# 自动定位项目根路径配置
BSP_DIR=$(cd "$(dirname "$0")" && pwd)
WORKSPACE_DIR=$(dirname "$BSP_DIR")
BUILDROOT_DIR="$WORKSPACE_DIR/buildroot"

# 网络服务目录配置
NFS_DIR="$HOME/linux/nfs/rootfs"                        # NFS 根文件系统挂载目录
TFTP_DIR="$HOME/linux/tftp"                         # TFTP 服务目录 (按你实际环境调整，如 ~/linux/tftp)

# 编译产物路径
OUTPUT_IMAGE_DIR="$BUILDROOT_DIR/output/images"
ROOTFS_FILE="$OUTPUT_IMAGE_DIR/rootfs.tar"
KERNEL_FILE="$OUTPUT_IMAGE_DIR/$KERNEL_IMAGE"
DTB_FILE="$OUTPUT_IMAGE_DIR/$TARGET_DTB"

# 外部 local 包增量重编检测（通用机制）
# 原理说明：
# 1) Buildroot 增量构建主要由包级 stamp 驱动；外部源码目录变更时，包不一定自动重编。
# 2) 这里自动扫描 BSP external 中启用且 SITE_METHOD=local 的包，比较“源码最新时间”与“包安装 stamp 时间”。
# 3) 若源码更新，则对该包执行 <pkg>-dirclean，使后续 make 重新 rsync/build/install。
# 4) 这样无需为每个外部源码包单独写 if 判断，新增 local 包也可复用同一套流程。
# ------------------------------------------------------------------

# 解析运行参数
MODE="inc"
if [ "$1" == "-a" ] || [ "$1" == "all" ]; then
    MODE="all"
elif [ "$1" == "-h" ] || [ "$1" == "help" ]; then
    grep -E '^#.*' "$0" | head -n 16 | sed 's/^#//g'
    exit 0
fi

echo "========================================"
echo " [1/3] 开始构建基础组件 (模式: $MODE)"
echo "========================================"
cd "$BUILDROOT_DIR" || exit 1

if [ "$MODE" == "all" ]; then
    echo ">>> 检测到全量编译，执行清理 (make clean)..."
    make clean
fi

if [ "$MODE" == "inc" ] && [ -f "$BUILDROOT_DIR/.config" ]; then
    echo ">>> 检测 external local 包是否需要重编..."
    for MK_FILE in "$BSP_DIR"/package/*/*.mk; do
        [ -f "$MK_FILE" ] || continue

        PKG_NAME=$(basename "$(dirname "$MK_FILE")")
        PKG_SYMBOL=$(echo "$PKG_NAME" | tr '[:lower:]-' '[:upper:]_')

        if ! grep -q "^BR2_PACKAGE_${PKG_SYMBOL}=y" "$BUILDROOT_DIR/.config"; then
            continue
        fi

        PKG_VAR=$(sed -n 's/^\([A-Za-z0-9_][A-Za-z0-9_]*\)_SITE_METHOD[[:space:]]*=[[:space:]]*local[[:space:]]*$/\1/p' "$MK_FILE" | head -n1)
        [ -n "$PKG_VAR" ] || continue

        SITE_RAW=$(sed -n "s/^${PKG_VAR}_SITE[[:space:]]*=[[:space:]]*//p" "$MK_FILE" | head -n1)
        SITE_RAW=${SITE_RAW%%#*}
        SITE_RAW=$(echo "$SITE_RAW" | xargs)
        [ -n "$SITE_RAW" ] || continue

        SITE_PATH=${SITE_RAW//\$\(TOPDIR\)/$BUILDROOT_DIR}
        if echo "$SITE_PATH" | grep -q '\$('; then
            echo ">>> 跳过 $PKG_NAME: SITE 路径包含未解析变量 ($SITE_RAW)"
            continue
        fi

        if [ "${SITE_PATH#/}" = "$SITE_PATH" ]; then
            SITE_PATH="$BUILDROOT_DIR/$SITE_PATH"
        fi

        if [ ! -d "$SITE_PATH" ]; then
            echo ">>> 跳过 $PKG_NAME: 未找到源码目录 $SITE_PATH"
            continue
        fi

        SRC_TS=$(find "$SITE_PATH" -type f -not -path '*/.git/*' -print0 2>/dev/null | xargs -0 -r stat -c '%Y' | sort -nr | head -n1)
        SRC_TS=${SRC_TS:-0}

        PKG_BUILD_DIR=$(find "$BUILDROOT_DIR/output/build" -maxdepth 1 -type d -name "$PKG_NAME-*" | head -n1)
        STAMP_TS=0
        if [ -n "$PKG_BUILD_DIR" ]; then
            if [ -f "$PKG_BUILD_DIR/.stamp_target_installed" ]; then
                STAMP_TS=$(stat -c '%Y' "$PKG_BUILD_DIR/.stamp_target_installed")
            elif [ -f "$PKG_BUILD_DIR/.stamp_built" ]; then
                STAMP_TS=$(stat -c '%Y' "$PKG_BUILD_DIR/.stamp_built")
            fi
        fi

        if [ "$SRC_TS" -gt "$STAMP_TS" ]; then
            echo ">>> 检测到 $PKG_NAME 源码更新，执行 ${PKG_NAME}-dirclean 触发重编..."
            make BR2_EXTERNAL="$BSP_DIR" BR2_PACKAGE_OVERRIDE_FILE="$BSP_DIR/local.mk" ${PKG_NAME}-dirclean
            if [ $? -ne 0 ]; then
                echo "❌ 错误: ${PKG_NAME}-dirclean 失败，请排查日志。"
                exit 1
            fi
        fi
    done
fi

# 新增：检测内核 DTS 变更
# 内核源码位于 src/linux-imx/，不属于 BSP external 包管理，
# Buildroot 增量编译不会自动感知 .dts 文件变更，需手动触发 linux-rebuild。
if [ "$MODE" == "inc" ] && [ -d "$WORKSPACE_DIR/src/linux-imx" ]; then
    DTS_FILE="$WORKSPACE_DIR/src/linux-imx/arch/arm/boot/dts/${TARGET_DTB%.dtb}.dts"
    DTSI_FILE="$WORKSPACE_DIR/src/linux-imx/arch/arm/boot/dts/${TARGET_DTB%.dtb}.dtsi"
    DTS_TS=0
    [ -f "$DTS_FILE" ] && DTS_TS=$(stat -c '%Y' "$DTS_FILE")
    # 如果存在同名 .dtsi，取较新的时间戳
    if [ -f "$DTSI_FILE" ]; then
        DTSI_TS=$(stat -c '%Y' "$DTSI_FILE")
        [ "$DTSI_TS" -gt "$DTS_TS" ] && DTS_TS="$DTSI_TS"
    fi

    LINUX_BUILD_DIR=$(find "$BUILDROOT_DIR/output/build" -maxdepth 1 -type d -name "linux-*" | head -n1)
    STAMP_TS=0
    if [ -n "$LINUX_BUILD_DIR" ] && [ -f "$LINUX_BUILD_DIR/.stamp_images_installed" ]; then
        STAMP_TS=$(stat -c '%Y' "$LINUX_BUILD_DIR/.stamp_images_installed")
    elif [ -n "$LINUX_BUILD_DIR" ] && [ -f "$LINUX_BUILD_DIR/.stamp_built" ]; then
        STAMP_TS=$(stat -c '%Y' "$LINUX_BUILD_DIR/.stamp_built")
    fi

    if [ "$DTS_TS" -gt "$STAMP_TS" ]; then
        echo ">>> 检测到 DTS 更新 (${TARGET_DTB%.dtb})，触发 linux-rebuild..."
        make BR2_EXTERNAL="$BSP_DIR" BR2_PACKAGE_OVERRIDE_FILE="$BSP_DIR/local.mk" linux-rebuild
        if [ $? -ne 0 ]; then
            echo "❌ 错误: linux-rebuild 失败，请排查日志。"
            exit 1
        fi
    fi
fi

# 核心编译指令
echo ">>> 开始多线程编译 (外部树: $BSP_DIR)..."
make BR2_EXTERNAL="$BSP_DIR" BR2_PACKAGE_OVERRIDE_FILE="$BSP_DIR/local.mk" -j$(nproc)

if [ $? -ne 0 ]; then
    echo "❌ 错误: Buildroot 编译运行失败，请根据上方日志排查！"
    exit 1
fi

echo -e "\n========================================"
echo " [2/3] 检查构建产物"
echo "========================================"
# 校验 Rootfs
if [ ! -f "$ROOTFS_FILE" ]; then echo "❌ 缺失: $ROOTFS_FILE"; exit 1; fi
# 校验 设定好的内核 
if [ ! -f "$KERNEL_FILE" ]; then echo "❌ 缺失: $KERNEL_FILE (请检查内核是否正确开启编译)"; exit 1; fi
# 校验 设定好的DTB
if [ ! -f "$DTB_FILE" ]; then echo "❌ 缺失: $DTB_FILE (请检查内核配置中是否指定了此DTB)"; exit 1; fi

echo "✅ 核心产物校验通过！涉及文件："
echo "   - $ROOTFS_FILE"
echo "   - $KERNEL_FILE"
echo "   - $DTB_FILE"

echo -e "\n========================================"
echo " [3/3] 开始网络部署 (TFTP & NFS)"
echo "========================================"

# >>>> A. 部署到 TFTP (通常不需要超级权限)
echo ">>> (A) 部署 $KERNEL_IMAGE 和 $TARGET_DTB 到 TFTP 目录: $TFTP_DIR"
mkdir -p "$TFTP_DIR"
cp "$KERNEL_FILE" "$TFTP_DIR/"
cp "$DTB_FILE" "$TFTP_DIR/"

if [ $? -ne 0 ]; then
    echo "❌ TFTP 拷贝失败，请检查目录权限。"
    exit 1
fi

# >>>> B. 部署到 NFS (需要 root 权限保留虚拟文件系统标志)
echo ">>> (B) 部署 rootfs 到 NFS 目录: $NFS_DIR"
echo "⚠️  若提示输入密码，是为赋予 rootfs 最真实的设备节点归属权限"

# 确保目录存在
mkdir -p "$NFS_DIR"
sudo rm -rf "$NFS_DIR"/*
sudo tar -xf "$ROOTFS_FILE" -C "$NFS_DIR"

if [ $? -eq 0 ]; then
    echo -e "\n========================================================"
    echo " 🎉 自动化构建 & 分发部署全部成功！"
    echo " TFTP已更新: $KERNEL_IMAGE, $TARGET_DTB"
    echo " NFS 已更新: 全量 Rootfs"
    echo " 🚀 现在，请给你的 i.MX6 开发板重新上电，起飞吧！"
    echo "========================================================"
else
    echo "❌ NFS 解压失败，请检查环境路径是否正确。"
    exit 1
fi