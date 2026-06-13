# IMX6 Qt5 Demo

本目录是当前 glibc + Qt5 rootfs 的最小板端 smoke test。它使用 Qt Widgets 和 linuxfb，不依赖 QML/Quick、OpenGL、EGLFS、X11 或 Wayland。

| 项 | 内容 |
| --- | --- |
| Buildroot 包 | `bsp/package/imx6-qt5-demo/` |
| 源码目录 | `src/imx6_qt5_demo/` |
| 构建类型 | `qmake-package` |
| 依赖 | `qt5base` |
| 板端程序 | `/usr/bin/imx6-qt5-demo` |
| 用户态工具链 | Buildroot internal `arm-buildroot-linux-gnueabihf-gcc 13.x + glibc` |

## 构建与部署

只改本目录 Qt demo 源码：

```bash
NFS_DIR=<nfs-dir> bash buildscripts/build_and_deploy.sh drv imx6-qt5-demo
```

如果刚启用 Qt5、切换 libc、修改字体/tslib/rootfs 配置，建议重新生成并全量部署 rootfs：

```bash
bash buildscripts/build_and_deploy.sh config reset buildroot
NFS_DIR=<nfs-dir> bash buildscripts/build_and_deploy.sh rootfs
```

如果 libc/toolchain 或 Qt 相关输出目录可能残留，走最干净路径：

```bash
NFS_DIR=<nfs-dir> TFTP_DIR=<tftp-dir> bash buildscripts/build_and_deploy.sh all
```

## 主机侧检查

```bash
buildroot/output/host/bin/arm-buildroot-linux-gnueabihf-g++ --version
file buildroot/output/target/usr/bin/imx6-qt5-demo
```

期望：

- target C++ compiler 显示 GCC 13.x。
- `imx6-qt5-demo` 是 ARM hard-float glibc 用户态程序，动态解释器通常是 `/lib/ld-linux-armhf.so.3`。

## 板端运行

只验证 Qt runtime 能加载：

```bash
QT_QPA_PLATFORM=linuxfb imx6-qt5-demo --self-test
```

短时 framebuffer smoke test：

```bash
QT_QPA_PLATFORM=linuxfb imx6-qt5-demo --duration-ms 5000
```

期望 `--self-test` 打印 Qt 版本并退出；`--duration-ms 5000` 在 framebuffer 上全屏显示约 5 秒后退出。如果提示找不到 platform plugin，先检查 rootfs 中 Qt5Base linuxfb、字体和动态库是否完整安装。

## 边界

- 这个 demo 只证明当前 glibc rootfs、Qt Widgets、linuxfb 和基本 C++ runtime 可用。
- 它不验证 QML、GPU、OpenGL、EGLFS、X11 或 Wayland。
- 它不参与内核 `.ko` ABI 验证；内核模块仍由 Linaro 4.9 工具链构建。
