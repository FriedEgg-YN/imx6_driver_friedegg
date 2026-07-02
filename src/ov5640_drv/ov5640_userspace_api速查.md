# OV5640 userspace API 速查

本文汇总 `ov5640_interface_demo.c` 文件开头的接口注释，并补充每一步用户态操作在当前 `mx6s_capture.c + ov5640.c` 驱动组合中主要做了什么。目标是快速回答两个问题：

- 用户态调用了哪个 syscall 或 `VIDIOC_*`？
- 这个操作进入驱动后，主要推进了哪个 V4L2/VB2/subdev/CSI 状态？

## 代码入口

示例程序：

```bash
./ov5640_interface_demo /dev/video1 --list
./ov5640_interface_demo /dev/video1 --configure --width 800 --height 480 --fps 30
./ov5640_interface_demo /dev/video1 --mmap --count 30
./ov5640_interface_demo /dev/video1 --read --count 1
./ov5640_interface_demo /dev/video1 --hflip 1 --vflip 1 --power-line 50
```

驱动层级关系：

```text
userspace syscall/ioctl
  -> /dev/videoX 的 v4l2_file_operations
  -> video_ioctl2() 或 VB2 file op helper
  -> mx6s_capture.c 的 v4l2_ioctl_ops / vb2_ops
  -> ov5640.c 的 v4l2_subdev_ops
  -> CSI host、OV5640 sensor、VB2 buffer 队列和 IRQ 完成路径
```

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

## 典型流程总览

### 发现能力

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

### 配置采集参数

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

### MMAP 采集

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

### read 采集

```text
configure
  -> read(fd, frame, sizeimage)
```

主要用途：使用 VB2 file-io 简化采集。当前代码保留了 `mx6s_csi_fops.read -> mx6s_csi_read() -> vb2_read()` 入口，但 `mx6s_csi_open()` 只把 `q->io_modes` 配成 `VB2_MMAP | VB2_USERPTR`，没有声明 `VB2_READ`，`querycap` 也只声明 video capture + streaming。因此按当前源码理解，`read()` 是“有 file operation 入口、但 VB2 队列未启用 read file-io”的路径，实际验证时可能返回 `-EINVAL`/不作为主采集路径。

## 设备生命周期和 file operations

| 用户态操作 | 驱动路径 | 主要工作 |
| --- | --- | --- |
| `open("/dev/videoX", O_RDWR)` | `mx6s_csi_fops.open -> mx6s_csi_open()` | 初始化本次打开的文件上下文；创建 `vb2_dma_contig` 分配上下文；配置并初始化 `vb2_queue`；打开 runtime PM；提高总线频率；调用 sensor `core.s_power(1)`；初始化 CSI host。 |
| `close(fd)` | `mx6s_csi_fops.release -> mx6s_csi_close()` | 释放 VB2 队列和残留 buffer；反初始化 CSI；调用 sensor `core.s_power(0)`；清理 DMA 分配上下文；释放总线频率请求；降低 runtime PM 使用计数。 |
| `ioctl(fd, VIDIOC_*, ...)` | `mx6s_csi_fops.unlocked_ioctl -> video_ioctl2() -> mx6s_csi_ioctl_ops.*` | V4L2 core 解析 ioctl 命令和参数结构，然后分发到当前 video node 注册的 `vidioc_*` 回调。 |
| `read(fd, buf, size)` | `mx6s_csi_fops.read -> mx6s_csi_read() -> vb2_read()` | VB2 file-io helper 会尝试在内部走 `REQBUFS/QBUF/STREAMON/DQBUF` 兼容层；但当前 `q->io_modes` 未启用 `VB2_READ`，所以这不是当前可靠主路径。 |
| `poll(fd, ...)` | `mx6s_csi_fops.poll -> vb2_fop_poll() -> vb2_poll()` | 等待 VB2 done 队列出现已完成 buffer。只有队列已经 streaming、且不处于“还没 QBUF 等 buffer”状态时才正常返回可读；否则可能返回 `POLLERR`。 |
| `mmap(fd, offset)` | `mx6s_csi_fops.mmap -> mx6s_csi_mmap() -> vb2_mmap()` | 根据 `VIDIOC_QUERYBUF` 返回的 offset，把内核分配的 MMAP buffer 映射到用户进程地址空间。 |

## 能力、输入源和拓扑发现

| 用户态 ioctl | 驱动路径 | 主要工作 |
| --- | --- | --- |
| `VIDIOC_QUERYCAP` | `mx6s_vidioc_querycap()` | 返回 driver/card/bus 信息和能力位。当前重点能力是 video capture + streaming。 |
| `VIDIOC_ENUMINPUT` | `mx6s_vidioc_enum_input()` | 枚举输入源。当前 host 只暴露一个 camera input，合法 index 为 `0`。 |
| `VIDIOC_G_INPUT` | `mx6s_vidioc_g_input()` | 返回当前输入源。当前固定为 `0`。 |
| `VIDIOC_S_INPUT` | `mx6s_vidioc_s_input()` | 选择输入源。当前只接受 `0`，更像参数合法性检查。 |

速查理解：

- 这一步还没有配置 sensor 寄存器，也没有启动 CSI DMA。
- 它的作用是确认 `/dev/videoX` 是不是目标采集节点，以及 host 是否接受 input 选择。

## 格式、分辨率和帧率枚举

| 用户态 ioctl | 驱动路径 | 主要工作 |
| --- | --- | --- |
| `VIDIOC_ENUM_FMT` | `mx6s_vidioc_enum_fmt_vid_cap() -> sensor video.enum_mbus_fmt -> ov5640_enum_fmt()` | sensor 先枚举 media-bus code，host 再映射成 V4L2 fourcc。当前 RGB565 路径会把 `MEDIA_BUS_FMT_RGB565_2X8_LE` 映射到 `V4L2_PIX_FMT_RGB565`。 |
| `VIDIOC_ENUM_FRAMESIZES` | `mx6s_vidioc_enum_framesizes() -> sensor pad.enum_frame_size -> ov5640_enum_framesizes()` | 按 pixel format 查询 sensor 支持的离散分辨率。host 先用 fourcc 找到对应 mbus code，再转发给 sensor。 |
| `VIDIOC_ENUM_FRAMEINTERVALS` | `mx6s_vidioc_enum_frameintervals() -> sensor pad.enum_frame_interval -> ov5640_enum_frameintervals()` | 按 pixel format + width + height 查询支持的帧间隔。当前 OV5640 路径主要支持离散 15 fps 和 30 fps 组合。 |

速查理解：

- `ENUM_*` 是发现能力，不应该改变 active format，也不应该写 sensor streaming 寄存器。
- host video node 面向用户态使用 fourcc；sensor subdev 内部更多使用 media-bus code。读代码时要把这两种格式概念分开。

## 格式和 stream 参数配置

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

## V4L2 controls

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

## Streaming buffer API

| 用户态 ioctl | 驱动路径 | 主要工作 |
| --- | --- | --- |
| `VIDIOC_REQBUFS` | `mx6s_vidioc_reqbufs() -> vb2_reqbufs() -> mx6s_videobuf_setup()` | 请求分配或切换 buffer 队列。MMAP 下由 VB2 分配 DMA buffer；USERPTR 下声明后续会传用户指针。`count=0` 用于释放队列；如果正在 streaming，VB2 会拒绝重新分配。 |
| `VIDIOC_CREATE_BUFS` | `vb2_ioctl_create_bufs() -> vb2_create_bufs() -> mx6s_videobuf_setup(fmt != NULL)` | 在已有队列基础上追加创建 buffer。当前 `.queue_setup` 会校验 create 请求的 type/pixelformat/width/height/sizeimage 必须与当前 active format 兼容。 |
| `VIDIOC_QUERYBUF` | `mx6s_vidioc_querybuf() -> vb2_querybuf()` | 按 index 查询 buffer 长度、offset、flags 等。MMAP 用户态随后用这个 offset 调 `mmap()`。当前驱动有旧兼容行为：buffer 已 mmap 时会把 `m.offset` 改成 DMA 物理地址。 |
| `VIDIOC_QBUF` | `mx6s_vidioc_qbuf() -> vb2_qbuf() -> vb2_queue 状态机` | 把一个空 buffer 的所有权交给 VB2。是否立刻调用 `.buf_queue -> mx6s_videobuf_queue()` 取决于 `q->streaming/start_streaming_called`，不是固定无条件路径。 |
| `VIDIOC_DQBUF` | `mx6s_vidioc_dqbuf() -> vb2_dqbuf() -> done_list` | 从 VB2 done 队列取回一帧。这个 buffer 必须已经由 CSI IRQ 或 stop/error 路径调用 `vb2_buffer_done()` 标记为 `DONE/ERROR`。无完成帧时，阻塞行为由 `O_NONBLOCK` 决定。 |

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

## STREAMON / STREAMOFF

### `VIDIOC_STREAMON`

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

### `VIDIOC_STREAMOFF`

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

## IRQ 完成和 DQBUF 的关系

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

## VB2 io_modes 与 buffer memory 模型

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

## crop 和 legacy std 接口

| 用户态 ioctl | 当前状态 | 速查理解 |
| --- | --- | --- |
| `VIDIOC_CROPCAP` | host 有 placeholder | 做基本类型检查或返回占位信息，不真正配置 OV5640 crop。 |
| `VIDIOC_G_CROP` | host 有 placeholder | 当前不是可靠的 sensor crop 状态来源。 |
| `VIDIOC_S_CROP` | host 有 placeholder | 不会真正写 OV5640 crop 窗口。 |
| `VIDIOC_G_STD / VIDIOC_S_STD / VIDIOC_QUERYSTD` | host 尝试转发给 subdev | OV5640 当前没有安装这些 analog-TV standard 回调，通常应视为不适用。 |

## debug-register API

`ov5640.c` 只在 `CONFIG_VIDEO_ADV_DEBUG` 下实现 subdev core `.g_register/.s_register`。当前 `mx6s_capture.c` 没有暴露匹配的 video-node ioctl，demo 也没有创建用户态 subdev node，所以 `ov5640_interface_demo.c` 不直接访问 OV5640 原始寄存器。

## 每个 demo 选项对应的主要操作

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

## 读代码时的最短路线

1. 从 `ov5640_interface_demo.c` 找用户态调用，例如 `VIDIOC_QBUF`。
2. 到 `mx6s_capture.c` 的 `mx6s_csi_ioctl_ops` 找对应 `.vidioc_qbuf`。
3. 进入 `mx6s_vidioc_qbuf()` 看它调用哪个 V4L2/VB2 helper。
4. 如果进入 VB2，继续看 `vb2_ops mx6s_videobuf_ops` 对应的 `.buf_prepare/.buf_queue/.start_streaming/.stop_streaming`。
5. 如果进入 sensor，继续看 `ov5640_subdev_ops` 下的 `.video/.pad/.core` 回调。
6. 最后把 buffer 完成路径和 IRQ 关联起来：`mx6s_csi_irq_handler() -> mx6s_csi_frame_done() -> vb2_buffer_done()`。

