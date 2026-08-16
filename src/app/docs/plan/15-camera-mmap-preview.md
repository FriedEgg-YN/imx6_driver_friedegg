# 15 Camera MMAP 采集与预览

## 目标与前置

在 [14-camera-capability.md](14-camera-capability.md) 的有效模式基础上，手写可重复 Start/Stop 的 V4L2 MMAP 采集循环，并将 RGB565 帧以约 10-15 FPS 投递 GUI。格式转换细节放在 [16-camera-frame-conversion.md](16-camera-frame-conversion.md)。

前置：能够解释 non-blocking fd、`select`、RAII、QObject affinity，并已从能力快照选出应用支持的模式。

## 最低必懂模型

初始化链路必须按顺序理解：

```text
S_FMT/S_PARM
  -> REQBUFS
  -> 对每个 index: QUERYBUF -> mmap
  -> 对每个 index: QBUF
  -> STREAMON
```

- `REQBUFS` 请求驱动建立 MMAP buffer 队列；返回的 `count` 可能小于请求值。
- `QUERYBUF` 返回指定 index 的长度和可用于 `mmap` 的 offset。
- `mmap` 建立用户虚拟地址到驱动 buffer 的映射，并不复制帧。
- 首次 `QBUF` 把所有 buffer 交给驱动；未入队的 buffer 不会被填充。
- `STREAMON` 在 buffer 已入队后启动采集。
- 采集步骤仍是 `ready -> DQBUF -> copy -> QBUF`，但 `CameraWorker` 不运行永久阻塞循环。Worker 使用 `QSocketNotifier`，或用短 single-shot `pump`；每次 ready/pump 只处理有限帧，及时返回事件循环，让 queued Stop/control 和 AF `QTimer` 有机会执行。

`bytesperline` 是每行跨度，可能大于 `width * bytesPerPixel`；`sizeimage` 是驱动为一帧声明的最大/所需存储量；`v4l2_buffer.bytesused` 是当前出队帧实际有效字节数。三者不能互换。

## V4L2/Qt 数据流

```text
GUI: CameraService::startPreview(mode)
              | queued command
              v
CameraWorker: open -> S_FMT -> REQBUFS/QUERYBUF/mmap/QBUF -> STREAMON
CameraWorker: socket-ready/short pump -> limited DQBUF/copy/QBUF -> return
              | throttled owning QImage
              v
CameraService -> Controller -> PreviewWidget
```

Service 保存公开状态和 pending operation，不保存 fd/MMAP 指针。Worker 不操作 Widget；Page 不直接停止线程或等待 fd。

## 线程和 buffer 所有权

| 时刻 | MMAP buffer owner | 允许的操作 |
| --- | --- | --- |
| `QBUF` 后、`DQBUF` 前 | 驱动 | 应用不得读写 |
| `DQBUF` 成功后 | CameraWorker | 校验 index/长度，复制必要数据 |
| 再次 `QBUF` 后 | 驱动 | 任何外部裸指针均失效 |

- fd、映射表、streaming 标志只属于 CameraWorker 线程。
- 跨线程传递的 `QImage` 必须拥有自己的像素数据，不能包装 MMAP 地址后立即 `QBUF`。
- 若 snapshot/recording 需要该帧，也必须在 `QBUF` 前复制为 `QImage`/`QByteArray`，再交给 Writer。
- 停止命令由 Worker 自己执行 `STREAMOFF -> munmap -> REQBUFS(count=0) -> close`。
- 预览节流只减少 GUI signal，不减少 DQBUF/QBUF；采集循环仍及时归还每个 buffer。

## 分步手写

1. 定义 `CameraState { Closed, Opening, Configuring, Streaming, Stopping, Error }` 和 `MappedBuffer { void *start; size_t length; }`，禁用 Worker 复制。
2. Worker 打开 `O_RDWR | O_NONBLOCK` fd，并重新验证 capability。
3. `VIDIOC_S_FMT` 后读取驱动回填的 width、height、pixel format、`bytesperline`、`sizeimage`；以回填值作为 active mode。
4. 可选设置 `VIDIOC_S_PARM`，同样保存驱动回填的实际 interval。
5. `REQBUFS(count=4, MMAP)`，要求至少 2 个；逐个 `QUERYBUF` 和 `mmap`。
6. 将全部 index `QBUF`，然后 `STREAMON(VIDEO_CAPTURE)`；只在成功后发布 Streaming。
7. 给 non-blocking fd 安装 `QSocketNotifier::Read`；或安排一个短 single-shot pump，不使用永久阻塞 `select`。
8. 每次 ready/pump 最多处理预设数量的 `DQBUF/copy/QBUF`；`EAGAIN` 立即返回，其他错误进入清理路径。
9. 校验 index、`bytesused <= mapped.length`，在 `QBUF` 前完成深拷贝/转换。
10. 无论当前帧是否投递预览，都尽快 `QBUF`；QBUF 失败时停止循环。
11. 用单调时钟按 67-100 ms 门限投递预览，统计 capture FPS 与 preview FPS；pump 返回后重新由事件循环调度。
12. Stop 时幂等清理；queued Stop、control 和 AF `QTimer` 必须能在下一轮事件循环执行。练习重复 Start/Stop 50 次并检查 fd/映射数量。

## 关键伪代码/片段

MMAP 建立片段：

```cpp
v4l2_buffer b = {};
b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
b.memory = V4L2_MEMORY_MMAP;
b.index = i;
xioctl(fd, VIDIOC_QUERYBUF, &b);
void *p = ::mmap(0, b.length, PROT_READ | PROT_WRITE,
                 MAP_SHARED, fd, b.m.offset);
```

采集循环要把“复制”和“归还”放在同一次迭代中：

```cpp
for (int n = 0; n < maxFramesPerPump; ++n) {
    v4l2_buffer b = {};
    if (!dequeueIfReady(fd, &b)) break; // EAGAIN: return to Qt event loop
    OwnedFrame frame = copyWhileDequeued(mapped[b.index], b, activeMode);
    const bool requeued = queue(fd, b);
    if (!requeued) break;
    publishConsumers(frame); // 此处不得再依赖 mapped 地址
}
// Callback returns; queued Stop/control and AF timer can run.
```

更稳妥的实现是在 `QBUF` 成功后才发 queued signal，但 `frame` 必须已是独立数据。异常处理需保证已经 DQBUF 的 buffer 要么重新 QBUF，要么立即结束 streaming。

预览节流：

```cpp
if (nowMs - lastPreviewMs >= 80) { // 约 12.5 FPS
    lastPreviewMs = nowMs;
    emit previewReady(frame.image);
}
```

不要用 `QTimer` 在 GUI 线程触发一次 `DQBUF`；由 CameraWorker 线程的 `QSocketNotifier` 或短 single-shot pump 维护采集，并让每次回调返回。

## 检查点

- 能画出每个 MMAP buffer 在驱动和 Worker 间的所有权转换。
- active mode 使用 `S_FMT` 回填值，而不是 UI 请求值。
- GUI 降到 10-15 FPS 时，capture FPS 仍接近设备实际帧率。
- Stop 后 `STREAMOFF`、全部 `munmap`、`REQBUFS(0)`、`close` 各执行至多一次。
- 模式切换先完整停止旧队列，再以新格式建立新队列。

## 常见错误

- `REQBUFS` 后直接 `STREAMON`，漏掉 `QUERYBUF/mmap/QBUF`。
- 假定驱动一定返回 4 个 buffer。
- 把 MMAP 地址封装为浅 `QImage` 跨线程，然后先 `QBUF`。
- 使用 `width * 2` 跨行，忽略 `bytesperline` padding。
- 用 `sizeimage` 当每帧实际 JPEG 长度，忽略 `bytesused`。
- 只在需要预览时 DQBUF，导致关闭显示后采集队列停住。
- 在 Worker 中使用永久阻塞 `select`/循环，依赖 queued Stop 才能退出，导致 Stop、control 或 AF timer 得不到执行机会。

## 交叉构建与板端验证

```bash
"${QT_HOST_DIR}/bin/qmake" "${SOURCE_DIR}/app.pro"
make -j2
bash buildscripts/build_and_deploy.sh drv "${PACKAGE}"
```

板端建立基线并运行：

```bash
v4l2-ctl -d "${VIDEO_DEVICE}" --stream-mmap=4 --stream-count=100 --stream-to=/dev/null
QT_QPA_PLATFORM=linuxfb "${APP_BINARY}"
```

观察 UI 的 active mode、capture FPS、preview FPS 和累计帧数。重复 Start/Stop、切换两个已验证 RGB565 模式；预期 UI 不冻结、帧继续变化，停止后可立即再次打开设备。可用 `ls -l /proc/<pid>/fd` 辅助确认 fd 不增长。

## 失败路径

- `S_FMT` 回填为应用不支持格式：在申请 buffer 前返回 `Unsupported`。
- 任一 `QUERYBUF/mmap/QBUF/STREAMON` 失败：沿统一清理路径释放已成功资源。
- `select` 被信号中断：`EINTR` 重试；持续 timeout 发布卡流诊断但仍响应 Stop。
- `DQBUF` 的 index 越界或 `bytesused > length`：不访问内存，结束 streaming 并报告协议错误。
- `QBUF` 失败：该队列已不可信，停止采集而不是继续循环。
- Stop 期间设备拔除：尽力 `STREAMOFF`，即使 ioctl 失败也继续 unmap/close，最终发布 stopped/error typed state。

## 完成标准

Camera App 可重复 Start/Stop 和切换已验证模式；`REQBUFS/QUERYBUF/mmap/QBUF/STREAMON` 与 `select/DQBUF/copy/QBUF` 顺序正确；GUI 无 V4L2 I/O；预览有节流且事件队列不持续增长；失败后资源可再次使用。

## 复盘问题

1. 为什么 `DQBUF` 后必须先复制再 `QBUF`？
2. `bytesperline`、`sizeimage`、`bytesused` 分别由谁产生、用于哪里？
3. 为什么预览节流不能通过减少 QBUF 实现？
4. `STREAMOFF` 之后为什么仍要 `munmap` 和 `REQBUFS(0)`？
5. 模式切换期间 Service 应如何表达 pending 与最终状态？
