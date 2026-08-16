# 14 Camera 能力发现与类型建模

## 目标与前置

本阶段只完成 Camera 能力发现，不启动采集。权威边界见 [SPEC](../../SPEC.md)。完成后，Camera App 能异步展示设备身份、像素格式、分辨率、帧率和关键 control，并能区分“设备不可用”“能力不支持”和“查询 I/O 失败”。

前置知识：Linux 文件描述符、`ioctl`、V4L2 fourcc、Qt signal/slot、QObject thread affinity，以及 C++11 值类型。可对照旧实现的 [camera_device.cpp](../../../imx6_smart_monitor/camera/camera_device.cpp)，但不要整类复制；新设计拆为 `CameraService`、`CameraWorker` 和后续的 `MediaWriter`。

## 最低必懂模型

- `/dev/videoX` 是查询和采集入口，不要把节点存在等同于可采集。
- `VIDIOC_QUERYCAP` 回答设备能力；若 `V4L2_CAP_DEVICE_CAPS` 有效，应检查 `device_caps`，否则检查 `capabilities`。
- 本项目要求 `V4L2_CAP_VIDEO_CAPTURE` 和 `V4L2_CAP_STREAMING`。
- `VIDIOC_ENUM_FMT` 枚举像素格式；每种格式再用 `VIDIOC_ENUM_FRAMESIZES` 和 `VIDIOC_ENUM_FRAMEINTERVALS` 展开模式。
- V4L2 的 frame interval 是每帧时间 `numerator/denominator`，UI 帧率是其倒数 `denominator/numerator`。
- `VIDIOC_QUERYCTRL`/`VIDIOC_QUERYMENU` 描述 control；`VIDIOC_G_CTRL` 读取当前值。`DISABLED` 不是可操作能力，`READ_ONLY` 不能写。
- 驱动枚举出的能力不等于应用已实现的能力。第一版白名单明确区分 `V4L2_PIX_FMT_RGB565`（fourcc `RGBP`）、单帧 JPEG 和 MJPEG；后两者不能仅因都含 JPEG payload 就视为同一格式。

建议最小值类型：

```cpp
struct CameraMode {
    quint32 pixelFormat = 0;
    int width = 0;
    int height = 0;
    int timePerFrameNumerator = 0;
    int timePerFrameDenominator = 1;
};

struct CameraCapabilities {
    DeviceStatus status;
    QString driver;
    QString card;
    QList<CameraMode> modes;
    QList<CameraControlInfo> controls;
};
```

不要把 fourcc 只保存成显示字符串；整数值用于配置，字符串只用于 UI。

## V4L2/Qt 数据流

```text
CameraPage -> CameraController -> CameraService::queryCapabilities(owner)
                                      | queued command
                                      v
CameraWorker thread: open -> QUERYCAP -> ENUM_FMT/SIZE/INTERVAL -> QUERYCTRL
                                      | owning value snapshot
                                      v
CameraService -> CameraController -> CameraPage
```

`CameraService` 是 GUI 线程中的窄门面，负责参数预检、busy/pending 状态和事件转发；`CameraWorker` 独占 fd 并执行 V4L2 调用。Page 不包含 `<linux/videodev2.h>`。

## 线程和 buffer 所有权

- 查询 fd 由 `CameraWorker` 打开、使用并关闭，即使中途失败也必须关闭。
- capability 快照中的 `QString`/`QList` 拥有数据，可通过 queued signal 跨线程。
- V4L2 查询结构体只在 Worker 栈上存在，不跨线程发送其指针。
- 本阶段没有 MMAP buffer；不要为了查询能力提前申请采集 buffer。
- 自定义跨线程类型需要 `Q_DECLARE_METATYPE`，并在建立 queued connection 前 `qRegisterMetaType<T>()`。

`CameraMode` 统一保存 V4L2 的 `timePerFrame` 分子/分母；UI 展示时计算 `fps = denominator / numerator`，不要在模型中另存一个独立 fps。

## 分步手写

1. 定义 `CameraFormatInfo`、`CameraMode`、`CameraControlInfo`、`CameraCapabilities`，先写非法值和 unavailable 快照。
2. 为 Worker 写一个处理 `EINTR` 的 `xioctl` 小函数。
3. 在 Worker 线程用 `O_RDWR | O_NONBLOCK` 打开设备，查询并验证 capture/streaming capability。
4. 以 `index=0..` 枚举格式，`EINVAL` 表示枚举结束，其他 errno 表示失败。
5. 对 discrete frame size 枚举 interval；遇到 stepwise/continuous 时记录范围，不伪造巨大模式列表。
6. 枚举 controls，保存 id、type、min/max/step/default、flags、菜单项和可读取的当前值。
7. 只把应用支持的格式映射成“可选择模式”；保留完整原始能力供诊断展示。
8. Worker 关闭 fd，发出一个拥有自身数据的 `CameraCapabilities`。
9. Service 清除 pending，并转发 typed result；Controller 生成 UI 文案。
10. 练习：写 `choosePreferredMode()`，优先板上已验证的 RGB565 模式，但没有匹配时明确返回 invalid。

## 关键伪代码/片段

能力位检查：

```cpp
const quint32 caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS)
        ? cap.device_caps : cap.capabilities;
if (!(caps & V4L2_CAP_VIDEO_CAPTURE) || !(caps & V4L2_CAP_STREAMING))
    return unavailable(OperationCode::Unsupported, "capture/streaming unsupported");
```

类型化异步结果必须携带操作种类，不解析 `"failed:"`：

```cpp
enum class CameraOperation { QueryCapabilities, StartPreview, StopPreview,
                             SetControl, Snapshot };

struct CameraOperationResult {
    CameraOperation operation = CameraOperation::QueryCapabilities;
    OperationCode code = OperationCode::Succeeded;
    QString error;
};
```

`queryCapabilities()` 的同步门面只返回 `Accepted/Busy`；异步完成由 `capabilitiesChanged(...)` 与 `operationFinished(Succeeded/IoError, ...)` 表达。`Succeeded` 的阶段语义见 [SPEC 的类型化异步结果](../../SPEC.md#10-类型化状态与异步结果)。第一版同类请求只能有一个 pending，因此暂不增加 request ID。

## 检查点

- 能解释 interval 与 fps 为什么互为倒数。
- UI 同时展示“驱动枚举格式”和“应用可采集模式”，二者不混淆。
- 不支持的 control 不显示为可点击；read-only control 只读展示。
- 设备断开时得到 `Unavailable`，不是空列表加“正常”。
- 连续查询不会遗留 fd，GUI 在查询期间仍响应。

## 常见错误

- 只检查 `cap.capabilities`，忽略 `V4L2_CAP_DEVICE_CAPS`。
- 将枚举结束的 `EINVAL` 当成整个查询失败，或吞掉其他 errno。
- 用 `1/30 fps` 展示 `timeperframe=1/30`。
- 假设所有 frame size/interval 都是 discrete。
- 枚举到 JPEG 就断言一定是完整合法 JPEG；帧内容仍需按 [16-camera-frame-conversion.md](16-camera-frame-conversion.md) 校验 SOI/EOI。
- 从 GUI 线程直接 `open/ioctl`，导致设备异常时界面卡顿。

## 交叉构建与板端验证

源文件接入 qmake 后，在独立构建目录使用交叉 Qt：

```bash
"${QT_HOST_DIR}/bin/qmake" "${SOURCE_DIR}/app.pro"
make -j2
```

项目包接通后使用统一入口：

```bash
bash buildscripts/build_and_deploy.sh drv "${PACKAGE}"
```

板端先建立命令行基线，再启动 App：

```bash
v4l2-ctl -d "${VIDEO_DEVICE}" --all
v4l2-ctl -d "${VIDEO_DEVICE}" --list-formats-ext
QT_QPA_PLATFORM=linuxfb "${APP_BINARY}"
```

预期：App 列表与 `v4l2-ctl` 的核心格式、尺寸、帧率一致；拔掉或指定错误节点时异步显示 unavailable，窗口可继续操作。设备节点按实际板卡探测结果替换。

## 失败路径

- `open` 失败：返回 `Unavailable` 或 `IoError`，附 errno 上下文，不能继续枚举。
- `QUERYCAP` 失败或能力不符：关闭 fd，发布失败快照。
- 某格式的 size/interval 枚举失败：非结束错误应保留上下文并使本次查询失败，避免发布半真半假的“成功”。
- 当前值 `G_CTRL` 失败：可保留 control 元数据，但标记 current value 无效。
- Service/Controller 已销毁：Qt context connection 自动丢弃晚到事件；Worker 不访问 Page 指针。

## 完成标准

Camera App 可重复查询真实设备；能力、应用支持范围和错误语义清楚；所有 V4L2 I/O 位于 Camera Worker 线程；结果为拥有数据的类型；能据此选择下一阶段的有效 RGB565 模式。

## 复盘问题

1. 为什么 capability 枚举和应用支持列表必须分开？
2. `device_caps` 在什么条件下优先于 `capabilities`？
3. queued signal 为什么不能携带栈结构体指针？
4. “请求接受”与“查询成功”为什么是两个事件？
5. 若未来允许并发查询，typed result 还缺少什么字段？
