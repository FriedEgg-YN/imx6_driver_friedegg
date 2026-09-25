# OV5640 预览异常调试记录

## 范围

本文记录 Smart Monitor 在 OV5640 + i.MX6ULL CSI 链路上出现的低频预览
异常。这里的内容是调试假设与验证记录，不是已经确认的根因结论。

## 跨 Agent 调试契约

### 任务目标与当前状态

目标不是立刻修改驱动，而是先回答两个可证伪的问题：

1. 水平循环偏移最早出现在哪一层：CSI/V4L2 MMAP raw frame、Qt 的
   `QImage`，还是最终 LCD 绘制？
2. 偏移发生前后是否存在 CSI overflow、HRESP、DMA 完成异常或明显的系统负载
   变化？

截至本文最新更新，已完成源码阅读、短时 smoke、一次纯 V4L2 soak，以及 Camera Test
无录像预览对照。最新 B 测试在长时间运行后复现水平循环偏移；同一时刻的 RGB565
Snapshot 也出现相同偏移。纯 V4L2 soak 的 CSI IRQ 行 `GPC 7 ... mx6s-csi-ai-ref`
从 131902 增长到 239902，增量 108000；该数量约为 54000 帧的两倍，说明 CSI
streaming 和 IRQ 路径确实在推进，但还没有 raw MMAP 帧、IRQ 状态位和 buffer 槽位的
逐帧证据。Camera Test 中执行 `preview/hide`、`preview/show` 后偏移保留，且右上角
仍显示 streaming；该操作只切换显示标志，不会执行 `VIDIOC_STREAMOFF`。

当前判断：问题最迟发生在 `CameraDevice` 生成 QImage 之前，CSI host 驱动路径为最高
优先级假设；`mx6s_csi_frame_done()` 的 active buffer 取法、simultaneous-done 跳过
分支、以及错误恢复不重新等待 SOF 都是待验证的驱动风险。仍不能排除 OV5640 DVP
时序、供电或信号完整性问题。下一步必须先取得 raw MMAP frame + V4L2 metadata，再
决定是否修改驱动；不得仅凭 Snapshot 或源码注释直接修复。

### 代码范围与所有权

| 层级 | 首要文件 | 当前职责 | 当前不确定点 |
| --- | --- | --- | --- |
| Smart Monitor 控制层 | `src/imx6_smart_monitor/qt/apps/launcher/monitor_controller.cpp` | 决定何时打开 camera、开始录制和关闭 camera。 | presence/录制期间的系统负载是否会提高故障概率。 |
| 用户态采集层 | `src/imx6_smart_monitor/camera/camera_device.cpp` | `CameraCaptureThread` 执行 MMAP、`DQBUF`、复制到 QImage、`QBUF`。 | 是否需要只在异常时保存 raw frame 和 V4L2 metadata。 |
| Qt 预览层 | `src/imx6_smart_monitor/qt/apps/launcher/smart_monitor_page.cpp` | 接收 QImage 并调用 `QPainter::drawImage()`。 | screen 异常是否与保存的 QImage 一致。 |
| CSI host 驱动 | `src/ov5640/mx6s_capture.c` | V4L2 video node、VB2、CSI DMA、IRQ、错误恢复。 | overflow/HRESP/simultaneous DMA done 后队列和帧边界是否仍同步。 |
| OV5640 sensor 驱动 | `src/ov5640/ov5640.c` | I2C 寄存器、模式、格式、sensor stream 开关和 PM。 | DVP 输出时序、供电或信号完整性是否触发 host 异常。 |

默认保持只读诊断。只有用户明确要求实现修复，且已有可复现证据指向某层后，
才修改该层最小范围代码。不要在没有证据时重写 Qt 预览、变更 sensor 模式表、
提高 buffer 数或修改 CSI recovery；这些动作会改变变量，降低低频问题的可比性。

### Agent 工作规则

1. 每次接手先读本文、根目录 `AGENTS.md` 和当前涉及文件的近邻源码；先确认
   当前工作区的未提交改动，不回滚其他人的内容。
2. 报告时明确标记 `源码事实`、`板端证据`、`推测` 和 `待验证`，不要混用。
3. `v4l2-ctl` 操作的是 `/dev/videoX` video node，不是直接调用 sensor 的
   `.s_stream`；解释时始终分开 video-node ioctl、VB2 callback、CSI IRQ 与
   sensor subdevice callback。
4. 一次测试只允许一个程序打开 `/dev/video1`。在 Smart Monitor、Camera Test、
   `v4l2-ctl`、`ov5640_test` 之间切换前，先完全退出前一个程序。
5. 板端验证产生的日志和 raw frame 只写入 `/tmp` 或用户指定的临时目录；不要
   把板卡地址、账号、NFS/TFTP 真实路径或其他私有运行状态写入仓库。
6. 不执行 `dmesg -C`、不在未确认目标时 reload 模块、不写 sensor 寄存器。故障
   发生后优先采证，再进行 Stop/Start 或更强的恢复动作。

### 每轮交接的最小证据包

后续 Agent 或用户汇报一次测试时，必须给出以下字段。缺失字段应写 `未采集`，
不得补猜。

```text
测试编号：
日期/起止时间：
程序与版本/工作区提交：
唯一视频节点：
请求模式与驱动实际模式：fourcc、width、height、bytesperline、sizeimage、fps
场景：纯 V4L2 / Camera Test 预览 / Smart Monitor 预览+录制 / 其他
持续时长、是否复现：
故障开始时间、偏移方向与估计像素数：
故障时 snapshot/JPG 或 raw frame 路径：
dmesg 关键行：
/proc/interrupts 前后差异：
CPU、内存、存储空间和 I/O 观察：
执行过的恢复动作及结果：
本轮结论：源码事实 / 板端证据 / 推测 / 下一步
```

### 何时可以进入修复阶段

满足下列任一条件，才有足够理由设计最小修复并再次对照验证：

- 偏移与 `Rx fifo overflow`、HRESP error 或其他 CSI 异常在同一时间窗口出现；
- raw RGB565 或由 `CameraDevice` 保存的 JPG 已证明图像在 Qt 绘制前损坏；
- 仅出现 FB1/FB2 DMA 完成状态异常，并能以 buffer index、sequence、DMA 地址
  证明软件队列与硬件槽位失同步；
- 外部测量已证明 DVP 的 PCLK、HSYNC、VSYNC 或数据线在故障时存在异常。

若上述证据都没有，优先改进诊断工具而不是猜测性修复。

## 现象

- Smart Monitor 以 RGB565 预览时，开始推流后的画面通常正常。
- 长时间运行后会随机出现水平循环偏移，可能约半小时才发生一次。
- 偏移会在后续异常中累积。例如一行原本为 `1 2 3 4 5`，可能变为
  `2 3 4 5 1`，下一次又变为 `3 4 5 1 2`。
- 当前暂按“每一行均以相同距离水平偏移”理解。如果实际现象是每一行的
  偏移量不同，或整幅图像发生垂直位移，必须先修订本文，再将其归因到 CSI
  的字节/帧同步问题。

## 当前数据链路

```text
SmartMonitorPage
  -> MonitorController
  -> CameraDevice / CameraCaptureThread
  -> /dev/video1 V4L2 MMAP streaming
  -> mx6s_capture CSI RxFIFO + embedded DMA FB1/FB2
  -> CameraCaptureThread 将 RGB565 逐行复制到新的 QImage
  -> CameraDevice queued signal
  -> PreviewPane::paintEvent() / QPainter::drawImage()
```

`main.cpp` 只负责创建 launcher。`MonitorController` 打开选中的 RGBP 模式
并启动 `CameraDevice`，见
[`ensureCameraRunning()`](src/imx6_smart_monitor/qt/apps/launcher/monitor_controller.cpp#L355)。

采集线程申请四个 MMAP buffer，从 V4L2 dequeue 一个已完成 buffer，按协商
得到的 `bytesperline` 逐行复制至新的 `QImage::Format_RGB16`，随后立即以
`QBUF` 归还 V4L2 buffer，见
[`readFrame()`](src/imx6_smart_monitor/camera/camera_device.cpp#L1394)。
UI 接收的预览信号被限制为最多每 100 ms 一帧，见
[`deliverFrame()`](src/imx6_smart_monitor/camera/camera_device.cpp#L2213)。
`PreviewPane` 只缩放和绘制图像，见
[`paintEvent()`](src/imx6_smart_monitor/qt/apps/launcher/smart_monitor_page.cpp#L45)。

## 驱动回调层级

```text
VIDIOC_S_FMT
  -> mx6s_vidioc_s_fmt_vid_cap()
  -> OV5640 .s_mbus_fmt / ov5640_s_fmt()
  -> 配置 CSI 格式和图像参数

VIDIOC_STREAMON
  -> mx6s_vidioc_streamon()
  -> OV5640 .s_stream / ov5640_s_stream(1)
  -> vb2_streamon()
  -> mx6s_start_streaming()
  -> CSI DMA FB1/FB2 与 IRQ 完成处理
```

video-node 层的启动顺序实现在
[`mx6s_vidioc_streamon()`](src/ov5640/mx6s_capture.c#L2264)。OV5640 subdevice
的 stream 回调负责 runtime-PM 状态和 sensor stream-control 寄存器开关，
不负责 CSI DMA buffer 完成处理，见
[`ov5640_s_stream()`](src/ov5640/ov5640.c#L3729) 与
[`ov5640_set_stream()`](src/ov5640/ov5640.c#L1807)。CSI buffer 所有权、DMA
和 IRQ 恢复均位于 `mx6s_capture.c`。

## 源码证据

1. Qt 的固定 stride 或 RGB565 格式不匹配概率较低。应用逐行采用驱动返回的
   `bytesperline`；若是固定错误，应从推流第一帧开始稳定出现，而不是长时间
   正常后随机出现。
2. `QPainter` 布局问题概率较低。`PreviewPane` 不修改像素，也不重新解释
   RGB565 数据，只调用 `drawImage()`。
3. 正常的并口 CSI 启动路径会等待 SOF、刷新 DMA，然后才开启 RxFIFO DMA。
   源码注释明确说明错误的时序可能丢失数据并导致图像 split，见
   [`mx6s_csi_enable()`](src/ov5640/mx6s_capture.c#L1105)。
4. CSI IRQ handler 会报告 RxFIFO overflow 和 HRESP error。它的恢复流程会
   立即清空/刷新/重新开启 CSI，但不会重新执行正常启动路径中的 SOF 等待，见
   [`mx6s_csi_irq_handler()`](src/ov5640/mx6s_capture.c#L1570)。
5. 该异常路径不会将已完成 buffer 标为 `VB2_BUF_STATE_ERROR`，所以用户态
   可能收到一个表面成功、实际已损坏的帧。
6. `mx6s_csi_frame_done()` 取 `active_bufs` 链表首节点，而不是按当前完成的
   FB 槽位查找 buffer。FB1+FB2 同时完成的分支会跳过队列推进。这是异常 IRQ
   时序下可能造成软件队列失同步的风险，但尚未证明就是本问题根因，见
   [`frame completion and IRQ dispatch`](src/ov5640/mx6s_capture.c#L1465)。

## OV5640 800x480 与 1024x768 寄存器对比

本节固定记录当前驱动、Linux 主线和公开网络表的寄存器差异。寄存器对采用高
字节在前的组合值表示，例如：

```text
0x3808/0x3809 = 0x04/0x00 = 0x0400 (1024)
0x3808/0x3809 = 0x03/0x20 = 0x0320 (800)
```

表中尺寸和时序统一写成“十六进制组合值（十进制含义）”。粗体表示该值与同一
目标模式的参考不一致：当前 800x480 列与网络 WVGA 表比较，当前 1024x768 列
与主线 XGA 比较。主线没有 800x480 固定表，网络表也不是 XGA 表；输出尺寸在
800x480 与 1024x768 之间不同是模式本身的预期差异，不单独视为错误。当前值来自
[`src/ov5640/ov5640.c`](src/ov5640/ov5640.c)；主线值来自 Linux 官方
[`drivers/media/i2c/ov5640.c`](https://github.com/torvalds/linux/blob/master/drivers/media/i2c/ov5640.c)；
网络 WVGA 值来自 ArduCAM 的
[`OV5640_QSXGA2WVGA`](https://github.com/ArduCAM/RaspberryPi/blob/master/SPI_Camera/src/ov5640_regs.h#L517-L550)。

### 尺寸与帧时序

| 寄存器 | 寄存器作用 | 当前 800x480 | 当前 1024x768@30 | 主线 1024x768 参考 | 网络 800x480 参考 |
| --- | --- | --- | --- | --- | --- |
| `0x3800/01` | 输入窗口水平起点 X | `0x0000 (0)` | `0x0000 (0)` | `0x0000 (0)` | `0x0000 (0)` |
| `0x3802/03` | 输入窗口垂直起点 Y | **`0x0004 (4)`** | `0x0004 (4)` | `0x0004 (4)` | **`0x0000 (0)`** |
| `0x3804/05` | 输入窗口水平终点 X | `0x0a3f (2623)` | `0x0a3f (2623)` | `0x0a3f (2623)` | `0x0a3f (2623)` |
| `0x3806/07` | 输入窗口垂直终点 Y | **`0x079b (1947)`** | `0x079b (1947)` | `0x079b (1947)` | **`0x079f (1951)`** |
| `0x3808/09` | DVP 输出宽度 | `0x0320 (800)` | `0x0400 (1024)` | `0x0400 (1024)` | `0x0320 (800)` |
| `0x380a/0b` | DVP 输出高度 | `0x01e0 (480)` | `0x0300 (768)` | `0x0300 (768)` | `0x01e0 (480)` |
| `0x380c/0d` | HTS，总水平周期 | **`0x0768 (1896)`** | `0x0768 (1896)` | `0x0768 (1896)` | **`0x0c80 (3200)`** |
| `0x380e/0f` | VTS，总垂直周期 | **`0x03d8 (984)`** | **`0x03d8 (984)`** | **`0x0438 (1080)`** | **`0x07d0 (2000)`** |
| `0x3810/11` | ISP 水平偏移 | `0x0010 (16)` | `0x0010 (16)` | `0x0010 (16)` | `0x0010 (16)` |
| `0x3812/13` | ISP 垂直偏移 | **`0x0006 (6)`** | `0x0006 (6)` | `0x0006 (6)` | **`0x0148 (328)`** |
| `0x3814` | 水平奇偶采样增量 | `0x31` | `0x31` | `0x31` | 未列出 |
| `0x3815` | 垂直奇偶采样增量 | `0x31` | `0x31` | `0x31` | 未列出 |
| `0x5680~0x5687` | ISP 输出裁剪窗口 | 当前表未写 | 当前表未写 | 使用 mode crop 元数据 | `0x0000~0x0a20`、`0x0000~0x0798` |

`0x3808/09` 的 `0x0400` 就是 1024，`0x380a/0b` 的 `0x0300` 就是
768。当前两个模式虽然输出尺寸不同，但 HTS、VTS 完全相同；主线 XGA 将
VTS 改为 1080，网络 WVGA 表则使用另一套 3200x2000 的总时序。

### PLL、系统时钟与 PCLK

主线 DVP 路径不是固定使用一组 PLL 表，而是在模式切换时由
`ov5640_set_dvp_pclk()` 根据 HTS、VTS、目标帧率、像素位宽和 DVP bus width
重新计算。下表的主线“典型运行值”假定 XCLK=24 MHz、YUV422、8-bit DVP；
若设备树总线宽度或像素格式不同，应重新计算。

| 寄存器 | 寄存器作用 | 当前 800x480 | 当前 1024x768@30 | 主线 XGA 典型运行值 | 网络 800x480 参考 |
| --- | --- | --- | --- | --- | --- |
| `0x3034` | PLL bit mode / charge pump | `0x1a` | **`0x1a`** | **`0x18`，低 4 位设为 8-bit** | 未列出 |
| `0x3035` | 系统时钟分频 | `0x21` (SysDiv=2) | **`0x21` (SysDiv=2)** | **`0x20`，由运行时计算** | 未列出 |
| `0x3036` | PLL 倍频系数 | `0x69 (105)` | **`0x69 (105)`** | **`0x7b (123)`，运行时计算** | 未列出 |
| `0x3037` | PLL 根分频/预分频 | `0x13` (根分频 2、预分频 3) | `0x13` (根分频 2、预分频 3) | `0x13` | 未列出 |
| `0x3108` | 系统根时钟分频 | `0x01` | `0x01` | `0x01` | 未列出 |
| `0x460c` | DVP PCLK 自动/手动选择 | `0x20`，bit1=0，自动 | **`0x20`，bit1=0，自动** | **`0x22`，bit1=1，手动** | 未列出 |
| `0x3824` | DVP PCLK 手动分频值 | `0x01` | **`0x01`** | **`0x02`** | 未列出 |
| `0x4837` | MIPI PCLK period | `0x22` | `0x22` | DVP 路径不作为主要配置项 | 未列出 |

当前表在 `0x460c=0x20` 下选择了 DVP PCLK 自动模式，因此同一表中的
`0x3824=0x01` 不应被单独解释成“当前 PCLK 必然除以 1”。数据手册规定
`0x460c[1]=0` 时由自动模式控制 DVP PCLK；只有 bit1=1 时才由 `0x3824`
控制。PLL 字段定义见 OV5640 数据手册 [PDF p.83](src/ov5640/docs/OV5640_CSP3_DS_2.01_Ruisipusheng.pdf)，
PCLK 手动选择见 [PDF p.131](src/ov5640/docs/OV5640_CSP3_DS_2.01_Ruisipusheng.pdf)，
MIPI `0x4837` 见 [PDF p.98](src/ov5640/docs/OV5640_CSP3_DS_2.01_Ruisipusheng.pdf)。

### 其他模式控制

这些寄存器主要影响翻转、模拟参数、曝光上限、BLC、JPEG/VFIFO 和 ISP，
不是造成 22.51 fps 的首要时序寄存器，但仍列出以避免只比较尺寸字段。

| 寄存器 | 寄存器作用 | 当前 800x480 | 当前 1024x768@30 | 主线低分辨率/XGA 参考 | 网络 WVGA 参考 |
| --- | --- | --- | --- | --- | --- |
| `0x3c07` | 光线计量阈值 | `0x08` | `0x08` | `0x08` | 未列出 |
| `0x3820` | ISP/传感器垂直翻转 | **`0x46`** | `0x41` | `0x41`，由控制项动态修改 | 未列出 |
| `0x3821` | 镜像与水平 binning | `0x07` | `0x07` | `0x07` | 未列出 |
| `0x3618` | 模拟电路控制 | `0x00` | `0x00` | `0x00` | 未列出 |
| `0x3612` | 模拟电路控制 | `0x29` | `0x29` | `0x29` | 未列出 |
| `0x3709` | 模拟控制 | `0x52` | `0x52` | `0x52` | 未列出 |
| `0x370c` | 模拟控制 | `0x03` | `0x03` | `0x03` | 未列出 |
| `0x3a02/03` | 60 Hz 最大曝光值 | **`0x0b88 (2952)`** | **`0x0b88 (2952)`** | **`0x03d8 (984)`** | 未列出 |
| `0x3a14/15` | 50 Hz 最大曝光值 | **`0x0b88 (2952)`** | **`0x0b88 (2952)`** | **`0x03d8 (984)`** | 未列出 |
| `0x4004` | BLC 行数 | `0x02` | `0x02` | `0x02` | 未列出 |
| `0x3002` | 重置 JPEG/FIFO | `0x1c` | `0x1c` | 主线模式表未列出 | 未列出 |
| `0x3006` | JPEG/JFIFO 时钟控制 | `0xc3` | `0xc3` | 主线模式表未列出 | 未列出 |
| `0x4713` | JPEG mode select | `0x03` | `0x03` | 主线模式表未列出 | 未列出 |
| `0x4407` | JPEG 量化比例 | `0x04` | `0x04` | `0x04` | 未列出 |
| `0x460b` | VFIFO/debug 控制 | `0x35` | `0x35` | `0x35` | 未列出 |
| `0x5001` | ISP 控制 | `0xa3` | `0xa3` | `0xa3` | `0xa3` |

### 对比结论

1. `d71214` 新增 800x480 表时，是从 XGA@30 表复制后只修改输出尺寸；
   当前 800x480 仍保留 `HTS=1896、VTS=984、0x3036=0x69`。
2. 当前 1024x768@30 表也使用同一组 HTS/VTS/PLL，因此两种输出尺寸出现
   相同的约 22.51 fps 是寄存器组合导致的结果，不是 `0x0400` 解析错误。
3. `cb7631` 只把 800x480 的 `0x3820` 从 `0x41` 改成 `0x46`，用于同步
   ISP 和传感器翻转，没有修改 PCLK、PLL、HTS 或 VTS。
4. ArduCAM 的 800x480 表是 JPEG/SPI 示例，使用 `HTS=3200、VTS=2000`，
   不能直接当作当前 Linux V4L2/DVP 30fps 表替换依据。

## 猜想与优先级

| 优先级 | 猜想 | 符合现象的原因 | 会削弱该猜想的证据 |
| --- | --- | --- | --- |
| 高 | CSI RxFIFO/HRESP 瞬态异常后进行了未同步的运行时恢复 | 丢失偶数字节 DVP 数据可表现为整像素水平位移；多次恢复扰动可累积残余偏移。 | 无 CSI 错误、纯 V4L2 路径无异常、Qt 前的 raw frame 也正常。 |
| 高 | DVP 信号完整性、PCLK/HSYNC/VSYNC 时序、sensor 输出或供电扰动触发初始 CSI 故障 | 低频物理扰动可随机发生，继而进入 CSI 恢复路径。 | 逻辑分析仪在复现时捕获到的信号完全稳定。 |
| 中 | 延迟或同时 DMA 完成后，FB1/FB2 软件队列失同步 | host 驱动有一个特殊的 simultaneous-done 跳过路径，未进行队列重建。 | 逐事件记录证明队列与 FB 地址始终一致。 |
| 中 | 预览/录制的 CPU、DDR 或存储负载放大了潜在 CSI 异常 | 录制和逐帧构造 QImage 会消耗 CPU/内存带宽，尽管采集线程会很快 QBUF。 | 无 Qt、无录制的 V4L2 soak 同样复现。 |
| 低 | Qt 预览绘制或永久的 stride/格式错误 | 应用在绘制前已拥有独立 QImage，固定错误也应从第一帧开始。 | 屏幕异常但同一时刻保存的源 QImage snapshot 正常。 |

## 验证方案

### 0. 最低必懂工具模型

| 工具/接口 | 本次用途 | 它证明什么 | 它不能证明什么 |
| --- | --- | --- | --- |
| `v4l2-ctl` | 对 `/dev/video1` 发起格式、帧率和 MMAP streaming ioctl。 | 驱动是否能长期完成 `DQBUF -> QBUF`，以及实际协商格式。 | 它不会直接调用 OV5640 的 `.s_stream`，也不显示画面。 |
| `dmesg` | 读取内核环形日志；`-w` 持续输出。 | 是否出现 RxFIFO overflow、HRESP error、timeout 等内核事件。 | 没有日志不等于绝对无硬件异常，某些分支仅 `pr_debug`。 |
| `/proc/interrupts` | 读取每个 IRQ 的累计次数。 | CSI 中断在测试期间是否持续推进，或是否在故障前后停止/异常跳变。 | 不能只凭 IRQ 数量判断某一帧的内容正确。 |
| `imx6-sm-camera-test` | 相同 `CameraDevice` 用户态采集链路，但可不录制。 | Qt/CameraDevice 预览在较低应用负载下是否复现。 | 不能单独排除 CSI 或 sensor。 |
| `imx6-smart-monitor` | 实际 preview + presence recording 场景。 | 录制、存储和 UI 负载是否是诱因。 | 不能单独证明 Qt 是直接根因。 |
| `ov5640_test` | 直接从 V4L2 拷到 framebuffer 的旧测试程序。 | 绕开 Qt `QImage/QPainter` 后能否看到同类画面异常。 | 它会循环 AF，是压力对照而不是纯净基线。 |

所有命令都在板端执行。以下验证会在 `/tmp` 写入日志，正常情况下不会修改
仓库、驱动配置或 sensor 寄存器。除模块已缺失等明确情况外，不要在开始测试前
reload 模块，以免掩盖长时间运行才能触发的状态。

### 1. 第一次操作前的板端检查

先确保没有正在运行的相机客户端。此步骤不开始采集，也不改变格式。

```bash
DEV=/dev/video1

command -v v4l2-ctl
command -v timeout
ls -l "$DEV"
v4l2-ctl --list-devices
v4l2-ctl -d "$DEV" --info
v4l2-ctl -d "$DEV" --all
v4l2-ctl -d "$DEV" --list-formats-ext
```

判读方式：

- `command -v` 必须分别打印 `v4l2-ctl` 和 `timeout` 的可执行路径；若缺少
  `v4l2-ctl`，先检查 target rootfs 的 `v4l-utils`，不要改驱动。
- `--info` 应显示 `Video Capture` 和 `Streaming` capability；`--all` 用于留存
  driver/card/bus 信息。
- `--list-formats-ext` 中应存在 `RGBP`。在 V4L2 fourcc 中，`RGBP` 对应当前
  用户态使用的 `V4L2_PIX_FMT_RGB565`。
- 若 `/dev/video1` 不存在，不要假定节点编号；先以 `--list-devices` 的实际
  输出替换 `DEV`。同一板端只能由一个客户端打开该节点。

可选地保存这一步的基础信息，后续所有测试都使用相同目录：

```bash
LOG=/tmp/ov5640-debug-$(date +%Y%m%d-%H%M%S)
mkdir -p "$LOG"
v4l2-ctl --list-devices > "$LOG/list-devices.txt" 2>&1
v4l2-ctl -d "$DEV" --all > "$LOG/v4l2-all.before.txt" 2>&1
v4l2-ctl -d "$DEV" --list-formats-ext > "$LOG/formats.txt" 2>&1
cat /proc/interrupts > "$LOG/interrupts.before-any-test.txt"
date -Iseconds > "$LOG/start-time.txt"
printf 'dev=%s\n' "$DEV" > "$LOG/test-config.txt"
```

### 2. 先完成短时 smoke test

短测试用于确认模式、工具和日志采集本身可用；它不能证明低频问题不存在。

```bash
v4l2-ctl -d "$DEV" \
  --set-fmt-video=width=800,height=480,pixelformat=RGBP \
  --set-parm=30 \
  --get-fmt-video --get-parm \
  --stream-mmap=4 --stream-count=120 --verbose
```

命令内部会执行 `S_FMT -> S_PARM -> REQBUFS/MMAP -> QBUF -> STREAMON ->`
循环 `DQBUF/QBUF`，最后自动 `STREAMOFF` 和关闭节点。`--stream-mmap=4` 请求
四个 MMAP buffer，与 Smart Monitor 当前的请求数一致；`--stream-count=120`
约为 4 秒的 30 fps 测试。

记录以下结果：

1. `--get-fmt-video` 的实际 `Width/Height/Pixel Format/Bytes per Line/Size Image`。
2. `--get-parm` 的实际 fps。驱动可能将不支持的请求调整到可支持的离散帧率。
3. `v4l2-ctl` 是否以 0 退出，是否出现 `VIDIOC_*`、timeout、`DQBUF` 错误。
4. `dmesg | tail -n 100` 中是否有 `overflow`、`Hresponse`、`ov5640` 或 `csi`
   相关的新错误。

- 实际参数为
  - Width 800
  - Height 480
  - Pixel Format RGB565
  - Bytes per line 1600
  - size Image 768000
- 实际fps：22.51
- 正常退出
- `dmesg | tail -n 100 | grep -Ei 'overflow|hresponse|ov5640|csi'`未显示错误

如 smoke test 已失败，停止后续 30 分钟实验，先在交接证据包中报告失败 ioctl、
驱动实际格式和完整 `dmesg` 片段。

### 3. 30 分钟纯 V4L2 基线

这个实验去掉 Qt 绘制和 JPEG 录制，用最少用户态工作负载持续驱动 CSI。输出丢到
`/dev/null`，避免因写入数十 GB raw RGB565 数据而人为制造存储压力。它不能直接
显示画面，但能回答“CSI 在无 Qt 情况下是否发生异常、是否能持续回收 buffer”。

在新的 shell 中执行以下完整命令。它会产生一个单独的 `/tmp` 日志目录。当前脚本
按 `--stream-count` 结束，未调用 `timeout`，因此不能把退出码 `124` 作为本脚本的
正常完成条件。

```bash
rm -f /tmp/ov5640-soak.sh

cat > /tmp/ov5640-soak.sh <<'EOF'
set -u

DEV=/dev/video1
WIDTH=800
HEIGHT=480
FPS=30
FRAME_COUNT=$((FPS * 1800))                 # 54000 帧 ≈ 1800s @30fps
LOG=/tmp/ov5640-v4l2-soak-$(date +%Y%m%d-%H%M%S)
HB=10                                       # 心跳间隔秒
WARN=60                                     # CPU/日志无进展多少秒后告警

mkdir -p "$LOG" || exit 1
command -v v4l2-ctl >/dev/null || { echo 'missing v4l2-ctl'; exit 1; }
[ -c "$DEV" ] || { echo "missing video node: $DEV"; exit 1; }

cleanup() {
    [ -n "${KMSG_PID:-}" ] && kill "$KMSG_PID" 2>/dev/null || true
    [ -n "${HB_PID:-}" ]   && kill "$HB_PID"   2>/dev/null || true
    cat /proc/interrupts > "$LOG/interrupts.after.txt"
    v4l2-ctl -d "$DEV" --get-fmt-video > "$LOG/fmt.after.txt" 2>&1 || true
    v4l2-ctl -d "$DEV" --get-parm      > "$LOG/parm.after.txt" 2>&1 || true
    date -Iseconds > "$LOG/end-time.txt"
}
trap cleanup EXIT INT TERM

date -Iseconds > "$LOG/start-time.txt"
cat /proc/interrupts > "$LOG/interrupts.before.txt"
v4l2-ctl -d "$DEV" --all > "$LOG/v4l2-all.before.txt" 2>&1

if dmesg --help 2>&1 | grep -q -- '-w'; then
    dmesg -w > "$LOG/kmsg.follow.log" 2>&1 &
    KMSG_PID=$!
else
    dmesg > "$LOG/kmsg.before.log" 2>&1
    KMSG_PID=''
fi

echo "==> log dir: $LOG"
echo "==> ${WIDTH}x${HEIGHT}@${FPS} RGBP, ${FRAME_COUNT} frames (~$((FRAME_COUNT/FPS))s)"
echo "==> heartbeat: every ${HB}s; warn if no progress for ${WARN}s"
echo

v4l2-ctl -d "$DEV" \
  --set-fmt-video=width="$WIDTH",height="$HEIGHT",pixelformat=RGBP \
  --set-parm="$FPS" \
  --get-fmt-video --get-parm \
  --stream-mmap=4 --stream-count="$FRAME_COUNT" \
  --stream-to=/dev/null --verbose > "$LOG/v4l2-stream.log" 2>&1 &
V4L_PID=$!
echo "==> v4l2-ctl pid=$V4L_PID"
echo

# -------- heartbeat / stall watchdog --------
(
    LAST_CPU=-1
    LAST_LOG=-1
    STALL=0
    while kill -0 "$V4L_PID" 2>/dev/null; do
        sleep "$HB"
        kill -0 "$V4L_PID" 2>/dev/null || break

        CPU=$(awk '{print $14+$15}' "/proc/$V4L_PID/stat" 2>/dev/null)
        [ -z "$CPU" ] && CPU=$LAST_CPU

        if [ -f "$LOG/v4l2-stream.log" ]; then
            SZ=$(wc -c < "$LOG/v4l2-stream.log" 2>/dev/null)
        else
            SZ=0
        fi
        [ -z "$SZ" ] && SZ=0

        if [ "$CPU" = "$LAST_CPU" ] && [ "$SZ" = "$LAST_LOG" ]; then
            STALL=$((STALL + HB))
            printf '[hb %s] pid=%s cpu=%s log=%sB STALL=%ss\n' \
                "$(date +%H:%M:%S)" "$V4L_PID" "$CPU" "$SZ" "$STALL"
            if [ "$STALL" -ge "$WARN" ]; then
                echo '[hb] *** no progress, tail of stream log: ***'
                tail -n 5 "$LOG/v4l2-stream.log" 2>/dev/null
                echo "[hb] *** if this persists: kill -TERM $V4L_PID ***"
            fi
        else
            STALL=0
            printf '[hb %s] pid=%s cpu=%s log=%sB ok\n' \
                "$(date +%H:%M:%S)" "$V4L_PID" "$CPU" "$SZ"
            LAST_CPU=$CPU
            LAST_LOG=$SZ
        fi
    done
) &
HB_PID=$!

wait "$V4L_PID"
RC=$?
kill "$HB_PID" 2>/dev/null || true

printf 'v4l2-ctl exit=%s\n' "$RC" > "$LOG/exit-status.txt"

if [ -z "${KMSG_PID:-}" ]; then
    dmesg > "$LOG/kmsg.after.log" 2>&1
fi

echo
echo "==> done, exit=$RC, logs: $LOG"
exit 0
EOF

echo '===== 前 10 行 ====='
sed -n '1,10p' /tmp/ov5640-soak.sh
echo '===== 尾 5 行 ====='
tail -n 5 /tmp/ov5640-soak.sh
echo

sh /tmp/ov5640-soak.sh
```

完成后复制了一份日志`cp -r /tmp/ov5640-v4l2-soak-19700101-011309/ /ov5640-v4l2-soak`

完成后执行：

```bash
LOG=/tmp/ov5640-v4l2-soak-19700101-011309
cat "$LOG/exit-status.txt"
grep -n -Ei 'Rx fifo overflow|Hresponse|timeout|error|ov5640|csi' \
  "$LOG"/kmsg*.log "$LOG"/v4l2-stream.log
diff -u "$LOG/interrupts.before.txt" "$LOG/interrupts.after.txt" | less
sed -n '1,220p' "$LOG/v4l2-stream.log"
```

结果：

```text
# LOG=/tmp/ov5640-v4l2-soak-19700101-011309
# cat "$LOG/exit-status.txt"
v4l2-ctl exit=0
# grep -n -Ei 'Rx fifo overflow|Hresponse|timeout|error|ov5640|csi' \
>   "$LOG"/kmsg*.log "$LOG"/v4l2-stream.log
/tmp/ov5640-v4l2-soak-19700101-011309/kmsg.after.log:71:SCSI subsystem initialized
/tmp/ov5640-v4l2-soak-19700101-011309/kmsg.after.log:169:imx2-wdt 20bc000.wdog: timeout 60 sec (nowayout=0)
/tmp/ov5640-v4l2-soak-19700101-011309/kmsg.after.log:208:usb 1-1.2: device no response, device descriptor read/64, error -32
/tmp/ov5640-v4l2-soak-19700101-011309/kmsg.after.log:209:usb 1-1.2: device no response, device descriptor read/64, error -32
/tmp/ov5640-v4l2-soak-19700101-011309/kmsg.after.log:211:usb 1-1.2: device no response, device descriptor read/64, error -32
/tmp/ov5640-v4l2-soak-19700101-011309/kmsg.after.log:234:camera ov5640, is found
/tmp/ov5640-v4l2-soak-19700101-011309/kmsg.after.log:235:mx6s-csi-ai-ref 21c4000.csi: bound sensor subdev ov5640 1-003c
/tmp/ov5640-v4l2-soak-19700101-011309/kmsg.after.log:236:mx6s-csi-ai-ref 21c4000.csi: i.MX6 CSI AI reference host registered
/tmp/ov5640-v4l2-soak-19700101-011309/kmsg.after.log:243:usb 1-1.2: device no response, device descriptor read/64, error 2
/tmp/ov5640-v4l2-soak-19700101-011309/kmsg.after.log:244:ov5640 1-003c: loaded ov5640_af.bin (4077 bytes)
/tmp/ov5640-v4l2-soak-19700101-011309/kmsg.before.log:71:SCSI subsystem initialized
/tmp/ov5640-v4l2-soak-19700101-011309/kmsg.before.log:169:imx2-wdt 20bc000.wdog: timeout 60 sec (nowayout=0)
/tmp/ov5640-v4l2-soak-19700101-011309/kmsg.before.log:208:usb 1-1.2: device no response, device descriptor read/64, error -32
/tmp/ov5640-v4l2-soak-19700101-011309/kmsg.before.log:209:usb 1-1.2: device no response, device descriptor read/64, error -32
/tmp/ov5640-v4l2-soak-19700101-011309/kmsg.before.log:211:usb 1-1.2: device no response, device descriptor read/64, error -32
/tmp/ov5640-v4l2-soak-19700101-011309/kmsg.before.log:234:camera ov5640, is found
/tmp/ov5640-v4l2-soak-19700101-011309/kmsg.before.log:235:mx6s-csi-ai-ref 21c4000.csi: bound sensor subdev ov5640 1-003c
/tmp/ov5640-v4l2-soak-19700101-011309/kmsg.before.log:236:mx6s-csi-ai-ref 21c4000.csi: i.MX6 CSI AI reference host registered
/tmp/ov5640-v4l2-soak-19700101-011309/kmsg.before.log:243:usb 1-1.2: device no response, device descriptor read/64, error 2
/tmp/ov5640-v4l2-soak-19700101-011309/kmsg.before.log:244:ov5640 1-003c: loaded ov5640_af.bin (4077 bytes)

--- /tmp/ov5640-v4l2-soak-19700101-011309/interrupts.before.txt
+++ /tmp/ov5640-v4l2-soak-19700101-011309/interrupts.after.txt
@@ -1,13 +1,13 @@
            CPU0       
- 16:     232206       GPC  55 Level     i.MX Timer Tick
- 19:      25612       GPC  26 Level     2020000.serial
+ 16:     285481       GPC  55 Level     i.MX Timer Tick
+ 19:      26093       GPC  26 Level     2020000.serial
  31:          0  gpio-mxc   1 Level     ap3216c
  39:          5  gpio-mxc   9 Edge      gt9147
  40:          0  gpio-mxc  10 Edge      ld2410c-out
  48:          0  gpio-mxc  18 Edge      GPIO Key0 Enter
  49:          0  gpio-mxc  19 Edge      2190000.usdhc cd
 198:          0       GPC   4 Level     20cc000.snvs:snvs-powerkey
-199:      23002       GPC 120 Level     20b4000.ethernet
+199:      39241       GPC 120 Level     20b4000.ethernet
 200:          0       GPC 121 Level     20b4000.ethernet
 201:          0       GPC  80 Level     20bc000.wdog
 204:          0       GPC  49 Level     imx_thermal
@@ -20,8 +20,8 @@
 224:         67       GPC  22 Level     mmc0
 225:         39       GPC  23 Level     mmc1
 226:         51       GPC  36 Level     21a0000.i2c
Log file: ed -n '1,220p' "$LOG/v4l2-stream.log"

```

补充diff，中断触发108000次，正好帧数的两倍

```text
-229:     131902       GPC   7 Level     mx6s-csi-ai-ref
+227:     390662       GPC  37 Level     21a4000.i2c
+229:     239902       GPC   7 Level     mx6s-csi-ai-ref
```

V4L2 流程正常退出；未采到 CSI 错误；持续时长、CSI IRQ 增量、实际帧数/帧率未证实；未观察图像内容，不能判断水平偏移是否发生。

结果判读：

| 观察 | 暂时结论 | 下一步 |
| --- | --- | --- |
| `exit=0`，`v4l2-stream.log` 证实完成预期帧数，无 CSI 错误，CSI IRQ 增长 | 纯 V4L2 基线暂未暴露问题；不能排除低频硬件异常。 | 继续 Camera Test/Smart Monitor 对照。 |
| 仅有 `exit=0`，未保存完整 stream log、持续时长或 CSI IRQ 差值 | 只能证明进程正常退出，基线证据不完整。 | 标记 A 为未完成，继续 B/C 对照并补强下一轮 A 采证。 |
| `VIDIOC_DQBUF`、`STREAMON` 或 timeout 失败 | 问题已经脱离 Qt，优先分析 V4L2/CSI 日志。 | 保留该目录，不要先 reload。 |
| 同一时间窗口出现 `Rx fifo overflow` 或 `Hresponse error` | 高优先级猜想获得板端证据。 | 故障发生时采集 JPG/raw frame，准备最小 CSI instrumentation。 |
| IRQ 不增长或突然停止 | CSI 中断/推流状态异常。 | 保存完整日志，检查 stream 进程状态后再讨论恢复。 |

### 4. UI 和负载的对照矩阵

每次只跑一个场景，每个场景使用相同的 `WIDTH/HEIGHT/FPS`，并至少运行到问题
通常出现的时长。每次开始和结束都执行第 1 节中的格式、日志和 IRQ 快照操作。
当前 A 仅为“V4L2 流程正常退出、证据不完整”，不作为已确认的无异常基线。

| 测试编号 | 场景 | 操作 | 用于区分 |
| --- | --- | --- | --- |
| A | 纯 V4L2 | 第 3 节的 `v4l2-ctl` soak。 | 基线 CSI/V4L2。 |
| B | Camera Test 预览 | `QT_QPA_PLATFORM=linuxfb imx6-sm-camera-test`，选择 RGBP，Start，不点 Record。 | `CameraDevice + QImage + Qt`，但无长时间文件写入。 |
| C | Smart Monitor | `QT_QPA_PLATFORM=linuxfb imx6-smart-monitor`，进入 `ActiveMonitoring`，确认页面出现 `REC`。 | 实际 controller、预览、QImage 编码和存储负载。 |
| D | 旧 framebuffer 测试，可选 | `ov5640_test /dev/video1 default`。 | 绕开 Qt 的直接显示对照；AF 循环是额外变量。 |

### 4.1 B/C 的共同约束

为了只比较应用层和录像负载，B/C 必须统一下列条件：

1. 只允许当前测试程序打开 `/dev/video1`；切换场景前先完全退出前一个客户端。
2. 统一使用 RGB565/RGBP `800x480`，并记录驱动实际协商的 fourcc、width、height、
   bytesperline、sizeimage 和 fps；请求 30 fps 不等于实际为 30 fps。
3. 固定相机、光照和包含垂直边缘、文字或网格的静态场景；补光固定为 Off，不使用
   Auto/Torch/Flash，避免把光照或 AF 操作混入变量。
4. 每轮记录 `date -Iseconds` 和 `cat /proc/uptime`。板端日期未校准时，以
   `/proc/uptime` 作为故障前后的相对时间；开始和结束保存 dmesg、`/proc/interrupts`、
   实际格式和 `df -h`/`free -h`。
5. B/C 运行相近时长，至少覆盖通常复现窗口。UI 帧计数持续增长只证明应用仍收到帧，
   不能证明像素内容正确。

### 4.2 测试 B：Camera Test 无录像预览

```bash
QT_QPA_PLATFORM=linuxfb imx6-sm-camera-test
```

1. 在 Preview 菜单选择与 A/C 一致的 RGB565/RGBP `800x480` 模式，点击 Start。
2. 保持 Capture 菜单的 RGB565/RGBP 模式一致，但在正常运行期间不点击 Record、
   Snapshot、AF，也不切换模式；这样 B 只保留 `CameraDevice + QImage + Qt` 预览路径。
3. 记录启动屏幕照片、active mode、帧计数、fps 和应用 Log；持续运行到目标时长。
4. 若出现偏移，按第 5 节先采证。此时可执行一次 RGB565 Snapshot，用来与同一时刻的
   屏幕照片比较；不要为截图临时切换到 JPEG 模式。

运行一段时间后发现出现偏移，用snapshot截图，同样出现相同偏移。UI执行`preview/hide`和`preview/show`之后，偏移保留，似乎因为后台streaming没有停流，右上角一直显示streaming。

尝试用`dmesg`抓错误输出，结果如下：

```text
# dmesg | tail -n 200 | grep -n -Ei 'Rx fifo overflow|Hresponse|timeout|error|ov5640|csi|skip'
29:SCSI subsystem initialized
127:imx2-wdt 20bc000.wdog: timeout 60 sec (nowayout=0)
188:camera ov5640, is found
189:mx6s-csi-ai-ref 21c4000.csi: bound sensor subdev ov5640 1-003c
190:mx6s-csi-ai-ref 21c4000.csi: i.MX6 CSI AI reference host registered
```

并未发现显著错误。

发现1024 * 768 偏移 128 pixel，测试其他分辨率，暂未发现类似情况

### 4.3 测试 C：Smart Monitor 预览加持续录像

```bash
QT_QPA_PLATFORM=linuxfb imx6-smart-monitor
```

1. 选择与 B 相同的 RGB565/RGBP `800x480` 预览模式，补光设为 Off。
2. 启动监控并使状态稳定进入 `ActiveMonitoring`；确认页面出现 `REC`，且
   `/smart-monitor/videos/presence-*.mjpeg` 的大小持续增长。
3. 保持 presence，避免 cooldown 自动停止采集；记录进入 `ActiveMonitoring` 的
   `/proc/uptime`、录像路径、初始文件大小和目标运行时长。
4. 若出现偏移，先拍屏、立即点 Snapshot，再按第 5 节采证。该 Snapshot 从当前
   `CameraDevice` 的 RGB565 `QImage` 保存，早于 `PreviewPane` 的 `drawImage()`。

### 4.4 B/C 结果判读

| B：Camera Test | C：Smart Monitor | screen 与 Snapshot | 暂时结论与下一步 |
| --- | --- | --- | --- |
| 复现 | 复现 | 两者均偏移 | 损坏最迟发生在 `CameraDevice` 生成 QImage 前；优先做第 6 节的 raw/metadata probe，不能仅据此断言 CSI 根因。 |
| 不复现 | 复现 | 两者均偏移 | 录像、JPEG 编码、存储 I/O 或实际监控负载可能放大采集链路问题；比较录制文件增长、CPU、内存、剩余空间和 CSI 日志。 |
| 不复现 | 复现 | Snapshot 正常、screen 偏移 | 优先检查 Smart Monitor 页面绘制或 display framebuffer；该结论只定位到 C 相对 B 新增的显示路径。 |
| 复现 | 任意 | Snapshot 正常、screen 偏移 | Camera Test 预览绘制或 display framebuffer 成为高优先级方向；固定静态场景后重复 Snapshot 对照。 |
| 不复现 | 不复现 | 未见偏移 | 本轮未覆盖触发条件，不排除低频问题；延长时长或补强 A 的 stream log、实际耗时和 CSI IRQ 证据。 |

对于 B/C：

1. 先确认应用没有报告 camera error，页面帧计数持续增长。
2. 记录启动时的模式文本、屏幕照片和 `dmesg` 时间点。
3. 故障出现时不要立即退出。先按第 5 节采证，再执行恢复操作。
4. B/C 任一轮的 screen 与 Snapshot 对照只能定位损坏最迟出现的位置，不能直接证明
   CSI、sensor 或 Qt 任一层是根因。
5. 若 D 也复现，则 Qt `QImage/QPainter` 的优先级下降；若仅 C 有屏幕异常而 C 的
   Snapshot 正常，才将排查重点下移到 UI 绘制或 display framebuffer。

### 5. 故障发生时的操作顺序

低频问题最容易因过早重启而丢失证据。出现偏移后按以下顺序执行：

1. 用手机拍下屏幕，画面中应包含能辨识偏移方向的静态物体或网格；记下当前
   时间到秒。
2. 立即在 Camera Test 或 Smart Monitor 点一次 RGB565 Snapshot。该 JPG 由
   `CameraDevice` 中的 `QImage` 编码，早于 `PreviewPane` 的 `drawImage()`；将文件
   路径写入证据包。Camera Test 不要为此切换到 JPEG capture mode。
3. 在另一终端执行，只读采证命令：

   ```bash
   date -Iseconds
   dmesg | tail -n 200
   cat /proc/interrupts
   v4l2-ctl -d /dev/video1 --get-fmt-video
   v4l2-ctl -d /dev/video1 --get-parm
   df -h /tmp /smart-monitor 2>&1
   free -h 2>&1
   ```

   如果应用独占 video node 导致 `--get-fmt-video` 或 `--get-parm` 失败，记录
   失败信息即可，不能为了执行它而先停止应用。
4. 对比 screen 照片与 snapshot JPG：两者都偏移，说明损坏最迟已发生在
   `QImage` 前；只有屏幕偏移而 JPG 正常，才优先检查 Qt/显示路径。
5. 完成采证后，在 UI 依次执行一次 Stop、等待画面关闭、再 Start。记录是否
   立即恢复。不要在同一轮测试中直接 reload module 或重新上电，除非用户明确
   指示且已完成上述取证。

### 6. 进一步定位需要的最小诊断工具

当 A/B/C 有任何一项复现后，最有效的下一步不是保存全量 raw video，而是新增
一个独立 V4L2 MMAP soak probe。它必须做到：

- 不使用 Qt，不显示、不编码、不录制；只保持 `DQBUF -> 检查 -> QBUF`。
- 每帧记录 `buffer.index`、`sequence`、timestamp、flags、bytesused，以及当前
  实际格式。
- 从固定 ROI 的多行像素估算相邻帧的水平位移；要求连续多帧得到一致偏移再判定，
  避免被运动物体误报。
- 仅在触发异常时落盘：异常前一帧、当前帧、异常后若干帧的 RGB565 raw 数据、
  metadata、`/proc/interrupts` 和可读取的 CSI 寄存器状态。
- 正常帧只保留轻量计数和周期统计，避免测试本身因磁盘写入制造 overflow。

该工具的验收标准不是“运行不报错”，而是能在一次真实偏移后回答：发生异常的
V4L2 sequence 是什么、raw frame 是否已偏移、前后是否有内核错误、buffer slot
和时间戳是否连续。只有这些证据齐全，才进入 CSI host 修复设计。

## 7. 本轮 raw probe：固化 MMAP 原始帧证据

### 7.1 本轮目标

本轮不修改 CSI 或 sensor 行为，只增加一个独立用户态采集程序
`ov5640_raw_probe`。它绕开 Qt、QImage、录像和显示，只执行：

```text
VIDIOC_S_FMT/S_PARM
  -> REQBUFS/MMAP
  -> STREAMON
  -> poll/DQBUF
  -> 复制 raw RGB565、记录 metadata、ROI 位移估算
  -> QBUF
```

它以第一帧为静态场景参考，在三条固定水平线、每 4 像素采样，搜索
`[-max_shift,+max_shift]` 的循环水平位移。只有非零位移连续达到阈值时才保存
故障前的环形帧、当前帧和后续帧；正常运行只写轻量 CSV，避免诊断工具本身制造
存储 I/O 或 CSI overflow。

### 7.2 编译与部署

probe 已加入 `src/ov5640/Makefile` 的 `app` 目标，并由
`bsp/package/ov5640/ov5640.mk` 安装到 `/usr/bin/ov5640_raw_probe`。使用现有
Buildroot 增量入口：

```bash
NFS_DIR=<nfs-dir> bash buildscripts/build_and_deploy.sh drv ov5640
```

该命令会重建 `ov5640` 本地包并把驱动包拥有的文件部署到 NFS。板端确认文件：

```bash
command -v ov5640_raw_probe
ov5640_raw_probe --help
```

如果只在主机验证编译：

```bash
make -C src/ov5640 app \
  CC="$(pwd)/buildroot/output/host/bin/arm-buildroot-linux-gnueabihf-gcc" \
  CFLAGS="" APP_CFLAGS="-Wall -Wextra -O2" \
  DEMO_CFLAGS="-Wall -Wextra -O2" RAW_PROBE_CFLAGS="-Wall -Wextra -O2"
```

实际交叉编译优先使用上面的 `build_and_deploy.sh`，以免手工 toolchain 前缀与
Buildroot 不一致。

### 7.3 板端操作

确保 Camera Test、Smart Monitor 和其他程序都已退出，只有 probe 打开 video node：

```bash
DEV=/dev/video1
LOG=/tmp/ov5640-raw-probe-$(date +%Y%m%d-%H%M%S)
rm -rf "$LOG"
mkdir -p "$LOG"

ov5640_raw_probe -d "$DEV" -o "$LOG" \
  -w 800 -h 480 -n 54000 -s 128 -k 3
echo "probe_exit=$?"
```

参数含义：`-n 54000` 约 30 分钟；`-s 128` 搜索最大正负 128 像素循环位移；
`-k 3` 要求连续 3 帧确认后才触发保存。测试场景必须固定，包含文字、网格或
明显垂直边缘；不要在运行中移动相机、切换格式、启动 AF 或启动其他录像程序。

输出文件：

```text
config.txt                 请求/实际格式、stride、sizeimage、参数和统计
metadata.csv               每帧 index/sequence/timestamp/bytesused/flags/位移分数
interrupts.before.txt      STREAMON 前 IRQ 快照
interrupts.after.txt       STREAMOFF 后 IRQ 快照
interrupts.anomaly.txt     首次确认异常时的 IRQ 快照
anomaly*-before-*.raw      异常前环形 raw 帧
anomaly*-after-*.raw       异常后的 raw 帧
```

这些文件都由 `ov5640_raw_probe` 在板端运行期间生成，来源如下：

| 文件 | 来源和用途 |
| --- | --- |
| `config.txt` | probe 创建文件后写入请求参数、驱动通过 `VIDIOC_S_FMT` 返回的实际 `width`/`height`/`bytesperline`/`sizeimage`，以及结束时的 `frames_seen`、`anomalies`。 |
| `metadata.csv` | 每次 `VIDIOC_DQBUF` 成功后写入一行；字段来自 V4L2 `v4l2_buffer`（`index`、`sequence`、时间戳、`bytesused`、`flags`）和 probe 的位移估算结果。第一行是表头。 |
| `interrupts.before.txt` | `VIDIOC_STREAMON` 之前读取 `/proc/interrupts` 的快照。 |
| `interrupts.anomaly.txt` | 首次满足连续位移阈值、确认异常的那个时刻读取 `/proc/interrupts` 的快照。 |
| `interrupts.after.txt` | `VIDIOC_STREAMOFF` 之后读取 `/proc/interrupts` 的快照。 |
| `anomaly*-before-*.raw` | 异常确认时从内存环形缓冲区保存的最近帧。 |
| `anomaly*-after-*.raw` | 异常确认后的后续 4 帧。 |

#### 环形 raw 帧从哪里来

probe 每次完成 `DQBUF` 后，将当前 MMAP buffer 的有效内容复制到 `copy`，再复制到
4 个槽位组成的环形缓冲区；新帧会覆盖最旧槽位。因此环形缓冲区不是额外的视频流，
而是 probe 已经收到的最近最多 4 帧在用户态内存中的副本。

当同一个非零 `shift` 连续达到 `-k` 指定的帧数时，probe 立即把环中现有帧保存为
`anomalyNNN-before-000000.raw` 等文件。当前帧会在完成本帧位移估算后、检查连续阈值
是否达到之前写入环形缓冲区，因此
`before` 文件组的最后一帧通常就是触发异常判定的当前帧；它表示“触发点前后紧邻的
历史帧”，并非严格意义上全部早于触发帧。随后再保存异常确认后的 4 帧为
`anomalyNNN-after-000000.raw` 到 `anomalyNNN-after-000003.raw`。

每个 `.raw` 文件没有文件头，内容是实际协商格式的整帧 RGB565 字节流。以本轮
`800x480`、`bytesperline=1600`、`sizeimage=768000` 为例，单帧文件应为 768000 字节；
具体解码参数必须以同一目录的 `config.txt` 为准。

#### 用 ffmpeg 查看 raw 帧

先把板端某个 raw 文件复制到有 `ffmpeg` 的主机（把目录名替换成实际值）：

```bash
scp <user>@<board-ip>:/tmp/ov5640-raw-probe-<timestamp>/anomaly001-before-000003.raw .
```

确认 `config.txt` 中实际值为 `800x480` 且 `bytesperline=1600` 后，将单帧转换为 PNG：

```bash
ffmpeg -y \
  -f rawvideo -pixel_format rgb565le -video_size 800x480 \
  -i anomaly001-before-000003.raw -frames:v 1 \
  anomaly001-before-000003.png
```

也可以批量转换当前目录下的所有异常帧：

```bash
for f in anomaly*.raw; do
  ffmpeg -y -loglevel error \
    -f rawvideo -pixel_format rgb565le -video_size 800x480 \
    -i "$f" -frames:v 1 "${f%.raw}.png"
done
```

`rgb565le` 对应当前 `V4L2_PIX_FMT_RGB565` 的低字节在前排列。如果 `config.txt` 中的
实际分辨率或 stride 不同，应先替换 `-video_size`；若 `bytesperline` 大于
`width*2`，文件含行尾填充，不能直接按上述 packed RGB565 命令解码，需要先按行去掉
padding。

### 7.4 关注点与判读

1. **raw 已偏移**：问题发生在 CSI DMA 写入 MMAP buffer 之前或之中，优先分析
   `mx6s_csi_irq_handler()`、`mx6s_csi_frame_done()`、CSI 恢复路径和 sensor DVP
   时序；同时用 `sequence/index` 与 FB1/FB2 DMA 地址做对应。
2. **raw 正常、Camera Test Snapshot 偏移**：问题位于 `CameraDevice::readFrame()`
   的 stride/拷贝或 QImage 交付路径，暂缓修改 CSI 驱动。
3. **位移出现但 metadata 连续**：不能据此排除 DMA；需要对比异常前后 raw 内容、
   `bytesused`、CSI IRQ 状态位和寄存器快照。
4. **`best_score` 与 `zero_score` 接近**：当前 ROI 区分度不足或场景在移动，
   该次位移判定无效，应更换静态场景，不要据此修复驱动。
5. **IRQ 总数约为帧数两倍**：与当前同时使能 SOF 和 FB1/FB2 DMA done IRQ 的实现
   相符；下一轮若需确认，应在驱动 instrumentation 中分别统计 SOF、FB1、FB2 和
   FIFO/HRESP/address 错误，不能只看总数。

故障发生后不要先 Stop/Start 或 reload 模块。先保留 raw、`metadata.csv`、两份
IRQ 快照和 `dmesg`；完成取证后再执行一次 Stop/Start，记录是否立即恢复。只有 raw
帧和 buffer/IRQ 证据同时指向 host 驱动，才进入最小驱动 instrumentation 或修复
设计。

实测结果：raw已经偏移，且同样偏移64列。但当前保存方式太重，最后只保存下来极少数raw数据，似乎受内存大小限制了，当前这个测试方案不太适用。

## 当前结论

- 偏移目前只出现在800 * 480分辨率下，且稳定偏移64列（单次），高度怀疑是时序相关配置问题导致出错
