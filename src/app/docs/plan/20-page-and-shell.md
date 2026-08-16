# 20. Page 与 Shell：单前台页面和资源安全切换

> 对应 [SPEC 阶段 7](../../SPEC.md#阶段-7简化-shell-和资源切换)。如本文与规格冲突，以 `SPEC.md` 为准。

## 目标与前置

### 目标

- 手写只发用户意图、只渲染 `ViewState` 的 Qt Widgets Page。
- 在 480x272 屏幕上完成可操作、可读、不拥挤的 Smart Monitor 页面。
- 实现简化 `DesktopShell`，同一时刻只允许一个前台 App。
- 使用简单 `OwnerId` 标识 Camera 命令来源，不提前实现 lease/抢占框架。
- 页面切换时先异步 `deactivate()`，收到完成信号后再销毁 Page/Controller。

### 前置

- Smart Monitor Controller 已能发布 `viewStateChanged()`、`previewFrameChanged()` 和类型化 operation result。
- Controller 的 `activate()` / `deactivate()` 语义明确，deactivate 达到资源 idle 后发完成信号。
- Camera App 也具备相同的异步停用完成语义。
- 已阅读 [UI 边界](../../SPEC.md#12-ui-边界)和[前台对象生命周期](../../SPEC.md#83-生命周期)。

## 最低必懂模型

Page 是被动视图：

```text
用户操作 -> Page signal -> Controller
Controller ViewState -> Page render
Controller preview QImage -> Page preview widget
```

`PreviewWidget` 可以做与显示直接相关的转换：依据实际 content rect 把 Widget 触点映射为 `FramePoint`。Page 只转发该类型化意图；Controller 判断当前状态是否允许对焦；Camera Worker 在访问设备前做最终 clamp。Page 不能判断 presence、决定是否录像、检查 storage 或直接操作 Camera。

Shell 是前台生命周期协调者：

```text
Desktop
  -> create Page + Controller + OwnerId
  -> activate
  -> foreground
  -> request switch/back
  -> disable old page
  -> await deactivate completion
  -> destroy old Page + Controller
  -> create and activate next page
```

第一版“单前台”是资源冲突的主要约束，`OwnerId` 只负责防止明显的错误来源调用。若旧页面仍在 stopping，Shell 显示 busy 并等待，不抢占、不同时创建另一个 Camera owner。

## 数据与控制流

```text
touch/click
   |
   v
SmartMonitorPage -- intent signals --> MonitorController --> Services
   ^                                       |
   |                                       v
   +-- render(ViewState) <---------- typed state/results
   +-- setPreview(QImage) <--------- throttled owned frame

DesktopShell -- activate/deactivate --> current Controller
DesktopShell -- owns -----------------> current Page + Controller
DesktopShell -- queues ---------------> requested next App id
```

切页控制流：

```text
open(Camera App) while Smart Monitor active
  -> Shell remembers requested App
  -> old Page input disabled
  -> old Controller.deactivate()
  -> torch off
  -> Writer stopped
  -> Camera idle
  -> old Controller emits deactivated()
  -> Shell deletes old foreground objects
  -> Shell creates Camera Page/Controller with a new OwnerId
  -> activate()
```

## 线程和生命周期

- Shell、Page、Controller 和 Widget 全部属于 GUI 线程。
- Page 只能在 GUI 线程更新控件；预览 `QImage` 必须是跨线程后拥有自身像素数据的值。
- Page 和 Controller 生命周期相同，均短于进程级 Sensor/Camera Service。
- 推荐由一个前台容器 QObject/struct 明确持有 Page 和 Controller；QObject 可使用 parent ownership，避免 `shared_ptr<QObject>`。
- Shell 发起 deactivate 后不能立即 `delete`，也不能在 GUI 线程无限 `wait()`。
- 切页期间忽略或排队后续入口点击；第一版只保留一个 `nextAppId`，不建立通用导航栈。
- 进程退出与普通切页共用 stop 语义，但进程退出另有有界等待兜底，详见 [阶段 8](21-failure-delivery-review.md)。

## 分步任务

### 任务 1：先定义页面意图和 ViewState

Page 只暴露用户意图：

- `startRequested()` / `stopRequested()`；
- `snapshotRequested()`；
- `previewModeSelected(int)`；
- `strobePolicySelected(StrobePolicy)`；
- `focusRequested()`；
- `touchFocusRequested(FramePoint)`；
- `backRequested()`。

ViewState 至少一次性给出所有可见文本所需数据和控件 enabled 状态。不要让 Page 向 Controller 逐项 getter 拉取状态。

**练习**：逐个检查 Page 的 `if`，只允许布局、格式化和本地交互判断；出现业务名词时考虑移入 Controller 的 ViewState 组装。

### 任务 2：搭建 480x272 固定信息层级

建议按实际像素预算分配：

- 顶栏：返回、标题、总状态，约 32-36 px；
- 主区：预览与关键状态，优先保留最大面积；
- 状态区：presence、lux、Camera/recording/storage，使用短标签；
- 底栏：Start/Stop、截图、对焦或模式入口，约 40-48 px。

设计约束：

- 不依赖 hover；触摸目标建议不小于约 36 px。
- 避免嵌套滚动和模态提示覆盖关键操作。
- 错误正文显示简短上下文，详细 errno 进日志。
- 文本允许换行或省略，不能撑破按钮或覆盖预览。
- 先用 layout 管理尺寸，只有预览画布等固定格式控件再设置明确约束。

**练习**：分别构造最长状态名、storage error 和设备 unavailable，确认布局不跳动、不遮挡 Stop/Back。

### 任务 3：连接 Page intent 与 Controller

连接方向保持单向：Page signal 到 Controller slot，Controller signal 到 Page render slot。不要让 Page 保存 Service 指针。

**练习**：在 Page 头文件中搜索 `Service`、`Worker`、`Backend`、Linux 设备头；结果应为空。

### 任务 4：实现幂等 render

`render(state)` 每次都从完整 ViewState 刷新必要控件，使相同状态重复渲染不会触发业务动作。

- 用 `QSignalBlocker` 防止程序设置 ComboBox/CheckBox 时反向发出用户意图。
- enabled 来自 ViewState，不在 Page 重算。
- rendering 不打开文件、不发 Camera 命令、不启动业务 timer。
- preview frame 单独更新，避免高频图像导致整页状态重复格式化。

### 任务 5：实现触摸对焦坐标映射与门控

`PreviewWidget` 负责识别预览内容实际绘制区域，把 Widget 点映射到帧坐标并产出 `FramePoint`。keep-aspect-ratio 的 letterbox 黑边点击由 Widget 忽略；Page 原样转发 `FramePoint`，不重复映射。Controller 仅在 active、Camera Streaming、无冲突 pending 且 focus capability 可用时转发给 Service；Camera Worker 根据当前实际帧尺寸做最终 clamp，防止模式切换或边界舍入产生越界。

关键接口与门控：

```cpp
void PreviewWidget::framePointRequested(const FramePoint &point);
void SmartMonitorPage::touchFocusRequested(const FramePoint &point);
void MonitorController::focusAt(const FramePoint &point);
void CameraWorker::focusAt(const OwnerId &owner, FramePoint point);

const bool canFocus = active_ && !deactivating_ &&
                      cameraActual_ == CameraState::Streaming &&
                      focusPending_ == PendingOp::None &&
                      cameraCapabilities_.touchFocus;
```

不变量：`PreviewWidget` 映射，Page 只转发，Controller 门控，Worker clamp。

**TODO 练习**：测试预览中心、四角、左右黑边；记录 Widget size、content rect、frame size、`FramePoint` 和 Worker clamp 后坐标。

### 任务 6：建立最小 App 标识和 OwnerId

第一版可以使用静态 enum 和简单值对象：

```cpp
enum class AppId { Desktop, SmartMonitor, Camera, Ap3216c, Ld2410c };

struct OwnerId {
    quint64 value = 0;
};
```

OwnerId 在创建前台 Controller 时分配，并随每次 Camera 命令传入。Service 至少拒绝与当前 owner 不匹配的 stop/control；第一版无需实现优先级和抢占。

**练习**：旧 Smart Monitor 停止后，用旧 OwnerId 再发一次 Camera control，预期返回 Busy/Cancelled/Invalid owner，而不是影响新页面。

### 任务 7：实现 Shell 前台状态

Shell 至少区分：

- `Desktop`：无前台业务页面；
- `Active`：一个 Page/Controller 正常使用；
- `Deactivating`：旧页面正在释放资源；
- `Exiting`：进程退出，不再创建页面。

只保留一个待打开 App；Deactivating 期间重复点击可以替换目标或直接忽略，但行为必须固定并可观察。

### 任务 8：实现异步切页

1. 收到 Back/App 入口请求。
2. 若当前无页面，直接创建目标页面。
3. 若当前 active，禁用旧页面输入并记录 next App。
4. 连接一次 deactivated completion，调用旧 Controller `deactivate()`。
5. completion 到达后销毁旧 Page/Controller。
6. 再创建 next Page/Controller，分配新 OwnerId 并 activate。

**练习**：在录像中快速连续按 Back、Camera、Smart Monitor，确认没有两个前台页面、两个 capture start 或已销毁对象回调。

## 关键接口与不变量

Page 接线保持直接；具体连接由练习完成：

```cpp
void SmartMonitorPage::startRequested();
void SmartMonitorPage::touchFocusRequested(const FramePoint &point);
void SmartMonitorPage::render(const SmartMonitorViewState &state);
void SmartMonitorPage::setPreviewFrame(const QImage &frame);
```

render 必须幂等，并用 `QSignalBlocker` 阻止控件回授。Shell 的切换接口保持窄小：

```cpp
void DesktopShell::openApp(AppId appId);
void DesktopShell::finishSwitch();
```

切换不变量：`finishSwitch()` 只由一次 deactivated completion 触发，先销毁已 idle 的旧前台对象，再创建下一页。若各 Controller 没有共同基类，不要仅为这段代码提前设计通用继承体系。

**TODO 练习**：补齐 Page 单向接线、幂等 render 和 Shell 状态分支，并验证重复 Back 不产生多个 completion connection。

## 检查点

- Page 头文件不包含设备、Service、Worker/backend 头文件。
- Page signal 名表达用户意图，不表达底层 `open/ioctl/startWriter`。
- ViewState 完整决定文本和 enabled，render 幂等且不产生业务副作用。
- 触摸链路固定为 PreviewWidget 映射 `FramePoint`、Page 转发、Controller 门控、Worker clamp。
- 480x272 下 Back、Stop 和关键状态始终可见且可触摸。
- Shell 任意时刻最多持有一个 active 前台 Page/Controller。
- 每个前台 Camera 使用者有明确 OwnerId。
- Deactivating 期间不会创建下一个 Camera Controller。
- 旧 Controller 发出 deactivated 后才销毁其 Page/Controller。
- 新页面启动后，旧 owner 的晚到命令不能控制 Camera。

## 常见错误

- Page 在按钮 slot 中直接调用 CameraService 或根据 presence 启停录像。
- Page 解析 `recording:...` 字符串决定按钮状态。
- `render()` 设置 ComboBox 导致 `currentIndexChanged` 再次提交命令。
- Page 或 Controller 重复做坐标映射，或按 QLabel 整体尺寸映射而忽略 letterbox 黑边。
- 用绝对坐标堆满 480x272，错误文本一长就覆盖按钮。
- Shell 先 delete Controller，再等待它发 stopped signal。
- 切页中在 GUI 线程调用无界 `thread->wait()`。
- Back 连点触发多次 deactivate 和多个 completion connection。
- OwnerId 使用页面裸指针，页面销毁后留下悬空身份。
- 为第一版提前实现 AppCatalog、导航栈、lease token 和抢占优先级。

## 最窄板端验证

```bash
QT_QPA_PLATFORM=linuxfb "${APP_BINARY}"
```

板端只走资源切换主线：

1. 从 Desktop 打开 Smart Monitor，确认画面、状态和触摸控件适配 480x272。
2. Start 并进入 Active/Recording。
3. 按 Back 或打开 Camera App，观察旧页先显示 stopping/busy，再返回 Desktop 或进入 Camera。
4. Camera App 成功 Start/Stop，确认没有 `EBUSY`、重复 open 或双预览。
5. 再切回 Smart Monitor，确认分配新 OwnerId 且旧页不再更新 UI。

可结合日志检查同一时刻只有一个 Camera owner 和一个 capture worker 在 Streaming。

## 异常验收

| 场景 | 预期结果 |
| --- | --- |
| 录像中切到 Camera App | Shell 等待 Writer/Camera idle 后才创建 Camera Page |
| deactivate 较慢时连续点击入口 | 最多保留一个 next App；不重复 stop、不创建第二页面 |
| Camera stop 返回错误 | 旧页保持 busy/error，不把资源当成已释放 |
| 旧 owner 晚到 control/result | 不影响新 owner；旧 Page 不再接收渲染 |
| preview frame 在切页时晚到 | receiver 存活时可忽略；销毁后连接自动断开，不崩溃 |
| 长错误文本 | 不覆盖 Back/Stop，必要时省略并把详情写日志 |
| 点击预览黑边 | 不产生越界帧坐标 |
| 模式切换时晚到 FramePoint | Controller 按状态门控；Worker 按当前帧尺寸 clamp |
| 触摸快速连按 Back | deactivate 幂等，completion 只处理一次 |

## 完成标准

- 所有 Page 只发意图、渲染 ViewState，不访问设备或执行业务状态机。
- Smart Monitor 在 480x272 实机上关键信息清晰，操作目标可触摸，无重叠和截断关键操作。
- Shell 同一时刻只有一个前台 App。
- Camera App 与 Smart Monitor 往返切换时不重复 open，不存在两个 capture owner。
- OwnerId 能阻止旧页面误控当前 Camera，但未引入不必要的 lease 框架。
- deactivate 是异步且幂等的，资源 idle 后才销毁 Page/Controller。
- 页面销毁后不再处理设备结果或更新 Widget。

## 复盘

完成后用实际代码回答：

1. 为什么坐标映射属于 PreviewWidget，而 Page 只转发 `FramePoint`、Controller 门控、Worker 最终 clamp？
2. 完整 ViewState 相比多个 getter 和多个零散 signal 有什么收益？
3. 为什么普通切页不能同步等待 Worker 退出？
4. Shell 如何证明任意时刻只有一个前台 Camera owner？
5. OwnerId 解决了什么问题，又没有解决哪些多 owner 并发问题？
6. 为什么第一版不需要 AppCatalog、lease token 和后台 session？何时才需要升级？

建议保留一张页面切换时序图和一张 480x272 控件尺寸草图，复盘时对照实机截图检查布局假设。

## 三分钟面试口述

> UI 层采用 Qt5 Widgets，Page 只发用户意图并渲染完整 ViewState。触摸对焦由 PreviewWidget 映射成 `FramePoint`，Page 只转发，Controller 门控，Camera Worker 最终 clamp。Shell 采用单前台和简单 OwnerId；切页时先禁用旧页并异步 deactivate，资源 idle 后才销毁旧对象、创建下一页。这样避免 UI 参与业务判断，也避免两个 Camera owner 在切换期间并存。
