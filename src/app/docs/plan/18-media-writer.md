# 18 MediaWriter、截图与 MJPEG 写入

## 目标与前置

在 [16-camera-frame-conversion.md](16-camera-frame-conversion.md) 的独立帧基础上实现 `MediaWriter`：异步检查存储、生成唯一路径、保存 RGB565 预览截图或原始 JPEG、写入连续 JPEG bytes 的 MJPEG 文件，并用有界队列隔离慢存储。设计边界以 [SPEC](../../SPEC.md) 为准：第一版不是标准 MP4/AVI 容器，也不提供相册和回放。

前置：稳定 CameraWorker 采集、独立 `QImage`/`QByteArray`、Qt worker thread、文件短写处理和 typed async result。

## 最低必懂模型

- CameraWorker 的首要职责是及时 `DQBUF -> copy -> QBUF`；JPEG 编码、建目录和写文件不能占用 capture 线程。
- Preview、Snapshot、Recording 共用同一采集源，但消费频率不同：预览约 10-15 FPS；截图按请求取一帧；录像按配置节流后入队。
- `V4L2_PIX_FMT_RGB565`（fourcc `RGBP`）截图由 Writer 使用 `QImageWriter` 编码 JPEG；单帧 JPEG capture payload 可直接保存，MJPEG capture 则按独立 JPEG 帧入队。capture 格式与输出的连续 JPEG bytes 文件不是同一概念。
- 第一版 MJPEG 文件是多个完整 JPEG payload 顺序拼接。它可用于验证链路，但不是带索引、时间戳和音视频同步的标准容器。
- Writer queue 必须固定上限。满时丢弃录像帧并累计 dropped，不阻塞 CameraWorker；snapshot/control 命令不能被无限录像帧淹没。
- `startRecording()` 返回 Accepted 只表示命令入队；文件成功打开后才发布 Recording。`stopRecording()` 也是异步的，文件 flush/close 后才发布最终结果。

## V4L2/Qt 数据流

```text
CameraWorker DQBUF -> owning CameraFrame -> QBUF
                         |
                         +-> throttled QImage -> CameraService -> GUI preview
                         +-> one frame -> MediaWriter snapshot queue
                         `-> rate-limited frame -> bounded recording queue

MediaWriter thread: storage check/path -> encode or raw JPEG -> write/flush/close
                         |
                         v
CameraService -> typed MediaOperationResult / RecordingStatus / dropped count
```

`CameraService` 协调 capture 与 writer 的命令顺序，但不写文件。`MediaWriter` 不访问 V4L2、不决定 presence/lux 策略，也不持有 Page。

## 线程和 buffer 所有权

- CameraWorker 在入队前已将 MMAP 数据深拷贝；Writer 永远不接收 MMAP 指针。
- `QImage`/`QByteArray` job 按值拥有数据。入队后 CameraWorker 不修改；Writer 可读取或生成新的编码 bytes。
- Writer 线程独占录像 `QFile`，只由它 open/write/flush/close。
- `MediaWriter` 使用 QObject 事件循环模型，但帧不能先逐个变成无界 queued event。跨线程 ingress 先在短临界区检查固定容量并立即接受/拒绝，只在队列从空变为非空时 queued 调度一次 `processNext()`。
- `processNext()` 每次只取一个 job，离开临界区后再编码/写盘，完成后按需调度下一次；不用永久 `QWaitCondition` 循环。
- Service/Page 销毁不等于 Writer job 自动取消；显式 stop/abort 协议决定是排空还是丢弃。
- 正常 Stop 建议停止接收新录像帧，处理已约定的队列策略，然后关闭文件；进程退出可有界等待，超时记录错误。

## 分步手写

1. 定义 `MediaFrame` tagged 语义：`RgbImage` 或 `JpegBytes`，二者只能有一个有效；不要靠“哪个字段非空”隐式猜测。
2. 定义 `MediaOperation { CheckStorage, SaveSnapshot, StartRecording, StopRecording }`、状态和 typed result。
3. 实现存储 root 异步检查：目录存在/可创建、可写；不要在 GUI 线程测试文件写入。
4. 实现唯一文件名：时间戳加进程内递增序号；在 Writer 线程用 `QFile` 的不覆盖语义最终确认。
5. 实现 snapshot job：RGB `QImageWriter(..., "JPEG")`；原始 JPEG 验证首尾 marker 后完整写入。
6. 实现 start recording：校验状态和路径，成功 open 后发布 Recording；失败发布 `IoError`。
7. CameraService 只按目标录像帧率提交帧，例如每 200 ms 一帧；这与预览节流独立。
8. Writer 使用固定容量 ingress，例如 8 帧；CameraWorker 在跨线程边界处立即得到 accepted/rejected，满时 `dropped++`，绝不先堆积 Qt queued events 或等待空位。
9. Writer 优先处理 stop/control/snapshot，避免录像帧让停止无限延迟；`processNext()` 每次处理一个 job 后返回事件循环。明确 Stop 时剩余录像帧是排空还是丢弃，第一版可丢弃并计数以缩短切页时间。
10. 每次 write 检查返回字节数；stop 时 flush/close，发布 path、writtenFrames、droppedFrames 和 error。
11. Service 停止顺序：停止向 Writer 投帧 -> 异步 stop recording -> 收到 stopped/failed -> stop CameraWorker。
12. 练习：人为把 Writer 每帧延迟 500 ms，证明 capture FPS/GUI 不受阻且 dropped 增长。

## 关键伪代码/片段

类型化结果：

```cpp
struct MediaOperationResult {
    MediaOperation operation = MediaOperation::CheckStorage;
    OperationCode code = OperationCode::Succeeded;
    QString path;
    quint64 writtenFrames = 0;
    quint64 droppedFrames = 0;
    QString error;
};
```

有界队列入队必须是非阻塞策略：

```cpp
bool MediaWriter::tryEnqueueRecordingFrame(const MediaFrame &frame)
{
    QMutexLocker lock(&queueMutex_);
    if (!acceptingFrames_ || frames_.size() >= maxQueuedFrames_) {
        ++droppedFrames_;
        return false;
    }

    const bool needSchedule = frames_.isEmpty() && !processScheduled_;
    frames_.enqueue(frame);
    if (needSchedule) {
        processScheduled_ = true;
        QMetaObject::invokeMethod(this, "processNext", Qt::QueuedConnection);
    }
    return true;
}

void MediaWriter::processNext()
{
    const MediaFrame frame = takeOneUnderLock();
    writeOneWithoutHoldingQueueLock(frame);
    scheduleNextUnderLockIfNeeded();
}
```

这只是并发不变量的示意，不是完整实现。调用侧先形成 owning frame；`tryEnqueueRecordingFrame()` 是唯一允许跨线程直接调用的窄入口，其全部共享状态受同一 mutex 保护。Writer 不在锁内执行 JPEG 编码或文件 I/O，也不能让多个 producer 重复调度消费者。练习时需要自行补齐 `processScheduled_` 的清除和“入队恰好发生在消费者准备退出时”的竞态测试。

可靠写完整 bytes：

```cpp
qint64 offset = 0;
while (offset < bytes.size()) {
    const qint64 n = file.write(bytes.constData() + offset, bytes.size() - offset);
    if (n <= 0) return ioError(file.errorString());
    offset += n;
}
```

JPEG 原始帧入队前已按 `bytesused` 裁剪到 SOI `FF D8` 与 EOI `FF D9`；Writer 可再次做低成本首尾校验，但不应重新扫描 `sizeimage` padding。

## 检查点

- 慢写入时 capture FPS 基本稳定，录像队列长度不超过固定上限。
- dropped 是 typed 数值统计，不藏在 `"recording: dropped 3"` 字符串中。
- RGB snapshot 可被 JPEG 解码器打开；原始 JPEG 首尾 marker 完整。
- start/open 成功前状态不是 Recording；stop/close 完成前状态不是 Idle。
- 页面切换先停止投帧和 Writer，再释放 Camera owner。
- storage 不可写只禁用 snapshot/recording，不阻止预览和 Smart Monitor 状态机展示。

## 常见错误

- 使用无界 `QQueue`，慢存储最终耗尽内存。
- 队列满时阻塞 CameraWorker，造成 V4L2 buffer 饥饿。
- 在 CameraWorker 中调用 `QImage::save()` 或 `QImageWriter`。
- 一次 `QFile::write()` 返回正数就假设全部写完。
- 把 MJPEG 字节流称为 MP4/标准视频容器。
- start API 返回 Accepted 后立即向 UI 宣称文件已创建成功。
- Stop 只清空队列但不 flush/close，或用永久 `QWaitCondition`/无界 `wait()` 阻塞 Writer 事件循环。

## 交叉构建与板端验证

```bash
"${QT_HOST_DIR}/bin/qmake" "${SOURCE_DIR}/app.pro"
make -j2
bash buildscripts/build_and_deploy.sh drv "${PACKAGE}"
```

板端：

```bash
QT_QPA_PLATFORM=linuxfb "${APP_BINARY}"
file "${SNAPSHOT_PATH}" "${RECORDING_PATH}"
df -h "${STORAGE_ROOT}"
```

验证 RGB565 snapshot、原始 JPEG snapshot、开始/停止短录像、连续截图、存储只读/空间不足和录像中退出页面。预期：文件路径唯一；截图可识别；MJPEG 文件非空且可按 JPEG marker 拆帧；错误时 UI 仍响应；Camera App 随后可重新占用设备。

可用主机小测试扫描 `FF D8 ... FF D9` 对，确认写入帧数与 typed `writtenFrames` 一致。测试路径使用 `<storage-root>`，不要提交真实 NFS/挂载绝对路径。

## 失败路径

- storage root 不存在且创建失败/不可写：发布 `Unavailable`，预览继续。
- snapshot 编码器不可用或编码失败：删除不完整目标文件，发布 `IoError`。
- recording open 失败：保持 Idle，不接收录像帧。
- write 短写、ENOSPC、设备移除：停止接收帧、清队列、关闭文件，发布 `IoError` 和统计；Camera capture 继续由 Service 决定。
- queue 满：丢弃新录像帧、增加 dropped、限频上报；这不是 capture error。
- Stop/退出：正常切页异步等待最终 typed result；进程退出仅允许有界等待，超时记录并进入 abort 清理。

## 完成标准

MediaWriter 运行在独立线程并独占文件；RGB/JPEG snapshot 均可验证；MJPEG start/append/stop 状态明确；队列固定上限且满时只丢录像帧；慢存储不阻塞 GUI/Capture；结果包含 operation、code、path 和 dropped/written 统计，不使用状态字符串做分支。

## 复盘问题

1. 为什么预览节流与录像入队节流必须独立？
2. 有界队列满时为何选择丢帧而不是阻塞 capture？
3. `Accepted`、`Recording`、`Stopped` 分别需要什么证据？
4. 连续 JPEG bytes 与标准视频容器差在哪里？
5. storage 失败为什么不应让 presence 状态机或 Camera 预览失效？
