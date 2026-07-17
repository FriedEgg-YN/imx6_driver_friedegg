# OV5640 Camera Driver

本目录维护 OV5640 camera 外置驱动、i.MX6ULL CSI host 适配和用户态测试程序。当前 Buildroot 包名和源码目录均为 `ov5640`，构建、部署和清理命令都使用这个包名。

## 目录概览

| 项 | 当前内容 |
| --- | --- |
| Buildroot 包 | `bsp/package/ov5640/` |
| 源码目录 | `src/ov5640/` |
| Sensor driver | `ov5640.c`，I2C V4L2 subdev，负责寄存器、电源、格式/帧率、controls、stream。 |
| CSI host driver | `mx6s_capture.c`，V4L2 video node、VB2、CSI DMA、IRQ 完成路径。 |
| 用户态测试 | `ov5640_test`、`ov5640_interface_demo`、`test_v4l2_matrix.sh`。 |
| 主要模块 | `ov5640.ko`、`mx6s_capture.ko`。 |

## 当前能力

- RGB565/YUV/Y8 等非压缩 media-bus 格式协商。
- OV5640 sensor JPEG 输出路径，首批覆盖 `640x480`、`800x480`、`1280x720` 的 15/30fps 稳定组合，其中实际可选帧率以模式表存在的寄存器组合为准。
- i.MX6ULL CSI host 将 JPEG 映射为 `V4L2_PIX_FMT_JPEG`，并以固定 DMA 外壳采集到 V4L2 MMAP buffer。
- 用户态可通过 `v4l2-ctl` 或 Smart Monitor Camera Test 选择 JPEG 格式采集。

## docs 索引

| 文档 | 内容 |
| --- | --- |
| [`docs/ov5640-jpeg-capture-path.md`](docs/ov5640-jpeg-capture-path.md) | 当前 JPEG sensor -> CSI -> V4L2 -> userspace 采集路径和验证计划。 |
| [`docs/ov5640_sensor_driver_guide.md`](docs/ov5640_sensor_driver_guide.md) | Sensor subdev 架构、状态机、回调导读；部分段落保留 JPEG 前的历史描述。 |
| [`docs/ov5640_host_driver_guide.md`](docs/ov5640_host_driver_guide.md) | `mx6s_capture.c` host 驱动架构、VB2/CSI DMA/IRQ 状态机。 |
| [`docs/ov5640_userspace_api速查.md`](docs/ov5640_userspace_api速查.md) | 用户态 V4L2 ioctl、`v4l2-ctl`、测试脚本速查。 |
| [`docs/v4l2-ctl-callback-guide.md`](docs/v4l2-ctl-callback-guide.md) | 从 `v4l2-ctl` 命令追踪到 host/sensor 回调的学习笔记。 |
| [`docs/移植效果分析及功能完善.md`](docs/移植效果分析及功能完善.md) | 早期移植效果、功能缺口和演进计划记录。 |
| `docs/*.pdf` | ATK 模块手册、OV5640 datasheet、i.MX6ULL CSI RM、AF firmware guide 等原始资料。 |

## 构建与部署

只改本目录驱动或测试程序：

```bash
NFS_DIR=<nfs-dir> bash buildscripts/build_and_deploy.sh drv ov5640
```

如果同时改了摄像头相关 DTS/DTSI：

```bash
TFTP_DIR=<tftp-dir> bash buildscripts/build_and_deploy.sh dtb
```

删除或重命名模块、测试程序后，显式清理 NFS 旧文件：

```bash
NFS_DIR=<nfs-dir> bash buildscripts/pkg_clean_stale.sh ov5640
```

## 板端验证入口

```bash
dmesg | grep -i -E 'ov5640|csi|video'
modprobe ov5640
modprobe mx6s_capture
v4l2-ctl -d /dev/video1 --list-formats-ext
v4l2-ctl -d /dev/video1 --set-fmt-video=width=640,height=480,pixelformat=JPEG --stream-mmap --stream-count=30 --stream-to=/tmp/ov5640-vga.mjpg
```

JPEG 文件的实际帧长由 Smart Monitor 等用户态扫描 SOI/EOI 得到；内核第一阶段不在 IRQ 路径读取 OV5640 JPEG length 寄存器。`v4l2-ctl --stream-to` 保存的是固定 DMA 外壳连续流，适合验证出流，不等同于裁剪后的 `.jpg`。
