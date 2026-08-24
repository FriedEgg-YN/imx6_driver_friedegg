# App HMI 手写重构规格

## 1. 文档目的

本文定义 `src/app/` 的目标、第一版边界、数据与控制链路、线程和资源所有权、Smart Monitor 业务规则、手写实施顺序及验收标准。

本项目不是对现有 `src/imx6_smart_monitor/` 的原地整理，也不以构建通用 HMI 平台或追求最佳工程鲁棒性为首要目标。现有实现继续作为以下内容的参考：

- Linux 设备接口的已验证调用方式；
- AP3216C、LD2410C、Camera 和存储能力；
- presence、暗光补光、预览、截图和录像闭环；
- 已暴露出的同步 I/O、字符串协议、线程退出和职责耦合问题。

新实现首先服务于个人手写学习、全链路理解、板端演示和面试讲解。只有能支撑当前闭环、消除真实问题或为明确后续功能保留稳定边界的抽象，才进入第一版。

## 2. 设计优先级

发生取舍时，按以下顺序决策：

1. 从 Linux 驱动接口到业务和 UI 的完整数据链路可解释；
2. 每个阶段都有可运行、可观察或可测试的闭环；
3. 状态机、线程、缓冲区和对象所有权清楚；
4. 对设备缺失、I/O 失败和退出清理具备必要的稳定性；
5. 代码边界允许后续增加 App、设备能力或后台模式；
6. 通用性、框架完整性和极端场景鲁棒性。

“保留扩展性”不等于提前实现完整平台。第一版通过稳定值类型、清晰依赖方向、窄服务门面和可替换 backend 保留演进空间；lease token、通用 request 路由、插件扫描和后台服务等机制只在出现真实需求后加入。

## 3. 项目目标

### 3.1 第一版最终形态

程序启动后进入简化的 Qt Widgets HMI：

- 桌面显示若干 App 入口，同一时刻只有一个前台页面；
- Smart Monitor 是第一版主闭环和默认演示入口；
- AP3216C、LD2410C 和 Camera 页面承担设备可视化及板端测试职责；
- App 可以返回桌面并切换到其他 App；
- 页面不直接访问 Linux 设备节点；
- 离开使用 Camera 的页面时，先停止采集和写入，再允许其他页面使用 Camera；
- UI 以清楚展示状态、预览和常用操作为目标，不追求复杂视觉效果。

### 3.2 第一版简历主线

```text
AP3216C IIO sysfs ----+
                      |
LD2410C misc/ioctl ---+--> SensorWorker --> typed samples
                      |                         |
LD2410C UART ----------+                         v
                                           MonitorEngine
                                                |
                                                v
OV5640 / V4L2 --> CameraWorker <-- CameraService <-- MonitorController --> SmartMonitorPage
                         |              |
                         v              v
                    owning frame   typed status/result
                         |
                         v
                    MediaWriter --> JPEG / MJPEG
```

完成后应能用这条主线讲清：驱动 ABI、用户态读取、跨线程传值、状态机决策、V4L2 buffer 生命周期、图像格式路径、文件写入和 UI 展示。

### 3.3 学习目标

完成后应能清楚解释：

1. AP3216C IIO sysfs 属性如何映射为 lux/raw 样本；
2. LD2410C misc ioctl、OUT/input 和 UART 接口分别解决什么问题；
3. V4L2 capability、format、frame interval 和 control 如何枚举；
4. `REQBUFS -> QUERYBUF -> mmap -> QBUF -> STREAMON` 初始化链路；
5. `select -> DQBUF -> copy -> QBUF` 采集循环和 buffer 所有权；
6. RGB565 如何进入 `QImage::Format_RGB16`，JPEG 如何裁剪 SOI/EOI 并写入文件；
7. GUI 线程、设备 I/O 线程和媒体写入线程为何分离；
8. QObject affinity、queued signal/slot 和对象退出顺序如何配合；
9. presence confirm、Active、Cooldown 和暗光滞回状态机；
10. Page、Controller、Engine、Service/Worker 和 backend 各自负责什么；
11. 如何用类型化状态消除 `recording:/saved:/failed:` 等字符串协议；
12. 如何先单设备验证，再组合成 Smart Monitor。

## 4. 技术约束

### 4.1 技术选择

- UI：Qt 5 Widgets；
- 运行模型：单进程、单前台 App；
- 构建：Buildroot Qt5 和 qmake；
- C++：以 C++11 为基线；
- 测试：纯 C++ 断言或 Qt Test，优先覆盖状态机和纯转换逻辑；
- 平台插件：板端使用 `linuxfb`，触摸沿用 tslib/evdev 环境；
- 图像：GUI 预览使用 `QImage`；
- 配置：第一版使用集中默认配置，核心闭环稳定后再接 `QSettings`。

第一版不引入 QML、QtMultimedia、GStreamer、libcamera、数据库、网络服务、插件系统、通用依赖注入框架和跨进程 IPC。

### 4.2 C++ 使用原则

- 使用 RAII 管理文件描述符、MMAP 映射和纯 C++ 对象；
- 使用 `enum class` 表达有限状态和命令；
- 使用 deleted copy 防止设备对象被复制；
- 使用 `override`，不为展示语法设计继承层级；
- 纯 C++ 对象优先直接成员或 `std::unique_ptr`；
- QObject 优先 Qt parent ownership，不使用 `shared_ptr<QObject>`；
- 跨线程只传递拥有自身数据的值类型；
- 需要 queued signal/slot 的自定义类型使用 `Q_DECLARE_METATYPE`；
- 业务代码优先组合，继承层级原则上为 Qt 基类到项目具体类；
- 不提前建立通用 repository、command bus、event bus 或 service locator。

### 4.3 不设计统一 Device 基类

AP3216C、LD2410C 和 Camera 的接口、生命周期和错误模型不同，不抽象为统一的 `open/read/configure()` 基类。

设备间只共享通用语义，例如：

```cpp
enum class Availability {
    Unknown,
    Available,
    Unavailable,
};

struct DeviceStatus {
    Availability availability = Availability::Unknown;
    QString error;
    qint64 updatedAtMs = 0;
};
```

只有出现真实的第二种实现，例如真实 Camera backend 与 Fake Camera backend，才为该具体能力抽取窄接口。第一版测试可以直接测试纯 Engine，不要求为所有设备建立 Fake 体系。

## 5. 功能范围

### 5.1 第一版必须完成

#### Smart Monitor 主闭环

- 手动 Start/Stop 只表示启停 monitoring；
- 自动读取 LD2410C presence；
- 自动读取 AP3216C lux；
- presence 持续超过确认时间后启动 Camera 和录像；
- 短暂无人进入 Cooldown，期间保持 Camera 和录像；
- Cooldown 内再次有人直接恢复 Active，录像不中断；
- Cooldown 到期后停止录像、关闭 torch 和 Camera；
- Auto strobe 使用 lux 双阈值滞回；
- Camera Streaming 时支持截图、AF 和触摸对焦；
- 未录像时允许选择已验证的预览模式；
- storage 不可写时继续显示传感器和状态机状态，但不启动录像；
- 设备失败显示 unavailable/error，不能解释为无人或正常值。

第一版不在 Smart Monitor 页面提供“无人时手动打开 Camera”。独立预览和复杂 Camera 控制由 Camera App 承担，避免手动模式与自动状态机产生两套所有权语义。

#### AP3216C App

- Start/Stop 周期采样；
- 显示 availability、lux、ALS raw、IR raw、proximity raw 和更新时间；
- 读取错误不能伪装成数值 0；
- 第一版不要求曲线和完整参数配置。

#### LD2410C App

- 显示 availability、presence、目标状态、运动/静止距离与能量；
- 显示 misc、OUT/input、UART 等接口可用性；
- 第一版只实现已验证且可回读的少量基础配置；
- 工程模式、噪声标定、恢复出厂和波特率切换保留在旧测试程序，后续按学习需求迁移。

#### Camera App

- 枚举 capability、像素格式、分辨率、帧率和关键 controls；
- Start/Stop RGB565 预览；
- 切换已验证可采集模式；
- 显示状态、当前模式、帧数、FPS 和错误；
- 支持截图、普通 AF、触摸对焦和 strobe 基础验证；
- 录像只验证 Smart Monitor 使用的 MJPEG 写入链路，不制作相册和回放 UI。

#### 简化 Desktop Shell

- 显示 App 入口并切换单一前台页面；
- 第一版可使用静态表或直接注册，不要求完整 `AppCatalog` 框架；
- Shell 不包含 Smart Monitor 业务判断；
- 离开页面时调用 Controller 的 `deactivate()` 或 `stop()`；
- Camera/Writer 停止完成后再销毁使用它们的页面；
- 进程退出时有序停止 Worker。

### 5.2 后续扩展

- `QSettings` 保存壁纸、模式、strobe policy 和 MonitorPolicy；
- AP3216C/LD2410C 固定容量实时曲线；
- LD2410C 高级配置和工程数据；
- Camera 相册、回放及标准视频容器；
- Smart Monitor 历史事件；
- 无传感器 App；
- 后台监控；
- 多个长期存活 Controller；
- 运行时插件或跨进程服务。

后续项不应迫使第一版重写核心数据类型和依赖方向，但允许扩展资源仲裁、持久化和进程模型。

## 6. 第一版架构

```text
main / CompositionRoot
  |
  +-- SensorService ----------------------------- process lifetime
  |     `-- SensorWorker ------------------------ Sensor I/O thread
  |           +-- Ap3216cBackend
  |           `-- Ld2410cBackend
  |
  +-- CameraService ----------------------------- process lifetime
  |     +-- CameraWorker ------------------------ Capture thread
  |     `-- MediaWriter ------------------------- Writer thread
  |
  `-- DesktopShell ------------------------------ GUI thread
        `-- current Page + Controller ----------- foreground lifetime
              `-- MonitorEngine ----------------- pure C++
```

这里的 Service 是窄 QObject 门面，用于跨线程命令和状态转发，不是通用服务框架。

依赖方向：

```text
Page -> Controller -> Engine / typed service API
Service facade -> Worker -> Linux backend
Engine -> value types only
```

禁止反向依赖：

- Worker 不依赖 Page、Controller 或产品状态机；
- Engine 不依赖 QObject、Widget、文件系统和设备节点；
- Page 不 include V4L2、IIO、UART、misc ioctl 或 backend 头文件；
- Shell 不处理 presence、录像和 strobe 策略；
- Service 不决定“有人时是否录像”等业务规则；
- backend 不生成 UI 文案。

### 6.1 为后续保留的稳定边界

- 设备状态通过值类型发布，后续可增加 Fake backend 或远程来源；
- Controller 只依赖 Service 的 public API，不依赖 Worker；
- Camera 的 capture 与 writer 边界独立，后续可替换编码或容器；
- Shell 只依赖页面创建入口，后续可演进为 `AppCatalog`；
- 所有控制命令预留 owner 字段的位置，但第一版单前台模型可以使用简单 `OwnerId`；
- 异步结果包含 operation 类型和结果值；只有出现并发同类请求时再增加 request ID。

### 6.2 不在第一版实现的平台机制

- 通用 `AppDescriptor/AppSession/AppCatalog` 体系；
- Camera lease token 和优先级抢占；
- 所有命令统一经过 request bus；
- 所有设备的 Fake/Interface 层；
- 动态插件扫描；
- 后台 App 生命周期；
- 跨进程设备服务。

当出现以下任一真实需求时，再升级对应机制：

| 真实需求 | 引入机制 |
| --- | --- |
| 两个长期 Controller 可能同时控制 Camera | lease token、`Busy` 和 owner 校验 |
| 同类异步操作允许并发或结果可能乱序 | request ID 和晚到结果过滤 |
| App 数量增长且 Shell 修改频繁 | `AppDescriptor`、`AppCatalog` 和工厂 |
| Smart Monitor 需要隐藏后继续运行 | 后台 session 和明确资源抢占策略 |
| 出现第二种 backend | 对该设备抽取窄接口和 Fake 实现 |

## 7. 建议目录

```text
src/app/
|-- README.md
|-- SPEC.md
|-- app.pro
|-- main/
|   |-- main.cpp
|   `-- composition_root.{h,cpp}
|-- shell/
|   `-- desktop_shell.{h,cpp}
|-- common/
|   |-- device_status.h
|   `-- operation_result.h
|-- device/
|   |-- sensors/
|   |   |-- sensor_service.{h,cpp}
|   |   |-- sensor_worker.{h,cpp}
|   |   |-- ap3216c_backend.{h,cpp}
|   |   `-- ld2410c_backend.{h,cpp}
|   |-- camera/
|   |   |-- camera_service.{h,cpp}
|   |   |-- camera_worker.{h,cpp}
|   |   `-- camera_types.h
|   `-- media/
|       |-- media_writer.{h,cpp}
|       `-- media_types.h
|-- application/
|   |-- ap3216c/
|   |-- ld2410c/
|   |-- camera/
|   `-- smart_monitor/
|       |-- monitor_types.h
|       |-- monitor_engine.{h,cpp}
|       |-- monitor_controller.{h,cpp}
|       `-- monitor_page.{h,cpp}
`-- tests/
    `-- monitor_engine_test.cpp
```

目录表示依赖边界，不要求预先创建空文件。一个功能较小时可以先放在一个 `.h/.cpp` 中，只有职责或复用确实增长后再拆分。

## 8. 线程与对象所有权

### 8.1 线程划分

| 线程 | 对象 | 工作 |
| --- | --- | --- |
| GUI 主线程 | Shell、Page、Controller、Service facade、业务 QTimer、MonitorEngine | Widget、轻量状态转换、命令提交 |
| Sensor I/O 线程 | SensorWorker、AP3216C/LD2410C backend、采样 QTimer | sysfs、misc ioctl、UART 和轮询 |
| Camera Capture 线程 | CameraWorker | V4L2 open、MMAP、select、DQBUF/QBUF、control |
| Media Writer 线程 | MediaWriter | 目录检查、JPEG 编码、snapshot/MJPEG 写入 |

第一版 AP3216C 和 LD2410C 共用 Sensor I/O 线程。只有实测表明 UART 配置显著阻塞 presence 采样时才拆分。

### 8.2 强制规则

- GUI 线程不得执行周期性 sysfs、ioctl、UART、文件写入或 JPEG 编码；
- Worker 在线程启动后初始化其 QTimer 和设备资源；
- 文件描述符只由打开它的 Worker/backend 使用和关闭；
- 跨线程不传 Widget 指针、MMAP 裸指针或可变共享容器；
- V4L2 buffer 在重新 `QBUF` 前，GUI/Writer 需要的数据必须完成复制或独立所有权转移；
- RGB565 预览通过拥有数据的 `QImage` 传递；原始 JPEG 使用 `QByteArray`；
- 高频预览在投递 GUI 前节流到约 10-15 FPS；
- Writer 队列有固定上限，满时丢弃录像帧并累计 dropped 计数；
- 普通 App 切换不在 GUI 线程无限 `wait()`；
- 进程退出允许有界等待，并记录超时；
- Page 销毁前断开用户操作，Controller 停止 timer 和异步命令。

### 8.3 生命周期

进程级对象：

```text
CompositionRoot
  -> SensorService / SensorWorker
  -> CameraService / CameraWorker / MediaWriter
  -> DesktopShell
```

前台对象：

```text
create Page + Controller
  -> activate
  -> foreground use
  -> deactivate/stop
  -> receive stopped or reach idle
  -> destroy
```

第一版单前台模型使用简单 `OwnerId` 防止错误调用即可。若旧页面停止尚未完成，Shell 显示 busy 并暂缓打开另一个 Camera 页面，不需要实现抢占。

## 9. Linux 驱动接口与数据链路验收

每个设备阶段除代码外，还必须形成以下学习产物：

1. 驱动 ABI 表：设备节点、sysfs 属性、ioctl/control、输入输出和错误；
2. 一张从驱动到 UI 或文件的数据流图；
3. 关键 buffer、线程和所有权说明；
4. 最窄板端验证命令及预期结果；
5. 能用于面试的三分钟口述主线。

### 9.1 AP3216C

必须讲清：

- IIO device 和属性如何发现；
- lux、ALS raw、IR raw 和 proximity raw 的属性来源；
- 文本 sysfs 读取、数值解析和错误传播；
- 为什么读取失败不能返回业务数值 0；
- sample timestamp 和 stale 判断。

### 9.2 LD2410C

必须讲清：

- OUT/input、misc ioctl 和 UART 的职责差异；
- presence、距离、能量和配置数据分别来自哪里；
- ioctl ABI 的结构体和命令边界；
- UART 帧读取与配置操作为何可能阻塞；
- 写后回读和只更新成功字段的原因。

### 9.3 Camera

必须讲清：

- `VIDIOC_QUERYCAP`、格式/帧率/control 枚举；
- MMAP buffer 的申请、映射、入队和 STREAMON；
- `select -> DQBUF -> copy -> QBUF`；
- `bytesperline`、`sizeimage` 和 `bytesused` 的区别；
- RGB565 到 `QImage::Format_RGB16` 的逐行复制；
- JPEG 模式下 SOI/EOI 裁剪；
- Preview、Snapshot 和 Recording 使用同一采集源但不同消费频率；
- 为什么 JPEG 编码和文件写入不放在 Capture 线程。

## 10. 类型化状态与异步结果

设备快照至少包含 availability、数据有效性、更新时间和错误。设备不可用、数据未知和业务值 false/0 必须是不同语义。

简单操作结果可以使用：

```cpp
enum class OperationCode {
    Accepted,
    Succeeded,
    Busy,
    InvalidArgument,
    Unavailable,
    IoError,
    Unsupported,
    Cancelled,
    Timeout,
};

struct OperationResult {
    OperationCode code = OperationCode::Accepted;
    QString error;
};
```

“请求已接受”和“操作最终成功”是两个阶段：同步门面使用 `Accepted` 表示命令已入队，异步 completion 使用 `Succeeded` 或具体错误码；长期状态仍通过 typed status signal 返回。第一版不要求所有 API 都带 request ID。例如 `startPreview()` 返回是否接受，最终状态通过 `cameraStatusChanged(CameraStatus)` 返回。

以下场景出现后再添加 request ID：

- 同类命令可同时 pending；
- 结果可能乱序；
- Controller 销毁后 Service 仍需要区分旧请求；
- 一个 Service 同时服务多个长期 owner。

状态 signal 使用结构体或 enum，不使用以下字符串做程序分支：

```text
recording: ...
saved: ...
failed: ...
open
close
retry
```

字符串只用于日志和最终 UI 展示。

## 11. Smart Monitor 设计

### 11.1 数据流

```text
Ld2410Snapshot ----+
                   |
Ap3216cSample -----+--> MonitorController --> MonitorEngine
                   |                            |
CameraStatus ------+                            v
StorageStatus -----+                     MonitorDecision
                                                |
                                                v
                                  CameraService / MediaWriter
```

`MonitorEngine` 是纯 C++ 状态机。Controller 把 Qt signal 和 timer timeout 转为 Engine 输入，并执行 Engine 输出的副作用请求。

### 11.2 最小类型

- `MonitorPolicy`：确认时间、冷却时间和 lux 阈值；
- `PresenceState`：Disabled、NoPerson、PersonPending、Active、Cooldown；
- `LightState`：Unknown、Normal、Dark；
- `MonitorState`：业务真实状态；
- `MonitorDecision`：Camera、recording、torch 和 timer 的期望；
- `SmartMonitorViewState`：Page 所需状态；
- 设备状态：`Ld2410Snapshot`、`Ap3216cSample`、`CameraStatus`、`StorageStatus`。

第一版不强制实现通用 `MonitorEvent` tagged union。可使用清晰的入口函数：

```cpp
MonitorDecision start();
MonitorDecision stop();
MonitorDecision onPresenceChanged(const PresenceSample &sample);
MonitorDecision onLuxChanged(const LuxSample &sample);
MonitorDecision onConfirmTimeout();
MonitorDecision onCooldownTimeout();
```

如果后续事件数量增长或需要回放事件流，再统一为 `dispatch(MonitorEvent)`。

### 11.3 Presence 状态机

```text
Disabled
   |
   | Start
   v
NoPerson -- present --> PersonPending
   ^                       |
   | absent                | confirm timeout and still present
   +-----------------------+
                           v
                         Active
                           |
                           | absent
                           v
                        Cooldown
                         /    \
                present /      \ timeout and still absent
                       v        v
                    Active   NoPerson
```

规则：

- `PersonPending` 不启动 Camera 和录像；
- Pending 期间 presence=false 直接回 NoPerson，不进入 Cooldown；
- Active 期间 presence=false 进入 Cooldown；
- Cooldown 保持 Camera 和录像；
- Cooldown 再次 presence=true 直接回 Active；
- Stop 从任何状态进入 Disabled，取消 timer 并请求停止副作用；
- presence 无效或 stale 是 Unknown，不等价于 false；
- Active 期间 presence 变 Unknown 时进入 fail-safe Cooldown 并启动 cooldown timer；已经处于 Cooldown 时保持原 timer；Pending 期间变 Unknown 时取消确认并回 NoPerson；全程显示 unavailable；
- Camera/storage 错误不改变 presence 状态，只影响副作用能否收敛。

### 11.4 暗光滞回

```text
lux <= darkEnterLux -> Dark
lux >= darkExitLux  -> Normal
中间区间            -> 保持原状态
```

Auto torch 需要同时满足：

- monitoring enabled；
- PresenceState 为 Active 或 Cooldown；
- strobe policy 为 Auto；
- lux 有效且未 stale；
- Camera 正在 Streaming 且支持 torch。

lux 无效时 Auto 默认关闭 torch 并显示原因。手动 Torch 不隐式启动 Camera，且仍受 Camera capability 和当前 owner 限制。

### 11.5 Decision 和 timer

```cpp
enum class TimerCommand {
    Keep,
    Start,
    Cancel,
};

struct MonitorDecision {
    bool cameraWanted = false;
    bool recordingWanted = false;
    bool torchWanted = false;
    TimerCommand confirmTimer = TimerCommand::Keep;
    TimerCommand cooldownTimer = TimerCommand::Keep;
};
```

Engine 决定何时启动或取消业务 timer，Controller 只负责 Qt `QTimer` 的实际执行。Camera retry timer 仍属于 Controller，因为它是设备收敛策略，不是 presence 状态机的一部分。

Controller 对 desired/actual 状态做收敛：

- wanted=true 且实际 idle 时提交 start；
- wanted=false 且实际 active 时提交 stop；
- 同一操作 pending 时不重复提交；
- Camera start 失败时按固定退避重试；
- storage 不可写时不启动 recording，但允许 Camera 预览继续；
- 每次设备状态变化后重新收敛，不依赖一次性动作字符串。

## 12. UI 边界

Page 只发用户意图并渲染 ViewState：

```text
Page -> startRequested()
Page -> stopRequested()
Page -> snapshotRequested()
Page -> previewModeSelected(index)
Page -> strobePolicySelected(policy)
Page -> focusRequested()
Page -> touchFocusRequested(framePoint)

Controller -> viewStateChanged(state)
Controller -> previewFrameChanged(image)
Controller -> operationFinished(result)
```

规则：

- Page 不持有 Worker/backend；
- Page 不解析状态字符串；
- Page 不判断 presence 状态机；
- 控件 enabled/disabled 由 ViewState 给出；
- Camera 触摸坐标由预览 Widget 映射到帧坐标；
- 用户错误简短展示，Linux errno 和详细上下文进入日志；
- 第一版 UI 重点是状态清楚、操作可用和 480x272 屏幕不拥挤；
- UI 可重做，但不为视觉效果增加新的业务层。

## 13. 现有实现的迁移原则

### 13.1 直接参考

| 现有内容 | 新实现处理 |
| --- | --- |
| `MonitorCore` 不访问设备和 Widget | 保留纯状态机思想，改为 typed state/decision |
| presence confirm/cooldown | 保留，修正 Pending false 和 Cooldown 重入语义 |
| lux enter/exit 双阈值 | 保留，增加 validity 和 stale |
| AP3216C IIO sysfs 兼容读取 | 迁移最小 backend，并记录属性映射 |
| LD2410C misc ioctl/UART 封装 | 按读取闭环和少量配置逐步迁移 |
| Camera V4L2 MMAP/select 路径 | 迁移并重新手写关键生命周期 |
| Capture/Save 后台线程 | 保留分线程思想，简化 facade 职责 |
| 预览节流和有界录像队列 | 保留并使用 typed dropped 统计 |
| 唯一媒体文件名 | 保留在 MediaWriter |
| 独立设备测试入口 | 改为 HMI 内设备 App，并保留板端单点验证能力 |

### 13.2 必须修改

| 现有问题 | 新实现要求 |
| --- | --- |
| GUI 线程轮询 sysfs/ioctl | 移入 SensorWorker |
| LD2410 读取失败写成 presence=false | 发布 unavailable/invalid |
| lux 失败继续使用旧值 | timestamp + stale |
| Core 输出字符串 action | typed desired state |
| recording/snapshot 解析状态字符串 | typed status/result |
| Controller 同时持有设备、路径、状态机和 UI 状态 | 设备归 Service/Worker，规则归 Engine |
| Camera 类同时承担过多协议和展示文本 | 保留薄 Service，capture/writer 分责 |
| Cooldown 再有人进入 Pending | 直接 Cooldown -> Active |
| 析构无界 `wait()` | 正常切页异步停止，退出有界等待 |
| 未可靠回读配置整体写回 | 只写已验证字段，写后逐项回读 |

### 13.3 迁移约束

- 不整文件复制旧类后改名；
- 每次迁移一个可验证能力；
- 迁移前写明驱动 ABI、输入输出、线程和 buffer 所有权；
- Linux API 调用可参考旧实现，业务状态和错误协议重新设计；
- 未被第一版闭环使用的能力不迁移；
- 每个设备先完成单 App 验证，再接入 Smart Monitor。

## 14. 手写实施顺序

每阶段都必须产生可运行、可观察或可测试的结果。下一阶段开始前，应能不看代码讲清当前数据流、线程位置和所有权。

### 阶段 0：旧闭环和 ABI 基线

产物：

- presence 状态转移表；
- AP3216C、LD2410C、Camera、Storage 的 ABI/输入输出表；
- 旧实现可参考和必须修改清单；
- 第一版与后续范围。

完成标准：能讲清 `presence -> decision -> V4L2 capture -> MJPEG file -> UI` 主线，不再扩大第一版范围。

### 阶段 1：MonitorEngine

实现 `MonitorPolicy`、状态、Decision 和 Engine 入口函数。

最低测试：

- Start 后无人；
- 短暂 presence 不通过 confirm；
- Pending 期间 absent 回 NoPerson；
- confirm 后 Active；
- Active absent 进入 Cooldown；
- Cooldown 返回不中断；
- Cooldown 到期结束；
- Stop 从任意状态退出；
- lux 滞回；
- lux/presence invalid 和 stale。

完成标准：测试不启动 QApplication，不 include 设备头文件。

### 阶段 2：AP3216C 全链路

```text
IIO sysfs -> Ap3216cBackend -> SensorWorker -> SensorService
           -> Ap3216cController -> Ap3216cPage
```

完成 Start/Stop、数值、validity、timestamp 和错误展示。同时完成 AP3216C ABI 表与板端验证记录。

完成标准：属性缺失或读取失败时 UI 仍响应并显示 unavailable。

### 阶段 3：LD2410C 读取链路

实现 misc/OUT 探测、presence 与距离能量读取，并在 LD2410C Page 展示接口来源和错误。UART 第一阶段只做必要探测或已验证读取，不急于迁移全部配置。

完成标准：设备缺失、恢复和读取失败有独立语义；Smart Monitor 可订阅 presence，但暂不控制 Camera。

### 阶段 4：Camera 预览链路

手写并理解：

1. capability、format、frame interval 和 controls；
2. open/config/REQBUFS/MMAP/QBUF/STREAMON；
3. select/DQBUF/copy/QBUF；
4. RGB565 到 QImage；
5. 预览节流；
6. STREAMOFF/unmap/close。

完成标准：Camera App 可重复 Start/Stop 和切模式；GUI 不执行 V4L2 ioctl；事件队列不持续增长。

### 阶段 5：MediaWriter、截图和录像

实现：

- storage root 异步检查；
- 唯一 snapshot/video 路径；
- RGB565 QImage JPEG 编码；
- 原始 JPEG SOI/EOI 路径；
- 有界 writer queue；
- MJPEG start/append/stop；
- typed snapshot/recording result 和 dropped 统计。

完成标准：慢存储或写失败不阻塞 GUI/Capture；录像停止后文件关闭且结果明确。

### 阶段 6：Smart Monitor 闭环

Controller 订阅 sensor、执行 Engine timer decision、收敛 Camera/recording/torch desired/actual 状态，并组装 ViewState。

完成标准：presence confirm 后开始预览和录像；Cooldown 内返回不切文件；storage 不可写时只禁用录像；Page 不访问设备。

### 阶段 7：简化 Shell 和资源切换

添加桌面入口和单前台页面切换。使用简单 OwnerId 和 async stop 解决 Camera 页面竞争。

完成标准：Camera App 与 Smart Monitor 切换时不重复 open，不存在两个 capture worker；旧页面销毁后不再处理结果。

### 阶段 8：LD2410C 少量配置、异常和交付

只迁移已验证可回读配置；补齐设备缺失、I/O 失败、storage 不可写、退出时正在写入等场景；接入 Buildroot。

完成标准：形成可部署 binary、板端演示脚本、数据流文档和简历讲解材料。

## 15. 测试与验收

### 15.1 本地测试

第一版只强制：

- MonitorEngine 状态转移；
- stale 判定；
- RGB565 行复制或格式转换纯函数；
- JPEG marker 裁剪纯函数；
- 媒体路径生成。

以下测试按真实风险添加，不作为开工前置：

- request ID 晚到结果；
- Camera lease；
- 通用 AppSession 生命周期；
- 完整 Fake Service Controller 测试。

### 15.2 板端单 App 验收

```bash
QT_QPA_PLATFORM=linuxfb "${APP_BINARY}"
```

每个设备至少验证正常工作、设备缺失、读写失败和退出清理。实际 binary 确定后更新本节。

### 15.3 Smart Monitor 集成验收

| 场景 | 预期 |
| --- | --- |
| Start 后无人 | NoPerson，Camera 不启动 |
| presence 短脉冲 | Pending 后回 NoPerson，不录像 |
| presence 持续 | confirm 后 Active，Camera 和录像启动 |
| 暗光 Auto | enter 阈值以下 torch on，exit 阈值以上 off |
| 人离开 | Cooldown，Camera 和录像保持 |
| Cooldown 内返回 | 直接 Active，录像文件不中断 |
| Cooldown 到期 | 停止录像、torch 和 Camera |
| presence 读取失败 | 显示 unavailable，不立即等价为无人 |
| storage 不可写 | 状态机和预览继续，录像不启动 |
| Camera 打开失败 | Error，有限频率 retry，GUI 可操作 |
| 录像中退出页面 | 异步停止，随后 Camera App 可打开 |

### 15.4 架构验收

- Page 不 include Linux 设备/backend 头文件；
- Linux fd 和 MMAP 只存在于 Worker/backend；
- GUI 线程没有周期性 sysfs/ioctl/file write；
- Engine 不依赖 QObject；
- 不使用动作/结果字符串做程序分支；
- Camera buffer 在 QBUF 前完成必要复制；
- Writer 队列有上限；
- 同一时刻最多一个 Camera owner；
- App 关闭后不再处理设备结果；
- 每个设备有 ABI 表、数据流图和板端验证记录。

## 16. Buildroot 和交付策略

当前 Buildroot 包仍指向 `src/imx6_smart_monitor/`。新实现开发期间与旧实现并存，避免未形成闭环前破坏已有演示程序。

建议策略：

1. `src/app/` 先建立独立 qmake 工程和临时 binary，例如 `imx6-hmi-lab`；
2. 各设备链路可本地交叉编译并手动部署；
3. Smart Monitor 闭环和 Shell 验收通过后，新增或切换 Buildroot local package；
4. 新 binary 达到功能验收后，再决定是否替换 `imx6-smart-monitor` 名称；
5. 旧目录保持只读参考，不为新接口增加兼容层。

最终必须提供：

```bash
bash buildscripts/build_and_deploy.sh drv "${PACKAGE}"
QT_QPA_PLATFORM=linuxfb "${APP_BINARY}"
```

并同步更新 `bsp/package/`、模块 README、实际 binary 名和板端验证命令。

## 17. 头文件注释规范

只对关键类说明：

1. 职责；
2. 所在线程；
3. owner 和生命周期；
4. public API 是同步完成还是异步接受；
5. 最终状态从哪个 signal 返回；
6. buffer 或设备资源所有权。

不为简单 getter/setter 和显而易见成员添加逐行注释。

## 18. 明确非目标

第一版不做：

- 通用跨设备 `Device` 继承体系；
- 多个前台 App 并行；
- 后台 Smart Monitor；
- 完整 lease/request bus；
- 动态插件；
- 网络和用户权限；
- MP4/H.264；
- AI 视觉识别；
- 完整相册和媒体数据库；
- 为旧 Smart Monitor API 增加兼容层；
- 以极端鲁棒性为目标的复杂恢复框架。

## 19. 完成定义

本轮手写重构完成不是指旧功能全部迁移，而是以下闭环成立：

```text
Desktop Shell
  -> open Smart Monitor
  -> SensorWorker reads real driver ABI
  -> typed samples carry validity and timestamp
  -> pure MonitorEngine decides desired state and timer intent
  -> Controller asynchronously controls Camera and MediaWriter
  -> V4L2 buffers become preview QImage and MJPEG frames
  -> Page only renders ViewState
  -> leave App stops resources
  -> Camera App can immediately use Camera
```

完成时还必须能脱离代码讲清：

- AP3216C 和 LD2410C 驱动接口；
- V4L2 MMAP 采集和 buffer 生命周期；
- RGB565/JPEG 两条图像路径；
- presence 和 lux 状态机；
- Qt 跨线程对象及数据所有权；
- 从 Buildroot 构建到板端运行的交付路径。

在上述闭环通过本地状态机测试、板端单设备测试、Smart Monitor 集成测试和页面切换测试后，再根据真实需求逐项引入 AppCatalog、lease token、request ID、Fake backend、后台模式和持久化能力。
