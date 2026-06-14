# IMX6 Qt5 Monitor HMI

本目录现在是 i.MX6ULL 触摸屏 HMI，用 Qt Widgets + linuxfb 控制 `imx6-monitor`：

- Qt 启动时先探测 `http://127.0.0.1:8080/api/status`。
- 如果 monitor 未运行，Qt 使用 `QProcess` 启动 `/usr/bin/imx6-monitor`。
- 触摸按钮控制 monitor、摄像头采集、Qt 内嵌 LCD 预览、摄像头网络访问、AP3216C 轮询和 AP3216C 网络访问。
- 本地预览由 Qt 请求本机 `/frame.rgb565` 原始帧后显示在界面中，避免 JPEG 编解码；monitor 默认不直接写 `/dev/fb0`。

| 项 | 内容 |
| --- | --- |
| Buildroot 包 | `bsp/package/imx6-qt5-demo/` |
| 源码目录 | `src/imx6_qt5_demo/` |
| 构建类型 | `qmake-package` |
| 依赖 | `qt5base` widgets/network/linuxfb、`imx6-monitor` |
| 板端程序 | `/usr/bin/imx6-qt5-demo` |
| 用户态工具链 | Buildroot internal `arm-buildroot-linux-gnueabihf-gcc 13.x + glibc` |

## 构建与部署

只改本目录 Qt HMI 源码：

```bash
NFS_DIR=<nfs-dir> bash buildscripts/build_and_deploy.sh drv imx6-qt5-demo
```

如果同时改了 monitor 后端：

```bash
NFS_DIR=<nfs-dir> bash buildscripts/build_and_deploy.sh drv imx6-monitor
NFS_DIR=<nfs-dir> bash buildscripts/build_and_deploy.sh drv imx6-qt5-demo
```

如果刚启用 Qt5、切换 libc、修改字体/tslib/rootfs 配置，建议重新生成并全量部署 rootfs：

```bash
bash buildscripts/build_and_deploy.sh config reset buildroot
NFS_DIR=<nfs-dir> bash buildscripts/build_and_deploy.sh rootfs
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

运行触摸 HMI：

```bash
QT_QPA_PLATFORM=linuxfb imx6-qt5-demo
```

短时 smoke test：

```bash
QT_QPA_PLATFORM=linuxfb imx6-qt5-demo --duration-ms 5000
```

Qt 启动 monitor 的默认参数：

```bash
imx6-monitor -n -s auto -p 8080 -W 800 -H 480 -r 10 -q 75 --camera-off --sensor-off --public-off
```

## 板端验证

```bash
ap3216c_test scan
ls -l /dev/video* /sys/bus/iio/devices/iio:device*/name /dev/fb0
QT_QPA_PLATFORM=linuxfb imx6-qt5-demo --self-test
QT_QPA_PLATFORM=linuxfb imx6-qt5-demo
```

在触摸屏上验证：

- `Monitor` 可启动/停止 monitor。若 Qt 检测到外部已有 monitor，只接管控制，不强杀外部进程。
- `Camera` 打开后 `LCD Preview` 可显示本机 RGB565 预览刷新。
- `Camera Net` 关闭时板外 `/stream.mjpg` 被拒绝，已有 MJPEG 长连接也会断开；打开后可浏览 MJPEG。
- `AP3216C` 打开后 IR、ALS、lux、PS 和状态刷新。
- `Sensor Net` 关闭时板外 `/api/status` 隐藏 sensor 数据，打开后可看到传感器数据。

## 边界

- HMI 使用 Qt Widgets，不依赖 QML/Quick、OpenGL、EGLFS、X11 或 Wayland。
- Qt 通过 HTTP 控制 monitor，不直接访问 V4L2 或 IIO sysfs。
- 本地 LCD 由 Qt 全屏占用；monitor 的 fbdev 预览只保留给手动调试模式。
