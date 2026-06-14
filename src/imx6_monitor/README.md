# IMX6 Monitor 监控服务

`imx6-monitor` 是 Qt 触摸屏 HMI 背后的轻量监控后端：

- 通过 Linux V4L2 streaming API 采集 OV5640 图像。
- 可选把最新 RGB565 帧复制到 `/dev/fb0`；Qt HMI 模式默认使用 `-n`，避免和 Qt 同时写 framebuffer。
- 把最新帧编码为 JPEG，并通过 `/snapshot.jpg` 和 `/stream.mjpg` 暴露；本机 Qt 预览可走 `/frame.rgb565` 原始帧快路径。
- 通过 AP3216C IIO sysfs 读取 IR、ALS、ALS lux 和 PS 数据。
- 通过本机 HTTP 控制接口让 Qt 打开/关闭摄像头、AP3216C 轮询和板外访问。

## Buildroot 集成

| 项 | 内容 |
| --- | --- |
| Buildroot 包 | `bsp/package/imx6-monitor/` |
| 源码目录 | `src/imx6_monitor/` |
| 构建类型 | `generic-package` |
| 用户态工具链 | Buildroot target toolchain |
| 板端程序 | `/usr/bin/imx6-monitor` |
| init 脚本 | 默认安装为 `/etc/init.d/disabled-S90imx6-monitor` |

当前包默认不自启动。Qt HMI 会按需启动 `/usr/bin/imx6-monitor`；需要传统开机启动时，再把 NFS rootfs 中的 `disabled-S90imx6-monitor` 改名为 `S90imx6-monitor`。

## 构建与部署

只改本目录 C 源码或 init 脚本时：

```bash
NFS_DIR=<nfs-dir> bash buildscripts/build_and_deploy.sh drv imx6-monitor
```

首次启用或改了 Buildroot 配置后：

```bash
bash buildscripts/build_and_deploy.sh config reset buildroot
NFS_DIR=<nfs-dir> bash buildscripts/build_and_deploy.sh rootfs
```

如果删除或重命名了安装文件，例如 init 脚本名字变化，再显式清理 NFS 旧文件：

```bash
NFS_DIR=<nfs-dir> bash buildscripts/pkg_clean_stale.sh imx6-monitor
```

## HMI 模式

Qt HMI 启动 monitor 的默认参数：

```bash
imx6-monitor -n -s auto -p 8080 -W 800 -H 480 -r 10 -q 75 --camera-off --sensor-off --public-off
```

默认状态下摄像头采集、AP3216C 轮询、摄像头网络访问和 AP3216C 网络访问全部关闭，由触摸屏按钮逐项打开。

## HTTP 接口

本机与板外都可访问：

```text
http://<board-ip>:8080/
http://<board-ip>:8080/api/status
http://<board-ip>:8080/snapshot.jpg
http://<board-ip>:8080/stream.mjpg
```

仅本机可访问 Qt 预览快路径：

```text
http://127.0.0.1:8080/frame.rgb565
```

`/api/status` 对 `127.0.0.1` 返回完整状态和控制位；板外访问会按 public 开关隐藏 camera 或 sensor 数据。

仅 `127.0.0.1` 可访问控制接口：

```text
/api/control?camera=on|off
/api/control?sensor=on|off
/api/control?camera_public=on|off
/api/control?sensor_public=on|off
```

可以一次设置多个参数，例如：

```bash
wget -O - 'http://127.0.0.1:8080/api/control?camera=on&sensor=on'
```

## AP3216C IIO

`-s` 参数默认是 `auto`，会在 `/sys/bus/iio/devices/iio:deviceX/name` 中查找 `ap3216c`。也可以显式传入：

```text
auto
ap3216c
iio:deviceX
N
/sys/bus/iio/devices/iio:deviceX
```

monitor 读取以下属性，并兼容带 `0` 和不带 `0` 的命名：

```text
in_intensity_ir_raw
in_illuminance_raw
in_illuminance_input
in_proximity_raw
```

关闭 AP3216C 时只停止 monitor 轮询，不卸载模块，也不强行切换 `operating_mode`。

## 板端验证

```bash
ap3216c_test scan
ls -l /dev/video* /sys/bus/iio/devices/iio:device*/name /dev/fb0
imx6-monitor -n -s auto -p 8080 -W 800 -H 480 -r 10 -q 75 --camera-off --sensor-off --public-off &
wget -O - http://127.0.0.1:8080/api/status
wget -O - 'http://127.0.0.1:8080/api/control?camera=on&sensor=on'
wget -O /tmp/snapshot.jpg http://127.0.0.1:8080/snapshot.jpg
```

板外访问验证：public 关闭时 `/stream.mjpg` 应返回拒绝，已有 MJPEG 长连接会断开；打开 `camera_public=on` 后浏览器可看 MJPEG；打开 `sensor_public=on` 后 `/api/status` 可看到 AP3216C 数据。

## 边界

- 摄像头关闭时释放 V4L2 设备和 mmap buffer；打开失败只计数和重试，不让 monitor 退出。
- LCD 预览仍保留 fbdev 路径，但 Qt HMI 模式默认不用它。
- JPEG 编码走 Buildroot 的 `jpeg` virtual package，当前 provider 是 `jpeg-turbo`。
- `pkg_redeploy.sh imx6-monitor` 只增量复制该包文件，不生成 `rootfs.tar`，也不清空 NFS rootfs。
