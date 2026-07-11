# mx6s_capture 驱动架构与状态机导读

本文面向 `src/ov5640/mx6s_capture.c`，目标是把 i.MX6ULL CSI host 采集驱动的整体结构、模块职责和关键状态机串起来。它和 `ov5640_userspace_api速查.md` 的关系是：那篇从用户态 `VIDIOC_*` 使用路径看驱动，这篇从 `mx6s_capture.c` 的源码组织和状态流转看驱动。

## 驱动功能概述

`mx6s_capture.c` 是 i.MX6S/i.MX6UL/i.MX6ULL CSI host 的 V4L2 capture 驱动。它不是 OV5640 sensor 驱动，而是负责把 `/dev/videoX` 上的 V4L2/VB2 buffer 生命周期转换为 CSI 控制器寄存器配置、FB1/FB2 双 DMA 地址轮转和 IRQ 帧完成处理。

当前工程中，OV5640 作为 I2C V4L2 subdev 存在于 `ov5640.c`；`mx6s_capture.c` 通过设备树 OF graph 找到远端 sensor 节点，再用 V4L2 async notifier 保存 `struct v4l2_subdev *sd`，后续通过 `v4l2_subdev_call()` 控制 sensor 上电、格式、帧率和出流。

## 1 目录 & 概括

### 1.1 总线类型与设备树

- **总线类型**：`platform`。驱动入口是 `struct platform_driver mx6s_csi_driver` 和 `module_platform_driver(mx6s_csi_driver)`。
- **DTS compatible 字符串**：驱动源码匹配 `"fsl,imx6s-csi"`；SoC dtsi 中 CSI 节点为 `"fsl,imx6ul-csi", "fsl,imx6s-csi"`。
- **对应硬件设备**：i.MX6ULL CSI 控制器，MMIO 基址和 IRQ 来自 `csi@021c4000`。
- **板级连接**：板级 dts 中 `&csi/port/endpoint` 的 `remote-endpoint` 指向 `ov5640@3c` 的 endpoint，形成 CSI host 到 OV5640 sensor 的 OF graph 连接。
- **关键资源**：
  - `reg`：CSI MMIO 寄存器窗口，probe 中 `devm_ioremap_resource()` 映射到 `csi_dev->regbase`。
  - `interrupts`：CSI 中断，probe 中 `platform_get_irq()` 获取，`devm_request_irq()` 绑定 `mx6s_csi_irq_handler()`。
  - `clock-names`：`disp-axi`、`csi_mclk`、`disp_dcic`，probe 中分别用 `devm_clk_get()` 获取。
  - `port/endpoint`：用于 V4L2 async notifier 匹配远端 sensor subdev。

相关源码：

| 主题 | 文件 |
| --- | --- |
| CSI host 驱动 | `../mx6s_capture.c` |
| OV5640 sensor subdev | `../ov5640.c` |
| CSI SoC 节点 | `../../linux-friedegg/arch/arm/boot/dts/imx6ull.dtsi` |
| 板级 CSI/OV5640 endpoint | `../../linux-friedegg/arch/arm/boot/dts/imx6ull-friedegg-emmc.dts` |
| Buildroot 包入口 | `../../../bsp/package/ov5640/ov5640.mk` |

### 1.2 总体分层

```text
userspace
  open/ioctl/mmap/poll/read on /dev/videoX
    |
V4L2 video node
  struct v4l2_file_operations
  struct v4l2_ioctl_ops
    |
VB2 queue
  struct vb2_queue
  struct vb2_ops
  vb2_dma_contig_memops
    |
mx6s_capture host state
  capture / active_bufs / discard
  csi_dev->pix / fmt / mbus_code
  slock / lock
    |
CSI hardware
  CSI_CSICR1 / CSI_CSICR3 / CSI_CSICR18
  CSI_CSIDMASA_FB1 / CSI_CSIDMASA_FB2
  CSI_CSISR IRQ status
    |
OV5640 sensor subdev
  s_power / s_stream / s_mbus_fmt / try_mbus_fmt
```

可以把这个驱动记成一句话：**V4L2 负责用户接口，VB2 负责 buffer 生命周期，mx6s_capture 负责把 buffer DMA 地址喂给 CSI 双 FB 槽，OV5640 subdev 负责真正输出像素流。**

### 1.3 核心私有对象

`struct mx6s_csi_dev` 是读代码的第一站，它把所有状态聚在一起：

| 字段 | 含义 |
| --- | --- |
| `dev` | platform device 的 `struct device` |
| `vdev` | 注册成 `/dev/videoX` 的 `struct video_device` |
| `sd` | async notifier 绑定到的 sensor subdev，当前通常是 OV5640 |
| `v4l2_dev` | V4L2 顶层设备对象 |
| `vb2_vidq` | VB2 capture 队列 |
| `alloc_ctx` | `vb2_dma_contig` 分配上下文 |
| `lock` | 进程上下文互斥锁，保护 open/close/ioctl/VB2 队列路径 |
| `slock` | IRQ 与进程上下文共享的自旋锁，保护链表和硬件槽位 |
| `clk_disp_axi/clk_disp_dcic/clk_csi_mclk` | CSI 访问和采集所需时钟 |
| `regbase` | CSI MMIO 基址 |
| `irq` | CSI IRQ |
| `fmt/pix/mbus_code` | 当前 host V4L2 格式和 sensor media-bus code |
| `capture` | 已 QBUF、等待装入 CSI FB 槽的 buffer 队列 |
| `active_bufs` | 已写入 CSI FB1/FB2，硬件正在 DMA 的 buffer 队列 |
| `discard` | 用户 buffer 不足时用于丢帧的内部 DMA buffer 队列 |
| `buf_discard[2]` | 两个内部丢帧节点，对应 CSI 双 FB 槽 |
| `subdev_notifier/asd/async_subdevs` | V4L2 async subdev 匹配状态 |

## 2 用户接口与回调总表

| 用户接口 / 触发来源 | 操作 | Core 行为 | 驱动回调 | 功能说明 | 证据状态 |
| --- | --- | --- | --- | --- | --- |
| platform bus | 设备匹配 | platform core 根据 OF compatible 调 probe | `mx6s_csi_probe` | 获取资源，注册 V4L2/VB2/video node/IRQ/notifier | 已查证 |
| platform bus | 设备移除 | platform core 调 remove | `mx6s_csi_remove` | 注销 notifier、video node、V4L2 device，关闭 runtime PM | 已查证 |
| V4L2 async core | sensor 绑定 | async core 按 OF node 匹配 subdev | `subdev_notifier_bound` | 保存 `csi_dev->sd`，聚合 sensor controls | 已查证 |
| V4L2 async core | sensor 解绑 | async core 通知 unbind | `subdev_notifier_unbind` | 清空 `sd`，重置 ctrl handler | 已查证 |
| `/dev/videoX` | `open` | VFS 调 V4L2 fops open | `mx6s_csi_open` | 初始化 VB2 队列、runtime PM、busfreq、sensor power、CSI 寄存器 | 已查证 |
| `/dev/videoX` | `close` | VFS 调 release | `mx6s_csi_close` | 释放 VB2，关 stream/power，反初始化 CSI | 已查证 |
| `/dev/videoX` | `read` | VFS 调 read，驱动转 VB2 file-io | `mx6s_csi_read -> vb2_read` | 当前 fops 有入口，但 `io_modes` 未声明 `VB2_READ`，不是主路径 | 已查证 |
| `/dev/videoX` | `poll` | V4L2/VB2 poll helper 等 done queue | `vb2_fop_poll` | 等待完成帧或错误 | 已查证 |
| `/dev/videoX` | `mmap` | VMA 映射进入 VB2 mmap helper | `mx6s_csi_mmap -> vb2_mmap` | 映射 MMAP buffer 到用户态 | 已查证 |
| `/dev/videoX` | `ioctl(VIDIOC_QUERYCAP)` | `video_ioctl2` 参数拷贝和分发 | `mx6s_vidioc_querycap` | 返回 driver/card/bus/capability | 已查证 |
| `/dev/videoX` | input ioctls | `video_ioctl2` 分发 | `enum/g/s_input` | 只支持 camera input 0 | 已查证 |
| `/dev/videoX` | format enum/try/set/get | `video_ioctl2` 分发，host 调 subdev | `enum_fmt/try_fmt/s_fmt/g_fmt` | host fourcc 与 sensor mbus code 协商，`S_FMT` 同步配置 CSI | 已查证 |
| `/dev/videoX` | frame size/interval enum | `video_ioctl2` 分发，host 调 sensor pad ops | `enum_framesizes/enum_frameintervals` | 查询 OV5640 支持尺寸和帧率 | 已查证 |
| `/dev/videoX` | stream parm | `video_ioctl2` 分发，host 转发给 sensor | `g_parm/s_parm` | 查询/设置帧率等 stream 参数 | 已查证 |
| `/dev/videoX` | crop ioctls | `video_ioctl2` 分发 | `cropcap/g_crop/s_crop` | 当前只做占位检查，不配置硬件 crop | 已查证 |
| `/dev/videoX` | std ioctls | `video_ioctl2` 分发，host 转发给 sensor | `querystd/s_std/g_std` | 更偏模拟视频制式；OV5640 当前通常不适用 | 已查证 |
| `/dev/videoX` | `VIDIOC_REQBUFS` | VB2 分配/释放队列 buffer | `mx6s_videobuf_setup` | 设置单 plane 大小和 dma-contig allocator | 已查证 |
| `/dev/videoX` | `VIDIOC_CREATE_BUFS` | VB2 create-bufs helper | `mx6s_videobuf_setup(fmt != NULL)` | 追加创建 buffer，并检查格式与 active format 兼容 | 已查证 |
| `/dev/videoX` | `VIDIOC_QUERYBUF` | VB2 填 buffer 信息 | `mx6s_vidioc_querybuf` | 查询 length/offset/flags；当前有返回 DMA 地址的旧兼容行为 | 已查证 |
| `/dev/videoX` | `VIDIOC_QBUF` | VB2 校验并转交 buffer | `mx6s_videobuf_prepare -> mx6s_videobuf_queue` | 设置 payload，将 buffer 放入 `capture` 等待队列 | 已查证 |
| `/dev/videoX` | `VIDIOC_DQBUF` | VB2 从 done_list 取完成 buffer | `mx6s_vidioc_dqbuf` | 等 IRQ 调 `vb2_buffer_done` 后取回帧 | 已查证 |
| `/dev/videoX` | `VIDIOC_STREAMON` | V4L2 分发，VB2 启动 streaming | `mx6s_vidioc_streamon -> mx6s_start_streaming` | 先开 sensor，再装入两个 DMA buffer 并启动 CSI | 已查证 |
| `/dev/videoX` | `VIDIOC_STREAMOFF` | VB2 cancel queue | `mx6s_vidioc_streamoff -> mx6s_stop_streaming` | 停 CSI，归还 buffer，再关 sensor stream | 已查证 |
| CSI IRQ | FB1/FB2 DMA done | IRQ core 调 handler | `mx6s_csi_irq_handler -> mx6s_csi_frame_done` | 推进 active/capture/discard 队列，唤醒 DQBUF | 已查证 |
| runtime PM | runtime suspend/resume | PM core 调 ops | `mx6s_csi_runtime_suspend/resume` | 当前仅 debug log，真实开关在 open/close 路径 | 已查证 |

## 3 模块讲解与状态机

### 3.1 Probe/remove 状态机

**触发来源**：内核根据设备树 compatible 创建 platform device，并匹配 `mx6s_csi_driver`。

```text
module load
  -> platform_driver_register
  -> OF compatible match: "fsl,imx6s-csi"
  -> mx6s_csi_probe
      -> devm_kzalloc private data
      -> platform_get_resource(IORESOURCE_MEM)
      -> platform_get_irq
      -> devm_ioremap_resource
      -> INIT_LIST_HEAD(capture/active_bufs/discard)
      -> devm_clk_get(disp-axi/disp_dcic/csi_mclk)
      -> optional mx6s_csi_mux_sel
      -> v4l2_device_register
      -> v4l2_ctrl_handler_init
      -> mutex/spinlock init
      -> video_device_alloc
      -> set fops/ioctl_ops/queue/release/lock
      -> mx6s_init_default_format
      -> video_set_drvdata
      -> video_register_device => /dev/videoX
      -> devm_request_irq(mx6s_csi_irq_handler)
      -> mx6sx_register_subdevs
      -> pm_runtime_enable
```

remove 方向基本相反：

```text
mx6s_csi_remove
  -> v4l2_async_notifier_unregister
  -> video_unregister_device
  -> v4l2_ctrl_handler_free
  -> v4l2_device_unregister
  -> pm_runtime_disable
```

**设计要点**：

- `devm_*` 管理的资源，例如私有内存、MMIO 映射、时钟句柄、IRQ，会随设备释放自动清理。
- `video_device_alloc()` 分配的 `vdev` 由 `video_device_release` 或 `video_unregister_device()` 后的 release 路径释放。
- probe 阶段注册了 `/dev/videoX`，但 sensor 可能尚未完成 probe；真正能控制 sensor 依赖 async notifier 后续绑定。

### 3.2 OF graph 与 subdev 绑定状态机

**触发来源**：`mx6s_csi_probe()` 调用 `mx6sx_register_subdevs()`。

```text
mx6sx_register_subdevs
  -> 遍历 CSI 设备树子节点
  -> 找到 port/endpoint
  -> of_graph_get_remote_port_parent(endpoint)
  -> 得到 sensor_node: ov5640@3c
  -> asd.match_type = V4L2_ASYNC_MATCH_OF
  -> asd.match.of.node = sensor_node
  -> notifier.bound = subdev_notifier_bound
  -> notifier.unbind = subdev_notifier_unbind
  -> v4l2_async_notifier_register

OV5640 probe
  -> v4l2_i2c_subdev_init
  -> v4l2_async_register_subdev
  -> async core 按 OF node 匹配
  -> subdev_notifier_bound
      -> csi_dev->sd = subdev
      -> v4l2_ctrl_add_handler
      -> vdev->ctrl_handler = &csi_dev->ctrl_handler
```

**设计要点**：

- CSI host 和 OV5640 sensor 是两个驱动、两个设备。host 不直接假设 I2C 地址，而是从 OF graph 找远端节点。
- `csi_dev->sd` 是后续 `v4l2_subdev_call(sd, ...)` 的关键；如果它为空，open/ioctl 路径就无法可靠控制 sensor。
- controls 是 sensor 暴露的，但通过 `v4l2_ctrl_add_handler()` 聚合到 video node，用户态可在 `/dev/videoX` 上操作。

### 3.3 Open/close 会话状态机

**用户态行为**：`open("/dev/videoX", O_RDWR)` 与 `close(fd)`。

```text
open
  -> file->private_data = csi_dev
  -> mutex_lock(csi_dev->lock)
  -> vb2_dma_contig_init_ctx
  -> 填 q->type / io_modes / drv_priv / ops / mem_ops
  -> q->buf_struct_size = sizeof(struct mx6s_buffer)
  -> q->timestamp_flags = MONOTONIC
  -> q->lock = &csi_dev->lock
  -> vb2_queue_init
  -> pm_runtime_get_sync
  -> request_bus_freq(BUS_FREQ_HIGH)
  -> v4l2_subdev_call(sd, core, s_power, 1)
  -> mx6s_csi_init
      -> csi_clk_enable
      -> csihw_reset
      -> csi_init_interface
      -> csi_dmareq_rff_disable
  -> unlock
```

```text
close
  -> mutex_lock(csi_dev->lock)
  -> vb2_queue_release
  -> v4l2_subdev_call(sd, video, s_stream, 0)
  -> mx6s_csi_deinit
  -> v4l2_subdev_call(sd, core, s_power, 0)
  -> vb2_dma_contig_cleanup_ctx
  -> unlock
  -> file->private_data = NULL
  -> release_bus_freq(BUS_FREQ_HIGH)
  -> pm_runtime_put_sync_suspend
```

**设计要点**：

- open 阶段只是初始化 CSI 和 sensor power，不代表已经开始采集。
- `q->io_modes = VB2_MMAP | VB2_USERPTR`，说明当前主路径是 streaming API；虽然 fops 有 `.read`，但 read file-io 未作为主能力声明。
- `request_bus_freq(BUS_FREQ_HIGH)` 体现视频 DMA 对总线/DDR 带宽的依赖。

### 3.4 格式协商状态机

**用户态行为**：`VIDIOC_ENUM_FMT`、`VIDIOC_TRY_FMT`、`VIDIOC_S_FMT`、`VIDIOC_G_FMT`。

```text
用户 fourcc
  -> format_by_fourcc
  -> host mx6s_fmt
      pixelformat: V4L2_PIX_FMT_*
      mbus_code: MEDIA_BUS_FMT_*
      bpp
  -> v4l2_fill_mbus_format
  -> sensor try_mbus_fmt or s_mbus_fmt
  -> sensor 可能调整 width/height/code
  -> format_by_mbus
  -> v4l2_fill_pix_format
  -> host 更新 pix.bytesperline / pix.sizeimage
```

`TRY_FMT` 与 `S_FMT` 的差异：

| 操作 | sensor 侧 | host 侧 | 是否提交状态 |
| --- | --- | --- | --- |
| `TRY_FMT` | 调 `try_mbus_fmt` | 临时规整 `f->fmt.pix` | 否 |
| `S_FMT` | 调 `s_mbus_fmt` | 更新 `csi_dev->fmt/pix/mbus_code`，再 `mx6s_configure_csi()` | 是 |
| `G_FMT` | 不访问 sensor | 返回 `csi_dev->pix` | 读当前缓存 |

`mx6s_configure_csi()` 的硬件配置重点：

- 根据 `pix->field` 开关 deinterlace 和 TV decoder 相关模式。
- 根据 fourcc 计算写入 `CSI_CSIIMAG_PARA` 的宽度参数。
  - 并口 8-bit 输入的 YUYV/UYVY/RGB565：宽度按 `pix->width * 2`。
  - MIPI 输入：宽度按像素宽度。
- MIPI 路径会设置 `CSI_CSICR18` 的 data format 和 `BIT_DATA_FROM_MIPI`。

**设计要点**：

- 用户态看到的是 V4L2 fourcc，sensor subdev 关心的是 media-bus code。
- `sizeimage = bytesperline * height` 是后续 VB2 buffer 分配和 payload 的依据。
- `S_FMT` 应在 `REQBUFS/STREAMON` 前完成；streaming 中切格式通常不是安全路径。

### 3.5 VB2 buffer 状态机

这是驱动最值得反复看的核心。

#### 3.5.1 VB2 公共状态

典型单个 buffer 状态流：

```text
DEQUEUED
  用户态持有，可写入参数或消费帧
  |
VIDIOC_QBUF
  v
PREPARED / QUEUED
  VB2 持有，等待交给驱动
  |
__enqueue_in_driver
  v
ACTIVE
  驱动/硬件持有，可能在 capture 或 active_bufs 中
  |
vb2_buffer_done(DONE/ERROR)
  v
DONE / ERROR
  VB2 done_list 中，等待 DQBUF
  |
VIDIOC_DQBUF
  v
DEQUEUED
```

#### 3.5.2 mx6s_capture 的三条链表

```text
capture
  用户已经 QBUF，驱动已经收到，但还没写入 CSI FB1/FB2 的队列

active_bufs
  已写入 CSI_CSIDMASA_FB1/FB2，硬件正在或即将 DMA 的队列

discard
  用户补 buffer 不及时，用内部 coherent DMA buffer 接收并丢弃帧
```

迁移关系：

```text
QBUF
  -> mx6s_videobuf_queue
  -> list_add_tail(buffer, capture)

STREAMON
  -> mx6s_start_streaming
  -> capture 前两个 buffer 移到 active_bufs
  -> 写入 CSI_CSIDMASA_FB1 / CSI_CSIDMASA_FB2

DMA done IRQ
  -> mx6s_csi_frame_done(bufnum)
  -> active_bufs 队首完成
      -> 非 discard: vb2_buffer_done(DONE/ERROR)
      -> discard: 回到 discard 队列
  -> 如果 capture 非空:
      -> capture 队首移到 active_bufs
      -> 写入刚完成的 FB 槽
  -> 如果 capture 为空:
      -> discard 队首移到 active_bufs
      -> FB 槽写 discard_buffer_dma

STREAMOFF
  -> mx6s_stop_streaming
  -> 停 CSI
  -> active_bufs/capture 中残留 buffer 以 ERROR 归还
  -> 清空 capture/active_bufs/discard
  -> 释放 discard_buffer
```

#### 3.5.3 为什么至少需要两个 buffer

CSI 硬件使用 `CSI_CSIDMASA_FB1` 和 `CSI_CSIDMASA_FB2` 两个 framebuffer 地址槽。`mx6s_start_streaming()` 启动时要求 `count >= 2`，然后把前两个用户 buffer 分别装入 FB1/FB2。每次 IRQ 只更新刚完成的那个槽，让另一个槽继续工作，从而形成双缓冲轮转。

如果用户态 `DQBUF` 后没有及时 `QBUF` 回来，`capture` 队列会空。驱动此时不会停止硬件，而是把内部 `discard_buffer` 写入 FB 槽继续接帧，相当于主动丢帧，避免硬件没有 DMA 目标。

### 3.6 STREAMON/STREAMOFF 状态机

#### STREAMON

```text
VIDIOC_STREAMON
  -> mx6s_vidioc_streamon
      -> 检查 type == V4L2_BUF_TYPE_VIDEO_CAPTURE
      -> v4l2_subdev_call(sd, video, s_stream, 1)
      -> vb2_streamon
          -> 校验 VB2 queue 状态
          -> 将 queued_list 中 buffer 交给驱动 buf_queue
          -> 调 mx6s_start_streaming
              -> 要求 count >= 2
              -> dma_alloc_coherent(discard_buffer)
              -> 准备两个 discard 节点
              -> capture[0] -> FB1 -> active_bufs
              -> capture[1] -> FB2 -> active_bufs
              -> mx6s_csi_enable
                  -> csisw_reset
                  -> 可选 interlaced/TV decoder
                  -> MIPI: 直接开 DMA request + IRQ + CSI
                  -> 并口: 等 SOF -> DMA refresh -> 开 DMA request + IRQ + CSI
```

**为什么先开 sensor 再开 host**：并口路径中 `mx6s_csi_enable()` 会等待 SOF，sensor 必须已经开始输出帧同步，否则 host 等不到有效 SOF。

#### STREAMOFF

```text
VIDIOC_STREAMOFF
  -> mx6s_vidioc_streamoff
      -> vb2_streamoff
          -> mx6s_stop_streaming
              -> mx6s_csi_disable
                  -> disable DMA request
                  -> disable IRQ
                  -> 清 FB1/FB2 地址
                  -> 清 stride/deinterlace/TV decoder
                  -> disable CSI
              -> active_bufs/capture 中 buffer 以 ERROR 归还
              -> 清三条链表
              -> dma_free_coherent(discard_buffer)
      -> v4l2_subdev_call(sd, video, s_stream, 0)
```

**设计要点**：

- STREAMON 是 sensor + VB2 + CSI 三层联动。
- STREAMOFF 先停 host/VB2，再关 sensor stream。
- STREAMOFF 后，旧 buffer 队列被取消，下一轮采集通常要重新 QBUF。

### 3.7 IRQ 和帧完成状态机

**触发来源**：CSI 硬件产生 IRQ，常见关键位是 `BIT_DMA_TSF_DONE_FB1` 或 `BIT_DMA_TSF_DONE_FB2`。

```text
mx6s_csi_irq_handler
  -> spin_lock(slock)
  -> status = csi_read(CSI_CSISR)
  -> csi_write(status, CSI_CSISR)  // 写 1 清 pending
  -> 如果 active_bufs 为空:
      warn and return
  -> 如果 RxFIFO overflow 或 HRESP error:
      关 CSI
      清 RX FIFO
      DMA refresh
      重新开 CSI
  -> 如果 base address change error:
      关 CSI
      DMA refresh
      重新开 CSI
  -> 如果 FB1/FB2 两个位同时置位:
      skip two frames
  -> 如果 FB1 done:
      mx6s_csi_frame_done(bufnum=0, err=false)
  -> 如果 FB2 done:
      mx6s_csi_frame_done(bufnum=1, err=false)
  -> spin_unlock(slock)
```

`mx6s_csi_frame_done()` 的语义：

```text
active_bufs 队首
  |
  +-- discard buffer:
  |     list_move_tail(active -> discard)
  |
  +-- 用户 buffer:
        校验当前 FB 寄存器地址是否等于 buffer DMA 地址
        填 timestamp
        填 sequence = frame_count
        vb2_buffer_done(DONE/ERROR)

frame_count++

给刚完成的 FB 槽找下一个 DMA 目标:
  capture 非空 -> 用户 buffer -> active_bufs -> 写 FBn
  capture 为空 -> discard buffer -> active_bufs -> 写 FBn
```

**设计要点**：

- IRQ handler 不能睡眠，所以这里只做 MMIO、链表和 `vb2_buffer_done()`。
- `slock` 保护 `capture/active_bufs/discard`，因为这些链表同时被 QBUF、start/stop 和 IRQ 修改。
- `vb2_buffer_done()` 是用户态 `poll/DQBUF` 被唤醒的关键点；`DQBUF` 本身不采集，只取已经完成的 buffer。

### 3.8 硬件控制状态机

核心 helper：

| 函数 | 作用 |
| --- | --- |
| `csi_clk_enable/disable` | 打开/关闭 CSI 相关时钟 |
| `csihw_reset` | 写默认复位值 |
| `csisw_reset` | 运行中关闭 CSI、清 FIFO、DMA refresh、清 pending、再开 CSI |
| `csi_init_interface` | 设置保守默认输入时序、默认图像参数、DMA refresh |
| `csi_enable_int/csi_disable_int` | 开关 SOF、overflow、FB1/FB2 DMA done 中断 |
| `csi_dmareq_rff_enable/disable` | 开关 RxFIFO DMA request |
| `csi_set_imagpara` | 写 `CSI_CSIIMAG_PARA` 的宽高参数 |
| `mx6s_update_csi_buf` | 写 `CSI_CSIDMASA_FB1/FB2` DMA 地址 |
| `mx6s_csi_enable/disable` | 组合上述 helper 完成采集启停 |

简化模型：

```text
open/init
  clocks on
  registers reset
  default interface
  DMA request off

S_FMT
  image parameters prepared
  MIPI/parallel/interlaced mode prepared

STREAMON
  FB1/FB2 DMA address valid
  sensor streaming on
  CSI reset/refresh
  DMA request on
  IRQ on
  CSI enable

IRQ
  hardware writes frame into FB slot
  software rotates FB slot address

STREAMOFF/close
  DMA request off
  IRQ off
  FB address clear
  CSI disable/reset
  clocks off
```

## 4 子系统专用 API 补充

### 4.1 V4L2 video node

| API / 结构体 | 本驱动用途 | 学习要点 |
| --- | --- | --- |
| `struct video_device` | 表示 `/dev/videoX` 节点，挂 `fops/ioctl_ops/queue/lock` | 用户态访问的不是 platform device，而是这个 video node |
| `video_register_device` | 创建 video 设备节点 | 第三个参数 `-1` 表示自动分配 minor |
| `video_set_drvdata` / `video_drvdata` | 让 fops/ioctl 回调取回 `csi_dev` | 这是 file 回调到设备私有结构的主通道 |
| `video_ioctl2` | V4L2 ioctl 通用分发入口 | 负责用户参数 copy、known ioctl 检查和 `v4l2_ioctl_ops` 分发 |
| `struct v4l2_ioctl_ops` | 当前驱动所有 `VIDIOC_*` 的回调表 | 读 ioctl 路径时先从这张表找入口 |

### 4.2 V4L2 subdev / async

| API / 结构体 | 本驱动用途 | 学习要点 |
| --- | --- | --- |
| `struct v4l2_subdev` | 表示 OV5640 sensor | sensor 不直接生成当前 `/dev/videoX`，而是被 host 调用 |
| `v4l2_subdev_call` | host 调 sensor 的 `s_power/s_stream/s_mbus_fmt/...` | 调用前必须有有效 `csi_dev->sd` |
| `struct v4l2_async_notifier` | 等待 OF graph 远端 sensor probe | 解决 host 和 sensor probe 先后顺序不确定的问题 |
| `V4L2_ASYNC_MATCH_OF` | 按 OF node 匹配 subdev | 当前不是按名字或 I2C 地址硬编码匹配 |
| `v4l2_ctrl_add_handler` | 把 sensor controls 合并到 video node | 用户态在 `/dev/videoX` 上操作 controls，底层可能是 sensor 寄存器 |

### 4.3 Videobuf2

| API / 结构体 | 本驱动用途 | 学习要点 |
| --- | --- | --- |
| `struct vb2_queue` | 表示采集 buffer 队列 | `type/io_modes/ops/mem_ops/buf_struct_size` 是初始化重点 |
| `struct vb2_ops` | 驱动提供 buffer 生命周期回调 | `queue_setup/buf_prepare/buf_queue/start_streaming/stop_streaming` 是主路径 |
| `vb2_reqbufs` | 分配/释放 buffer | 会调用 `.queue_setup` 决定 plane 数、大小和 allocator |
| `vb2_qbuf` | 用户把 buffer 交给 VB2 | 可能触发 `.buf_prepare` 和 `.buf_queue` |
| `vb2_streamon` | 启动 streaming | 会把 queued buffer 交给驱动，并调用 `.start_streaming` |
| `vb2_buffer_done` | 驱动归还完成 buffer | IRQ frame done 后调用，唤醒 `poll/DQBUF` |
| `vb2_dma_contig_memops` | 使用连续 DMA buffer | CSI FB 地址需要可 DMA 的物理/总线地址 |

## 5 通用 API 索引

| 分类 | API / 机制 | 出现位置 | 本驱动用途 | 建议追问 |
| --- | --- | --- | --- | --- |
| 设备模型 | `module_platform_driver` | 文件末尾 | 注册 platform driver | 是 |
| 资源管理 | `devm_kzalloc` | probe | 分配 `mx6s_csi_dev` | 视情况 |
| 资源管理 | `platform_get_resource` / `platform_get_irq` | probe | 获取 DT 转换后的 MMIO/IRQ 资源 | 是 |
| MMIO | `devm_ioremap_resource` | probe | 映射 CSI 寄存器窗口 | 是 |
| MMIO | `__raw_readl` / `__raw_writel` | `csi_read/csi_write` | 访问 CSI 寄存器 | 是 |
| clock | `devm_clk_get` / `clk_prepare_enable` | probe/open | 获取并打开 CSI 相关时钟 | 是 |
| DMA | `dma_alloc_coherent` / `dma_free_coherent` | start/stop streaming | 分配内部 discard buffer | 是 |
| 并发 | `mutex` | open/close/ioctl/mmap/read | 串行化进程上下文 | 视情况 |
| 并发 | `spin_lock_irqsave` / `spin_lock` | QBUF/start/stop/IRQ | 保护链表与 FB 槽状态 | 是 |
| 链表 | `list_head` / `list_move_tail` | buffer 队列 | 管理 capture/active/discard 状态迁移 | 是 |
| OF graph | `of_graph_get_remote_port_parent` | subdev 注册 | 从 CSI endpoint 找 sensor node | 是 |
| regmap | `regmap_update_bits` | `mx6s_csi_mux_sel` | 可选 MIPI mux GPR 配置 | 视情况 |
| runtime PM | `pm_runtime_get_sync` / `pm_runtime_put_sync_suspend` | open/close | 管理设备运行期电源引用 | 是 |

## 6 代码阅读路线

建议按下面顺序读，不要从文件头寄存器宏一路硬啃。

1. **先看私有数据结构**：`struct mx6s_csi_dev`。确认有哪些状态字段，特别是 `sd`、`vb2_vidq`、`pix/fmt`、三条 buffer 链表和锁。
2. **看 probe**：`mx6s_csi_probe()`。确认这个 platform device 如何变成 `/dev/videoX`，以及 IRQ/notifier 如何注册。
3. **看 open/close**：`mx6s_csi_open()`、`mx6s_csi_close()`。确认一次用户会话的电源、时钟、VB2 队列初始化和释放顺序。
4. **看 ioctl ops 表**：`mx6s_csi_ioctl_ops`。先把所有用户态入口放到脑子里。
5. **看格式协商**：`mx6s_negotiate_format()`、`mx6s_vidioc_s_fmt_vid_cap()`、`mx6s_configure_csi()`。理解 fourcc、mbus code、CSI 图像参数之间的转换。
6. **看 VB2 ops 表**：`mx6s_videobuf_ops`。重点读 `mx6s_start_streaming()` 和 `mx6s_stop_streaming()`。
7. **看 IRQ 路径**：`mx6s_csi_irq_handler()`、`mx6s_csi_frame_done()`。把 `capture -> active_bufs -> done/discard` 的迁移画出来。
8. **最后回头看寄存器 helper**：`csi_*` 和 `csisw_reset()`。这时每个寄存器位才有上下文。

## 7 典型调用链速记

### 7.1 用户态 MMAP 采集

```text
open
  -> QUERYCAP
  -> ENUM_FMT / ENUM_FRAMESIZES / ENUM_FRAMEINTERVALS
  -> S_INPUT(0)
  -> TRY_FMT
  -> S_FMT
  -> G/S_PARM
  -> REQBUFS(MMAP)
  -> QUERYBUF + mmap
  -> QBUF * N
  -> STREAMON
  -> poll
  -> DQBUF
  -> consume frame
  -> QBUF recycle
  -> STREAMOFF
  -> munmap
  -> REQBUFS(0)
  -> close
```

### 7.2 驱动内部主路径

```text
open
  -> mx6s_csi_open
  -> vb2_queue_init
  -> sensor.s_power(1)
  -> mx6s_csi_init

S_FMT
  -> mx6s_negotiate_format
  -> sensor.s_mbus_fmt
  -> mx6s_configure_csi

QBUF
  -> vb2_qbuf
  -> mx6s_videobuf_prepare
  -> mx6s_videobuf_queue
  -> capture list

STREAMON
  -> sensor.s_stream(1)
  -> vb2_streamon
  -> mx6s_start_streaming
  -> FB1/FB2 address valid
  -> mx6s_csi_enable

IRQ
  -> mx6s_csi_irq_handler
  -> mx6s_csi_frame_done
  -> vb2_buffer_done

DQBUF
  -> vb2_dqbuf
  -> userspace gets frame
```

## 8 常见误区

- **误区 1：`mx6s_capture.c` 是 OV5640 驱动。**  
  不是。它是 CSI host 驱动；OV5640 是 subdev，位于 `ov5640.c`。

- **误区 2：`QBUF` 一定马上写硬件地址。**  
  不一定。`QBUF` 先进入 VB2 和驱动 `capture` 队列；只有 STREAMON 装初始两个 buffer 或 IRQ 换槽时，才写 `CSI_CSIDMASA_FB1/FB2`。

- **误区 3：`DQBUF` 会触发采集一帧。**  
  不会。采集由 CSI DMA 和 IRQ 完成；`DQBUF` 只是从 VB2 done_list 取回已经完成的 buffer。

- **误区 4：STREAMON 只开 CSI。**  
  不是。当前顺序是先 `sensor.s_stream(1)`，再 `vb2_streamon()`，最后进入 host 的 `mx6s_start_streaming()` 和 `mx6s_csi_enable()`。

- **误区 5：runtime PM ops 负责真实开关硬件。**  
  当前 `mx6s_csi_runtime_suspend/resume()` 只是 debug log，真正的时钟和 sensor power 主要在 open/close 与 streamon/streamoff 中处理。

## 9 阅读缺口与验证建议

### 9.1 源码缺口

- CSI 寄存器位的硬件语义需要结合 i.MX6ULL Reference Manual 深读；本文只按源码中的使用路径解释。
- VB2 core 源码已抽查关键路径：`vb2_reqbufs`、`vb2_qbuf`、`vb2_streamon`、`vb2_buffer_done`、`vb2_dqbuf`。若要理解所有边界条件，可继续读 `drivers/media/v4l2-core/videobuf2-core.c`。
- V4L2 ioctl core 已抽查 `video_ioctl2 -> video_usercopy -> __video_do_ioctl`。control core 的完整路径本文未展开。
- MIPI mux 路径当前只按 `csi-mux-mipi` 可选属性和代码逻辑说明，当前板级 dts 是否使用该属性需按实际 DTS 确认。

### 9.2 最窄构建验证

在主机侧验证外置驱动包构建：

```bash
bash buildscripts/build_and_deploy.sh drv ov5640
```

预期结果：

- 生成或更新 `mx6s_capture.ko` 和 `ov5640.ko`。
- 无 `mx6s_capture.c` 编译错误。

### 9.3 板端功能验证

以下命令需在板端执行，未在本文生成时实际执行：

```bash
reinsmod.sh
ls -l /dev/video*
ov5640_interface_demo /dev/video1 --list
ov5640_interface_demo /dev/video1 --configure --width 800 --height 480 --fps 30
ov5640_interface_demo /dev/video1 --mmap --count 30
```

预期结果：

- `reinsmod.sh` 能加载 `mx6s_capture.ko` 和 `ov5640.ko`。
- `/dev/videoX` 中出现 CSI capture 节点。
- `--list` 能看到 capture 能力、格式、分辨率、帧率和 controls。
- `--mmap` 能完成 STREAMON、连续 DQBUF/QBUF，并正常 STREAMOFF。

如果要观察 IRQ 和 buffer 轮转，可临时打开相关 `dev_dbg` 或使用 ftrace/dynamic debug；注意不要把板卡 IP、私有路径等运行环境信息写入仓库文档。
