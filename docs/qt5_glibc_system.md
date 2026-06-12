# i.MX6ULL glibc + Qt5 系统画像

## 主线配置

当前主线使用 Buildroot internal toolchain 生成 rootfs、用户态程序和 Qt5 应用：

- C library：`BR2_TOOLCHAIN_BUILDROOT_GLIBC=y`
- GCC：`BR2_GCC_VERSION_13_X=y`
- C++：`BR2_TOOLCHAIN_BUILDROOT_CXX=y`
- Kernel headers：`BR2_KERNEL_HEADERS_AS_KERNEL=y`，series 固定为 `BR2_PACKAGE_HOST_LINUX_HEADERS_CUSTOM_4_1=y`
- Qt：`BR2_PACKAGE_QT5=y`，启用 `Qt5Base` GUI、Widgets、linuxfb、fontconfig、JPEG、PNG、tslib
- 默认 QPA：`BR2_PACKAGE_QT5BASE_DEFAULT_QPA="linuxfb"`

第一阶段 GUI 只走 Qt Widgets + linuxfb，不启用 Qt Quick/QML、OpenGL、EGLFS、X11 或 Wayland。这样可以先把 rootfs ABI、字体、触摸和 framebuffer 路径跑稳，再决定是否另开性能实验分支评估 QML/Quick。

## ABI 边界

本工程保留双工具链边界：

| 范围 | 工具链 | 说明 |
| --- | --- | --- |
| Linux 4.1.15、DTB、外置 `.ko` | `arm-linux-gnueabihf-gcc 4.9.4` | 由 `bsp/toolchain.mk` 覆盖 `LINUX_MAKE_FLAGS`，保证 Buildroot 的 kernel 和 `kernel-module` 包仍使用旧内核验证过的 Linaro 工具链 |
| rootfs、driver test、`imx6-monitor`、Qt5 应用 | `arm-buildroot-linux-gnueabihf-gcc 13.x + glibc` | 由 Buildroot internal toolchain 生成，不混用 Poky Qt5 SDK 或 Linaro glibc 用户态程序 |

切换到 glibc rootfs 后，之前针对 musl rootfs 构建出的用户态二进制不能直接视为可复用产物。需要重新构建 rootfs 和所有安装进 rootfs 的用户态程序，并用 `file` 或目标板运行结果确认动态链接器已经变为 glibc hard-float 解释器，通常是 `/lib/ld-linux-armhf.so.3`。

## Qt5 Demo 包

本地 Qt5 demo 由以下目录组成：

- `bsp/package/imx6-qt5-demo/`：Buildroot external package，使用 `qmake-package`，依赖 `qt5base`
- `src/imx6_qt5_demo/`：Qt Widgets 示例程序，安装为 `/usr/bin/imx6-qt5-demo`

运行方式：

```sh
QT_QPA_PLATFORM=linuxfb imx6-qt5-demo --self-test
QT_QPA_PLATFORM=linuxfb imx6-qt5-demo --duration-ms 5000
```

`--self-test` 只打印 Qt 版本并退出，用于确认程序和 Qt runtime 能被正确加载；`--duration-ms` 用于板端短时全屏 smoke test。

## 干净构建流程

libc 从 musl 切到 glibc 后必须做干净构建，不能依赖旧 toolchain/rootfs 输出目录：

```sh
bash bsp/build_and_deploy.sh config reset buildroot
make -C buildroot BR2_EXTERNAL=../bsp BR2_PACKAGE_OVERRIDE_FILE=../bsp/local.mk clean
bash bsp/build_and_deploy.sh rootfs
```

内核和驱动仍按最小范围验证：

```sh
bash bsp/build_and_deploy.sh zimage
bash bsp/build_and_deploy.sh dtb
bash bsp/build_and_deploy.sh drv ap3216c
bash bsp/build_and_deploy.sh drv ov5640_drv
```

完整交付前执行：

```sh
bash bsp/build_and_deploy.sh all
```

## 主机侧验证

```sh
buildroot/output/host/bin/arm-buildroot-linux-gnueabihf-gcc --version
file buildroot/output/target/usr/bin/ap3216c_test \
     buildroot/output/target/usr/bin/ov5640_test \
     buildroot/output/target/usr/bin/imx6-monitor \
     buildroot/output/target/usr/bin/imx6-qt5-demo
buildroot/output/host/sbin/modinfo buildroot/output/build/ap3216c-1.0/ap3216c.ko
buildroot/output/host/sbin/modinfo buildroot/output/build/ov5640_drv-1.0/ov5640.ko
buildroot/output/host/sbin/modinfo buildroot/output/build/ov5640_drv-1.0/mx6s_capture.ko
```

期望结果：

- target compiler 显示 GCC 13.x。
- 用户态程序显示 ARM hard-float glibc interpreter，通常是 `/lib/ld-linux-armhf.so.3`。
- `.ko` 的 vermagic 与 `4.1.15` 内核匹配。

Buildroot/NFS 输出验证：

```sh
bash bsp/build_and_deploy.sh verify zimage
bash bsp/build_and_deploy.sh verify dtb
bash bsp/build_and_deploy.sh verify ko ap3216c
bash bsp/build_and_deploy.sh verify ko ov5640_drv
bash bsp/build_and_deploy.sh verify nfs-pkg ap3216c
bash bsp/build_and_deploy.sh verify nfs-pkg ov5640_drv
```

## 板端验证

```sh
uname -r
modprobe ap3216c
modprobe ov5640
modprobe mx6s_capture
ap3216c_test
ov5640_test
imx6-monitor
QT_QPA_PLATFORM=linuxfb imx6-qt5-demo --self-test
QT_QPA_PLATFORM=linuxfb imx6-qt5-demo --duration-ms 5000
```

期望 `uname -r` 为 `4.1.15`。Qt demo 应能在 framebuffer 上全屏显示，`--duration-ms 5000` 约 5 秒后退出。

## 版本固定建议

当前 `buildroot/` 是 git 源码树，Qt、glibc、gcc 和包符号会随源码版本漂移。建议后续在项目文档或子模块管理策略中固定 Buildroot release/tag/commit，并在每次升级时单独验证 Qt5、glibc、kernel-module 和 rootfs ABI。
