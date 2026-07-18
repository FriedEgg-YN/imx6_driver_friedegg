# Smart Monitor v1 Closed Loop

本文记录 `imx6-smart-monitor` 精简后的 record-first 闭环。目标不是覆盖所有硬件实验功能，而是突出一条适合面试讲解的主线：

```text
LD2410C presence -> MonitorCore -> CameraDevice record -> /smart-monitor/videos
```

## 最低必懂模型

Smart Monitor v1 是单进程 Qt 应用，主路径如下：

```text
SmartMonitorPage
  -> MonitorController
      -> SensorHub
          -> AP3216C lux / raw
          -> LD2410C presence / distance / energy
      -> MonitorCore
          -> monitoring enabled
          -> presence pending / active / cooldown
          -> dark/light hysteresis
          -> camera / recording / strobe action
      -> CameraDevice
          -> /dev/videoX RGB565 preview
          -> manual snapshot
          -> continuous MJPEG recording while person is present
          -> AF / touch focus / torch control
      -> StorageManager
          -> /smart-monitor/frames/<timestamp>.jpg
          -> /smart-monitor/videos/<timestamp>.mjpeg
```

UI 层只显示 `MonitorSnapshot`、预览帧和按钮状态，只向 controller 发 Start/Stop、Snapshot、strobe mode、AF、touch focus、mode switch 和 Tools 切页命令。V4L2、IIO、LD2410 misc/UART 和 storage 文件写入都不下沉到 UI。

## 状态机语义

`MonitorCore` 是纯决策层，不访问 Qt 控件和设备节点。它接收三类状态输入：

| 输入 | 来源 | 作用 |
| --- | --- | --- |
| `handleSensorState()` | controller 汇总后的 `SensorHub` | presence pending/active/cooldown，lux hysteresis，torch wanted。 |
| `handleCameraState()` | `CameraDevice` signal | 更新 camera/error/mode/frame/AF 状态，camera error 时输出 retry action。 |
| `handleStorageState()` | `StorageManager` 检查结果 | 更新 storage ready/degraded/error 状态。 |
| `handleRecordingState()` | `CameraDevice` record status | 更新当前录制路径和录制状态文本。 |

关键输出集中在 `MonitorSnapshot`：

| 字段 | 含义 |
| --- | --- |
| `monitoringEnabled` | Start/Stop 总开关。Stop 时相机和录制都应关闭。 |
| `presence` / `presenceSource` | presence gate 的状态和来源。当前以 LD2410C 为低延迟 gate。 |
| `lux` / `light` / `torchWanted` | AP3216C lux 与暗光滞回决策。 |
| `cameraWanted` / `cameraAction` | controller 是否应打开、关闭或 retry camera。 |
| `recordingWanted` / `recordingAction` | controller 是否应启动或停止人在期间录制。 |
| `recordingPath` / `recordingStatus` | UI 展示最近一次录制文件和状态。 |
| `activeMode` / `frameCount` / `afStatus` | UI 展示 camera 当前工作状态。 |
| `cameraError` / `storageError` | 异常验收入口。 |

presence 时间语义由 controller 的 Qt timer 表达：

| 阶段 | 触发 | 结果 |
| --- | --- | --- |
| `NoPerson -> PersonPending` | LD2410C presence true | 启动 confirm timer。 |
| `PersonPending -> ActiveMonitoring` | confirm timer 到期且 presence 仍 true | 打开 RGB565 preview，并启动 `.mjpeg` 录制。 |
| `ActiveMonitoring -> Cooldown` | presence false | camera 和 recording 先保持 active，启动 cooldown timer。 |
| `Cooldown -> NoPerson` | cooldown timer 到期且 presence 仍 false | 停止录制并关闭 camera。 |

## Storage 产物

Storage 默认写入：

```text
/smart-monitor/
  frames/
    snapshot-YYYYMMDD-HHMMSS-zzz.jpg
  videos/
    presence-YYYYMMDD-HHMMSS-zzz.mjpeg
```

`StorageManager` 只检查/创建 root、frames、videos，并生成带时间戳的唯一文件名。它不再维护 session、latest、JSON 或 JSONL 索引。这样目录结构和面试主线保持一致：截图是图片，人在期间录制是视频文件。

## Camera 路径

主页面预览优先使用 RGB565/RGBP mode。手动 Snapshot 调用 `CameraDevice::requestSnapshot()`，由保存 worker 将当前 RGB565 预览帧编码为 `.jpg`。

人在进入 `ActiveMonitoring` 后，controller 调用：

```cpp
camera.startRecording(path, 0);
```

`maxDurationMs = 0` 表示持续录制，直到无人 cooldown 结束或用户 Stop。Camera Test 仍传入 `5000`，保留 5 秒短录制实验行为。

`.mjpeg` 是连续 JPEG 帧拼接文件，没有额外容器 header。当前不引入 GStreamer、QtMultimedia 或 MP4 容器。

## 可配置项

| 配置 | 主页面行为 | 完整实验入口 |
| --- | --- | --- |
| 分辨率/帧率 | mode 下拉选择 RGB565/RGBP 预览模式；录制中禁用切换。 | Camera Test。 |
| Strobe | `Auto / Off / Torch`；Auto 根据 AP3216C lux 决定。 | Camera Test 可验证底层 control。 |
| LD2410C | 主页面提供配置入口。 | LD2410 Test 保留完整 misc/UART/工程模式配置。 |
| presence confirm/cooldown | `MonitorPolicy` 中保留。 | Core Test 可独立验证状态机。 |

## 验证

本地交叉编译：

```bash
cd src/imx6_smart_monitor
../../buildroot/output/host/bin/qmake imx6_smart_monitor.pro
make -j4
file bin/imx6-smart-monitor bin/imx6-sm-core-test
```

Buildroot 包构建和部署：

```bash
NFS_DIR=<nfs-dir> bash buildscripts/build_and_deploy.sh drv imx6-smart-monitor
```

单点回归：

```bash
QT_QPA_PLATFORM=linuxfb imx6-sm-ap3216c-test
QT_QPA_PLATFORM=linuxfb imx6-sm-ld2410-test
QT_QPA_PLATFORM=linuxfb imx6-sm-storage-test
QT_QPA_PLATFORM=linuxfb imx6-sm-camera-test
QT_QPA_PLATFORM=linuxfb imx6-sm-core-test
```

集成验收：

```bash
QT_QPA_PLATFORM=linuxfb imx6-smart-monitor
```

预期结果：

| 场景 | 预期 |
| --- | --- |
| Start 后无人 | `Monitor enabled`，presence 为 `NoPerson`，camera 保持 closed。 |
| 检测到人 | presence 先 `PersonPending`，confirm 后进入 `ActiveMonitoring`。 |
| ActiveMonitoring | 打开 RGB565 preview，生成 `/smart-monitor/videos/presence-*.mjpeg`，UI 显示 recording。 |
| 暗光 Auto strobe | lux 低于 enter 阈值后请求 torch；高于 exit 阈值后关闭。 |
| 手动 Snapshot | `/smart-monitor/frames/snapshot-*.jpg` 增加一张图片。 |
| Touch focus / AF | 触摸预览区域后发送 frame 坐标；不支持 touch AF 时回退普通 AF 或显示 unsupported。 |
| 无人 cooldown | cooldown 到期后停止录制并关闭 camera。 |

存储验收：

```bash
find /smart-monitor -maxdepth 2 -type f | sort
```

只应看到：

```text
/smart-monitor/frames/*.jpg
/smart-monitor/videos/*.mjpeg
```

不应再看到 `latest/`、`sessions/`、`session.json`、`events.jsonl` 或 `index.jsonl`。

异常验收：

| 场景 | 预期 |
| --- | --- |
| `/smart-monitor` 不可写 | UI storage 显示 `Degraded`，presence 状态机继续运行，不启动录制写文件。 |
| camera open 失败 | UI camera 显示 `Error`，controller 周期 retry。 |

## 当前边界

- 不提供 Smart Monitor Playback。
- 不引入 GStreamer、QtMultimedia、libcamera、服务化或 AI 视觉算法。
- 不新增独立 `src/imx6_camera/` 包，继续复用 `src/imx6_smart_monitor/camera/CameraDevice`。
- 主页面不直接访问 V4L2/IIO/UART/Storage；设备封装不下沉到 UI。
- UART 工程数据主要在 LD2410 Test 展示和调参；主闭环只依赖 presence gate。
