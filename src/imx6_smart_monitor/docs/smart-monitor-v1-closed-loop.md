# Smart Monitor v1 Closed Loop

本文记录 `imx6-smart-monitor` 从单点测试 launcher 收束到 Smart Monitor v1 产品闭环后的架构、状态机、session 产物和验收方法。

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
          -> camera/storage/strobe action
      -> CameraDevice
          -> /dev/videoX RGB565 preview
          -> snapshot from current preview frame
          -> AF / touch focus / torch control
      -> StorageManager
          -> /smart-monitor/sessions/<session>/
          -> /smart-monitor/latest/current.jpg
```

UI 层只显示 `MonitorSnapshot`、预览帧和 session 列表，只向 controller 发 Start/Stop、Snapshot、Torch、AF、touch focus、mode switch、Playback/Tools 切页命令。V4L2、IIO、LD2410 misc/UART 和 storage 文件写入都不下沉到 UI。

## 状态机语义

`MonitorCore` 是纯决策层，不访问 Qt 控件和设备节点。它接收三类状态输入：

| 输入 | 来源 | 作用 |
| --- | --- | --- |
| `handleSensorState()` | controller 汇总后的 `SensorHub` | presence pending/active/cooldown，lux hysteresis，torch wanted。 |
| `handleCameraState()` | `CameraDevice` signal | 更新 camera/error/mode/frame/AF 状态，camera error 时输出 retry action。 |
| `handleStorageState()` | `StorageManager` 调用结果 | 更新 session/degraded/error 状态。 |

关键输出集中在 `MonitorSnapshot`：

| 字段 | 含义 |
| --- | --- |
| `monitoringEnabled` | Start/Stop 总开关。Stop 时相机和 session 都应关闭。 |
| `presence` / `presenceSource` | presence gate 的状态和来源。当前以 LD2410C 为低延迟 gate。 |
| `lux` / `light` / `torchWanted` | AP3216C lux 与暗光滞回决策。 |
| `cameraWanted` / `cameraAction` | controller 是否应打开、关闭或 retry camera。 |
| `storage` / `storageAction` / `sessionId` | controller 是否应打开/关闭 monitor session，或显示 degraded。 |
| `activeMode` / `frameCount` / `afStatus` | UI 展示 camera 当前工作状态。 |
| `cameraError` / `storageError` | 异常验收入口。 |

presence 时间语义由 controller 的 Qt timer 表达：

| 阶段 | 触发 | 结果 |
| --- | --- | --- |
| `NoPerson -> PersonPending` | LD2410C presence true | 启动 confirm timer。 |
| `PersonPending -> ActiveMonitoring` | confirm timer 到期且 presence 仍 true | 打开 monitor session 和 RGB565 preview。 |
| `ActiveMonitoring -> Cooldown` | presence false | camera 保持 wanted，启动 cooldown timer。 |
| `Cooldown -> NoPerson` | cooldown timer 到期且 presence 仍 false | 关闭 camera 和 session。 |

## Storage 产物

monitor session 默认写入：

```text
/smart-monitor/
  latest/
    current.jpg
    status.json
  sessions/
    monitor-YYYYMMDD-HHMMSS/
      session.json
      events.jsonl
      index.jsonl
      frames/
        frame-YYYYMMDD-HHMMSS.jpg
```

`session.json` 描述 session id、类型、开始时间、camera device 和初始 mode。`events.jsonl` 记录 `session_open`、`snapshot_requested`、`snapshot_saved`、`camera_error`、`session_close` 等事件。`index.jsonl` 记录 JPEG 帧序号、路径、类型和状态。Playback 第一版只静态读取这些文件，显示 session、事件列表和 JPEG 帧，不做视频 seek、导出或容器封装。

## Camera 路径

Smart Monitor v1 的首屏预览只使用 RGB565/RGBP mode。自动 snapshot 和手动 snapshot 调用 `CameraDevice::requestSnapshot()`，由现有 save worker 将当前 RGB565 预览帧编码成 JPEG 并写入 monitor session。

OV5640 sensor JPEG 路径仍由 `imx6-sm-camera-test` 的 Capture 菜单验证。这样 v1 闭环避免自动监控期间频繁切换 RGB565/JPEG mode，预览稳定性优先。

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
| ActiveMonitoring | 打开 RGB565 preview，创建 `monitor-*` session，周期写 JPEG snapshot。 |
| 暗光 | lux 低于 enter 阈值后 `torchWanted`，controller 请求 torch；高于 exit 阈值后关闭。 |
| 手动 Snapshot | 当前 session 的 `frames/` 增加 JPEG，`index.jsonl` 增加一行。 |
| Touch focus / AF | 触摸预览区域后发送 frame 坐标；不支持 touch AF 时回退普通 AF 或显示 unsupported。 |
| Playback | 能看到 monitor session、事件列表和 JPEG 帧。 |
| 无人 cooldown | cooldown 到期后 camera closed，session 写 `session_close`。 |

存储验收：

```bash
find <nfs-dir>/smart-monitor/sessions -maxdepth 3 -type f | sort
cat <nfs-dir>/smart-monitor/latest/status.json
```

至少应看到：

```text
sessions/monitor-*/session.json
sessions/monitor-*/events.jsonl
sessions/monitor-*/index.jsonl
sessions/monitor-*/frames/*.jpg
latest/current.jpg
latest/status.json
```

异常验收：

| 场景 | 预期 |
| --- | --- |
| `/smart-monitor` 不可写 | UI storage 显示 `Degraded`，preview 和 presence 状态机继续运行。 |
| camera open 失败 | UI camera 显示 `Error`，若 session 已打开则写 `camera_error`，controller 周期 retry。 |

## 当前边界

- 不引入 GStreamer、QtMultimedia、libcamera、服务化或 AI 视觉算法。
- 不新增独立 `src/imx6_camera/` 包，继续复用 `src/imx6_smart_monitor/camera/CameraDevice`。
- 主页面不直接访问 V4L2/IIO/UART/Storage；设备封装不下沉到 UI。
- Playback 第一版只静态读取 JPEG 和 JSONL，不做视频 seek 或导出。
- UART 工程数据当前用于状态显示和事件记录的基础扩展点；presence gate 仍以 LD2410C 状态读取为主。
