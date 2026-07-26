# OV5640 userspace API 与采集路径

本文以当前 `mx6s_capture.c + ov5640.c` 为准，把用户态 API、host 回调、VB2 队列、OV5640 subdev、CSI DMA 和验证工具放在同一条采集路径中说明。它不是单纯的 ioctl 字段列表，阅读目标是回答三个问题：

- 用户态调用了哪个 syscall 或 `VIDIOC_*`？
- 这个操作进入驱动后，哪个层负责状态变化，硬件何时真正被配置？
- 采集失败时，应沿哪一个回调和状态边界定位？

## 目录

- [1. 使用方式](#1-使用方式)
- [2. 系统模型](#2-系统模型)
- [2.1 源码入口](#21-源码入口)
- [2.2 subdev 绑定是所有路径的前置条件](#22-subdev-绑定是所有路径的前置条件)
- [2.3 三种容易混淆的状态](#23-三种容易混淆的状态)
- [2.4 进程上下文和 IRQ 上下文](#24-进程上下文和-irq-上下文)
- [3. 典型流程总览](#3-典型流程总览)
- [3.1 发现能力](#31-发现能力)
- [3.2 配置采集参数](#32-配置采集参数)
- [3.3 MMAP 采集](#33-mmap-采集)
- [3.4 read 采集](#34-read-采集)
- [4. 设备生命周期和 file operations](#4-设备生命周期和-file-operations)
- [5. 能力、输入源和拓扑发现](#5-能力输入源和拓扑发现)
- [6. 格式、分辨率和帧率枚举](#6-格式分辨率和帧率枚举)
- [6.1 当前 host 的 fourcc 到 mbus 映射](#61-当前-host-的-fourcc-到-mbus-映射)
- [7. 常用 V4L2 结构体和字段](#7-常用-v4l2-结构体和字段)
- [7.1 结构体与 ioctl 对照](#71-结构体与-ioctl-对照)
- [7.2 `struct v4l2_capability`](#72-struct-v4l2_capability)
- [7.3 `struct v4l2_fmtdesc`](#73-struct-v4l2_fmtdesc)
- [7.4 `struct v4l2_frmsizeenum` 和 `struct v4l2_frmivalenum`](#74-struct-v4l2_frmsizeenum-和-struct-v4l2_frmivalenum)
- [7.4.1 `v4l2_frmsizeenum`](#741-v4l2_frmsizeenum)
- [7.4.2 `v4l2_frmivalenum`](#742-v4l2_frmivalenum)
- [7.5 `struct v4l2_format` 和 `struct v4l2_pix_format`](#75-struct-v4l2_format-和-struct-v4l2_pix_format)
- [7.6 `struct v4l2_streamparm`、`v4l2_captureparm` 和 `v4l2_fract`](#76-struct-v4l2_streamparmv4l2_captureparm-和-struct-v4l2_fract)
- [7.7 `struct v4l2_requestbuffers`](#77-struct-v4l2_requestbuffers)
- [7.8 `struct v4l2_buffer` 和 `struct v4l2_plane`](#78-struct-v4l2_buffer-和-struct-v4l2_plane)
- [7.9 `struct v4l2_control`](#79-struct-v4l2_control)
- [8. 格式和 stream 参数配置](#8-格式和-stream-参数配置)
- [9. V4L2 controls](#9-v4l2-controls)
- [10. Streaming buffer API](#10-streaming-buffer-api)
- [11. STREAMON / STREAMOFF](#11-streamon--streamoff)
- [11.1 `VIDIOC_STREAMON`](#111-vidioc_streamon)
- [11.2 `VIDIOC_STREAMOFF`](#112-vidioc_streamoff)
- [12. IRQ 完成和 DQBUF 的关系](#12-irq-完成和-dqbuf-的关系)
- [13. VB2 io_modes 与 buffer memory 模型](#13-vb2-io_modes-与-buffer-memory-模型)
- [14. crop 和 legacy std 接口](#14-crop-和-legacy-std-接口)
- [15. debug-register API](#15-debug-register-api)
- [16. 每个 demo 选项对应的主要操作](#16-每个-demo-选项对应的主要操作)
- [17. 读代码时的最短路线](#17-读代码时的最短路线)
- [18. 实际工具与最短验证](#18-实际工具与最短验证)
- [18.1 板端准备和基础命令](#181-板端准备和基础命令)
- [18.2 v4l2-ctl 配置和采集](#182-v4l2-ctl-配置和采集)
- [19. v4l2-ctl controls 和回调](#19-v4l2-ctl-controls-和回调)
- [20. v4l2-ctl 矩阵测试和验证](#20-v4l2-ctl-矩阵测试和验证)
- [21. 最短验证路径](#21-最短验证路径)
- [22. 一帧如何完成](#22-一帧如何完成)
- [23. 失败定位的分层方法](#23-失败定位的分层方法)
- [24. 与本文件对应的工具入口](#24-与本文件对应的工具入口)

## 1. 使用方式

如果只想验证板端采集，先看“[最短验证路径](#21-最短验证路径)”；如果要理解一次 MMAP 采集如何运行，按“[系统模型](#2-系统模型)”到“[一帧如何完成](#22-一帧如何完成)”阅读；如果需要查 UAPI 字段，再进入“[常用 V4L2 结构体和字段](#7-常用-v4l2-结构体和字段)”。

本文中的“当前驱动”特指本仓库版本，不等同于上游 Linux 通用行为。凡是驱动的兼容行为、占位回调或实验性 mode，都会单独标出。

## 2. 系统模型

示例程序：

```bash
./ov5640_interface_demo /dev/video1 --list
./ov5640_interface_demo /dev/video1 --configure --width 800 --height 480 --fps 30
./ov5640_interface_demo /dev/video1 --mmap --count 30
./ov5640_interface_demo /dev/video1 --read --count 1
./ov5640_interface_demo /dev/video1 --hflip 1 --vflip 1 --power-line 50
```

一次 `/dev/videoX` 采集涉及四个边界：

```text
userspace syscall/ioctl
  -> /dev/videoX 的 v4l2_file_operations
  -> video_ioctl2() 或 VB2 file op helper
  -> mx6s_capture.c 的 v4l2_ioctl_ops / vb2_ops
  -> ov5640.c 的 v4l2_subdev_ops
  -> CSI host、OV5640 sensor、VB2 buffer 队列和 IRQ 完成路径
```

| 边界 | 主要对象 | 状态拥有者 | 典型硬件效果 |
| --- | --- | --- | --- |
| 用户态 video node | `v4l2-ctl`、`ov5640_interface_demo` | 用户拥有 fd 和 `DEQUEUED` buffer | 发起 ioctl、等待和消费帧 |
| V4L2 host | `mx6s_csi_fops`、`mx6s_csi_ioctl_ops` | host 的 active `csi_dev->pix/fmt` | 把 fourcc 转成 mbus code，配置 CSI 图像参数 |
| VB2 队列 | `vb2_queue`、`mx6s_videobuf_ops` | VB2/host 共同管理 buffer 所有权 | 分配或映射 DMA buffer，维护 `capture/active_bufs` |
| sensor subdev | `ov5640_subdev_ops`、control handler | OV5640 的 `sensor->state` 和 `sensor->lock` | 写 mode、格式、control 和 stream 寄存器 |

核心分工不是“每个 ioctl 都直接写寄存器”：枚举通常只读能力；`TRY_FMT` 只规整请求；`S_FMT/S_PARM` 负责提交 sensor/host 配置；`STREAMON` 才让 sensor 出流并启动 CSI；IRQ 完成后才有可供 `DQBUF` 取回的帧。

### 2.1 源码入口

| 主题 | 当前源码 |
| --- | --- |
| video node file ops | [`mx6s_csi_fops`](../mx6s_capture.c#L1848) |
| video node ioctl ops | [`mx6s_csi_ioctl_ops`](../mx6s_capture.c#L2507) |
| VB2 queue ops | [`mx6s_videobuf_ops`](../mx6s_capture.c#L1454) |
| host format table | [`formats[]`](../mx6s_capture.c#L318) |
| video device 注册 | [`video_register_device()`](../mx6s_capture.c#L2885) |
| async subdev 绑定 | [`subdev_notifier_bound()`](../mx6s_capture.c#L2535) |
| sensor subdev ops | [`ov5640_subdev_ops`](../ov5640.c#L4278) |
| sensor controls | [`ov5640_init_controls()`](../ov5640.c#L2915) |
| 用户态 demo | [`ov5640_interface_demo.c`](../ov5640_interface_demo.c) |
| 矩阵脚本 | [`test_v4l2_matrix.sh`](../test_v4l2_matrix.sh) |

需要特别区分三类回调：

| 层级 | 回调表 | 当前实现 | 作用 |
| --- | --- | --- | --- |
| video node ioctl | `struct v4l2_ioctl_ops` | `mx6s_vidioc_*()` | `/dev/videoX` 上的用户态 ioctl 入口 |
| VB2 queue | `struct vb2_ops` | `mx6s_videobuf_*()`、`mx6s_start_streaming()`、`mx6s_stop_streaming()` | 管理采集 buffer、DMA 队列和 CSI host 启停 |
| sensor subdev | `struct v4l2_subdev_ops` | `ov5640_*()` | 配置 OV5640 的格式、帧率、控制项、电源和出流 |

读下面的驱动路径时要注意：表格里的 `->` 表示常见主路径或可能进入的下层回调，不表示任何状态下都会无条件调用。尤其是 VB2 buffer API 会被 `struct vb2_queue` 状态机驱动：

- `q->streaming` 表示用户态已经执行过 `VIDIOC_STREAMON`，VB2 队列进入 streaming 语义。
- `q->start_streaming_called` 表示 VB2 已经成功调用驱动 `.start_streaming`，CSI host 才真正处于运行采集路径。
- `q->queued_count`、`queued_list`、`done_list` 和每个 `vb2_buffer.state` 决定 `QBUF/DQBUF/poll/STREAMOFF` 的具体行为。
- 单个 buffer 的核心状态流转是 `DEQUEUED -> PREPARED -> QUEUED -> ACTIVE -> DONE/ERROR -> DEQUEUED`。`DEQUEUED` 属于用户态，`QUEUED/ACTIVE` 属于 VB2/驱动，`DONE/ERROR` 是等待用户态 `DQBUF` 取回。

### 2.2 subdev 绑定是所有路径的前置条件

CSI host 在 probe 时从设备树 OF graph 找远端 sensor，向 V4L2 async core 注册 notifier；OV5640 probe 完成并匹配后，`subdev_notifier_bound()` 保存 `csi_dev->sd`，同时通过 `v4l2_ctrl_add_handler()` 把 sensor control handler 聚合到 video node。只有这一步成功，后续 `v4l2_subdev_call(sd, ...)` 才有目标，video node 才能看到 OV5640 controls。

因此“节点存在”不等于“sensor 已绑定”。当 `--info` 能成功、但格式枚举为空或采集路径出现空 subdev 时，应先检查：

```bash
dmesg | grep -i -E 'ov5640|csi|Registered sensor subdevice|video'
v4l2-ctl --list-devices
```

### 2.3 三种容易混淆的状态

| 状态 | 所属层 | 含义 | 进入/退出 |
| --- | --- | --- | --- |
| `sensor->state.powered` | OV5640 runtime PM | sensor 可访问寄存器的电源引用状态 | video node `open/close` 通过 `.s_power` 管理 |
| `sensor->state.streaming` | OV5640 subdev | OV5640 是否正在输出像素 | host `STREAMON/OFF` 通过 `.s_stream` 管理 |
| `q->streaming` / `q->start_streaming_called` | VB2/CSI host | VB2 队列是否 streaming、host `.start_streaming` 是否成功 | `vb2_streamon/off` 和 `.start/.stop_streaming` 管理 |

本工程的启动顺序是 sensor `.s_stream(1)` 先执行，再进入 `vb2_streamon()` 和 CSI enable；停止顺序相反。不要把 sensor 已出流误认为 CSI DMA 已经启动，也不要把 `.s_power(1)` 误认为 sensor 已经开始输出像素。

### 2.4 进程上下文和 IRQ 上下文

这条路径还有一个不能忽略的并发边界：

| 上下文 | 代表代码 | 保护对象 | 不能做什么 |
| --- | --- | --- | --- |
| 进程上下文 | `open/close/ioctl/read/mmap`、sensor control/format | `csi_dev->lock`、`sensor->lock` | 不应在持 mutex 时等待硬件 IRQ 完成而造成锁反转 |
| 中断上下文 | `mx6s_csi_irq_handler()`、`mx6s_csi_frame_done()` | `csi_dev->slock`，使用 `spin_lock` 或 `spin_lock_irqsave` | 不能睡眠，不能执行可能阻塞的 I2C/runtime-PM 操作 |

host 的 `capture`、`active_bufs` 和 `discard` 链表由 IRQ 与用户态触发的 `QBUF/STREAMON/STREAMOFF` 共同访问，所以 `mx6s_videobuf_queue()` 和 `.start/.stop_streaming` 使用 `slock`；sensor 的寄存器和 `sensor->state` 则由 sensor mutex 串行化。调试“偶发丢帧、链表损坏或停流卡住”时，应先判断问题发生在哪个上下文和锁边界。

## 3. 典型流程总览

### 3.1 发现能力

```text
open
  -> VIDIOC_QUERYCAP
  -> VIDIOC_ENUMINPUT / VIDIOC_G_INPUT
  -> VIDIOC_ENUM_FMT
  -> VIDIOC_ENUM_FRAMESIZES
  -> VIDIOC_ENUM_FRAMEINTERVALS
  -> VIDIOC_QUERYCTRL / VIDIOC_G_CTRL
  -> close
```

主要用途：确认这个 video node 是否是采集设备、支持哪些格式/分辨率/帧率，以及 sensor 暴露了哪些 V4L2 control。

### 3.2 配置采集参数

```text
open
  -> VIDIOC_S_INPUT(0)
  -> VIDIOC_TRY_FMT
  -> VIDIOC_S_FMT
  -> VIDIOC_G_PARM
  -> VIDIOC_S_PARM
  -> 可选 VIDIOC_S_CTRL
```

主要用途：选择 camera input，协商 RGB565 + 分辨率，提交 active format，设置 15/30 fps 这类 sensor 支持的帧率组合，并按需设置翻转或防频闪。

### 3.3 MMAP 采集

```text
configure
  -> VIDIOC_REQBUFS(MMAP)
  -> VIDIOC_QUERYBUF
  -> mmap
  -> VIDIOC_QBUF 所有 buffer
  -> VIDIOC_STREAMON
  -> poll
  -> VIDIOC_DQBUF
  -> consume frame
  -> VIDIOC_QBUF recycle
  -> VIDIOC_STREAMOFF
  -> munmap
  -> VIDIOC_REQBUFS(count=0)
```

主要用途：经典 V4L2 streaming 流程。用户态先申请一组可 mmap 的 DMA buffer，然后反复 `DQBUF -> 使用帧 -> QBUF`，让驱动持续复用 buffer。

### 3.4 read 采集

```text
configure
  -> read(fd, frame, sizeimage)
```

主要用途：使用 VB2 file-io 简化采集。当前代码保留了 `mx6s_csi_fops.read -> mx6s_csi_read() -> vb2_read()` 入口，但 `mx6s_csi_open()` 只把 `q->io_modes` 配成 `VB2_MMAP | VB2_USERPTR`，没有声明 `VB2_READ`，`querycap` 也只声明 video capture + streaming。因此按当前源码理解，`read()` 是“有 file operation 入口、但 VB2 队列未启用 read file-io”的路径，实际验证时可能返回 `-EINVAL`/不作为主采集路径。

## 4. 设备生命周期和 file operations

| 用户态操作 | 驱动路径 | 主要工作 |
| --- | --- | --- |
| `open("/dev/videoX", O_RDWR)` | `mx6s_csi_fops.open -> mx6s_csi_open()` | 初始化本次打开的文件上下文；创建 `vb2_dma_contig` 分配上下文；配置并初始化 `vb2_queue`；打开 runtime PM；提高总线频率；调用 sensor `core.s_power(1)`；初始化 CSI host。 |
| `close(fd)` | `mx6s_csi_fops.release -> mx6s_csi_close()` | 释放 VB2 队列和残留 buffer；反初始化 CSI；调用 sensor `core.s_power(0)`；清理 DMA 分配上下文；释放总线频率请求；降低 runtime PM 使用计数。 |
| `ioctl(fd, VIDIOC_*, ...)` | `mx6s_csi_fops.unlocked_ioctl -> video_ioctl2() -> mx6s_csi_ioctl_ops.*` | V4L2 core 解析 ioctl 命令和参数结构，然后分发到当前 video node 注册的 `vidioc_*` 回调。 |
| `read(fd, buf, size)` | `mx6s_csi_fops.read -> mx6s_csi_read() -> vb2_read()` | VB2 file-io helper 会尝试在内部走 `REQBUFS/QBUF/STREAMON/DQBUF` 兼容层；但当前 `q->io_modes` 未启用 `VB2_READ`，所以这不是当前可靠主路径。 |
| `poll(fd, ...)` | `mx6s_csi_fops.poll -> vb2_fop_poll() -> vb2_poll()` | 等待 VB2 done 队列出现已完成 buffer。只有队列已经 streaming、且不处于“还没 QBUF 等 buffer”状态时才正常返回可读；否则可能返回 `POLLERR`。 |
| `mmap(fd, offset)` | `mx6s_csi_fops.mmap -> mx6s_csi_mmap() -> vb2_mmap()` | 根据 `VIDIOC_QUERYBUF` 返回的 offset，把内核分配的 MMAP buffer 映射到用户进程地址空间。 |

## 5. 能力、输入源和拓扑发现

| 用户态 ioctl | 驱动路径 | 主要工作 |
| --- | --- | --- |
| `VIDIOC_QUERYCAP` | `mx6s_vidioc_querycap()` | 返回 driver/card/bus 信息和能力位。当前重点能力是 video capture + streaming。 |
| `VIDIOC_ENUMINPUT` | `mx6s_vidioc_enum_input()` | 枚举输入源。当前 host 只暴露一个 camera input，合法 index 为 `0`。 |
| `VIDIOC_G_INPUT` | `mx6s_vidioc_g_input()` | 返回当前输入源。当前固定为 `0`。 |
| `VIDIOC_S_INPUT` | `mx6s_vidioc_s_input()` | 选择输入源。当前只接受 `0`，更像参数合法性检查。 |

速查理解：

- 这一步还没有配置 sensor 寄存器，也没有启动 CSI DMA。
- 它的作用是确认 `/dev/videoX` 是不是目标采集节点，以及 host 是否接受 input 选择。

## 6. 格式、分辨率和帧率枚举

| 用户态 ioctl | 驱动路径 | 主要工作 |
| --- | --- | --- |
| `VIDIOC_ENUM_FMT` | `mx6s_vidioc_enum_fmt_vid_cap() -> sensor video.enum_mbus_fmt -> ov5640_enum_fmt()` | sensor 先枚举 media-bus code，host 再映射成 V4L2 fourcc。当前 RGB565 路径会把 `MEDIA_BUS_FMT_RGB565_2X8_LE` 映射到 `V4L2_PIX_FMT_RGB565`。 |
| `VIDIOC_ENUM_FRAMESIZES` | `mx6s_vidioc_enum_framesizes() -> sensor pad.enum_frame_size -> ov5640_enum_framesizes()` | 按 pixel format 查询 sensor 支持的离散分辨率。host 先用 fourcc 找到对应 mbus code，再转发给 sensor。 |
| `VIDIOC_ENUM_FRAMEINTERVALS` | `mx6s_vidioc_enum_frameintervals() -> sensor pad.enum_frame_interval -> ov5640_enum_frameintervals()` | 按 pixel format + width + height 查询支持的帧间隔。当前 OV5640 路径主要支持离散 15 fps 和 30 fps 组合。 |

速查理解：

- `ENUM_*` 是发现能力，不应该改变 active format，也不应该写 sensor streaming 寄存器。
- host video node 面向用户态使用 fourcc；sensor subdev 内部更多使用 media-bus code。读代码时要把这两种格式概念分开。

### 6.1 当前 host 的 fourcc 到 mbus 映射

`mx6s_capture.c` 的 [`formats[]`](../mx6s_capture.c#L318) 是 host 对外能力和 sensor 输入 code 之间的桥。当前主要映射为：

| 用户态 fourcc | sensor media-bus code | host `bytesperline` 计算 | CSI 路径要点 |
| --- | --- | --- | --- |
| `RGBP` / `V4L2_PIX_FMT_RGB565` | `MEDIA_BUS_FMT_RGB565_2X8_LE` | `2 * width` | parallel 8-bit 输入按两个 byte cycle 配置宽度 |
| `UYVY` | `MEDIA_BUS_FMT_UYVY8_2X8` | `2 * width` | parallel 和 MIPI 的 CSI 配置分支不同 |
| `YUYV` | `MEDIA_BUS_FMT_YUYV8_2X8` | `2 * width` | 同上 |
| `GREY` | `MEDIA_BUS_FMT_Y8_1X8` | `1 * width` | MIPI 时选择 RAW8 data type |
| `YUV32` | `MEDIA_BUS_FMT_AYUV8_1X32` | `4 * width` | host 表存在，实际能力仍以 sensor 枚举为准 |
| `JPEG` | `MEDIA_BUS_FMT_JPEG_1X8` | `0` | `sizeimage = width * height * 2` 固定外壳；当前 `mx6s_configure_csi()` 拒绝 MIPI JPEG |

这张表解释了一个常见误区：用户态看到的 `RGBP` 并不意味着 sensor 也使用 FourCC；sensor 通过 mbus code 协商，host 再根据选中的 format 计算 buffer 大小并配置 CSI。`S_FMT` 成功后务必使用驱动回写的 `pixelformat/width/height/bytesperline/sizeimage`。

## 7. 常用 V4L2 结构体和字段

`VIDIOC_*` 的第三个参数不是任意的用户态数据，而是一个与 ioctl 命令严格匹配的 V4L2 UAPI 结构体指针。例如 `VIDIOC_QUERYCAP` 必须传入 `struct v4l2_capability *`，`VIDIOC_S_FMT` 必须传入 `struct v4l2_format *`。这些结构体既有“用户态先填写、驱动读取”的字段，也有“驱动回写、用户态读取”的字段；理解这个输入/输出方向，比只记字段名称更重要。

本项目的用户态入口主要是 [`ov5640_interface_demo.c`](../ov5640_interface_demo.c)；早期的 [`ov5640_test.c`](../ov5640_test.c) 也使用了同一套单平面 capture API。内核侧 UAPI 定义见 [`videodev2.h`](../../linux-friedegg/include/uapi/linux/videodev2.h)。

### 7.1 结构体与 ioctl 对照

| 结构体 | 主要 ioctl | 一句话作用 | 当前路径中的角色 |
| --- | --- | --- | --- |
| `struct v4l2_capability` | `VIDIOC_QUERYCAP` | 查询 video node 的身份和能力 | 确认 capture/streaming 能力 |
| `struct v4l2_input` | `VIDIOC_ENUMINPUT/G_INPUT/S_INPUT` | 描述和选择输入源 | 当前 host 只接受 input `0` |
| `struct v4l2_fmtdesc` | `VIDIOC_ENUM_FMT` | 枚举支持的像素格式 | 用户态看到 fourcc，如 `RGB3`、`JPEG` |
| `struct v4l2_frmsizeenum` | `VIDIOC_ENUM_FRAMESIZES` | 枚举某个格式支持的尺寸 | 当前主要返回离散尺寸 |
| `struct v4l2_frmivalenum` | `VIDIOC_ENUM_FRAMEINTERVALS` | 枚举某个格式/尺寸支持的帧间隔 | 当前主要返回 15/30 fps |
| `struct v4l2_format` | `VIDIOC_TRY_FMT/G_FMT/S_FMT` | 试算、读取或提交当前格式 | `fmt.pix` 描述单平面 capture |
| `struct v4l2_pix_format` | 包含于 `v4l2_format` | 描述宽高、fourcc、stride、帧大小 | host/sensor 协商后的 active format |
| `struct v4l2_streamparm` | `VIDIOC_G_PARM/S_PARM` | 查询或设置流参数 | `parm.capture.timeperframe` 表示帧间隔 |
| `struct v4l2_requestbuffers` | `VIDIOC_REQBUFS` | 请求创建、切换或释放 buffer 队列 | 选择 MMAP 或 USERPTR |
| `struct v4l2_buffer` | `VIDIOC_QUERYBUF/QBUF/DQBUF` | 描述一个具体 buffer 及其状态 | 连接用户态 buffer 和 VB2 buffer |
| `struct v4l2_plane` | 多平面 `v4l2_buffer` 内 | 描述一个 plane | 当前单平面路径不使用 |
| `struct v4l2_control` | `VIDIOC_G_CTRL/S_CTRL` | 读取或设置一个 control | 当前可用于 HFLIP/VFLIP 等 |

当前 video node 使用的是 `V4L2_BUF_TYPE_VIDEO_CAPTURE`，即“单平面视频采集”。因此格式使用 `fmt.fmt.pix`，buffer 使用 `buf.m.offset` 或 `buf.m.userptr`。不要把它与 `V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE` 的 `fmt.fmt.pix_mp`、`buf.m.planes` 混用。

### 7.2 `struct v4l2_capability`

调用 `VIDIOC_QUERYCAP` 后，驱动填充设备描述和能力位。定义见 [`videodev2.h:302`](../../linux-friedegg/include/uapi/linux/videodev2.h#L302)。

| 字段 | 含义 | 读写方向和当前关注点 |
| --- | --- | --- |
| `driver[16]` | 驱动模块名称 | 驱动回写，例如用于确认是否命中 `mx6s_capture` |
| `card[32]` | 设备或 video card 的人类可读名称 | 驱动回写，便于日志识别 |
| `bus_info[32]` | 总线或设备连接信息 | 驱动回写，OV5640 通常位于 I2C + CSI 拓扑 |
| `version` | 驱动版本编码 | 驱动回写，不能直接当作字符串打印 |
| `capabilities` | 物理设备整体能力位图 | 检查 `V4L2_CAP_VIDEO_CAPTURE` 和 `V4L2_CAP_STREAMING` |
| `device_caps` | 当前 node 的能力位图 | 只有 `V4L2_CAP_DEVICE_CAPS` 置位时才按该字段判断 |
| `reserved[3]` | 未来扩展保留区 | 初始化为 0，不要写入自定义数据 |

`capabilities` 和 `device_caps` 都是 bitmask，判断能力必须使用按位与，而不是相等比较：

```c
if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
    /* 这个 node 支持视频采集 */
}
```

`V4L2_CAP_VIDEO_CAPTURE` 只说明方向是 capture；本项目后续使用 `REQBUFS/QBUF/DQBUF/STREAMON`，所以还应确认 `V4L2_CAP_STREAMING`。如果设备同时设置了 `V4L2_CAP_DEVICE_CAPS`，实际 node 能力应使用 `device_caps`，否则使用 `capabilities`。

### 7.3 `struct v4l2_fmtdesc`

`VIDIOC_ENUM_FMT` 每成功调用一次就返回一个格式。定义见 [`videodev2.h:549`](../../linux-friedegg/include/uapi/linux/videodev2.h#L549)。用户态通常只需先填写 `index` 和 `type`，其余字段由驱动回写。

| 字段 | 含义 |
| --- | --- |
| `index` | 格式序号，从 `0` 开始；每次成功后递增 |
| `type` | 要枚举的队列类型，本项目为 `V4L2_BUF_TYPE_VIDEO_CAPTURE` |
| `flags` | 格式属性；`V4L2_FMT_FLAG_COMPRESSED` 表示压缩，`V4L2_FMT_FLAG_EMULATED` 表示模拟格式 |
| `description[32]` | 人类可读描述，例如 `RGB565`、`JPEG` |
| `pixelformat` | 机器识别的 FourCC，例如 `V4L2_PIX_FMT_RGB565`、`V4L2_PIX_FMT_JPEG` |
| `reserved[4]` | 保留区，置 0 |

`description` 只适合显示，后续 `ENUM_FRAMESIZES`、`TRY_FMT`、`S_FMT` 都应使用 `pixelformat`。FourCC 是 32 位整数，日志中直接打印十六进制不如转换成四字符直观。当前 JPEG 路径由 host 对外映射为 `V4L2_PIX_FMT_JPEG`，sensor 内部仍可能使用不同的 media-bus code。

### 7.4 `struct v4l2_frmsizeenum` 和 `struct v4l2_frmivalenum`

这两个结构体分别回答“支持哪些尺寸”和“这个尺寸支持多快”。定义见 [`videodev2.h:586`](../../linux-friedegg/include/uapi/linux/videodev2.h#L586) 和 [`videodev2.h:614`](../../linux-friedegg/include/uapi/linux/videodev2.h#L614)。

#### 7.4.1 `v4l2_frmsizeenum`

| 字段 | 含义 |
| --- | --- |
| `index` | 尺寸序号，从 `0` 开始 |
| `pixel_format` | 查询对象，例如 `V4L2_PIX_FMT_RGB565` |
| `type` | `DISCRETE`、`CONTINUOUS` 或 `STEPWISE` |
| `discrete.width/height` | `DISCRETE` 类型下的一组固定宽高 |
| `stepwise.min_width/max_width/step_width` | `STEPWISE` 类型下的宽度范围和步长 |
| `stepwise.min_height/max_height/step_height` | `STEPWISE` 类型下的高度范围和步长 |
| `reserved[2]` | 保留区，置 0 |

当前 OV5640 mode 表以固定尺寸为主，demo 读取 `discrete.width/height`。不过 host 的 `mx6s_vidioc_enum_framesizes()` 在 sensor 返回范围时仍把 `type` 写成 `V4L2_FRMSIZE_TYPE_DISCRETE`，并填充 `stepwise` union，这属于当前驱动实现细节；通用程序应以 `type` 为准，不要盲目读取另一个 union 成员。

#### 7.4.2 `v4l2_frmivalenum`

| 字段 | 含义 |
| --- | --- |
| `index` | 帧间隔序号，从 `0` 开始 |
| `pixel_format` | 查询对象的像素格式 |
| `width/height` | 查询对象的分辨率，必须与已枚举尺寸匹配 |
| `type` | 帧间隔的 `DISCRETE`、`CONTINUOUS` 或 `STEPWISE` 类型 |
| `discrete.numerator/denominator` | 单个固定帧间隔，单位为秒 |
| `stepwise.min/max/step` | 可变帧间隔的范围和步进；每个成员都是 `struct v4l2_fract` |
| `reserved[2]` | 保留区，置 0 |

帧率和帧间隔互为倒数：

```text
timeperframe = numerator / denominator 秒
fps = denominator / numerator
```

所以 `1/30` 表示 30 fps，而不是 1 fps。当前驱动重点支持 15 fps 和 30 fps 离散组合；实际可用值必须以指定 fourcc、宽度和高度下的枚举结果为准。

### 7.5 `struct v4l2_format` 和 `struct v4l2_pix_format`

`v4l2_format` 是格式 ioctl 的外层容器，定义见 [`videodev2.h:1895`](../../linux-friedegg/include/uapi/linux/videodev2.h#L1895)；单平面 capture 使用其内部的 `fmt.pix`，具体字段定义见 [`videodev2.h:361`](../../linux-friedegg/include/uapi/linux/videodev2.h#L361)。

| 字段 | 含义 | 本项目使用方式 |
| --- | --- | --- |
| `v4l2_format.type` | 格式所属队列类型 | 填 `V4L2_BUF_TYPE_VIDEO_CAPTURE` |
| `fmt.pix.width` | 图像宽度，像素数 | `TRY_FMT/S_FMT` 前请求，ioctl 后读取实际值 |
| `fmt.pix.height` | 图像高度，像素数 | 同上；驱动可能规整到最接近支持值 |
| `fmt.pix.pixelformat` | 图像 FourCC | 例如 `RGB565` 或 `JPEG` |
| `fmt.pix.field` | 逐行/隔行扫描方式 | 摄像头通常是 `V4L2_FIELD_NONE` |
| `fmt.pix.bytesperline` | 一行占用字节数，可能包含对齐 padding | 用户态处理每行时优先使用该值 |
| `fmt.pix.sizeimage` | 一帧 buffer 所需大小或最大大小 | `REQBUFS`、`read()` 和用户态保存帧时的重要依据 |
| `fmt.pix.colorspace` | 色彩空间 | 描述颜色解释方式 |
| `fmt.pix.priv` | 像素格式相关私有字段 | 通常保持驱动返回值，不自行猜测 |
| `fmt.pix.flags` | 格式标志 | 例如 premultiplied alpha，按格式定义解释 |
| `fmt.pix.ycbcr_enc` | Y'CbCr 编码 | RGB565 路径通常不关注 |
| `fmt.pix.quantization` | 量化范围，full/limited 等 | YUV 路径更重要 |

`TRY_FMT` 只试算和规整，不提交 active format；`S_FMT` 才提交。两者都可能修改用户请求，因此 ioctl 成功后必须使用驱动回写的 `width/height/pixelformat/bytesperline/sizeimage`，而不是继续使用原始请求值。当前文档中的 `G_FMT` 也用于确认这些实际值。

特别注意 `bytesperline` 和 `sizeimage`：RGB565 在无 padding 时通常约为 `width * 2`，一帧约为 `bytesperline * height`；但 DMA 对齐、压缩格式和驱动固定 buffer 外壳都会使简单计算失效。用户态不应只用 `width * height * 2` 代替驱动返回的 `sizeimage`。

### 7.6 `struct v4l2_streamparm`、`v4l2_captureparm` 和 `v4l2_fract`

定义见 [`videodev2.h:897`](../../linux-friedegg/include/uapi/linux/videodev2.h#L897) 和 [`videodev2.h:1920`](../../linux-friedegg/include/uapi/linux/videodev2.h#L1920)。capture 场景下使用：

```text
streamparm.type
streamparm.parm.capture
streamparm.parm.capture.timeperframe
```

| 字段 | 含义 |
| --- | --- |
| `streamparm.type` | 流类型，本项目为 `VIDEO_CAPTURE` |
| `capture.capability` | 设备支持的采集参数能力；检查 `V4L2_CAP_TIMEPERFRAME` |
| `capture.capturemode` | 当前采集模式，少数驱动用于 high-quality 等扩展 |
| `capture.timeperframe` | 每帧间隔，`numerator/denominator` 秒 |
| `capture.extendedmode` | 驱动私有扩展，通常保持 0 |
| `capture.readbuffers` | read I/O 模式使用的 buffer 数量 |
| `reserved[4]` | 保留区，置 0 |

典型顺序是先 `G_PARM`，确认 `capture.capability & V4L2_CAP_TIMEPERFRAME`，再填写 `timeperframe = 1/30` 调 `S_PARM`。驱动可能将请求规整为实际支持的 15/30 fps，因此 `S_PARM` 后仍应读取回写结果。这个参数是在已经选定的格式/分辨率上协商帧率，不替代 `ENUM_FRAMEINTERVALS` 的能力发现。

### 7.7 `struct v4l2_requestbuffers`

定义见 [`videodev2.h:690`](../../linux-friedegg/include/uapi/linux/videodev2.h#L690)，对应 `VIDIOC_REQBUFS`。

| 字段 | 含义 | 本项目要点 |
| --- | --- | --- |
| `count` | 请求或返回的 buffer 数量 | 输入是请求值，成功后应使用驱动回写的实际值 |
| `type` | buffer 队列类型 | 填 `V4L2_BUF_TYPE_VIDEO_CAPTURE` |
| `memory` | buffer 内存模型 | 当前主路径是 `V4L2_MEMORY_MMAP`，也验证 `USERPTR` |
| `reserved[2]` | 保留区 | 置 0 |

`count=0` 通常表示释放该 memory 类型的 buffer。`count` 不是“必定得到的数量”：驱动可因内存不足或队列限制返回不同数量，用户态必须按回写的 `req.count` 分配自己的映射数组并循环 `QUERYBUF`，不能无条件使用请求值。

常用 memory 模型：

- `V4L2_MEMORY_MMAP`：驱动/VB2 分配 buffer，用户态通过 `QUERYBUF` 得到 offset 后 `mmap()`。
- `V4L2_MEMORY_USERPTR`：用户态提供 `buf.m.userptr` 和 `buf.length`，VB2 在 `QBUF` 时 pin/map 用户页。
- `V4L2_MEMORY_DMABUF`：用户态通过 `buf.m.fd` 导入外部 DMA-BUF；当前队列没有声明为主能力。

### 7.8 `struct v4l2_buffer` 和 `struct v4l2_plane`

`v4l2_buffer` 定义见 [`videodev2.h:730`](../../linux-friedegg/include/uapi/linux/videodev2.h#L730)，是 `QUERYBUF/QBUF/DQBUF` 交换单个 buffer 信息的结构体。当前使用单平面 capture。

| 字段 | 含义 | 当前 MMAP capture 的意义 |
| --- | --- | --- |
| `index` | buffer 编号 | `QUERYBUF` 遍历 0..count-1；`DQBUF` 返回已完成 buffer 的编号 |
| `type` | 队列类型 | 必须与 `REQBUFS` 一致 |
| `bytesused` | 实际有效 payload 字节数 | `DQBUF` 后用于判断当前帧有效长度，JPEG 尤其重要 |
| `flags` | buffer 状态/时间戳等标志 | 可检查 `DONE`、`ERROR`、timestamp 类型 |
| `field` | 当前帧的 field 类型 | 通常由驱动回写 |
| `timestamp` | 帧时间戳 | `DQBUF` 后由驱动/VB2 回写 |
| `timecode` | SMPTE 等时间码 | 普通 OV5640 采集通常不用 |
| `sequence` | 帧序号 | 可用于检测丢帧或乱序 |
| `memory` | 本次 buffer 的 memory 模型 | 必须与 `REQBUFS.memory` 一致 |
| `m.offset` | MMAP 的设备内存 offset/cookie | `QUERYBUF` 后传给 `mmap()`，不是通用的物理地址 |
| `m.userptr` | USERPTR 的用户虚拟地址 | 仅 USERPTR 有效 |
| `m.fd` | DMABUF 文件描述符 | 仅 DMABUF 有效 |
| `m.planes` | 多平面 plane 数组指针 | 仅 `*_MPLANE` 类型有效 |
| `length` | 单平面 buffer 总容量 | `QUERYBUF` 后作为 `mmap()` 长度；不是 `bytesused` |

MMAP 的最小操作关系是：

```text
REQBUFS(count, MMAP)
  -> QUERYBUF(index) 得到 length + m.offset
  -> mmap(m.offset, length)
  -> QBUF(index)
  -> STREAMON
  -> DQBUF 得到 index + bytesused + timestamp + sequence
  -> 使用映射数组[index]中的数据
  -> QBUF(index) 重新交给驱动
```

这里有三个经常混淆的“长度/位置”字段：

- `length`：整块 buffer 容量，通常用于 `mmap()`；
- `bytesused`：本次已完成帧实际使用的 payload 长度；
- `m.offset`：MMAP 映射入口的 offset/cookie，不能按通用 V4L2 规则解释成物理地址。

`v4l2_plane` 只在多平面 API 中通过 `buf.m.planes` 使用。单平面 RGB565、YUYV、JPEG 采集使用 `v4l2_buffer.m.offset`；多平面 NV12M 等格式才需要分别管理 Y plane 和 UV plane 的 `bytesused/length/m.mem_offset` 等字段。

### 7.9 `struct v4l2_control`

定义见 [`videodev2.h:1364`](../../linux-friedegg/include/uapi/linux/videodev2.h#L1364)，对应 `VIDIOC_G_CTRL/S_CTRL`。

| 字段 | 含义 |
| --- | --- |
| `id` | control ID，例如 `V4L2_CID_HFLIP` |
| `value` | 控件值；设置时由用户态填写，读取时由驱动回写 |

当前 demo 的 `HFLIP/VFLIP/POWER_LINE_FREQUENCY` 会经过 V4L2 control core 进入 OV5640 的 control 回调。自动对焦和触摸区域同样使用 `id/value` 传递控制值；自定义 control 的合法范围、步进和单位必须以驱动注册的 control 定义为准，不能只根据整数类型猜测。

## 8. 格式和 stream 参数配置

| 用户态 ioctl | 驱动路径 | 主要工作 |
| --- | --- | --- |
| `VIDIOC_TRY_FMT` | `mx6s_vidioc_try_fmt_vid_cap() -> mx6s_negotiate_format(apply=false) -> sensor video.try_mbus_fmt -> ov5640_try_fmt()` | 试算并规整用户请求的格式。返回驱动实际能接受的 width/height/pixelformat/bytesperline/sizeimage，但不提交为 active state。 |
| `VIDIOC_S_FMT` | `mx6s_vidioc_s_fmt_vid_cap() -> mx6s_negotiate_format(apply=true) -> sensor video.s_mbus_fmt -> ov5640_s_fmt() -> mx6s_configure_csi()` | 提交 active format。sensor 侧选择模式并应用格式/控制项；host 侧配置 CSI 接收格式和 DMA 相关参数。sensor 已 streaming 时 `ov5640_s_fmt()` 返回 `-EBUSY`，所以应在 `REQBUFS/STREAMON` 前完成。 |
| `VIDIOC_G_FMT` | `mx6s_vidioc_g_fmt_vid_cap()` | 返回当前 active video capture format，用户态据此确认 `sizeimage`、`bytesperline` 等 buffer 计算字段。 |
| `VIDIOC_G_PARM` | `mx6s_vidioc_g_parm() -> sensor video.g_parm -> ov5640_g_parm()` | 读取当前 stream 参数，重点是 `timeperframe`。 |
| `VIDIOC_S_PARM` | `mx6s_vidioc_s_parm() -> sensor video.s_parm -> ov5640_s_parm()` | 设置帧率请求。当前 sensor 会按已支持的 size/fps 组合校验，常见目标是 15 fps 或 30 fps；如果正在 streaming 且要改变帧率，会返回 `-EBUSY`。 |

速查理解：

- `TRY_FMT` 是“问这样行不行，帮我规整一下”，不应产生硬件提交。
- `S_FMT` 是“把这个格式真正作为当前采集格式”，会影响后续 buffer 大小和 CSI 配置；它属于启动前配置，不是运行中随意切换的接口。
- `S_PARM` 设置的是时间参数，常见字段是 `struct v4l2_streamparm.parm.capture.timeperframe`；运行中改变帧率会被 sensor 侧拒绝。

## 9. V4L2 controls

| 用户态 ioctl | 驱动路径 | 主要工作 |
| --- | --- | --- |
| `VIDIOC_QUERYCTRL` | `video_ioctl2() -> V4L2 control core -> OV5640 ctrl_handler` | 查询 control 是否存在、名字、类型、最小值、最大值、步进和默认值。 |
| `VIDIOC_QUERYMENU` | `video_ioctl2() -> V4L2 control core` | 对 menu 类型 control 枚举可选项，例如电源线频率。 |
| `VIDIOC_G_CTRL` | `video_ioctl2() -> V4L2 control core` | 读取当前 control 值。 |
| `VIDIOC_S_CTRL` | `video_ioctl2() -> V4L2 control core -> ov5640_s_ctrl()` | 写入 control。当前 `HFLIP/VFLIP` 进入 `ov5640_set_flip()`，`POWER_LINE_FREQUENCY` 进入 `ov5640_set_power_line_frequency()`。 |

当前 demo 关心的 control：

- `V4L2_CID_HFLIP`
- `V4L2_CID_VFLIP`
- `V4L2_CID_POWER_LINE_FREQUENCY`

速查理解：

- control 不是 host `mx6s_vidioc_*` 手写逐个处理，而是由 V4L2 control core 通过 `ctrl_handler` 聚合处理。
- 翻转和防频闪最终是 sensor 寄存器行为，不是 CSI host DMA 行为。

## 10. Streaming buffer API

| 用户态 ioctl | 驱动路径 | 主要工作 |
| --- | --- | --- |
| `VIDIOC_REQBUFS` | `mx6s_vidioc_reqbufs() -> vb2_reqbufs() -> mx6s_videobuf_setup()` | 请求分配或切换 buffer 队列。MMAP 下由 VB2 分配 DMA buffer；USERPTR 下声明后续会传用户指针。`count=0` 用于释放队列；如果正在 streaming，VB2 会拒绝重新分配。 |
| `VIDIOC_CREATE_BUFS` | `vb2_ioctl_create_bufs() -> vb2_create_bufs() -> mx6s_videobuf_setup(fmt != NULL)` | 在已有队列基础上追加创建 buffer。当前 `.queue_setup` 会校验 create 请求的 type/pixelformat/width/height/sizeimage 必须与当前 active format 兼容。 |
| `VIDIOC_QUERYBUF` | `mx6s_vidioc_querybuf() -> vb2_querybuf()` | 按 index 查询 buffer 长度、offset、flags 等。MMAP 用户态随后用这个 offset 调 `mmap()`。当前驱动有旧兼容行为：buffer 已 mmap 时会把 `m.offset` 改成 DMA 物理地址。 |
| `VIDIOC_QBUF` | `mx6s_vidioc_qbuf() -> vb2_qbuf() -> vb2_queue 状态机` | 把一个空 buffer 的所有权交给 VB2。是否立刻调用 `.buf_queue -> mx6s_videobuf_queue()` 取决于 `q->streaming/start_streaming_called`，不是固定无条件路径。 |
| `VIDIOC_DQBUF` | `mx6s_vidioc_dqbuf() -> vb2_dqbuf() -> done_list` | 从 VB2 done 队列取回一帧。这个 buffer 必须已经由 CSI IRQ 或 stop/error 路径调用 `vb2_buffer_done()` 标记为 `DONE/ERROR`。无完成帧时，阻塞行为由 `O_NONBLOCK` 决定。 |

`VIDIOC_CREATE_BUFS` 没有单独的 host 实现，而是由 ioctl 表直接挂到 `vb2_ioctl_create_bufs`；它仍会进入本驱动的 `mx6s_videobuf_setup(fmt != NULL)`。因此追加 buffer 时，`type/pixelformat/width/height` 必须和当前 active format 一致，`sizeimage` 不能小于当前值；它不是切换格式或扩大单帧 payload 的接口。

`q->io_modes` 是 VB2 队列的“能力声明”，当前 `mx6s_csi_open()` 配置为 `VB2_MMAP | VB2_USERPTR`。用户态在 `VIDIOC_REQBUFS` 中传入的 `struct v4l2_requestbuffers.memory` 才是本次队列实际选择的 memory 类型，VB2 会据此更新 `q->memory` 并检查当前 `io_modes` 和 `mem_ops` 是否支持。

`QBUF` 的状态差异：

- STREAMON 前：`vb2_qbuf()` 校验 type/index/memory，必要时调用 `mx6s_videobuf_prepare()` 设置 payload，然后把 buffer 标成 `VB2_BUF_STATE_QUEUED` 并放进 VB2 `queued_list`。此时 `q->start_streaming_called == 0`，通常还不会进入 `mx6s_videobuf_queue()`，CSI `capture` 链表也未必拿到这个 buffer。
- STREAMON 执行时：`vb2_streamon()` 会把之前预先 QBUF 的 buffer 交给驱动，逐个进入 `.buf_queue -> mx6s_videobuf_queue()`；随后调用 `.start_streaming -> mx6s_start_streaming()`，当前驱动再从 `capture` 链表取前两个 buffer 装入 CSI FB1/FB2。
- STREAMING 运行中：`q->start_streaming_called == 1`，新的 `QBUF` 会在准备后立即 `__enqueue_in_driver()`，状态进入 `ACTIVE`，并调用 `mx6s_videobuf_queue()` 追加到 CSI `capture` 等待队列。它仍然不一定马上写硬件寄存器，通常等下一次 IRQ frame_done 时被装入空出的 FB 槽。

`DQBUF` 的状态差异：

- STREAMON 前或 STREAMOFF 后：`q->streaming == 0` 时，`vb2_dqbuf()` 不会等待帧，通常返回 `-EINVAL`。
- STREAMING 中但还没有完成帧：非阻塞 fd 返回 `-EAGAIN`；阻塞 fd 在 `done_wq` 睡眠，直到 IRQ 完成、STREAMOFF 或队列错误。
- 有完成帧时：`vb2_dqbuf()` 从 `done_list` 取一个 `DONE/ERROR` buffer，填回 `struct v4l2_buffer` 的 index/bytesused/timestamp/sequence/flags，把它从 `queued_list` 删除并置回 `DEQUEUED`，所有权回到用户态。
- STREAMOFF 期间：VB2 会取消队列，当前驱动在 `.stop_streaming` 中把还在 `active_bufs/capture` 的 buffer 以 `ERROR` 归还；随后 VB2 清空 queued/done 列表并把所有 buffer 回到 `DEQUEUED`，所以 STREAMOFF 不是“继续 DQBUF 清库存”的流程。

速查理解：

- `QBUF` 后，buffer 所有权离开用户态；但它可能只是停在 VB2 `QUEUED`，也可能已经进入驱动 `ACTIVE`，取决于 streaming 状态。
- `DQBUF` 后，buffer 所有权回到用户态；用户态读完帧后再次 `QBUF`，才能让驱动复用这块 buffer。
- 当前驱动自身要求 `.start_streaming` 收到至少两个已交给驱动的 buffer，才能启动 CSI 双 buffer DMA；这个要求在 `mx6s_start_streaming()` 中检查，而不是通过 `q->min_buffers_needed` 配置给 VB2。

## 11. STREAMON / STREAMOFF

### 11.1 `VIDIOC_STREAMON`

调用链：

```text
VIDIOC_STREAMON
  -> video_ioctl2()
  -> mx6s_csi_ioctl_ops.vidioc_streamon
  -> mx6s_vidioc_streamon()
  -> v4l2_subdev_call(sd, video, s_stream, 1)
  -> ov5640_s_stream()
  -> vb2_streamon()
  -> vb2_ops.start_streaming
  -> mx6s_start_streaming()
  -> mx6s_csi_enable()
```

主要工作：

- host video-node 层先处理用户态 `VIDIOC_STREAMON`。
- host 调 sensor subdev `.s_stream(1)`，让 OV5640 开始输出像素流和同步信号。
- host 再进入 VB2 `streamon`；如果 `queued_count` 已满足启动条件，VB2 会把已 QBUF 的 buffer 交给驱动 `.buf_queue`，并尝试调用 `.start_streaming`。
- VB2 调 `.start_streaming` 后，`q->start_streaming_called` 才代表 host DMA 启动成功；当前实现会准备 CSI 初始 DMA buffer、检查 buffer 数量、启动 CSI host。
- 如果后续 CSI IRQ 报告某个 FB 槽完成，驱动会调用 `vb2_buffer_done()`，用户态的 `poll/DQBUF/read` 才能拿到帧。

注意点：

- 用户态看到的是 `VIDIOC_STREAMON`；sensor 里的 `.s_stream` 是 host 在内部调用的 subdev 回调，不是用户直接调的 `/dev/videoX` ioctl。
- 当前顺序是先启动 sensor，再启动 CSI host。如果 `vb2_streamon()` 或 `.start_streaming` 失败，host 回调会关闭 sensor 作为回滚；VB2 也会取消队列并要求驱动归还已接管的 buffer。

### 11.2 `VIDIOC_STREAMOFF`

调用链：

```text
VIDIOC_STREAMOFF
  -> video_ioctl2()
  -> mx6s_csi_ioctl_ops.vidioc_streamoff
  -> mx6s_vidioc_streamoff()
  -> vb2_streamoff()
  -> vb2_ops.stop_streaming
  -> mx6s_stop_streaming()
  -> v4l2_subdev_call(sd, video, s_stream, 0)
  -> ov5640_s_stream()
```

主要工作：

- VB2 先进入队列 cancel 路径：如果 `q->start_streaming_called` 为真，调用 `.stop_streaming`；随后清 `q->streaming/start_streaming_called/queued_count`。
- `mx6s_stop_streaming()` 关闭 CSI host，并把驱动还持有的 `active_bufs/capture` buffer 用 `VB2_BUF_STATE_ERROR` 归还给 VB2。
- host 再通知 sensor `.s_stream(0)` 停止输出像素流。

注意点：

- `STREAMOFF` 后，旧的 queued/active/done buffer 都会被归还或取消，VB2 把所有 buffer 置回 `DEQUEUED`。下一次重新采集通常要重新 `QBUF`。
- VB2 要求驱动在 `.stop_streaming` 返回前，归还所有已经从 `.buf_queue` 接管的 buffer。

## 12. IRQ 完成和 DQBUF 的关系

采集中断路径：

```text
CSI DMA done IRQ
  -> mx6s_csi_irq_handler()
  -> mx6s_csi_frame_done()
  -> vb2_buffer_done(VB2_BUF_STATE_DONE 或 ERROR)
  -> 唤醒 poll / DQBUF / read 等待者
```

速查理解：

- `DQBUF` 不负责“采一帧”，它只是取回已经完成的 buffer。
- 真正把一帧变成可取状态的是 CSI IRQ 完成路径中的 `vb2_buffer_done()`。
- `poll()` 等的是同一个完成条件：VB2 done 队列里出现 buffer；如果还没 streaming、队列错误，或 capture 队列尚未 QBUF，VB2 poll 会按错误/不可读处理。

## 13. VB2 io_modes 与 buffer memory 模型

`enum vb2_io_modes` 描述一个 `vb2_queue` 支持哪些访问方式。它不是单个 buffer 的状态，也不是硬件 DMA 模式；它决定用户态可以选择哪种 V4L2 buffer API，以及 VB2 要检查哪组 `mem_ops` 或 file-io helper。

| VB2 io mode | 用户态接口 | buffer 来自哪里 | VB2/驱动主要动作 | 当前驱动状态 | 典型场景 |
| --- | --- | --- | --- | --- | --- |
| `VB2_MMAP` | `REQBUFS(memory=V4L2_MEMORY_MMAP)`、`QUERYBUF`、`mmap()`、`QBUF/DQBUF` | VB2 allocator 分配，用户态 mmap 同一块 buffer | `REQBUFS` 时调用 `.queue_setup` 决定大小和 allocator，再由 `vb2_dma_contig_memops.alloc -> dma_alloc_coherent()` 分配连续 DMA buffer；`mmap()` 时映射给用户态；驱动用 `vb2_dma_contig_plane_dma_addr()` 取 DMA 地址写 CSI FB 寄存器 | 已启用，主路径 | 摄像头采集最常用。用户态零拷贝读帧，驱动掌握 DMA buffer 分配，适合 CSI 这类需要连续 DMA 地址的硬件。 |
| `VB2_USERPTR` | `REQBUFS(memory=V4L2_MEMORY_USERPTR)`、`QBUF(buf.m.userptr, buf.length)`、`DQBUF` | 用户态自己分配的一段虚拟地址空间 | `REQBUFS` 只建立队列语义；每次 `QBUF` 时 VB2 调 `mem_ops.get_userptr` pin/map 用户页，并检查这段内存能否满足 DMA 访问要求 | 已启用，可验证 | 兼容旧应用，或用户态已有固定缓冲区。对需要物理连续地址的硬件不如 MMAP 稳，用户内存不连续时可能失败。 |
| `VB2_DMABUF` | 外部 fd 通过 `QBUF(memory=V4L2_MEMORY_DMABUF, buf.m.fd)` 导入；也可用 `VIDIOC_EXPBUF` 导出 MMAP buffer | 其他设备或共享 allocator 导出的 `dma_buf` | VB2 调 `attach_dmabuf/map_dmabuf` 把外部 buffer 映射给当前设备；多个硬件模块可共享同一块帧内存 | 当前未启用 `VB2_DMABUF`，所以导入路径会被 VB2 拒绝；`vb2_dma_contig_memops` 自身有相关回调但队列能力未声明 | 摄像头到 GPU、显示、编码器、NPU 的零拷贝 pipeline。需要各设备 DMA 约束兼容。 |
| `VB2_READ` | `read(fd, user_buf, size)` | VB2 file-io helper 内部临时管理 buffer，最后 copy 到用户 buffer | VB2 在内部模拟 `REQBUFS/QBUF/STREAMON/DQBUF`，把完成帧拷贝给 `read()` 的用户指针 | 当前未在 `q->io_modes` 声明；虽然 fops 有 `.read`，但不是可靠主路径 | 快速抓一帧、简单调试工具。性能和控制能力不如 streaming API。 |
| `VB2_WRITE` | `write(fd, user_buf, size)` | 用户写入的数据经 VB2 file-io 送给输出队列 | 主要服务 video output 或 mem2mem 输出方向 | 当前 capture 驱动不使用 | 显示输出、编码输入、mem2mem output 队列。摄像头采集通常不用。 |

各模式与 `struct v4l2_buffer` 字段的关系：

- MMAP：`QUERYBUF/DQBUF` 返回 `buf.m.offset`；用户态用这个 offset 调 `mmap()`，后续用 `buf.index` 在自己的映射数组里找到帧数据。
- USERPTR：用户态在 `QBUF` 填 `buf.m.userptr` 和 `buf.length`；`DQBUF` 返回同一个 index/长度语义，但帧数据位置仍是用户传入的地址。
- DMABUF：用户态在 `QBUF` 填 `buf.m.fd`；buffer 内容通常由另一个设备或内存分配器拥有，当前 video node 只是导入并让硬件 DMA 访问。
- READ/WRITE：用户态不直接管理 `struct v4l2_buffer` 队列，VB2 file-io helper 在内核里代替用户态完成队列动作。

当前 OV5640 + CSI 路径优先使用 MMAP：`sizeimage` 来自 `VIDIOC_S_FMT` 后缓存的 `csi_dev->pix.sizeimage`，`REQBUFS(MMAP)` 让 VB2/dma-contig 分配 coherent DMA buffer，驱动在 `STREAMON` 和 IRQ 换帧路径中取 DMA 地址写入 `CSI_CSIDMASA_FB1/FB2`。USERPTR 可以作为学习和兼容路径验证；DMABUF/READ/WRITE 不是当前驱动声明的主能力。

USERPTR 与 MMAP 的关键差异是：`REQBUFS(USERPTR)` 不替用户分配帧内存，用户态在每次 `QBUF` 填入 `m.userptr` 和 `length`，VB2 再通过 `vb2_dma_contig_memops` 获取可供 CSI 使用的 DMA 映射。当前 demo 的 `--userptr` 会分配 `sizeimage` 大小的用户缓冲并走完整 `QBUF/STREAMON/DQBUF`；它适合验证内存映射兼容性，不代表当前主路径已支持 DMABUF 或 read file-io。

## 14. crop 和 legacy std 接口

| 用户态 ioctl | 当前状态 | 速查理解 |
| --- | --- | --- |
| `VIDIOC_CROPCAP` | host 有 placeholder | 做基本类型检查或返回占位信息，不真正配置 OV5640 crop。 |
| `VIDIOC_G_CROP` | host 有 placeholder | 当前不是可靠的 sensor crop 状态来源。 |
| `VIDIOC_S_CROP` | host 有 placeholder | 不会真正写 OV5640 crop 窗口。 |
| `VIDIOC_G_STD / VIDIOC_S_STD / VIDIOC_QUERYSTD` | host 尝试转发给 subdev | OV5640 当前没有安装这些 analog-TV standard 回调，通常应视为不适用。 |

## 15. debug-register API

`ov5640.c` 只在 `CONFIG_VIDEO_ADV_DEBUG` 下实现 subdev core `.g_register/.s_register`。当前 `mx6s_capture.c` 没有暴露匹配的 video-node ioctl，demo 也没有创建用户态 subdev node，所以 `ov5640_interface_demo.c` 不直接访问 OV5640 原始寄存器。

## 16. 每个 demo 选项对应的主要操作

| demo 选项 | 主要用户态操作 | 用途 |
| --- | --- | --- |
| `--list` | `QUERYCAP`、input 枚举、format/size/interval 枚举、control 查询 | 快速确认节点能力和 sensor 当前暴露的接口。 |
| `--configure` | `S_INPUT`、`TRY_FMT`、`S_FMT`、`G_PARM`、`S_PARM`、`G_FMT` | 设置采集格式和帧率，为后续 capture 做准备。 |
| `--mmap` | `REQBUFS`、`QUERYBUF`、`mmap`、`QBUF`、`STREAMON`、`poll`、`DQBUF`、`QBUF`、`STREAMOFF` | 标准 streaming capture 验证。 |
| `--userptr` | `REQBUFS(USERPTR)`、`QBUF(userptr)`、`STREAMON`、`poll`、`DQBUF`、`QBUF`、`STREAMOFF` | 验证用户态内存作为 DMA buffer 的路径。 |
| `--read` | `read(fd, frame, sizeimage)` | 尝试 VB2 file-io 简化路径；当前 `q->io_modes` 未启用 `VB2_READ`，应以板端返回值为准，不是主验证路径。 |
| `--create-bufs` | `VIDIOC_CREATE_BUFS`、`REQBUFS(count=0)` | 验证追加创建 buffer 的可选 API，同时验证 create 请求格式必须匹配当前 active format。 |
| `--hflip/--vflip/--power-line` | `QUERYCTRL`、`G_CTRL`、`S_CTRL` | 验证 OV5640 control handler 和对应 sensor 寄存器配置。 |
| `--std-crop` | `CROPCAP/G_CROP/S_CROP/G_STD/QUERYSTD` | 查看占位或 legacy 转发接口，不是当前 OV5640 采集主路径。 |

## 17. 读代码时的最短路线

1. 从 `ov5640_interface_demo.c` 找用户态调用，例如 `VIDIOC_QBUF`。
2. 到 `mx6s_capture.c` 的 `mx6s_csi_ioctl_ops` 找对应 `.vidioc_qbuf`。
3. 进入 `mx6s_vidioc_qbuf()` 看它调用哪个 V4L2/VB2 helper。
4. 如果进入 VB2，继续看 `vb2_ops mx6s_videobuf_ops` 对应的 `.buf_prepare/.buf_queue/.start_streaming/.stop_streaming`。
5. 如果进入 sensor，继续看 `ov5640_subdev_ops` 下的 `.video/.pad/.core` 回调。
6. 最后把 buffer 完成路径和 IRQ 关联起来：`mx6s_csi_irq_handler() -> mx6s_csi_frame_done() -> vb2_buffer_done()`。

## 18. 实际工具与最短验证

前面的流程也可以用 `v4l2-ctl` 直接触发。它操作的是 `/dev/videoX`，不会绕过 V4L2 core，也不会直接调用 OV5640 的 subdev 回调。

### 18.1 板端准备和基础命令

```bash
command -v v4l2-ctl
ls -l /dev/video*
dmesg | grep -i -E 'ov5640|csi|video'
v4l2-ctl --list-devices
```

后续命令假设目标节点为：

```bash
DEV=/dev/video1
```

确认能力和节点：

```bash
v4l2-ctl -d "$DEV" --info
v4l2-ctl -d "$DEV" --all
v4l2-ctl -d "$DEV" --list-inputs
v4l2-ctl -d "$DEV" --list-formats-ext
```

`--all` 会连续发出多个查询类 ioctl，不适合证明某个回调单独正确。定位问题时，应拆成更窄的命令；需要观察工具打印的 ioctl 过程时可加 `--verbose`。

### 18.2 v4l2-ctl 配置和采集

```bash
v4l2-ctl -d "$DEV" --set-fmt-video=width=800,height=480,pixelformat=RGBP
v4l2-ctl -d "$DEV" --get-fmt-video
v4l2-ctl -d "$DEV" --set-parm=30
v4l2-ctl -d "$DEV" --get-parm
v4l2-ctl -d "$DEV" --stream-mmap --stream-count=30
```

等价的一条命令如下，但调试时不如分步执行容易定位失败阶段：

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

RGB565 在无 padding 时一帧约为 `800 * 480 * 2 = 768000` 字节；实际保存长度应以 `VIDIOC_G_FMT` 返回的 `sizeimage` 为准。

## 19. v4l2-ctl controls 和回调

枚举和设置 controls：

```bash
v4l2-ctl -d "$DEV" --list-ctrls
v4l2-ctl -d "$DEV" --list-ctrls-menus
v4l2-ctl -d "$DEV" --set-ctrl=horizontal_flip=1
v4l2-ctl -d "$DEV" --set-ctrl=vertical_flip=0
v4l2-ctl -d "$DEV" --set-ctrl=power_line_frequency=1
v4l2-ctl -d "$DEV" --get-ctrl=horizontal_flip,vertical_flip,power_line_frequency
```

control 的典型分发路径是：

```text
VIDIOC_QUERYCTRL / VIDIOC_QUERYMENU
  -> video_ioctl2()
  -> V4L2 control core
  -> video_device.ctrl_handler
  -> aggregated sensor ctrl_handler

VIDIOC_S_CTRL / VIDIOC_S_EXT_CTRLS
  -> V4L2 control core
  -> ov5640_ctrl_ops.s_ctrl
  -> ov5640_s_ctrl()
```

当前 host 在 sensor async bound 时通过 [`v4l2_ctrl_add_handler()`](../mx6s_capture.c#L2561) 聚合 sensor controls，因此 video node 可以看到 [`ov5640_init_controls()`](../ov5640.c#L2915) 注册的 control。除翻转和防频闪外，当前 sensor 还注册了闪光灯、手动对焦、自动对焦状态和 touch AF 区域等 control，实际名称和合法范围以 `--list-ctrls-menus` 输出为准。

需要注意：未 powered 时，普通硬件 control 可能只更新缓存而不立即写寄存器，后续 power-on/init 时由 `ov5640_apply_controls()` 应用；`auto_focus_start` 要求 sensor powered，否则可能返回 `-EPIPE`；volatile control 读取时可能通过 `ov5640_g_volatile_ctrl()` 访问实际 sensor/AF 状态。

## 20. v4l2-ctl 矩阵测试和验证

当前包提供矩阵测试脚本：

```bash
test_v4l2_matrix.sh -d "$DEV"
test_v4l2_matrix.sh -d "$DEV" --list-only
test_v4l2_matrix.sh -d "$DEV" \
  --formats "RGBP UYVY YUYV GREY" \
  --sizes "800x480 640x480 320x240 1280x720" \
  --fps "15 30"
test_v4l2_matrix.sh -d "$DEV" --full
```

默认每个组合使用 `stream-count = 请求 fps * 2`，快速扫描可追加 `-c 10`。脚本开始前默认执行 `RGBP 800x480 30fps` warm-up，并对 stream 阶段允许一次重试；单次 stream 默认 15 秒超时。脚本对每个组合依次执行格式设置/读取、帧率设置/读取和 MMAP 采集。失败阶段可以按下表回到对应回调：

| 结果 | 主要怀疑路径 | 回调入口 |
| --- | --- | --- |
| `FAIL_FMT` | fourcc、尺寸、streaming 中切格式、sensor 写寄存器 | `mx6s_vidioc_s_fmt_vid_cap()`、`ov5640_s_fmt()` |
| `FAIL_PARM` | fps 不在离散表、当前尺寸没有该 fps mode、streaming 中改 fps | `mx6s_vidioc_s_parm()`、`ov5640_s_parm()` |
| `FAIL_STREAM` | buffer 不足、sensor 出流、SOF timeout、DMA/IRQ | `mx6s_vidioc_streamon()`、`ov5640_s_stream()`、`mx6s_start_streaming()` |

推荐最窄验证序列：

1. 用 `--list-devices`、`--info`、`--list-formats-ext` 确认节点和能力。
2. 用 `--set-fmt-video`、`--get-fmt-video`、`--set-parm`、`--get-parm` 确认一个稳定格式和帧率组合。
3. 用 `--stream-mmap --stream-count=30` 验证 VB2、CSI DMA、IRQ 和 `DQBUF` 完成路径。
4. 用 `test_v4l2_matrix.sh -d "$DEV" -c 10` 批量扫描已暴露的格式、尺寸和帧率。

常见现象定位：

| 现象 | 优先检查 | 原因方向 |
| --- | --- | --- |
| `--info` 失败 | video node 注册、模块和设备树匹配 | `fops/ioctl_ops` 未注册或节点错误 |
| 无格式可枚举 | sensor async bound、`enum_mbus_fmt` | host 未绑定 sensor，或格式映射不一致 |
| `--set-fmt-video` 返回 `EINVAL` | fourcc、尺寸和当前状态 | host 格式映射或 sensor mode/fps 表拒绝 |
| `--set-fmt-video` 返回 `EBUSY` | streaming 状态 | 当前驱动不支持运行中切换格式 |
| `--set-parm` 失败 | 当前尺寸对应的 fps | sensor 没有该尺寸/fps 寄存器表 |
| `--stream-mmap` 很快失败 | buffer 数、sensor 出流、SOF | `mx6s_start_streaming()` 至少需要两个 buffer |
| `--stream-mmap` 卡住 | IRQ/DMA 完成路径 | buffer 未进入 done_list，`DQBUF` 无法完成 |
| control 设置后未立即生效 | sensor 电源状态和 control 名称 | control 可能只更新了缓存 |

脚本结果还要区分：`PASS_ADJUST` 表示驱动把请求规整成了其他实际格式/尺寸/fps，`PASS_RETRY` 表示首次 stream 失败但重试成功，`PASS_NOFPS` 表示无法从工具输出解析实际 fps。这些结果仍计入 pass，但不能与“请求值原样生效”混为一谈。

## 21. 最短验证路径

下面的顺序对应前文的命令，但把每一步的证明目标明确分开。每一步都先确认返回值，再进入下一层，避免用一次 `--all` 或一次 stream 失败覆盖真正的故障阶段。

1. 设备发现：`command -v v4l2-ctl`、`v4l2-ctl --list-devices` 和 `dmesg` 确认工具、video node、模块加载和 async bound。
2. 能力发现：`v4l2-ctl -d "$DEV" --info` 确认 `VIDEO_CAPTURE + STREAMING`；`--list-formats-ext` 确认 fourcc/mbus 映射和 sensor 枚举链。
3. 配置确认：按“[v4l2-ctl 配置和采集](#v4l2-ctl-配置和采集)”执行 `S_FMT/G_FMT/S_PARM/G_PARM`，确认 active `sizeimage`、尺寸、格式和帧率。
4. 采集确认：执行 `--stream-mmap --stream-count=30`，这一步才覆盖 VB2、sensor 出流、CSI DMA、IRQ 和 `DQBUF`。

建议把 `DEV=/dev/video1` 放在当前 shell 中，再按上述顺序执行；节点编号只是假设值，应以 `--list-devices` 为准。

## 22. 一帧如何完成

以 MMAP 为例，`STREAMON` 后的实际所有权和硬件路径如下：

```text
用户态 QBUF
  -> VB2 PREPARE，payload = csi_dev->pix.sizeimage
  -> host capture 链表
  -> start_streaming：前两个 buffer -> CSI FB1/FB2，另建 discard buffer
  -> sensor s_stream(1)
  -> host 等待并检测 SOF，打开 CSI DMA request/IRQ
  -> CSI DMA done IRQ
  -> mx6s_csi_frame_done()
  -> 当前 active buffer 设置 timestamp/sequence，vb2_buffer_done(DONE/ERROR)
  -> 从 capture 链表补下一个 buffer；没有时切换 discard buffer
  -> poll 唤醒，DQBUF 把 index/bytesused/timestamp/sequence 交给用户态
  -> 用户处理后再次 QBUF
```

这里有两个当前实现要点：

- `mx6s_start_streaming()` 要求至少两个已排队 buffer，因为硬件有 CSI FB1/FB2 两个 DMA 槽；它还分配 coherent discard buffer，避免用户态补 buffer 不及时导致硬件停住。discard buffer 不会作为用户帧返回。
- 普通格式的 `bytesused` 由驱动固定为 `sizeimage`；JPEG 也使用固定大小的 CSI/VFIFO 传输外壳，当前 IRQ 路径不读取 OV5640 JPEG length 寄存器。因此 JPEG 的真实有效长度不能直接等同于 `DQBUF.bytesused`，用户态需要按 SOI/EOI 等协议裁剪，项目 README 也明确说明 `--stream-to` 保存的是固定外壳连续流。

对应源码可从 [`mx6s_start_streaming()`](../mx6s_capture.c#L1286)、[`mx6s_csi_frame_done()`](../mx6s_capture.c#L1465)、[`mx6s_csi_irq_handler()`](../mx6s_capture.c#L1560) 和 [`ov5640_s_stream()`](../ov5640.c#L3719) 继续追踪。

## 23. 失败定位的分层方法

不要直接从“采不到图”跳到寄存器。按最后一个已成功边界定位：

| 最后成功步骤 | 下一步失败时优先检查 | 主要源码路径 |
| --- | --- | --- |
| video node `--info` | async bound、格式枚举和 control 是否存在 | `subdev_notifier_bound()`、`mx6s_vidioc_enum_fmt_vid_cap()` |
| `--list-formats-ext` | 请求 fourcc/尺寸是否真实存在 | `format_by_fourcc()`、`ov5640_try_fmt()`、mode/fps 表 |
| `--get-fmt-video` | `sizeimage/bytesperline` 和 active state | `mx6s_negotiate_format()`、`mx6s_configure_csi()` |
| `--get-parm` | 当前尺寸是否支持目标 fps | `ov5640_s_parm()`、`ov5640_format_supports_mode_fps()` |
| `STREAMON` 返回失败 | sensor runtime PM、至少两个 buffer、SOF/reflash timeout | `ov5640_s_stream()`、`mx6s_start_streaming()`、`mx6s_csi_enable()` |
| stream 启动但 `DQBUF` 超时 | CSI IRQ、FB 槽、capture/active 列表和 `vb2_buffer_done()` | `mx6s_csi_irq_handler()`、`mx6s_csi_frame_done()` |
| 能 DQBUF 但图像异常 | 输入路径、CSI width/byte cycle、format、JPEG 外壳 | `mx6s_configure_csi()`、`mx6s_update_csi_buf()`、sensor format table |

这一分层也解释了为什么 `TRY_FMT` 成功不代表能采集：它只说明请求可以被规整；`S_FMT` 成功不代表 sensor 已出流；`STREAMON` 成功后仍要等 IRQ 把 buffer 标记为 DONE。

## 24. 与本文件对应的工具入口

| 目标 | 推荐入口 | 说明 |
| --- | --- | --- |
| 快速能力发现 | `v4l2-ctl --list-formats-ext` | 真实走 video node ioctl，适合先确认绑定和格式映射 |
| 精确 API 路径 | [`ov5640_interface_demo.c`](../ov5640_interface_demo.c) | 覆盖 list/configure/MMAP/USERPTR/read/CREATE_BUFS/crop-control 入口 |
| 稳定组合扫描 | [`test_v4l2_matrix.sh`](../test_v4l2_matrix.sh) | 有 warmup、stream retry、timeout 和实际值解析 |
| 原始寄存器调试 | subdev 节点 + `CONFIG_VIDEO_ADV_DEBUG` | 当前 host video node 不转发，本文 demo 不访问 |

`--read` 虽然有 `read -> mx6s_csi_read() -> vb2_read()` file operation，但 `mx6s_csi_open()` 的 `q->io_modes` 只声明 `VB2_MMAP | VB2_USERPTR`，`QUERYCAP` 也没有 `V4L2_CAP_READWRITE`。所以它是源码中存在的实验入口，不是当前主验证路径；板端若验证，应记录实际 errno，而不能把失败视为 MMAP 路径失败。
