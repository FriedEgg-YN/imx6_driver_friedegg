# IMX6 Monitor 监控服务

`imx6-monitor` 把当前 AP3216C 与 OV5640 驱动工作串成一个轻量监控节点：

- 通过 Linux V4L2 streaming API 采集 OV5640 图像。
- 可选把最新 RGB565 帧复制到 `/dev/fb0`，用于本地 LCD 预览。
- 把最新帧编码为 JPEG，并通过 `/snapshot.jpg` 和 `/stream.mjpg` 暴露。
- 通过 `/api/status` 暴露 AP3216C 传感器状态。
- 提供一个小型 C socket HTTP 服务，避免引入重型 Web 框架。

MJPEG over HTTP 是当前第一阶段目标：Windows 浏览器能直接打开，`curl`/`wget` 容易调试，也不要求 i.MX6ULL 先承担 RTSP/WebRTC 或硬编码链路。后续如果要做 RTSP/H.264，应作为独立多媒体栈实验推进。

## Buildroot 集成

| 项 | 内容 |
| --- | --- |
| Buildroot 包 | `bsp/package/imx6-monitor/` |
| 源码目录 | `src/imx6_monitor/` |
| 构建类型 | `generic-package` |
| 用户态工具链 | Buildroot target toolchain，即 `TARGET_CC/TARGET_CFLAGS/TARGET_LDFLAGS` |
| 板端程序 | `/usr/bin/imx6-monitor` |
| init 脚本 | 默认安装为 `/etc/init.d/disabled-S90imx6-monitor` |

当前包默认不自启动。需要开机启动时，把 NFS rootfs 中的 `disabled-S90imx6-monitor` 改名为 `S90imx6-monitor`，或在包安装规则中调整目标文件名后重新部署 rootfs。

## 构建与部署

首次启用或改了 Buildroot 配置后：

```bash
bash buildscripts/build_and_deploy.sh config reset buildroot
NFS_DIR=<nfs-dir> bash buildscripts/build_and_deploy.sh rootfs
```

只改本目录 C 源码或 init 脚本时：

```bash
NFS_DIR=<nfs-dir> bash buildscripts/build_and_deploy.sh drv imx6-monitor
```

如果删除或重命名了安装文件，例如 init 脚本名字变化，再显式清理 NFS 旧文件：

```bash
NFS_DIR=<nfs-dir> bash buildscripts/pkg_clean_stale.sh imx6-monitor
```

## 手动运行

```bash
imx6-monitor -d /dev/video0 -f /dev/fb0 -s /dev/ap3216c -p 8080 -W 320 -H 240 -r 10
```

常用访问地址：

```text
http://<board-ip>:8080/
http://<board-ip>:8080/snapshot.jpg
http://<board-ip>:8080/stream.mjpg
http://<board-ip>:8080/api/status
```

## 板端验证

```bash
dmesg | grep -i -E 'ov5640|csi|ap3216c|video'
ls -l /dev/video* /dev/fb0 /dev/ap3216c
cat /proc/modules | grep -E 'ov5640|mx6s_capture|ap3216c'
wget -O - http://127.0.0.1:8080/api/status
wget -O /tmp/snapshot.jpg http://127.0.0.1:8080/snapshot.jpg
```

稳定性验证建议让服务至少运行 30 分钟，记录 CPU 占用、目标帧率、浏览器重连行为，以及 LCD 预览是否保持平滑。

## 边界

- 本服务直接使用 V4L2 MMAP，核心路径会经过 `VIDIOC_QUERYCAP`、`VIDIOC_S_FMT`、`VIDIOC_REQBUFS`、`VIDIOC_QBUF`、`VIDIOC_DQBUF` 和 `VIDIOC_STREAMON`。
- LCD 预览当前继续使用 fbdev，因为它和现有 BSP 验证路径一致。
- JPEG 编码走 Buildroot 的 `jpeg` virtual package，当前 provider 是 `jpeg-turbo`。
- `pkg_redeploy.sh imx6-monitor` 只增量复制该包文件，不生成 `rootfs.tar`，也不清空 NFS rootfs。
