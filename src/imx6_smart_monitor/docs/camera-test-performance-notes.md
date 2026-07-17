# Camera Test Performance Notes

本文记录 `imx6-sm-camera-test` 在 OV5640 + i.MX6ULL CSI 上的 RGB565 与 JPEG 采集路径、验证命令和当前限制。它面向板端验证和性能对比，具体数据流见 [`camera-data-flow-and-format-path.md`](camera-data-flow-and-format-path.md)。

## 当前结论

- RGB565 仍是首选预览路径：CSI MMAP 得到 RGB565 buffer 后，Qt 转成 `QImage::Format_RGB16` 显示；Snapshot/Record 再由 Qt 编码成 JPEG。
- JPEG 是首批采集/保存路径：sensor 侧输出 JPEG byte stream，CSI host 不解码，Camera Test 按 SOI/EOI 裁剪实际 JPEG 后保存。
- JPEG 作为 Snapshot/Record 的独立采集格式使用；预览始终走 RGB565。预览打开时触发 JPEG 采集会临时切换格式，完成后恢复 RGB565 预览。

## 资料依据

- `ATK-MC5640模块用户手册V1.0.pdf` p.3：ATK-MC5640/OV5640 模块支持 DVP 8-bit 与 JPEG 输出。
- `OV5640_CSP3_DS_2.01_Ruisipusheng.pdf` p.80-p.82：datasheet 给出 JPEG enable、JPEG/JFIFO/SFIFO reset、JPEG clock 等压缩控制寄存器。
- `OV5640_CSP3_DS_2.01_Ruisipusheng.pdf` p.91-p.92、p.128-p.132：datasheet 给出 DVP/JPEG/VFIFO 相关寄存器区域。
- `IMX6ULLRM_ch19_csi.pdf` p.6-p.7：i.MX6ULL CSI RxFIFO embedded DMA 按图像宽高写入 FB1/FB2。

## 支持矩阵

| 路径 | 首批尺寸 | 帧率 | 目标 |
| --- | --- | --- | --- |
| RGB565 | 现有模式表可枚举尺寸 | 现有模式表可枚举帧率 | 预览、截图、短录制基线。 |
| JPEG | `640x480`、`800x480`、`1280x720` | 15/30fps 中模式表实际存在的组合 | sensor JPEG 采集和保存。 |

## 验证命令

构建和部署：

```bash
NFS_DIR=<nfs-dir> bash buildscripts/build_and_deploy.sh drv ov5640
NFS_DIR=<nfs-dir> bash buildscripts/build_and_deploy.sh drv imx6-smart-monitor
```

板端枚举：

```bash
modprobe ov5640
modprobe mx6s_capture
v4l2-ctl -d /dev/video1 --list-formats-ext
```

预期 `--list-formats-ext` 中出现 `JPEG`，并能看到首批 JPEG 尺寸。

JPEG stream smoke test：

```bash
for mode in 640x480 800x480 1280x720; do
  w=${mode%x*}
  h=${mode#*x}
  v4l2-ctl -d /dev/video1 \
    --set-fmt-video=width=$w,height=$h,pixelformat=JPEG \
    --stream-mmap --stream-count=60 \
    --stream-to=/tmp/ov5640-${mode}.mjpg
done

dmesg | grep -i -E 'ov5640|csi|overflow|hresp|error'
```

Smart Monitor：

```bash
QT_QPA_PLATFORM=linuxfb imx6-sm-camera-test
```

在 Camera Test 中：

- Preview 菜单选择 RGB565/RGBP 模式并启动预览。
- Capture 菜单选择 JPEG 模式后，Snapshot 应生成可打开的 `.jpg`。
- Capture 菜单选择 JPEG 模式后，Record 应生成非空 `.mjpeg`。
- JPEG Snapshot/Record 完成后应自动回到 RGB565 预览；RGB565 snapshot/record 不应回退。

## 性能观察点

| 指标 | RGB565 | JPEG |
| --- | --- | --- |
| CSI DMA buffer | `width * height * 2` | 固定上界 `width * height * 2` |
| 用户态 CPU | 预览拷贝 + Snapshot/Record 编码 | SOI/EOI 扫描；不在采集线程中做 JPEG 预览解码 |
| 存储带宽 | Record 写 Qt 编码后的 JPEG 帧 | Record 直接写 sensor JPEG 帧 |
| 成功判据 | 预览显示、截图可打开、录制非空 | SOI/EOI 裁剪成功、`.jpg` 可打开、`.mjpeg` 非空 |

## 已知限制

- 第一阶段没有 JPEG quality UI/control，沿用固定寄存器默认值。
- 内核不读取 `0x4414~0x4416` JPEG length，`buffer.bytesused` 表示固定 DMA 外壳长度；实际 JPEG 长度由用户态扫描 SOI/EOI 得到。
- JPEG 路径不做预览解码；LCD 显示仍依赖 RGB565/RGB framebuffer 数据。
- 不做 RGB565 与 JPEG 双路并行采集，Camera Test 在 snapshot/record 前临时切换到采集格式。
