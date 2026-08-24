# 19. Smart Monitor 集成：从事件输入到异步收敛

> 对应 [SPEC 阶段 6](../../SPEC.md#阶段-6smart-monitor-闭环)。如本文与规格冲突，以 `SPEC.md` 为准。

## 目标与前置

### 目标

- 手写 `MonitorController`，把 sensor signal 和业务 timer timeout 转成 `MonitorEngine` 输入。
- 将 Engine 输出理解为 desired state，不把它误写成“一次性动作列表”。
- 用 desired / actual / pending 三组状态驱动 Camera、recording、torch 收敛。
- 组装类型化 `SmartMonitorViewState` 给 Page，Page 不参与业务判断。
- 保证 storage 不可写只阻止录像，不改变 presence 状态机，也不阻断 Camera 预览。

### 前置

- 已完成纯 C++ `MonitorEngine` 及状态转移测试。
- `SensorService` 能发布带 validity、timestamp 和 error 的 LD2410C/AP3216C 值类型。
- `CameraService` 和 `MediaWriter` 已提供异步命令、状态 signal 和类型化结果。
- 已理解 [SPEC 的 Smart Monitor 设计](../../SPEC.md#11-smart-monitor-设计)与[线程规则](../../SPEC.md#8-线程与对象所有权)。

## 最低必懂模型

Controller 不是第二个状态机，也不是设备 Worker。它承担三个窄职责：

1. **适配输入**：Qt signal、`QTimer::timeout` 进入纯 C++ Engine。
2. **执行意图**：保存 Engine 的 desired state，对照服务发布的 actual state 和本地 pending 状态提交异步命令。
3. **投影视图**：把业务状态、设备状态和操作可用性组装成 `SmartMonitorViewState`。

必须分清三种状态：

| 状态 | 含义 | 来源 |
| --- | --- | --- |
| desired | 业务现在希望资源是什么状态 | `MonitorDecision` |
| actual | Worker/Writer 已经确认的真实状态 | Service status signal |
| pending | 同步请求返回 `Accepted`，但异步最终状态尚未返回 | Controller |

例如 `cameraWanted=true` 不代表 Camera 已经 Streaming；它只允许 Controller 在 `actual=Idle` 且 `pending=None` 时提交一次 start。同步 `Accepted` 只表示请求被接受，异步最终成功由状态 signal 或 `Succeeded` completion 确认；失败使用 `IoError`、`Busy` 等具体错误码。

## 数据与控制流

```text
Ld2410Snapshot signal ----+
Ap3216cSample signal -----+--> MonitorController --> MonitorEngine
confirmTimer timeout -----+         |                    |
cooldownTimer timeout ----+         |                    `-- MonitorDecision
Camera/Writer status -----+         |
StorageStatus signal -----+         +--> reconcileCamera()
                                    +--> reconcileRecording()
                                    +--> reconcileTorch()
                                    `--> SmartMonitorViewState --> Page
```

一次典型闭环：

```text
presence=true
  -> Engine: NoPerson -> PersonPending, confirmTimer=Start
  -> Controller 启动单次 confirm timer
  -> timeout
  -> Engine: PersonPending -> Active, cameraWanted=true, recordingWanted=true
  -> Controller 先提交 Camera start，camera pending=Starting
  -> CameraStatus=Streaming，清除 camera pending
  -> 再次 reconcile，storage writable 时提交 recording start
  -> RecordingStatus=Recording，actual 与 desired 收敛
```

事件到达后都遵循同一顺序：更新 actual/input，调用 Engine（若事件属于业务输入），应用 timer decision，保存 desired，执行 reconcile，最后发布 ViewState。

## 线程和生命周期

- `MonitorController`、`MonitorEngine`、业务 `QTimer` 和 Page 均在 GUI 线程。
- Sensor、Camera、Writer 的 signal 通过 queued connection 抵达 Controller；跨线程参数必须拥有数据。
- Controller 只能调用 Service facade，不能直接调用 Worker/backend，也不能执行 sysfs、ioctl 或文件 I/O。
- `activate()` 后才接收用户操作并启动 monitoring；`deactivate()` 必须先禁止新操作、停止业务 timer，再把 desired 全部置为 false 并异步收敛。
- Controller 销毁前必须达到 idle，或由 Shell 保持对象直到 stop completion；不能依赖析构函数阻塞等待。
- 晚到 signal 应由 Qt connection 随 receiver 销毁自动断开，但销毁前的 pending 结果仍要通过当前生命周期状态过滤，不能重新启动资源。

## 分步任务

### 任务 1：列出 Controller 持有的状态

只保存完成收敛所需的信息：

- 最近一次有效/无效 sensor snapshot；
- `MonitorDecision desired_`；
- Camera、recording、torch 的 actual 状态；
- 每类异步操作的 pending 状态；
- storage writable/unknown/error；
- Controller 是否 active/deactivating；
- Camera retry timer 和有限退避计数。

**练习**：先画字段表，逐项写出“谁更新、何时清除”。若一个字段没有明确生产者和清除点，暂不加入类中。

### 任务 2：接通 Engine 输入

为以下输入各写一个窄 slot：

- LD2410C snapshot；
- AP3216C sample；
- confirm timeout；
- cooldown timeout；
- Start/Stop 用户意图。

slot 不直接拼 UI 文案，不直接调用多个设备动作；统一把 `MonitorDecision` 交给 `applyDecision()`。

**练习**：制造 presence invalid/stale，确认传给 Engine 的是 Unknown 语义，而不是 `present=false`。

### 任务 3：执行 timer decision

- confirm/cooldown timer 使用 single-shot。
- `Start` 按 policy 时长启动，`Cancel` 停止，`Keep` 不改变现状。
- Camera retry timer 属于 Controller 收敛策略，不放进 Engine。
- Stop/deactivate 无条件停止 retry timer，避免资源被重新拉起。

**练习**：记录每次 timer start/cancel/timeout，验证 Cooldown 内恢复 Active 会取消 cooldown timer。

### 任务 4：实现 Camera 收敛

- wanted 且 actual Idle/Error、无同类 pending、Controller active 时提交 start。
- 不 wanted 时先请求 torch off，再请求 Writer/recording stop；只有 `torchActual=Off`、`recordingActual=Stopped` 且两者无 pending，才允许提交 Camera stop。
- start 被拒绝或失败后，不在 signal 回调内紧密重试；用固定或有上限退避。
- stop 的优先级高于 retry，deactivating 时禁止 start。

Camera stop 的硬门控为：

```text
cameraStopAllowed = !cameraWanted
                    AND torchActual == Off
                    AND torchPending == None
                    AND recordingActual == Stopped
                    AND recordingPending == None
```

**练习**：让 Camera 连续失败三次，检查同一时刻最多一个 start pending，GUI 仍可响应 Stop。

### 任务 5：实现 recording 与 storage 门控

recording 的有效 desired 应为：

```text
desired.recordingWanted
AND storage is writable
AND Camera actual is Streaming
AND Controller is active
```

storage 不可写时：

- 不向 Engine 注入 absent/stop；
- 不修改 `PresenceState`；
- 不停止业务 Camera 预览；
- 若正在录像，异步请求停止并明确显示 storage error；
- storage 恢复后重新 reconcile，可在当前 Active/Cooldown 中重新开始录像。

**练习**：在 Active 中切换 storage writable -> unwritable -> writable，记录 presence、Camera 和 recording 三条状态线。

### 任务 6：实现 torch 收敛

- 只在 Camera Streaming、capability 支持且 owner 正确时提交 torch 命令。
- lux invalid/stale 时 Auto desired 为 off，并在 ViewState 中给出原因。
- torch 操作 pending 时不重复提交。
- Camera stop 前先期望 torch off；确认 Off 后才继续停止 Writer/recording，最终设备清理由 Camera Worker 兜底。

### 任务 7：组装 ViewState

ViewState 至少表达：

- monitoring enabled 和 `PresenceState`；
- presence/lux validity、更新时间与简短错误；
- Camera、recording、torch 的实际状态与 pending；
- storage writable/error；
- Start/Stop、snapshot、focus 等控件是否 enabled；
- dropped frame 或录像错误等用户可观察信息。

**练习**：Page 只根据 ViewState 设置文本和 enabled，删除所有对 Camera/Storage/presence 的条件判断。

### 任务 8：实现 deactivate

`deactivate()` 应是异步流程：

1. 标记 deactivating，拒绝新 Start/snapshot/focus。
2. 停止 confirm、cooldown 和 retry timer。
3. 调用 Engine stop 或直接应用其 stop decision。
4. desired torch/recording/camera 全部为 false。
5. 反复由状态 signal 驱动 reconcile，严格按 `torch off -> Writer/recording stopped -> Camera idle` 串行收敛；前一步未确认完成时不得提交后一步 stop。
6. 达到 idle 后发出 `deactivated()`，交给 Shell 销毁页面与 Controller。

## 关键接口与门控

类型名应以实际 Service API 为准：

```cpp
enum class PendingOp { None, Starting, Stopping };

void MonitorController::applyDecision(const MonitorDecision &decision);
void MonitorController::reconcileTorch();
void MonitorController::reconcileRecording();
void MonitorController::reconcileCamera();
```

启动 recording 的门控保持为一个清晰布尔表达式：

```cpp
const bool canRecord = active_ && !deactivating_ &&
                       desired_.recordingWanted &&
                       storage_.writable &&
                       cameraActual_ == CameraState::Streaming;
```

停止链路的不变量：

```text
request torch off
  -> observe torch Off and no pending
  -> request Writer/recording stop
  -> observe recording Stopped and no pending
  -> request Camera stop
```

每个状态 signal 都负责更新 actual、清除对应 pending，再触发下一轮 reconcile；不得一次并行提交三类 stop。

**TODO 练习**：实现上述四个接口，并用日志证明 Camera stop 从未早于 torch Off 和 recording Stopped。

## 检查点

- Engine 没有 include Qt、Service、Widget 或文件系统头文件。
- 每个 signal/timeout 只通过明确的 Engine 入口改变业务状态。
- desired、actual、pending 可分别打印，日志能解释为何提交或未提交命令。
- 同类命令 pending 时不会重复发送。
- Camera stop 只在 torch 已 Off、recording 已 Stopped 且两者无 pending 时提交。
- storage 不可写时 `PresenceState` 仍按 presence 正常转移，预览仍可 Streaming。
- Camera start 失败只按有限频率重试，Stop/deactivate 能取消 retry。
- Page 接收单一 ViewState，不解析 `recording:`、`failed:` 等字符串。
- deactivate 达到 idle 后才发 completion。

## 常见错误

- 把 `MonitorDecision` 当动作队列，每次 signal 都重复 start/stop。
- start API 返回 Accepted 就把 actual 写成 Streaming。
- 只用一个 `bool cameraRunning`，无法表达 Starting、Stopping、Error 和 pending。
- storage 错误时调用 Engine stop，导致业务状态错误退回 Disabled。
- recording start 不等待 Camera Streaming，造成顺序竞争。
- torch、recording 和 Camera 并行 stop，导致 Writer 或补光仍使用已停止的 Camera。
- 在 Camera error signal 中立即递归 retry，形成高频命令风暴。
- Stop 后 retry timer 仍存活，Camera 被再次打开。
- Controller 析构中 `wait()`，阻塞 GUI 且可能等不到 queued completion。
- 用最后一次 lux/presence 数值代替 invalid/stale 状态。

## 最窄板端验证

先运行实际生成的 binary；名称未确定前保留占位符：

```bash
QT_QPA_PLATFORM=linuxfb "${APP_BINARY}"
```

只验证阶段 6 主闭环：

1. 打开 Smart Monitor，按 Start，无人时观察 `NoPerson`，Camera 不启动。
2. 短暂触发 presence，在 confirm 到期前离开，应回 `NoPerson` 且不录像。
3. 持续 presence 至 Active，应先看到 Camera Streaming，再看到 Recording。
4. 离开进入 Cooldown，确认预览与同一录像继续；到期后按 torch off、Writer/recording stop、Camera stop 依次收敛。
5. 在 Cooldown 内返回，应直接 Active，录像文件不切换。

建议日志至少包含时间、owner、desired/actual/pending 和 operation result，避免只凭 UI 文案判断。

## 异常验收

| 注入场景 | 预期结果 |
| --- | --- |
| presence invalid/stale | 显示 unavailable；Active 进入 fail-safe Cooldown，不直接当无人 |
| lux invalid/stale | Auto torch 关闭并显示原因；Camera/录像不因此停止 |
| storage 启动前不可写 | presence 状态机与预览继续；不提交 recording start |
| 录像中 storage 写失败 | 异步停止录像并报错；Camera 与业务状态继续 |
| Camera open 失败 | actual=Error；有限退避；GUI Stop 可用 |
| Camera start pending 时 Stop | 不再重试 start，最终收敛到 Idle |
| Writer stop 较慢 | 页面进入 stopping/busy；不销毁 Controller，不阻塞 GUI |
| torch off 较慢 | 暂不提交 recording/Camera stop；状态 signal 到达后继续顺序收敛 |
| 连续重复 Start | 不产生多个 Camera/recording start pending |

## 完成标准

- presence confirm 后 Camera 和 recording 最终收敛到 active。
- Cooldown 内返回不停止 Camera、不切换录像文件。
- storage 不可写时状态机和预览不受影响，录像明确 unavailable/error。
- Camera、recording、torch 各自具有可观察的 desired/actual/pending。
- Controller 只通过 Service public API 控制资源，Page 不访问设备。
- deactivate 能按 torch、Writer/recording、Camera 的顺序异步达到 idle 并发出完成信号。
- 能用日志还原一次从 sensor signal 到 Engine decision 再到异步 completion 的全过程。

## 复盘

完成后用自己的代码回答：

1. 哪些事件属于 Engine，哪些 timer/重试属于 Controller，为什么？
2. 为什么 desired state 比动作字符串更适合异步设备？
3. actual 和 pending 分开后解决了哪些重复命令与退出竞态？
4. storage 为什么是 recording 的能力门控，而不是 presence 状态机输入？
5. Controller 在 GUI 线程是否违背“业务不阻塞 UI”？哪些工作仍必须留在 Worker？
6. deactivate 期间晚到 Streaming/Recording signal 如何被重新收敛到 idle？

建议画一张实际运行时序图，标出 signal 所在线程、queued 边界、每次状态写入点和资源 owner。

## 三分钟面试口述

> Smart Monitor 的业务规则放在纯 C++ `MonitorEngine` 中，Controller 只负责适配输入、保存 desired，并对照 actual/pending 提交异步命令。同步 `Accepted` 不代表设备已完成，最终状态由 signal 或 `Succeeded` completion 确认。presence confirm 后先拉起 Camera，确认 Streaming 且 storage writable 后再开始录像；storage 失败只降级录像。停止时严格按 torch off、Writer/recording stop、Camera stop 收敛，Camera stop 门控前两者已完成，避免并行 stop 竞态。页面离开后达到 idle，Shell 才销毁对象。
