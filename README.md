# i.MX6ULL BSP 与驱动学习工程

本仓库是一个面向 i.MX6ULL 开发板的 embedded Linux 工程，覆盖 U-Boot、Linux 4.1.15、Buildroot rootfs、本地驱动包、用户态测试程序和轻量 GUI/监控示例。当前主线重点是把 BSP 集成、驱动开发和板端验证做成可复现的学习闭环。

## 目录边界

| 路径 | 角色 |
| --- | --- |
| `buildscripts/` | 编译部署入口脚本，负责调用 Buildroot、部署 TFTP/NFS、管理固化配置 |
| `bsp/` | Buildroot external，保存 defconfig、BusyBox/Linux 固化配置、local package、rootfs overlay 和工具链覆盖 |
| `src/<pkg>/` | 本地包源码；包级说明放在对应目录的 `README.md` |
| `src/linux-friedegg/` | Linux 4.1.15 移植源码和 DTS，大源码树，默认只读，任务明确需要时才改 |
| `src/uboot-friedegg/` | U-Boot 移植源码，大源码树，默认只读 |
| `buildroot/` | Buildroot 构建系统和输出目录，大源码树/子工程，默认只读 |
| `docs/` | 项目级、跨模块说明，例如编译命令、Git 工作流、AI 协作规则 |
| `.notrace/` | 本地草稿和未整理笔记，默认不作为项目公开文档来源 |

真实的 NFS/TFTP 路径、板卡 IP、用户名和主机目录不写入项目文件；文档统一使用 `<nfs-dir>`、`<tftp-dir>`、`<board-ip>`、`<workspace>` 这类占位符。

## 文档职责

本仓库文档按“门面索引 + 细节说明”分层：

- `README.md` 是项目级入口，只放项目目标、系统画像、目录边界、构建入口、本地包索引和项目级 docs 索引。
- `docs/` 放项目级或跨模块说明；包级调试过程、运行命令和模块细节不放在这里。
- `src/<pkg>/README.md` 是模块入口，只放模块职责、当前能力、构建部署、板端验证和模块 docs 索引。
- `src/<pkg>/docs/` 放模块细节、数据流、调试复盘、验证记录和学习笔记。
- `.notrace/` 放个人草稿或不希望提交的材料；除非明确要求，整理文档时不修改这里。

## 当前系统画像

当前 rootfs 主线使用 Buildroot internal toolchain 生成用户态系统：

- C library：`BR2_TOOLCHAIN_BUILDROOT_GLIBC=y`
- GCC：`BR2_GCC_VERSION_13_X=y`
- C++：`BR2_TOOLCHAIN_BUILDROOT_CXX=y`
- Kernel headers：跟随内核 4.1 系列
- `/dev` 管理：`BR2_ROOTFS_DEVICE_CREATION_DYNAMIC_MDEV=y`
- Qt：Qt5Base GUI + Widgets + linuxfb + fontconfig + JPEG + PNG + tslib
- 默认 QPA：`BR2_PACKAGE_QT5BASE_DEFAULT_QPA="linuxfb"`

第一阶段 GUI 只验证 Qt Widgets + linuxfb，不启用 Qt Quick/QML、OpenGL、EGLFS、X11 或 Wayland。这样可以先把 glibc rootfs、字体、触摸和 framebuffer 路径跑稳，再决定是否单独评估更重的图形栈。

## 工具链与 ABI 边界

本工程保留双工具链边界，避免把老内核和新 rootfs 的 ABI 搅在一起：

| 范围 | 工具链 | 说明 |
| --- | --- | --- |
| Linux 4.1.15、DTB、外置 `.ko` | Linaro `arm-linux-gnueabihf-gcc 4.9.4` | 由 `bsp/toolchain.mk` 通过 `BSP_KERNEL_CROSS_COMPILE` 覆盖 Buildroot 的 kernel make flags |
| rootfs、driver test、`imx6-monitor`、Qt5 应用 | Buildroot internal `arm-buildroot-linux-gnueabihf-gcc 13.x + glibc` | 由 Buildroot 生成，安装进 rootfs 的用户态程序都走这条链路 |

切换到 glibc rootfs 后，旧 musl rootfs 下构建出的用户态二进制不能直接复用。需要重新构建 rootfs 和所有安装进 rootfs 的用户态程序，并用 `file` 或板端运行结果确认解释器变为 glibc hard-float 路径，通常是 `/lib/ld-linux-armhf.so.3`。

## 常用构建入口

兼容总入口：

```bash
cd <workspace>
NFS_DIR=<nfs-dir> TFTP_DIR=<tftp-dir> bash buildscripts/build_and_deploy.sh dtb
NFS_DIR=<nfs-dir> TFTP_DIR=<tftp-dir> bash buildscripts/build_and_deploy.sh zimage
NFS_DIR=<nfs-dir> TFTP_DIR=<tftp-dir> bash buildscripts/build_and_deploy.sh rootfs
NFS_DIR=<nfs-dir> TFTP_DIR=<tftp-dir> bash buildscripts/build_and_deploy.sh drv <pkg>
NFS_DIR=<nfs-dir> TFTP_DIR=<tftp-dir> bash buildscripts/build_and_deploy.sh all
bash buildscripts/build_and_deploy.sh config status
```

也可以直接运行拆分后的脚本：

```bash
bash buildscripts/dtb.sh
bash buildscripts/linux_rebuild.sh
bash buildscripts/rootfs_redeploy.sh
bash buildscripts/pkg_redeploy.sh <pkg>
bash buildscripts/pkg_clean_stale.sh <pkg>
bash buildscripts/config.sh status
```

旧的 `verify` 子命令已删除；验证方式改为按改动范围选择最窄构建命令，再到板端运行对应包内 README 里的测试命令。脚本边界、Buildroot 中间目录和残留清理规则见 [docs/编译命令总结.md](docs/编译命令总结.md)。

## 本地包一览

| Buildroot 包名 | 源码目录 | 主要产物 | 包内说明 |
| --- | --- | --- | --- |
| `ap3216c` | `src/ap3216c/` | `ap3216c.ko`、`/usr/bin/ap3216c_test` | [src/ap3216c/README.md](src/ap3216c/README.md) |
| `ov5640` | `src/ov5640/` | `ov5640.ko`、`mx6s_capture.ko`、`/usr/bin/ov5640_test`、`/usr/bin/ov5640_interface_demo` | [src/ov5640/README.md](src/ov5640/README.md) |
| `gt9147` | `src/gt9147/` | `gt9147.ko` | [src/gt9147/README.md](src/gt9147/README.md) |
| `imx6-monitor` | `src/imx6_monitor/` | `/usr/bin/imx6-monitor`、禁用态 init 脚本 | [src/imx6_monitor/README.md](src/imx6_monitor/README.md) |
| `imx6-qt5-demo` | `src/imx6_qt5_demo/` | `/usr/bin/imx6-qt5-demo` | [src/imx6_qt5_demo/README.md](src/imx6_qt5_demo/README.md) |
| `imx6-smart-monitor` | `src/imx6_smart_monitor/` | `/usr/bin/imx6-smart-monitor`、`/usr/bin/imx6-sm-*-test` | [src/imx6_smart_monitor/README.md](src/imx6_smart_monitor/README.md) |
| `print_chasing_led` | `src/print_chasing_LED/` | `print_chasing_LED.ko`、`/usr/bin/print_chasing_LED_test` | 暂无包内 README |

包源码通过 `bsp/local.mk` 和各自 `bsp/package/<pkg>/<pkg>.mk` 接入 Buildroot。Buildroot 会把 `src/<pkg>` rsync 到 `buildroot/output/build/<pkg>-...`，再根据 `kernel-module`、`generic-package` 或 `qmake-package` 规则编译并安装到 `buildroot/output/target`。

## 配置固化

固化配置集中在 `bsp/configs/`：

- `imx6ull_friedegg_emmc_defconfig`：Buildroot 主配置
- `busybox.config`：BusyBox 配置
- `linux.config`：Linux 内核配置

常用命令：

```bash
bash buildscripts/config.sh status
bash buildscripts/config.sh save buildroot
bash buildscripts/config.sh save busybox
bash buildscripts/config.sh save linux
bash buildscripts/config.sh reset all
```

如果改了 libc、Qt、工具链、Buildroot package 集合或 rootfs overlay，优先用 `rootfs` 或 `all` 做干净验证；如果只改某个本地包源码，优先用 `drv <pkg>`。

## 文档入口

- [docs/编译命令总结.md](docs/编译命令总结.md)：脚本选择、Buildroot 中间目录、NFS/TFTP 部署和残留清理规则。
- [docs/GIT_WORKFLOW.md](docs/GIT_WORKFLOW.md)：主仓与大源码树边界、提交信息约定。
- [docs/codex-ai-components-guide.md](docs/codex-ai-components-guide.md)：VSCode + Codex 协作组件说明。
- [docs/alias与mdev自动加载ko.md](docs/alias与mdev自动加载ko.md)：modalias、mdev 和内核模块自动加载机制说明。

包级调试记录、运行命令和板端验证步骤不再放在 `docs/`，而是放在对应 `src/<pkg>/README.md` 中。
