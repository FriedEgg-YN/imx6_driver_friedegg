# 架构、线程与所有权

## 目标/前置

目标是在写 QObject 和设备 I/O 前，手工确定职责、依赖方向、线程 affinity 及资源释放顺序。前置是完成 [现有代码审查](01-current-code-review.md)，并阅读 [SPEC](../../SPEC.md) 中“第一版架构”“线程与对象所有权”和“UI 边界”。

## 最低必懂模型

所有权必须回答四个问题：谁创建、在哪个线程使用、跨边界传什么、谁停止/销毁。

| 对象/资源 | 所在线程 | owner | 跨线程边界 |
| --- | --- | --- | --- |
| Shell/Page/Controller/Engine | GUI | Qt parent 或前台生命周期 | typed state、用户意图 |
| Service facade | GUI | CompositionRoot | queued 命令/状态 |
| SensorWorker/backend/fd | Sensor I/O | Service 生命周期 | 拥有数据的 sample |
| CameraWorker/fd/MMAP | Capture | CameraService 生命周期 | `QImage`/`QByteArray` 副本 |
| MediaWriter/file/queue | Writer | CameraService 生命周期 | 有界帧任务 |

QObject affinity 决定 slot 在哪个线程执行；Qt parent ownership 决定 QObject 生命周期。两者相关但不是同一概念。Linux fd、MMAP 和文件句柄则由实际执行 I/O 的对象独占。

## 文件或数据流

建议在纸上或本文练习区先画 Composition Root：

```text
main
 `-- CompositionRoot
      |-- SensorService -> QThread -> SensorWorker -> sensor backends
      |-- CameraService -> QThread -> CameraWorker
      |                `-> QThread -> MediaWriter
      `-- DesktopShell -> current Page + Controller -> MonitorEngine
```

停止数据流：

```text
Page disables input
 -> Controller cancels business timers and requests stop
 -> Writer closes recording
 -> CameraWorker STREAMOFF/unmap/close
 -> stopped status reaches Controller
 -> Shell destroys Page/Controller
```

## 分步手写任务

1. 为每个目标类写一行职责，出现“以及”超过两次时重新拆分职责。
2. 画 include 依赖，只允许 Page 指向 Controller API、Controller 指向 Engine/Service API、Worker 指向 backend。
3. 为每个 QObject 标记创建线程、最终 affinity、parent 和销毁触发者。
4. 为 AP3216C fd、LD2410C fd/UART、V4L2 fd、MMAP buffer、预览图像、录像队列和输出文件填写所有权表。
5. 设计跨线程类型：sample/status 必须拥有自身数据；禁止传 MMAP 裸指针和 Widget 指针。
6. 设计前台切换握手：`deactivate()` 先发异步 stop，收到 idle/stopped 后 Shell 才销毁旧页面并打开新 Camera owner。
7. 设计进程退出：先禁止新命令，再停止 writer/capture/sensor，最后对线程做有界等待并记录超时。
8. 用架构规则审查每个新 include 和每个 `moveToThread()`。

## 关键代码片段或伪代码

Service 只做线程边界门面：

```cpp
// public API 同步返回是否受理；最终结果仍由 statusChanged 异步返回。
class CameraService : public QObject {
    Q_OBJECT
public:
    OperationResult startPreview(const CameraMode &mode, OwnerId owner);
signals:
    // 内部 queued command signal 只携带命令，不冒充操作结果。
    void startWorkerRequested(CameraMode mode, OwnerId owner);
    void statusChanged(CameraStatus status);
};
```

所有 Service public command API 统一同步返回 `OperationResult`，只描述参数/owner/当前状态下是否接受并已提交命令；发往 Worker 的内部 command signal 返回类型为 `void`。设备操作最终成功或失败通过 typed status/result signal 异步发布。

采集 buffer 的关键所有权规则：

```text
DQBUF: CameraWorker 暂时拥有驱动 buffer 的可读窗口
copy: 复制 GUI/Writer 需要的数据到 owning value
QBUF: 归还后任何消费者都不得引用原 MMAP 地址
```

练习：画出一帧 RGB565 从 `DQBUF` 到 Page 的每次所有权变化，并指出在哪一步可以安全 `QBUF`。

## 检查点

- Engine 不继承 QObject，不 include Qt、设备和文件系统头文件。
- Worker 不知道 Page、Controller 或 presence 业务规则。
- GUI 线程不执行周期性 sysfs/ioctl、JPEG 编码或文件写入。
- queued signal 的参数是可复制、拥有自身数据且已注册的类型。
- 普通页面切换没有 GUI 线程无限 `wait()`。
- 同一时刻最多一个 Camera owner。

## 常见错误

- 在构造函数仍运行于 GUI 线程时创建 Worker 的 QTimer 或打开设备。
- 给 `moveToThread()` 后的 QObject 设置属于另一线程的 parent。
- 以 `shared_ptr<QObject>` 混合 Qt parent ownership，造成销毁线程不明确。
- 发出指向局部数组、MMAP 或可变共享容器的跨线程信号。
- 认为 signal 发出即代表异步设备操作最终成功。
- 页面析构中无界等待 Worker，造成 UI 卡死。

## 最窄验证

架构阶段先做静态检查和一个线程探针：

```bash
rg '#include.*(v4l2|ioctl|backend)' src/app/application src/app/shell
rg 'wait\(' src/app
```

在临时 QObject 探针中打印创建、slot 和销毁时的 `QThread::currentThread()`；交叉编译后板端运行：

```bash
QT_QPA_PLATFORM=linuxfb "${THREAD_PROBE_BINARY}"
```

预期：GUI slot 留在主线程，Worker slot 位于目标线程，退出时先观察到 stop/close，再观察到线程结束。

## 完成标准

- 有一张对象/线程/owner/销毁顺序表和一张帧数据流图。
- 能解释 QObject affinity、Qt parent、C++ RAII 和 Linux 资源 owner 的区别。
- Camera 页面切换和进程退出都有明确、可观察的停止协议。
- 架构未引入第一版不需要的通用 Device 基类、lease token 或 request bus。

## 复盘问题

1. Service 为什么位于 GUI 线程，而 Worker 必须位于 I/O 线程？
2. `QBUF` 后仍让 QImage 引用 MMAP 内存会发生什么？
3. 页面关闭与进程退出为何可以采用不同的等待策略？
4. 哪些对象适合 Qt parent ownership，哪些资源适合 RAII？
5. 如果未来出现两个长期 Camera Controller，当前哪条假设会失效？
