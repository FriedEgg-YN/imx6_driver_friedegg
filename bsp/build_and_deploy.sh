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