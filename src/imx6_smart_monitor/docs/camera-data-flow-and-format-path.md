# Camera Data Flow And Format Path

本文展开 Smart Monitor Camera Test 中 sensor 之后的图像数据流、格式转换和保存语义。范围从 OV5640 输出到 `/dev/videoX` MMAP buffer，再到 Qt preview、snapshot、record 文件。

## 总体分层

```text
OV5640 sensor
  -> DVP 8-bit parallel bus
  -> i.MX6ULL CSI RxFIFO
  -> CSI embedded DMA FB1/FB2
  -> V4L2 VB2 MMAP buffer
  -> CameraCaptureThread
  -> CameraDevice facade
  -> CameraSaveWorker / PreviewWidget
```

CSI host 不做 JPEG 解码，也不把 JPEG 转 RGB。它只根据当前 V4L2 pixelformat 配置 CSI 宽高和 DMA buffer 大小。

## RGB565 路径

```text
OV5640 formatter: RGB565
  -> DVP byte stream, 2 bytes/pixel
  -> CSI DMA: sizeimage = width * height * 2
  -> CameraCaptureThread copies lines into QImage::Format_RGB16
  -> PreviewWidget draws QImage
  -> Snapshot: QImageWriter encodes JPEG to .jpg
  -> Record: scaled QImage is encoded as JPEG and appended to .mjpeg
```

语义：

- `bytesperline` 为 `width * 2`。
- Camera Test 的 preview 成功依赖 RGB565 buffer 拷贝和 Qt 绘制。
- Snapshot/Record 的 JPEG 质量来自 Camera Test 保存 worker，和 sensor JPEG 无关。

## JPEG 路径

```text
OV5640 JPEG encoder
  -> DVP 8-bit JPEG byte stream
  -> CSI DMA: fixed shell sizeimage = width * height * 2
  -> CameraCaptureThread scans SOI 0xffd8 and EOI 0xffd9
  -> deliver raw JPEG bytes to CameraDevice
  -> Snapshot: write bytes directly to .jpg
  -> Record: append each raw JPEG frame directly to .mjpeg
  -> restore RGB565 stream for preview when capture finishes
```

语义：

- Kernel 对 JPEG 使用 `bytesperline = 0`，`sizeimage = width * height * 2` 作为保守 DMA 上界。
- CSI frame done 路径把 `buffer.bytesused` 固定设置为 `sizeimage`，表示完整 DMA 外壳长度，而不是 JPEG 压缩后的真实长度。
- 用户态在这个扫描范围内只保存 SOI 到 EOI 之间的实际 JPEG payload，丢弃 CSI/VFIFO 外壳中的 padding 或尾部旧数据。
- `.mjpeg` 是连续 JPEG 帧拼接文件，没有额外容器 header。

## 模式切换

Camera Test 现在拆成两个选择：Preview 菜单只列 RGB565/RGBP；Capture 菜单列 RGB565/RGBP 与 JPEG/MJPG。用户在预览打开时点击 Snapshot/Record：

- Capture 选择 RGB565：保持当前 RGB565 stream，`deliverFrame(QImage)` 承担 preview、pending snapshot 和 record enqueue。
- Capture 选择 JPEG：先暂停 preview 显示并把 V4L2 stream 切到 JPEG，`deliverJpegFrame(QByteArray)` 承担 pending snapshot 和 record enqueue；动作完成后切回 Preview 菜单选中的 RGB565 模式继续预览。

JPEG 模式不再在 capture 线程中解码预览帧。这样可以避免 sensor JPEG 先解码成 RGB 再重新编码成 JPEG，也避免 JPEG decode 阻塞采集线程。

## 文件输出

| 操作 | RGB565 Capture | JPEG Capture |
| --- | --- | --- |
| Preview | RGB565 `QImage::Format_RGB16` 连续显示 | 不显示 JPEG 预览；采集完成后恢复 RGB565 预览 |
| Snapshot | 下一帧 `QImage` 编码成 `.jpg` | SOI/EOI 裁剪后的 raw JPEG 直接写 `.jpg` |
| Record | 低频采样 `QImage`，缩放后编码 JPEG，追加到 `.mjpeg` | SOI/EOI 裁剪后的 raw JPEG 帧追加到 `.mjpeg` |
| latest/current.jpg | 复制 snapshot 文件 | 复制 raw JPEG snapshot 文件 |

## 验证重点

- `v4l2-ctl --list-formats-ext` 能枚举 RGB565/RGBP 与 JPEG。
- JPEG Snapshot 文件头应以 `ff d8` 开始、以 `ff d9` 结束。
- JPEG Record 文件非空，且可以用支持 MJPEG 的工具拆帧或播放。
- `dmesg` 中不应出现持续 RxFIFO overflow、HRESP error 或 sensor I2C 写失败。

## 后续可优化项

- 将 sensor JPEG length 寄存器接入 kernel 或 userspace debug 路径，减少 SOI/EOI 扫描依赖。
- 暴露 JPEG quality/control 后，再对比 sensor quality、文件大小和 CSI 压力。
- 如果需要连续 JPEG 预览，可引入独立解码线程或硬件解码/转换路径，避免阻塞 capture 线程。
