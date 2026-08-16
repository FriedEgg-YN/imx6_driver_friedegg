# 16 Camera 帧转换与独立所有权

## 目标与前置

把 [15-camera-mmap-preview.md](15-camera-mmap-preview.md) 中 DQBUF 得到的短暂 MMAP 数据转换为跨线程安全的值类型，分别实现 RGB565 到深拷贝 `QImage::Format_RGB16`、JPEG SOI/EOI 裁剪。转换函数尽量保持纯函数，以便主机测试。

前置：知道 active mode 是 `S_FMT` 回填结果，能区分 `bytesperline`、`sizeimage`、`bytesused`，理解 Qt implicit sharing 不会自动延长外部裸内存寿命。

## 最低必懂模型

本阶段分别处理 `V4L2_PIX_FMT_RGB565`（fourcc `RGBP`）和 JPEG/MJPEG capture payload。JPEG 是单帧压缩格式；MJPEG 是连续 JPEG 帧流，不能混为同一格式或容器。

`V4L2_PIX_FMT_RGB565`（fourcc `RGBP`）路径：

```text
MMAP bytes + width/height/bytesperline/bytesused
  -> 校验每行范围
  -> 分配 QImage(width, height, Format_RGB16)
  -> 逐行 memcpy 有效像素字节
  -> owning QImage
```

`QImage(data, ..., Format_RGB16)` 默认只是包装外部地址。即使 QImage 对象按值跨线程，像素仍可能指向已被 QBUF 归还的 MMAP；本阶段必须分配目标 `QImage` 并逐行复制，或对包装图像立即 `.copy()`，前者更容易检查 stride。

JPEG/MJPEG payload 路径：

```text
MMAP[0 .. bytesused)
  -> 找第一个 SOI FF D8
  -> 从 SOI 后找第一个 EOI FF D9
  -> 连同 marker 深拷贝到 QByteArray
```

`sizeimage` 用于驱动格式的最大/所需缓冲容量，不是 JPEG payload 长度；压缩帧必须以 `bytesused` 为扫描上界，并再限制到映射长度。

## V4L2/Qt 数据流

```text
CameraWorker DQBUF
  +-- RGB565 -> copyRgb565(...) -> QImage -> preview consumer
  |                                      `-> MediaWriter snapshot/recording
  `-- JPEG   -> extractJpeg(...) -> QByteArray -> MediaWriter
CameraWorker QBUF
```

转换发生在 CameraWorker 持有 DQBUF buffer 时。转换成功与否不改变 buffer 必须归还的规则。Service 只转发拥有数据的预览 `QImage` 和 typed status；后续 Writer 接收独立 `QImage`/`QByteArray`。

## 线程和 buffer 所有权

- 输入 `const uchar *` 只在 `DQBUF` 到 `QBUF` 窗口内有效，转换函数不得保存它。
- 输出 `QImage` 自己拥有已分配像素；queued signal 的按值副本通过 Qt 隐式共享安全读取。
- 输出 `QByteArray` 必须由复制构造得到，不能使用 `fromRawData()` 跨线程。
- 消费者把图像视为不可变值。若 Writer 要缩放/修改，生成自己的结果，不原地修改共享帧。
- CameraWorker 在 `QBUF` 后不再读输入指针；MediaWriter 永远看不到 MMAP 地址。

## 分步手写

1. 定义 `FrameLayout`，包含 width、height、pixelFormat、`bytesPerLine`、`sizeImage`，禁止用默认 0 悄悄兜底。
2. 写乘法溢出检查，验证 width/height 为正、RGB565 每行有效字节是 `width * 2`。
3. 验证 `bytesPerLine >= width * 2`，并计算最后一行末端是否落在 `min(bytesused, mappedLength)` 内。
4. 分配 `QImage(width, height, QImage::Format_RGB16)`；检查 `isNull()`。
5. 每行从 `src + y * bytesPerLine` 复制 `width * 2` 字节到 `image.scanLine(y)`；目标行剩余 padding 不参与像素。
6. 写 `extractJpegPayload()`：检查空输入与最小 4 字节，先 SOI 后 EOI，返回独立 `QByteArray`。
7. 将转换错误建模为 code + detail；不要用空图像同时表示“不支持”“截断”“未采到帧”。
8. 为两条纯函数写主机测试，再接入 CameraWorker。
9. 练习：构造带 source padding 的 2x2 RGB565 数据，证明第二行从 source stride 而不是 `width*2` 起始。
10. 练习：构造前后带垃圾字节的 JPEG，确认输出以 `FF D8` 开始、`FF D9` 结束。

## 关键伪代码/片段

RGB565 深拷贝核心：

```cpp
QImage image(width, height, QImage::Format_RGB16);
const size_t pixelBytes = static_cast<size_t>(width) * 2;
for (int y = 0; y < height; ++y) {
    const uchar *row = src + static_cast<size_t>(y) * bytesPerLine;
    std::memcpy(image.scanLine(y), row, pixelBytes);
}
return image; // 像素由 QImage 拥有
```

调用前至少满足：

```cpp
validBytes = qMin(static_cast<size_t>(buffer.bytesused), mapped.length);
required = static_cast<size_t>(height - 1) * bytesPerLine + width * 2u;
if (buffer.bytesused == 0 || required > validBytes) return truncated();
```

对于未压缩格式，若目标驱动实际把 `bytesused` 留为 0，不能未经验证直接按 `sizeimage` 猜测。先用板端日志确认该驱动行为，再把兼容规则写成显式、可测试的 backend 约束。

JPEG marker 裁剪：

```cpp
size_t soi = findPair(data, validBytes, 0xff, 0xd8, 0);
if (soi == npos) return invalidJpeg("SOI missing");
size_t eoi = findPair(data, validBytes, 0xff, 0xd9, soi + 2);
if (eoi == npos) return invalidJpeg("EOI missing");
return QByteArray(reinterpret_cast<const char *>(data + soi),
                  static_cast<int>(eoi + 2 - soi));
```

本项目的 marker 搜索是传感器帧边界清理，不是完整 JPEG 解码器。是否能解码仍可由 `QImage::fromData()` 或 Writer 在非 Capture 热路径验证。

## 检查点

- RGB565 输出每行只复制 `width*2`，源地址按 `bytesperline` 前进。
- 验证上界使用当前 `bytesused` 与 mapped length，`sizeimage` 只作布局/容量诊断。
- 输出 QImage 在原 buffer QBUF 并被下一帧覆盖后仍保持不变。
- JPEG 输出首尾 marker 正确，前后 padding 被裁掉。
- malformed frame 被丢弃并计数，采集循环仍能 QBUF。

## 常见错误

- `QImage(src, width, height, Format_RGB16)` 后直接 emit，以为按值传递就是深拷贝。
- 整块复制 `width * height * 2`，忽略 source stride。
- 把 `image.bytesPerLine()` 当 source `bytesperline`。
- JPEG 从 offset 0 写 `sizeimage` 字节，导致尾部垃圾或混入旧数据。
- 找到 EOI 却没确认此前存在 SOI，或裁剪结果漏掉 marker 自身。
- 用 `QByteArray::fromRawData` 包装 MMAP 地址交给 Writer。

## 交叉构建与板端验证

纯转换测试优先在主机执行；接入工程后：

```bash
"${QT_HOST_DIR}/bin/qmake" "${SOURCE_DIR}/app.pro"
make -j2
bash buildscripts/build_and_deploy.sh drv "${PACKAGE}"
```

板端：

```bash
QT_QPA_PLATFORM=linuxfb "${APP_BINARY}"
file "${SNAPSHOT_PATH}"
```

RGB565 模式观察颜色、行错位和撕裂；若整幅图周期性错行，优先打印 width、height、`bytesperline`、`sizeimage`、`bytesused`。JPEG 模式保存单帧后，预期 `file` 识别为 JPEG，十六进制检查首尾分别为 `ff d8`、`ff d9`。路径由 App 的脱敏测试配置生成，不在文档固定真实挂载点。

## 失败路径

- 布局整数溢出、stride 太小、有效字节不足：返回 conversion error，累计 invalid frame，仍归还 V4L2 buffer。
- QImage 分配失败：丢弃本帧并上报资源错误；不要发 null image 伪装成功。
- JPEG 缺 SOI/EOI：丢帧并限频记录错误，不能把不完整数据交给 Writer。
- 连续转换失败超过阈值：CameraWorker 转 Error 并停止 streaming，避免无限高速刷日志。
- Consumer 已关闭：Service 丢弃预览；拥有数据的临时值自然释放，不影响 QBUF。

## 完成标准

RGB565 和 JPEG 转换均有纯函数测试；RGB565 `QImage` 是真实深拷贝且处理 stride；JPEG 严格按 `bytesused` 范围裁剪 SOI/EOI；跨线程不暴露 MMAP 指针；坏帧不会破坏 buffer 回收。

## 复盘问题

1. Qt 隐式共享与外部内存所有权是什么关系？
2. 为什么最后一行所需长度不是简单的 `height * bytesperline`？
3. JPEG 为什么看 `bytesused` 而不是 `sizeimage`？
4. 转换失败时为什么仍要 QBUF？
5. 哪些转换逻辑适合纯函数测试，哪些必须板端验证？
