# OV5640 JPEG Capture Path

本文记录当前 OV5640 JPEG 采集路径的实现边界、寄存器依据、host 协商语义和验证计划。它是 `src/ov5640` 中 JPEG 功能的当前权威入口。

## 目标

第一阶段打通：

```text
OV5640 sensor JPEG
  -> DVP 8-bit
  -> i.MX6ULL CSI
  -> V4L2 MMAP
  -> userspace SOI/EOI trim
  -> .jpg / .mjpeg
```

预览仍优先 RGB565；JPEG 路径以采集和保存为主。

## 资料依据

- `ATK-MC5640模块用户手册V1.0.pdf` p.3：模块支持 DVP 8-bit 与 JPEG 输出。
- `OV5640_CSP3_DS_2.01_Ruisipusheng.pdf` p.80-p.82：JPEG enable、JPEG/JFIFO/SFIFO reset、JPEG clock 等压缩控制寄存器。
- `OV5640_CSP3_DS_2.01_Ruisipusheng.pdf` p.91-p.92、p.128-p.132：DVP、JPEG、VFIFO 输出相关寄存器区域。
- `IMX6ULLRM_ch19_csi.pdf` p.6-p.7：CSI RxFIFO embedded DMA 按图像宽高写入 FB1/FB2。

## Sensor driver 实现

`ov5640.c` 新增 `MEDIA_BUS_FMT_JPEG_1X8`，并使用 `V4L2_COLORSPACE_JPEG`。JPEG 首批只允许：

- `640x480`
- `800x480`
- `1280x720`

帧率只允许 15/30fps 中模式表存在的组合。这样避免把尚未验证的大尺寸或高帧率组合暴露给用户态。

JPEG 格式应用时显式处理：

| 寄存器 | 作用 |
| --- | --- |
| `0x3821[5]` | JPEG enable。 |
| `0x3002` | JPEG/JFIFO/SFIFO reset 控制。 |
| `0x3006` | JPEG 相关 clock enable。 |
| `0x4400~0x440C`、`0x4430~0x4431` | JPEG control / ISI/comment control。 |
| `0x4600~0x460D` | VFIFO 输出宽高、dummy data 和控制。 |
| `0x4713` | DVP JPEG mode。 |
| `0x471F` | DVP HREF control。 |

非 JPEG 格式会先关闭 JPEG enable、复位 JPEG/JFIFO/SFIFO，并关闭 JPEG clock，避免从 JPEG 模式切回 RGB/YUV/Y8 时残留压缩路径状态。

## CSI host 实现

`mx6s_capture.c` 新增 `V4L2_PIX_FMT_JPEG` 映射：

- media-bus code：`MEDIA_BUS_FMT_JPEG_1X8`
- `VIDIOC_ENUM_FMT`：标记 `V4L2_FMT_FLAG_COMPRESSED`
- `bytesperline = 0`
- `sizeimage = width * height * 2`
- 并口 CSI 宽度按 `width * 2` 配置，作为固定传输外壳

host 不解码 JPEG，也不理解 JPEG 实际帧长。CSI/VB2 只负责把 byte stream 搬到 MMAP buffer。帧完成时驱动把 VB2 payload/`bytesused` 设置为 `sizeimage`，表示固定 DMA 外壳长度，避免用户态拿到 0 或旧 payload；真实 JPEG 长度仍不在 IRQ 路径计算。

## Userspace 语义

用户态按以下规则处理 JPEG buffer：

1. 使用 `buffer.bytesused` 作为扫描范围；当前 JPEG 下它等于固定 DMA 外壳 `sizeimage`。
2. 从第一个 SOI `0xff 0xd8` 开始。
3. 到第一个 EOI `0xff 0xd9` 结束，得到真实 JPEG payload length。
4. Snapshot 直接写 `.jpg`。
5. Record 把每帧裁剪后的 JPEG payload 追加到 `.mjpeg`。

第一阶段不读取 `0x4414~0x4416` JPEG length，因此 SOI/EOI 是 Smart Monitor 保存 `.jpg/.mjpeg` 的实际长度来源。直接用 `v4l2-ctl --stream-to` 保存时得到的是固定外壳连续流，可能包含 padding，适合 smoke test，不等同于已经裁剪好的单帧 JPEG 文件。

## 验证

构建：

```bash
NFS_DIR=<nfs-dir> bash buildscripts/build_and_deploy.sh drv ov5640
```

板端枚举：

```bash
modprobe ov5640
modprobe mx6s_capture
v4l2-ctl -d /dev/video1 --list-formats-ext
```

JPEG 采集：

```bash
v4l2-ctl -d /dev/video1 \
  --set-fmt-video=width=640,height=480,pixelformat=JPEG \
  --stream-mmap --stream-count=30 \
  --stream-to=/tmp/ov5640-vga.mjpg
```

Smart Monitor：

```bash
QT_QPA_PLATFORM=linuxfb imx6-sm-camera-test
```

预期结果：

- `--list-formats-ext` 出现 `JPEG`。
- 三组首批尺寸可完成 stream count。
- `dmesg` 无持续 RxFIFO overflow/HRESP error。
- Camera Test 中 Preview 只走 RGB565；Capture 选择 JPEG 后 Snapshot 生成可打开 `.jpg`，Record 生成非空 `.mjpeg`，完成后恢复 RGB565 预览。

## 当前限制

- 不新增 JPEG quality UI/control。
- 不做 RGB565/JPEG 双路并行。
- Kernel 不在 IRQ 路径读取 JPEG length，`bytesused` 暂表示固定 DMA 外壳长度。
- JPEG 不做预览解码；预览恢复依赖切回 RGB565。
