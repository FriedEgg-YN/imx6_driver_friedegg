# 17 Camera Controls、自动对焦与补光

## 目标与前置

基于 [14-camera-capability.md](14-camera-capability.md) 枚举的 control 元数据和 [15-camera-mmap-preview.md](15-camera-mmap-preview.md) 的稳定 Streaming 状态，实现少量已验证 controls：普通 AF、触摸对焦、torch/flash 基础验证。第一版不制作通用 control 面板，也不假设每个 OV5640 驱动暴露相同私有 ID。

前置：能区分 control capability、当前值和命令型 control；理解 Service 接受命令、Worker 执行 ioctl、最终结果异步返回。

## 最低必懂模型

- `VIDIOC_QUERYCTRL`/`QUERYMENU` 告诉应用 control 是否存在、类型、范围、步长和 flags。
- `VIDIOC_G_CTRL` 读取当前值；`VIDIOC_S_CTRL` 设置值或触发按钮型命令。
- control 存在不代表当前状态可写：还要检查 `DISABLED`、`READ_ONLY`、`INACTIVE`、`GRABBED`。
- 标准 control 优先使用标准 `V4L2_CID_*`；触摸 AF 的私有 control 必须来自当前驱动 ABI，并以 capability 探测结果为准。
- control ID 是稀疏 ABI 编号，不能假设连续，也不能把 ID 空洞当成有效 control；枚举边界使用 `V4L2_CID_NEXT_CTRL` 语义或驱动返回的实际 ID。
- 普通 AF 通常写 `V4L2_CID_AUTO_FOCUS_START`，然后周期读取 `V4L2_CID_AUTO_FOCUS_STATUS`；它不是一次 S_CTRL 返回就完成。
- `V4L2_CID_AUTO_FOCUS_STATUS` 是位掩码：分别按 `V4L2_AUTO_FOCUS_STATUS_BUSY`、`V4L2_AUTO_FOCUS_STATUS_REACHED`、`V4L2_AUTO_FOCUS_STATUS_FAILED` 位判断，不能按互斥整数枚举直接比较。
- torch 是持续补光状态；flash/strobe 是一次触发流程。二者不要共用一个布尔值。
- 触摸点来自 PreviewWidget 坐标，必须先映射到未缩放、可能 letterbox 的 active frame 坐标，再 clamp。

## V4L2/Qt 数据流

```text
PreviewWidget touch -> 按 content rect 映射为 FramePoint
  -> CameraService::focusAt(owner, FramePoint)
  -> queued CameraWorker command
  -> QUERYCTRL guard -> S_CTRL(private zone/x/y) -> S_CTRL(AF_START)
  -> G_CTRL(AF_STATUS) polling
  -> CameraService typed CameraControlResult -> UI
```

torch 路径相同，但 Controller/MonitorEngine 决定期望策略，Service 只执行明确的 control 命令。CameraWorker 不读取 lux，也不判断 presence。

## 线程和 buffer 所有权

- 所有 control ioctl 与 capture fd 位于 CameraWorker 线程，避免与 STREAMOFF/close 竞争。
- Service 只传 `FramePoint`、`StrobeMode`、control id/value 等值，不传 Widget 或鼠标事件指针。PreviewWidget 负责 content rect/letterbox 映射，Controller 负责能力、状态和操作门控，Worker 最后对 FramePoint 做边界 clamp。
- AF polling timer 应属于 Worker 线程并在该线程创建；也可复用采集循环的短周期检查，但不能阻塞 DQBUF。
- control 操作不持有 MMAP buffer；不要在 DQBUF 后等待 AF 完成再 QBUF。
- Page 销毁后 Controller 停止提交；Service 仍可完成 Worker 收敛，但通过 context connection 避免回调旧页面。

## 分步手写

1. 从 capability 快照筛选第一版 control：flash LED mode、strobe/start/stop、AF start/stop/status，以及板上驱动明确枚举出的 touch AF controls。
2. 定义 `CameraControlOperation` 和 `CameraControlResult`，包含 operation、code、可选回读值和 error。
3. Service 在 GUI 线程校验 owner、Streaming 状态、pending 状态和参数范围；同步门面返回 `Accepted/Busy`，最终异步结果返回 `Succeeded/IoError` 等状态，阶段语义以 [SPEC](../../SPEC.md#10-类型化状态与异步结果) 为准。
4. Worker 收到命令后再次检查 fd/state 和最新 control flags，执行 `S_CTRL`。
5. 普通 AF：触发 start，进入 `AfState::Running`，非阻塞轮询 status，按 BUSY/REACHED/FAILED 位映射为 Running/Reached/Failed，再处理 Cancelled/Timeout。
6. 触摸 AF：Controller 先做 Widget 到 frame 坐标变换；Worker clamp 后按当前驱动 ABI写 zone/x/y，再启动 AF。
7. torch：写 LED mode 后 `G_CTRL` 回读；只在回读确认后发布 On。
8. flash：明确 prepare、trigger、stop/restore；错误时尽力恢复 None。
9. Stop preview 前取消 AF polling、停止 strobe、关闭 torch，再执行 STREAMOFF。
10. 练习：设备不支持 touch controls 时，普通 AF 保持可用，UI 仅禁用触摸对焦。

## 关键伪代码/片段

通用小范围 S_CTRL：

```cpp
OperationResult CameraWorker::setControl(quint32 id, int value)
{
    if (!controlWritable(id)) return unsupported();
    v4l2_control ctrl = {};
    ctrl.id = id;
    ctrl.value = value;
    if (xioctl(fd, VIDIOC_S_CTRL, &ctrl) < 0) return ioError(errno);
    return succeeded(); // ioctl 已完成；AF 最终结果仍由 status polling 给出
}
```

类型化最终结果：

```cpp
enum class CameraControlOperation { AutoFocus, TouchFocus, SetTorch, TriggerFlash };
enum class AutoFocusOutcome { Busy, Reached, Failed, Cancelled, Timeout };

struct CameraControlResult {
    CameraControlOperation operation;
    OperationCode code;
    AutoFocusOutcome afOutcome = AutoFocusOutcome::Cancelled;
    QString error;
};
```

不要让 UI 解析 `"AF: REACHED"` 或 `"torch:on"`。字符串只由 Controller/ViewState 生成。

预览坐标映射要扣除 letterbox：

```text
scale = min(widgetW/frameW, widgetH/frameH)
drawW = frameW * scale; drawH = frameH * scale
contentRect = centeredRect(widgetSize, frameSize)
if (!contentRect.contains(touch)) reject()
framePoint = FramePoint((touch-contentRect.topLeft()) / scale)
// Controller gates the operation; Worker clamps framePoint to active bounds.
```

Qt5/C++11 实现时用浮点计算后明确取整；点在画面外时应拒绝，而不是吸附到边缘误对焦。

## 检查点

- UI enable 状态来自实际 control capability，不按设备名称猜测。
- AF start 返回后 UI 显示 Running，只有 status 回读终态才显示完成。
- control ioctl 不降低 DQBUF/QBUF 的及时性。
- touch 点在缩放和留黑边情况下仍映射到正确 frame 区域。
- Stop/错误退出后 torch 熄灭、AF polling 停止。

## 常见错误

- 硬编码私有 touch AF ID，却不先 QUERYCTRL。
- 把 `S_CTRL(AF_START)` 成功当成对焦成功。
- 在 GUI 线程 sleep 等待 AF status。
- 直接按 Widget 宽高比例映射，忽略保持比例缩放产生的 letterbox。
- 使用字符串状态驱动按钮 enabled/disabled。
- Stop preview 先 close fd，再尝试关闭 torch。

## 交叉构建与板端验证

```bash
"${QT_HOST_DIR}/bin/qmake" "${SOURCE_DIR}/app.pro"
make -j2
bash buildscripts/build_and_deploy.sh drv "${PACKAGE}"
```

板端先确认 ABI：

```bash
v4l2-ctl -d "${VIDEO_DEVICE}" --list-ctrls-menus
QT_QPA_PLATFORM=linuxfb "${APP_BINARY}"
```

逐项验证：Streaming 后普通 AF；点击画面中心和四个区域；torch 开/关；已确认安全时再测 flash。预期 UI 显示 typed 状态对应的 Running/Reached/Failed，连续操作返回 Busy 而不是重入。不要把真实设备路径、存储路径或板卡地址写入项目文档。

## 失败路径

- capability 不存在或 disabled/read-only：返回 `Unsupported`，不发 ioctl。
- `S_CTRL` 返回 `EBUSY`：发布 Busy，保持当前已知状态，不伪造成功。
- AF status 读取失败：结束 polling，发布 `IoError`；必要时尝试 AF_STOP。
- AF 超时：发送 AF_STOP（若支持），发布 Timeout，capture 继续。
- torch/flash 中途失败：尽力写 stop/None；恢复失败要保留明确 error，Stop preview 仍继续清理。
- 模式切换/Stop 与 control pending 相遇：取消 control 操作，先收敛设备生命周期。

## 完成标准

Camera App 只展示并操作驱动实际支持的关键 controls；普通 AF、触摸对焦和补光均走 Worker；AF 有非阻塞终态；触摸坐标映射正确；所有结果为类型化异步结果；异常和 Stop 能恢复到可解释状态。

## 复盘问题

1. capability 存在为何仍可能不能写？
2. AF 的“命令已接受”和“镜头已合焦”分别由什么证据表达？
3. touch point 为什么应由 Controller 映射、Worker clamp？
4. torch 和 flash 生命周期有什么本质区别？
5. control 与 Stop 并发时，为什么设备生命周期命令优先？
