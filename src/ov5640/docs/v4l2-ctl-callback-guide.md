# v4l2-ctl 与 OV5640 驱动回调实战指南

本文面向当前 `src/ov5640/mx6s_capture.c + src/ov5640/ov5640.c` 实现，目标是把 `v4l2-ctl` 常用命令、V4L2/VB2/subdev 回调和板端验证方法串起来。

重点不是背命令，而是建立这条路径：

```text
v4l2-ctl
  -> open("/dev/videoX")
  -> ioctl(fd, VIDIOC_*, ...)
  -> mx6s_capture.c: v4l2_file_operations / v4l2_ioctl_ops
  -> VB2 buffer queue 或 v4l2_subdev_call(...)
  -> ov5640.c: v4l2_subdev_ops / v4l2_ctrl_ops
  -> CSI host DMA、OV5640 寄存器、runtime PM 和 buffer 状态机
```

## 源码入口

| 主题 | 当前源码 |
| --- | --- |
| video node file ops | [`mx6s_csi_fops`](../mx6s_capture.c#L1803) |
| video node ioctl ops | [`mx6s_csi_ioctl_ops`](../mx6s_capture.c#L2454) |
| VB2 queue ops | [`mx6s_videobuf_ops`](../mx6s_capture.c#L1411) |
| host fourcc/mbus 映射 | [`formats[]`](../mx6s_capture.c#L318) |
| video device 注册 | [`vdev->fops/ioctl_ops/queue`](../mx6s_capture.c#L2812) |
| sensor subdev ops | [`ov5640_subdev_ops`](../ov5640.c#L3618) |
| sensor video ops | [`ov5640_subdev_video_ops`](../ov5640.c#L3594) |
| sensor pad ops | [`ov5640_subdev_pad_ops`](../ov5640.c#L3605) |
| sensor core ops | [`ov5640_subdev_core_ops`](../ov5640.c#L3610) |
| sensor controls | [`ov5640_init_controls()`](../ov5640.c#L2274) |
| 矩阵测试脚本 | [`test_v4l2_matrix.sh`](../test_v4l2_matrix.sh) |

V4L2 core 侧可以对照：

| 主题 | 内核源码 |
| --- | --- |
| `struct v4l2_ioctl_ops` | [`include/media/v4l2-ioctl.h`](../../linux-friedegg/include/media/v4l2-ioctl.h#L20) |
| `video_ioctl2()` | [`v4l2-ioctl.c`](../../linux-friedegg/drivers/media/v4l2-core/v4l2-ioctl.c#L2619) |
| `v4l2_subdev_call()` | [`include/media/v4l2-subdev.h`](../../linux-friedegg/include/media/v4l2-subdev.h#L705) |
| `struct vb2_ops` | [`include/media/videobuf2-core.h`](../../linux-friedegg/include/media/videobuf2-core.h#L312) |
| `vb2_reqbufs/qbuf/dqbuf/streamon/streamoff` | [`include/media/videobuf2-core.h`](../../linux-friedegg/include/media/videobuf2-core.h#L451) |

## 先分清三层回调

`v4l2-ctl` 面向的是 `/dev/videoX`，不是直接调用 sensor 的 `.s_stream` 或 `.s_mbus_fmt`。当前工程里有三层关键回调：

```text
用户可见入口:
  VIDIOC_* ioctl
  -> mx6s_capture.c: struct v4l2_ioctl_ops.vidioc_*
  -> mx6s_vidioc_*()

VB2 buffer 队列:
  VIDIOC_REQBUFS/QBUF/DQBUF/STREAMON/STREAMOFF
  -> vb2_*()
  -> mx6s_capture.c: struct vb2_ops
  -> mx6s_videobuf_*(), mx6s_start_streaming(), mx6s_stop_streaming()

sensor 子设备:
  host 使用 v4l2_subdev_call(sd, ...)
  -> ov5640.c: struct v4l2_subdev_ops
  -> ov5640_s_power(), ov5640_s_fmt(), ov5640_s_parm(), ov5640_s_stream()
```

最容易混淆的是 stream：

```text
v4l2-ctl --stream-mmap
  -> VIDIOC_REQBUFS / QUERYBUF / QBUF / STREAMON / DQBUF / QBUF / STREAMOFF

VIDIOC_STREAMON
  -> mx6s_csi_ioctl_ops.vidioc_streamon
  -> mx6s_vidioc_streamon()
  -> v4l2_subdev_call(sd, video, s_stream, 1)
  -> ov5640_s_stream(1)
  -> vb2_streamon()
  -> mx6s_start_streaming()
  -> CSI DMA enable

VIDIOC_STREAMOFF
  -> mx6s_csi_ioctl_ops.vidioc_streamoff
  -> mx6s_vidioc_streamoff()
  -> vb2_streamoff()
  -> mx6s_stop_streaming()
  -> CSI DMA disable and return buffers
  -> v4l2_subdev_call(sd, video, s_stream, 0)
  -> ov5640_s_stream(0)
```

所以：`STREAMON` 是 video node ioctl；`.s_stream` 是 sensor subdev 内部回调；`.start_streaming` 是 VB2 队列启动 host DMA 的回调。

## 板端准备

确认工具和节点：

```bash
command -v v4l2-ctl
ls -l /dev/video*
dmesg | grep -i -E 'ov5640|csi|video'
```

加载模块后再验证：

```bash
modprobe ov5640
modprobe mx6s_capture
dmesg | grep -i -E 'ov5640|csi|video'
```

后续命令统一假设：

```bash
DEV=/dev/video1
```

如果实际节点不是 `/dev/video1`，先用：

```bash
v4l2-ctl --list-devices
```

## v4l2-ctl 命令形态

常用格式：

```bash
v4l2-ctl -d "$DEV" <one-or-more-options>
```

建议调试时加 `--verbose`，让工具打印更多 ioctl 过程：

```bash
v4l2-ctl --verbose -d "$DEV" --get-fmt-video
```

可以把多个操作写在一条命令里，例如：

```bash
v4l2-ctl -d "$DEV" \
  --set-fmt-video=width=800,height=480,pixelformat=RGBP \
  --set-parm=30 \
  --stream-mmap \
  --stream-count=30
```

但定位问题时更推荐拆成多条命令，因为返回值能直接定位失败阶段：

```bash
v4l2-ctl -d "$DEV" --set-fmt-video=width=800,height=480,pixelformat=RGBP
v4l2-ctl -d "$DEV" --get-fmt-video
v4l2-ctl -d "$DEV" --set-parm=30
v4l2-ctl -d "$DEV" --get-parm
v4l2-ctl -d "$DEV" --stream-mmap --stream-count=30
```

## 能力发现

### 查看设备信息

```bash
v4l2-ctl -d "$DEV" --info
v4l2-ctl -d "$DEV" --all
```

回调路径：

```text
VIDIOC_QUERYCAP
  -> video_ioctl2()
  -> mx6s_csi_ioctl_ops.vidioc_querycap
  -> mx6s_vidioc_querycap()
```

当前实现见 [`mx6s_vidioc_querycap()`](../mx6s_capture.c#L2183)：

- `cap->driver` 返回 `MX6S_CAM_DRV_NAME`。
- `cap->card` 返回 `MX6S_CAM_DRIVER_DESCRIPTION`。
- `device_caps` 是 `V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING`。
- 当前主路径是 streaming capture，不是 read file-io。

注意：`--all` 会连续发多个查询类 ioctl，不适合用来证明某一个回调是否单独正确；定位时应该换成更窄的选项。

### 输入源

```bash
v4l2-ctl -d "$DEV" --list-inputs
v4l2-ctl -d "$DEV" --get-input
v4l2-ctl -d "$DEV" --set-input=0
```

回调路径：

```text
VIDIOC_ENUMINPUT -> mx6s_vidioc_enum_input()
VIDIOC_G_INPUT   -> mx6s_vidioc_g_input()
VIDIOC_S_INPUT   -> mx6s_vidioc_s_input()
```

当前实现见 [`mx6s_vidioc_enum_input()`](../mx6s_capture.c#L1824)：只支持一个 camera input，合法 index 是 `0`。这一步不会改 sensor 寄存器，也不会启动 DMA。

## 格式、分辨率和帧率枚举

### 枚举格式

```bash
v4l2-ctl -d "$DEV" --list-formats
v4l2-ctl -d "$DEV" --list-formats-ext
```

回调路径：

```text
VIDIOC_ENUM_FMT
  -> mx6s_vidioc_enum_fmt_vid_cap()
  -> v4l2_subdev_call(sd, video, enum_mbus_fmt, index, &code)
  -> ov5640_enum_fmt()
  -> host format_by_mbus(code)
```

当前实现要分清两个格式概念：

| 层级 | 概念 | 当前源码 |
| --- | --- | --- |
| 用户态 `/dev/videoX` | V4L2 fourcc，例如 `RGBP`、`UYVY`、`YUYV`、`GREY` | [`formats[]`](../mx6s_capture.c#L318) |
| sensor subdev | media-bus code，例如 `MEDIA_BUS_FMT_RGB565_2X8_LE` | [`ov5640_colour_fmts[]`](../ov5640.c#L1036) |

`mx6s_vidioc_enum_fmt_vid_cap()` 先问 sensor 支持哪个 mbus code，再映射成用户态 fourcc。当前 `test_v4l2_matrix.sh` 默认测试：

```text
RGBP UYVY YUYV GREY
```

这对应 sensor 侧当前暴露的 RGB565、UYVY、YUYV、Y8/GREY 路径。BA81/SBGGR8 当前不暴露，原因是 OV5640 RAW DVP 输出位宽和 host 1 byte/pixel buffer 尚未闭环验证。Y8/GREY 依据 OV5640 datasheet 6.5/7.15 的 Format Control 00 配置：`0x501f=0x00` 选择 ISP YUV422 formatter 输入，`0x4300=0x10` 选择 formatter 输出 Y8。

### 枚举分辨率

```bash
v4l2-ctl -d "$DEV" --list-framesizes=RGBP
v4l2-ctl -d "$DEV" --list-framesizes=UYVY
```

回调路径：

```text
VIDIOC_ENUM_FRAMESIZES
  -> mx6s_vidioc_enum_framesizes()
  -> host format_by_fourcc(pixel_format)
  -> v4l2_subdev_call(sd, pad, enum_frame_size, NULL, &fse)
  -> ov5640_enum_framesizes()
```

当前实现见 [`mx6s_vidioc_enum_framesizes()`](../mx6s_capture.c#L2378) 和 [`ov5640_enum_framesizes()`](../ov5640.c#L3440)：

- 用户态传入 fourcc。
- host 转成 mbus code。
- sensor 遍历 [`ov5640_modes[]`](../ov5640.c#L809) 中有效 mode。
- 每个 mode 以 discrete frame size 返回。
- 没有寄存器表的 mode 不算有效能力。

### 枚举帧率

```bash
v4l2-ctl -d "$DEV" --list-frameintervals=width=800,height=480,pixelformat=RGBP
v4l2-ctl -d "$DEV" --list-frameintervals=width=640,height=480,pixelformat=UYVY
```

回调路径：

```text
VIDIOC_ENUM_FRAMEINTERVALS
  -> mx6s_vidioc_enum_frameintervals()
  -> host format_by_fourcc(pixel_format)
  -> v4l2_subdev_call(sd, pad, enum_frame_interval, NULL, &fie)
  -> ov5640_enum_frameintervals()
```

当前实现见 [`mx6s_vidioc_enum_frameintervals()`](../mx6s_capture.c#L2426) 和 [`ov5640_enum_frameintervals()`](../ov5640.c#L3478)：

- 必须同时给 `width`、`height`、`pixelformat`。
- sensor 先确认格式 code 有效，再按 width/height 找 mode。
- 每个 mode 的 fps 由寄存器表决定，不是所有尺寸都支持所有 fps。
- `expose_pdf_modes` 为 false 时，实验性 PDF 目标帧率不会被当作稳定能力暴露。

## 格式协商和提交

### TRY_FMT：只试算，不提交

如果当前板端的 `v4l2-ctl` 支持 `--try-fmt-video`：

```bash
v4l2-ctl -d "$DEV" --try-fmt-video=width=801,height=479,pixelformat=RGBP
```

回调路径：

```text
VIDIOC_TRY_FMT
  -> mx6s_vidioc_try_fmt_vid_cap()
  -> mx6s_negotiate_format(apply=false)
  -> v4l2_subdev_call(sd, video, try_mbus_fmt, &mbus_fmt)
  -> ov5640_try_fmt()
```

当前实现见 [`mx6s_vidioc_try_fmt_vid_cap()`](../mx6s_capture.c#L2114) 和 [`ov5640_try_fmt()`](../ov5640.c#L3301)：

- 不写 sensor 寄存器。
- 不更新 host active `csi_dev->pix`。
- 不启动 CSI DMA。
- sensor 会把不支持的 mbus code 调整到默认格式。
- sensor 会按当前 `state.frame_rate` 选择最近的有效尺寸。
- host 会回填 `bytesperline = bpp * width` 和 `sizeimage = bytesperline * height`。

用途：验证用户请求会被规整成什么，不影响后续采集状态。

### S_FMT：提交 active format

```bash
v4l2-ctl -d "$DEV" --set-fmt-video=width=800,height=480,pixelformat=RGBP
v4l2-ctl -d "$DEV" --get-fmt-video
```

回调路径：

```text
VIDIOC_S_FMT
  -> mx6s_vidioc_s_fmt_vid_cap()
  -> mx6s_negotiate_format(apply=true)
  -> v4l2_subdev_call(sd, video, s_mbus_fmt, &mbus_fmt)
  -> ov5640_s_fmt()
  -> mx6s_configure_csi()

VIDIOC_G_FMT
  -> mx6s_vidioc_g_fmt_vid_cap()
```

当前实现见 [`mx6s_vidioc_s_fmt_vid_cap()`](../mx6s_capture.c#L2133)、[`mx6s_negotiate_format()`](../mx6s_capture.c#L2056)、[`ov5640_s_fmt()`](../ov5640.c#L3337)：

- host 先把用户 fourcc 转成 sensor mbus code。
- sensor 复用 `ov5640_try_fmt()` 做格式和尺寸规整。
- sensor streaming 中调用 `S_FMT` 会返回 `-EBUSY`。
- 如果 sensor 已 powered，`ov5640_s_fmt()` 会切换 mode、应用输出格式、应用 controls，并确保 sensor 先停流。
- host 缓存最终的 `csi_dev->fmt`、`mbus_code`、`pix`、`bytesperline`、`type`。
- host 调 `mx6s_configure_csi()` 配置 CSI 输入格式和图像参数。

调试重点：

- `--get-fmt-video` 返回的是 host 缓存的 active `struct v4l2_pix_format`。
- `bytesperline` 和 `sizeimage` 是后续 VB2 buffer 大小的依据。
- `S_FMT` 应在 `REQBUFS/STREAMON` 前做；streaming 中切格式不是当前驱动支持路径。

## 帧率设置

```bash
v4l2-ctl -d "$DEV" --get-parm
v4l2-ctl -d "$DEV" --set-parm=30
v4l2-ctl -d "$DEV" --get-parm
```

回调路径：

```text
VIDIOC_G_PARM
  -> mx6s_vidioc_g_parm()
  -> v4l2_subdev_call(sd, video, g_parm, a)
  -> ov5640_g_parm()

VIDIOC_S_PARM
  -> mx6s_vidioc_s_parm()
  -> v4l2_subdev_call(sd, video, s_parm, a)
  -> ov5640_s_parm()
```

当前实现见 [`mx6s_vidioc_g_parm()`](../mx6s_capture.c#L2341)、[`mx6s_vidioc_s_parm()`](../mx6s_capture.c#L2358)、[`ov5640_g_parm()`](../ov5640.c#L3135)、[`ov5640_s_parm()`](../ov5640.c#L3183)：

- `--set-parm=30` 主要对应 `timeperframe = 1/30`。
- sensor 会把目标 fps 规整到 `ov5640_framerates[]` 中的离散枚举。
- 当前尺寸必须有对应 fps 的寄存器表，否则返回 `-EINVAL`。
- streaming 中如果要改变 fps，会返回 `-EBUSY`。
- sensor 已 powered 且 fps 发生变化时，会立即切 mode、重新应用格式和 controls，并保持硬件停流。

推荐顺序：

```bash
v4l2-ctl -d "$DEV" --set-fmt-video=width=640,height=480,pixelformat=RGBP
v4l2-ctl -d "$DEV" --set-parm=30
v4l2-ctl -d "$DEV" --get-fmt-video
v4l2-ctl -d "$DEV" --get-parm
```

如果某个尺寸/fps 组合失败，用 `--list-frameintervals` 确认它是否真实暴露。

## Controls

### 枚举 controls

```bash
v4l2-ctl -d "$DEV" --list-ctrls
v4l2-ctl -d "$DEV" --list-ctrls-menus
```

回调路径：

```text
VIDIOC_QUERYCTRL / VIDIOC_QUERYMENU
  -> video_ioctl2()
  -> V4L2 control core
  -> video_device.ctrl_handler
  -> aggregated sensor ctrl_handler
```

当前 host 在 sensor async bound 时调用 [`v4l2_ctrl_add_handler()`](../mx6s_capture.c#L2495)，把 sensor 的 control handler 聚合到 video node。因此用户态对 `/dev/videoX` 查询 controls，会看到 [`ov5640_init_controls()`](../ov5640.c#L2274) 注册的 control。

当前 sensor 侧注册的重点 control：

| control | 类型 | 当前用途 |
| --- | --- | --- |
| `horizontal_flip` | boolean | 进入 `V4L2_CID_HFLIP`，最终写 sensor flip 寄存器 |
| `vertical_flip` | boolean | 进入 `V4L2_CID_VFLIP`，当前默认值为 1 |
| `power_line_frequency` | menu | 50Hz/60Hz/auto 防频闪 |
| `flash_led_mode` | menu | 闪光/补光模式 |
| `flash_strobe` | button | 触发 strobe |
| `flash_strobe_stop` | button | 停止 strobe |
| `focus_absolute` | integer | 手动对焦 VCM target |
| `auto_focus_start` | button | 启动 AF |
| `auto_focus_stop` | button | 停止 AF |
| `auto_focus_status` | volatile | 读取 AF 状态 |
| `af_zone_mode` | custom menu | 默认区域或 touch 区域 |
| `af_touch_x` | custom integer | touch AF X 坐标 |
| `af_touch_y` | custom integer | touch AF Y 坐标 |
| `af_zone_result` | custom volatile single-bit | 单 AF 区域结果：1 表示 focused，0 表示 failed 或未完成 |

实际 control 名称以 `--list-ctrls-menus` 输出为准。不同 v4l-utils 版本对按钮和 menu 的显示略有差异。

### 设置 controls

```bash
v4l2-ctl -d "$DEV" --set-ctrl=horizontal_flip=1
v4l2-ctl -d "$DEV" --set-ctrl=vertical_flip=0
v4l2-ctl -d "$DEV" --set-ctrl=power_line_frequency=1
v4l2-ctl -d "$DEV" --get-ctrl=horizontal_flip,vertical_flip,power_line_frequency
```

AF 示例：

```bash
v4l2-ctl -d "$DEV" --set-ctrl=af_zone_mode=1,af_touch_x=32768,af_touch_y=32768
v4l2-ctl -d "$DEV" --set-ctrl=auto_focus_start=1
v4l2-ctl -d "$DEV" --get-ctrl=auto_focus_status,af_zone_result
v4l2-ctl -d "$DEV" --set-ctrl=auto_focus_stop=1
```

回调路径：

```text
VIDIOC_S_CTRL / VIDIOC_S_EXT_CTRLS
  -> V4L2 control core
  -> ov5640_ctrl_ops.s_ctrl
  -> ov5640_s_ctrl()

VIDIOC_G_CTRL / VIDIOC_G_EXT_CTRLS
  -> V4L2 control core
  -> volatile control uses ov5640_ctrl_ops.g_volatile_ctrl
  -> ov5640_g_volatile_ctrl()
```

当前实现见 [`ov5640_s_ctrl()`](../ov5640.c#L2124)、[`ov5640_g_volatile_ctrl()`](../ov5640.c#L2194)、[`ov5640_ctrl_ops`](../ov5640.c#L2221)：

- `af_zone_mode/af_touch_x/af_touch_y` 先更新 AF 区域状态，不要求 sensor powered。
- `auto_focus_start` 要求 sensor powered，否则返回 `-EPIPE`。
- 普通硬件 controls 如果 sensor 未 powered，当前 `ov5640_s_ctrl()` 直接返回成功但不写寄存器；后续 power-on/init 会通过 `ov5640_apply_controls()` 应用缓存值。
- volatile controls 每次 get 都可能读 sensor/AF 状态，而不是只读缓存。

## Streaming 与 buffer

`v4l2-ctl --stream-mmap` 不是一个单独 ioctl。它内部走完整 streaming 流程：

```text
VIDIOC_REQBUFS
VIDIOC_QUERYBUF *
mmap *
VIDIOC_QBUF *
VIDIOC_STREAMON
poll/select
VIDIOC_DQBUF
VIDIOC_QBUF
...
VIDIOC_STREAMOFF
munmap
VIDIOC_REQBUFS(count=0)
```

### 最小采集

```bash
v4l2-ctl -d "$DEV" \
  --set-fmt-video=width=800,height=480,pixelformat=RGBP \
  --set-parm=30 \
  --stream-mmap \
  --stream-count=30
```

保存一帧原始数据：

```bash
v4l2-ctl -d "$DEV" \
  --set-fmt-video=width=800,height=480,pixelformat=RGBP \
  --stream-mmap \
  --stream-count=1 \
  --stream-to=/tmp/ov5640-800x480-rgb565.raw
```

RGB565 一帧大小应等于：

```text
800 * 480 * 2 = 768000 bytes
```

如果 `--get-fmt-video` 返回了不同尺寸或像素格式，以返回的 `sizeimage` 为准。

### REQBUFS

回调路径：

```text
VIDIOC_REQBUFS
  -> mx6s_vidioc_reqbufs()
  -> vb2_reqbufs()
  -> mx6s_videobuf_setup()
```

当前实现见 [`mx6s_vidioc_reqbufs()`](../mx6s_capture.c#L1930) 和 [`mx6s_videobuf_setup()`](../mx6s_capture.c#L920)：

- 队列 type 是 `V4L2_BUF_TYPE_VIDEO_CAPTURE`。
- open 阶段设置 `q->io_modes = VB2_MMAP | VB2_USERPTR`，见 [`mx6s_csi_open()`](../mx6s_capture.c#L1625)。
- MMAP buffer 大小来自当前 `csi_dev->pix.sizeimage`。
- 如果 count 为 0，VB2 释放队列。
- 当前驱动用单 plane。

### QUERYBUF / mmap

回调路径：

```text
VIDIOC_QUERYBUF
  -> mx6s_vidioc_querybuf()
  -> vb2_querybuf()

mmap()
  -> mx6s_csi_mmap()
  -> vb2_mmap()
```

当前实现见 [`mx6s_vidioc_querybuf()`](../mx6s_capture.c#L1951) 和 [`mx6s_csi_mmap()`](../mx6s_capture.c#L1781)：

- `QUERYBUF` 返回 buffer length、offset、flags。
- 用户态把 offset 传给 `mmap()`。
- 当前驱动有旧兼容逻辑：buffer 已 mmap 时可能把 `m.offset` 改成 DMA 物理地址。

### QBUF

回调路径：

```text
VIDIOC_QBUF
  -> mx6s_vidioc_qbuf()
  -> vb2_qbuf()
  -> mx6s_videobuf_prepare()
  -> mx6s_videobuf_queue()
```

当前实现见 [`mx6s_vidioc_qbuf()`](../mx6s_capture.c#L1980)、[`mx6s_videobuf_prepare()`](../mx6s_capture.c#L978)、[`mx6s_videobuf_queue()`](../mx6s_capture.c#L1017)：

- `prepare` 把 plane 0 payload 设为当前 `sizeimage`。
- buffer 容量不足会返回 `-EINVAL`。
- `buf_queue` 把 buffer 挂到 host 私有 `capture` 链表。
- 这一步通常还没有写 CSI FB1/FB2 DMA 地址；真正装入硬件在 `.start_streaming` 或 IRQ 续帧路径。

### STREAMON

回调路径：

```text
VIDIOC_STREAMON
  -> mx6s_vidioc_streamon()
  -> ov5640_s_stream(1)
  -> vb2_streamon()
  -> mx6s_start_streaming()
  -> mx6s_csi_enable()
```

当前实现见 [`mx6s_vidioc_streamon()`](../mx6s_capture.c#L2211)、[`ov5640_s_stream()`](../ov5640.c#L3088)、[`mx6s_start_streaming()`](../mx6s_capture.c#L1263)：

- host 先让 sensor 出流，再启动 VB2/CSI。
- 这样 `mx6s_csi_enable()` 等待 SOF 时，sensor 已经输出同步信号。
- sensor `.s_stream(1)` 会获取 runtime PM 引用，并通过 `ov5640_set_stream(true)` 写 stream 控制寄存器。
- VB2 `.start_streaming` 要求至少两个 buffer，少于两个返回 `-ENOBUFS`。
- host 把前两个 queued buffer 装入 CSI FB1/FB2。
- host 分配 discard buffer，用于用户态补 buffer 不及时的丢帧路径。
- `mx6s_csi_enable()` 打开 CSI DMA 请求和中断。
- 如果 VB2 启动失败，host 会回滚调用 sensor `.s_stream(0)`。

### DQBUF / poll

回调路径：

```text
poll()
  -> vb2_fop_poll()

VIDIOC_DQBUF
  -> mx6s_vidioc_dqbuf()
  -> vb2_dqbuf()
  -> done_list
```

当前实现见 [`mx6s_vidioc_dqbuf()`](../mx6s_capture.c#L2001)：

- 完成帧来自 CSI IRQ 路径调用 `vb2_buffer_done()`。
- 阻塞还是立即返回由 `O_NONBLOCK` 决定。
- `v4l2-ctl --stream-mmap` 会循环 `DQBUF -> 写文件/统计 -> QBUF`。

### STREAMOFF

回调路径：

```text
VIDIOC_STREAMOFF
  -> mx6s_vidioc_streamoff()
  -> vb2_streamoff()
  -> mx6s_stop_streaming()
  -> ov5640_s_stream(0)
```

当前实现见 [`mx6s_vidioc_streamoff()`](../mx6s_capture.c#L2246)、[`mx6s_stop_streaming()`](../mx6s_capture.c#L1370)、[`ov5640_s_stream()`](../ov5640.c#L3088)：

- host 先停 VB2/CSI，再停 sensor 输出。
- `mx6s_stop_streaming()` 会禁用 CSI，并把 `active_bufs/capture` 里尚未完成的 buffer 以 `VB2_BUF_STATE_ERROR` 归还。
- sensor `.s_stream(0)` 写停流寄存器，并释放 streaming runtime PM 引用。

## open/close 与电源

`v4l2-ctl -d "$DEV" ...` 每次运行都会 open/close 一次 video node。

open 路径：

```text
open("/dev/videoX")
  -> mx6s_csi_fops.open
  -> mx6s_csi_open()
  -> configure vb2_queue
  -> pm_runtime_get_sync(CSI)
  -> request_bus_freq(BUS_FREQ_HIGH)
  -> v4l2_subdev_call(sd, core, s_power, 1)
  -> ov5640_s_power(1)
  -> mx6s_csi_init()
```

close 路径：

```text
close(fd)
  -> mx6s_csi_fops.release
  -> mx6s_csi_close()
  -> vb2_queue_release()
  -> v4l2_subdev_call(sd, video, s_stream, 0)
  -> mx6s_csi_deinit()
  -> v4l2_subdev_call(sd, core, s_power, 0)
  -> ov5640_s_power(0)
  -> release_bus_freq()
  -> pm_runtime_put_sync_suspend(CSI)
```

当前实现见 [`mx6s_csi_open()`](../mx6s_capture.c#L1625)、[`mx6s_csi_close()`](../mx6s_capture.c#L1718)、[`ov5640_s_power()`](../ov5640.c#L3057)：

- `.s_power` 是电源/runtime PM 引用，不等同于 pixel output。
- `.s_stream` 是 sensor 像素输出开关，不等同于 VB2 队列状态。
- 单独运行 `--list-formats-ext` 也会触发 open/power-on/close/power-off 生命周期。

## crop/selection/debug register

当前 host 实现了 crop 相关占位回调：

```bash
v4l2-ctl -d "$DEV" --get-cropcap
v4l2-ctl -d "$DEV" --get-crop
```

回调路径：

```text
VIDIOC_CROPCAP -> mx6s_vidioc_cropcap()
VIDIOC_G_CROP  -> mx6s_vidioc_g_crop()
VIDIOC_S_CROP  -> mx6s_vidioc_s_crop()
```

当前实现见 [`mx6s_vidioc_cropcap()`](../mx6s_capture.c#L2276)、[`mx6s_vidioc_g_crop()`](../mx6s_capture.c#L2298)、[`mx6s_vidioc_s_crop()`](../mx6s_capture.c#L2320)：只校验 type 并返回，不做真实裁剪。

debug register 只在 sensor subdev core ops 中按条件编译：

```c
#ifdef CONFIG_VIDEO_ADV_DEBUG
  .g_register = ov5640_get_register,
  .s_register = ov5640_set_register,
#endif
```

见 [`ov5640_subdev_core_ops`](../ov5640.c#L3610)。当前普通 `/dev/videoX` host ioctl ops 没有显式暴露 sensor register debug 路径；如要使用 `v4l2-ctl --get-register/--set-register`，需要确认内核启用了 `CONFIG_VIDEO_ADV_DEBUG`，并确认板端是否存在可访问的 subdev 节点或 host 转发路径。

## 矩阵测试脚本

当前包会把脚本安装到 `/usr/bin/test_v4l2_matrix.sh`，安装规则见 [`bsp/package/ov5640/ov5640.mk`](../../../bsp/package/ov5640/ov5640.mk#L13)。

快速 smoke：

```bash
test_v4l2_matrix.sh -d "$DEV"
```

只列能力：

```bash
test_v4l2_matrix.sh -d "$DEV" --list-only
```

指定组合：

```bash
test_v4l2_matrix.sh -d "$DEV" \
  --formats "RGBP UYVY YUYV GREY" \
  --sizes "800x480 640x480 320x240 1280x720" \
  --fps "15 30" \
  -c 10
```

更宽矩阵：

```bash
test_v4l2_matrix.sh -d "$DEV" --full -c 10
```

脚本执行顺序见 [`test_v4l2_matrix.sh`](../test_v4l2_matrix.sh#L133)：

```text
--list-formats-ext
for fmt/size/fps:
  --set-fmt-video
  --get-fmt-video
  --set-parm
  --get-parm
  --stream-mmap --stream-count=N
```

失败阶段和回调定位：

| 结果 | 主要怀疑路径 | 回调入口 |
| --- | --- | --- |
| `FAIL_FMT` | fourcc 不支持、尺寸无有效 mode、streaming 中切格式、sensor 写寄存器失败 | `mx6s_vidioc_s_fmt_vid_cap()`、`ov5640_s_fmt()` |
| `FAIL_PARM` | fps 不在离散表、当前尺寸没有该 fps 寄存器表、streaming 中改 fps | `mx6s_vidioc_s_parm()`、`ov5640_s_parm()` |
| `FAIL_STREAM` | buffer 不足、sensor 出流失败、CSI 等 SOF 超时、DMA/IRQ 问题 | `mx6s_vidioc_streamon()`、`ov5640_s_stream()`、`mx6s_start_streaming()` |

日志目录默认在 `/tmp/ov5640-v4l2-matrix-<time>`，每个组合一个 log。先看失败 log 里的最后一个 `v4l2-ctl` 命令，再回到上表定位回调。

## 推荐验证序列

### 1. 确认节点和基础能力

```bash
v4l2-ctl --list-devices
v4l2-ctl -d "$DEV" --info
v4l2-ctl -d "$DEV" --list-inputs
v4l2-ctl -d "$DEV" --list-formats-ext
```

预期：

- 能看到 mx6s CSI capture 设备。
- `--info` 能看到 video capture + streaming 能力。
- `--list-formats-ext` 至少能列出当前 sensor 暴露的有效格式。

### 2. 确认一个稳定格式

```bash
v4l2-ctl -d "$DEV" --set-fmt-video=width=800,height=480,pixelformat=RGBP
v4l2-ctl -d "$DEV" --get-fmt-video
v4l2-ctl -d "$DEV" --set-parm=30
v4l2-ctl -d "$DEV" --get-parm
```

预期：

- `--get-fmt-video` 返回有效 `Width/Height`、`Pixel Format`、`Bytes per Line`、`Size Image`。
- `--get-parm` 显示目标 fps 或驱动规整后的 fps。

### 3. 确认 stream 和 IRQ 完成路径

```bash
v4l2-ctl -d "$DEV" --stream-mmap --stream-count=30
```

预期：

- 命令正常退出。
- dmesg 无持续 I2C 写失败、SOF timeout、DMA timeout。
- 如果保存 raw，文件大小接近 `stream-count * sizeimage`。

### 4. 批量扫组合

```bash
test_v4l2_matrix.sh -d "$DEV" -c 10
```

预期：

- smoke 组合中稳定能力应 PASS。
- 不支持的 fps/size 应在 set-fmt 或 set-parm 阶段明确失败，而不是 stream 阶段随机失败。

## 常见问题定位

| 现象 | 优先看哪里 | 原因方向 |
| --- | --- | --- |
| `--info` 失败 | video node 注册、模块加载、设备树匹配 | `vdev->fops/ioctl_ops` 未注册或节点不是目标设备 |
| `--list-formats-ext` 无格式 | sensor async bound、`enum_mbus_fmt` | host 没绑定 `sd`，或 sensor 格式表/mbus 映射不一致 |
| `--set-fmt-video` 返回 `Invalid argument` | fourcc、尺寸、fps 当前状态 | host `format_by_fourcc()` 或 sensor mode/fps 表拒绝 |
| `--set-fmt-video` 返回 `Device or resource busy` | streaming 状态 | 当前 `ov5640_s_fmt()` 拒绝 streaming 中改格式 |
| `--set-parm` 失败 | 当前尺寸对应 fps | `ov5640_s_parm()` 要求当前 mode 有该 fps 寄存器表 |
| `--stream-mmap` 很快失败 | buffer 数、sensor 出流、SOF | `mx6s_start_streaming()` 至少要 2 个 buffer，CSI 要等到 SOF |
| `--stream-mmap` 卡住 | IRQ/DMA 完成路径 | buffer 没有进入 done_list，`DQBUF` 等不到完成帧 |
| controls 设置了但没立即生效 | sensor 未 powered 或 control 名称不对 | `ov5640_s_ctrl()` 未 powered 时可能只更新缓存 |
| AF start 失败 | sensor power/stream/firmware | `auto_focus_start` 要求 powered，AF helper 还依赖固件/stream 状态 |

## 读源码顺序

如果目的是理解 `v4l2-ctl`，建议按这个顺序读：

1. [`mx6s_csi_fops`](../mx6s_capture.c#L1803)：确认 open/ioctl/mmap/poll 入口。
2. [`mx6s_csi_ioctl_ops`](../mx6s_capture.c#L2454)：把 `VIDIOC_*` 映射到 `mx6s_vidioc_*()`。
3. [`mx6s_vidioc_s_fmt_vid_cap()`](../mx6s_capture.c#L2133) 和 [`mx6s_negotiate_format()`](../mx6s_capture.c#L2056)：理解 fourcc 到 mbus code 的转换。
4. [`ov5640_s_fmt()`](../ov5640.c#L3337)、[`ov5640_try_fmt()`](../ov5640.c#L3301)：理解 sensor 尺寸和格式规整。
5. [`ov5640_s_parm()`](../ov5640.c#L3183)：理解 fps 为什么要看当前尺寸。
6. [`mx6s_vidioc_streamon()`](../mx6s_capture.c#L2211)、[`ov5640_s_stream()`](../ov5640.c#L3088)、[`mx6s_start_streaming()`](../mx6s_capture.c#L1263)：理解出流、VB2 streaming 和 CSI DMA 的启动顺序。
7. [`mx6s_videobuf_queue()`](../mx6s_capture.c#L1017)、[`mx6s_stop_streaming()`](../mx6s_capture.c#L1370)：理解 buffer 所有权和停止时归还规则。
8. [`ov5640_s_ctrl()`](../ov5640.c#L2124)、[`ov5640_g_volatile_ctrl()`](../ov5640.c#L2194)：理解 `--set-ctrl/--get-ctrl`。

## 关键结论

- `v4l2-ctl` 是 ioctl 发生器；它不绕过 V4L2 core，也不会直接调用 sensor 函数。
- `/dev/videoX` 的入口在 `mx6s_capture.c`，OV5640 sensor 在 `ov5640.c` 里作为 subdev 被 host 调用。
- 用户态 pixel format 是 fourcc；sensor 侧是 media-bus code；host 负责两者映射。
- `TRY_FMT` 是试算，`S_FMT` 是提交；当前驱动不支持 streaming 中切格式。
- `--set-parm` 是否成功取决于当前 active size 是否有目标 fps 的寄存器表。
- `--stream-mmap` 会触发完整 VB2 buffer 生命周期，不只是 `STREAMON`。
- sensor `.s_stream` 控制像素输出，VB2 `.start_streaming` 控制 host DMA；这两个开关是不同层级。
- controls 通过 V4L2 control core 从 video node 聚合到 sensor，不是 host 手写每个 `vidioc_s_ctrl`。
