# 09 Qt Worker 线程：QThread、Worker 与 QTimer

> 权威线程规则见 [SPEC](../../SPEC.md) 第 8 节。本阶段在 [08 Qt 对象模型](08-qt-object-model.md) 基础上，用可控假数据练异步链路。

## 目标与前置

目标：手写 `Service facade -> Worker -> QTimer`，理解 `moveToThread()`、queued signal/slot 和有序退出；保证 GUI 不执行阻塞 I/O。

前置：完成 08；理解互斥和事件循环的基本概念。暂不接 AP3216C/LD2410C。

## 最低必懂模型

- `QThread` 实例通常在创建它的 GUI 线程；`QThread::run()` 管理的是另一个执行线程。因此不要把业务 slot 放进 `QThread` 子类并误以为会自动在工作线程执行。
- 推荐 worker-object 模式：创建无 parent 的 Worker，`moveToThread(&thread)`，通过 queued command 调它。
- 有 parent 的 QObject 不能 `moveToThread()`；Worker 的 QObject 子对象必须在正确线程创建。
- Worker 的 `QTimer` 应在 Worker affinity 所在线程、线程启动后创建/启动，timeout 才在 I/O 线程执行。
- GUI 调 Service public API 同步得到 `OperationResult`，它只表示命令被接受或拒绝；Service 发往 Worker 的内部 command signal 为 `void`，最终状态由 Worker typed signal 异步返回。
- 跨线程不要共享可变容器、Widget 指针、fd 或 backend 指针。传值类型；错误、validity、timestamp 随数据一起传。
- 正常切页异步 stop，不在 GUI 线程无限 `wait()`；进程退出可 `quit()` 后有界 `wait(timeout)` 并记录超时。

## 数据流、线程与所有权

```text
GUI thread                         Sensor I/O thread
Page -> Controller -> Service --queued--> SensorWorker
                          ^                  `-- QTimer
                          `--- sampleReady --------'
```

| 对象 | affinity | owner/清理 |
| --- | --- | --- |
| Page/Controller/Service/QThread 对象 | GUI | CompositionRoot/Qt parent |
| SensorWorker | Sensor I/O | 无 parent；线程结束时 `deleteLater()` |
| Worker 内 QTimer/backend/fd | Sensor I/O | Worker；同线程创建、使用、销毁 |
| `FakeSample` | 无 affinity | queued connection 按值复制 |

CompositionRoot 拥有 Service 和 QThread 生命周期；Worker 独占 I/O 资源。Service 不直接读设备。

## 分步手写任务

1. 定义含 `valid`、`value`、`updatedAtMs`、`error` 的 `FakeSample` 并注册 metatype。
2. 写 `SensorWorker`，先只在 `start()` 中创建 `QTimer(this)`，每 500 ms 递增 sequence 并发 sample。
3. 写 `SensorService`：public `startSampling()/stopSampling()` 同步返回 `OperationResult`，受理后发 `void` 内部 command signal，不直接调用 Worker 方法。
4. 创建 `QThread` 和无 parent Worker，先 `moveToThread()`，再建立连接，最后 `thread.start()`。
5. 分别打印 command slot、timer timeout、Page render 的当前线程，确认工作发生在预期线程。
6. 增加故障开关：每第五次发布 `valid=false` 和 error；UI 显示 unavailable，不把 value 清成业务 0。
7. 实现 stop：停止 timer、释放模拟资源、发 `stopped()`；页面等待状态回调后再销毁。
8. 实现进程退出：先提交 stop，收到 stopped 后 `thread.quit()`；仅退出路径有界 `wait(1000)`。

## 关键 Qt/C++ 片段

```cpp
connect(service, &SensorService::startWorker,
        worker, &SensorWorker::start, Qt::QueuedConnection);
connect(worker, &SensorWorker::sampleReady,
        service, &SensorService::sampleReady, Qt::QueuedConnection);

worker->moveToThread(sensorThread);
connect(sensorThread, &QThread::finished,
        worker, &QObject::deleteLater);
sensorThread->start();
```

连接应在发布 command 前完成。Worker 的 timer 在 slot 内创建：

```cpp
void SensorWorker::start()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!timer_) {
        timer_ = new QTimer(this);
        connect(timer_, &QTimer::timeout, this, &SensorWorker::sampleOnce);
    }
    timer_->start(500);
}
```

练习：连续点两次 Start，Worker 应幂等，不应创建两个 timer 或加倍采样。

Sensor Worker 采用 QTimer 驱动的短时 `sampleOnce()`，每次 timeout 完成一次有界采样并立即返回事件循环。耗时 slot 会阻塞同一 Worker event loop，使 timer、queued stop 和错误恢复都无法及时处理；真实 backend 必须使用非阻塞 fd、`poll/select` 超时或拆分操作。Camera Capture 可采用带超时的事件驱动采集循环，MediaWriter 则使用线程安全的有界队列唤醒专用 writer 线程，不应机械套用 Sensor QTimer 模型。

## 检查点

- `sampleOnce()` 与 `QTimer::timeout` 在 Sensor I/O 线程。
- Page render 在 GUI 线程；GUI 持续响应拖动/点击。
- Worker 没有 parent 才移动线程；移动后不从 GUI 直接调用其普通方法。
- Start/Stop 是异步命令，pending 时不会重复提交。
- 退出后 timer 不再触发，Worker 和线程都结束。

## 常见错误

- `QTimer timer_;` 在 GUI 线程构造后随 Worker 移动，却又从错误线程启动；最清楚的练习方式是在 Worker `start()` 中创建。
- `worker->start()` 看似简单，实际是 GUI 线程直接调用；应用 signal 或 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`。
- 给 Worker 设置 GUI parent，导致 `moveToThread` 失败。
- 在线程仍运行时直接析构 `QThread`。
- 在 GUI 析构中无界 `wait()`，板端设备卡住时界面永久冻结。
- 用共享 bool 当停止标志却不处理同步；本阶段用同一 Worker event loop 中的 queued stop。
- 设备错误时发 `value=0` 或 `presence=false`；必须发 invalid/unavailable 和 error。
- 在 timer timeout slot 中执行无上限阻塞调用，导致同一 event loop 中的 stop command 永远排不到。

## 交叉构建与板端最窄验证

```bash
mkdir -p /tmp/worker-lab-build
"${BUILDROOT_DIR}/output/host/bin/qmake" -o /tmp/worker-lab-build/Makefile "${SOURCE_DIR}/${WORKER_LAB_PROJECT}.pro"
make -C /tmp/worker-lab-build -j2
file "/tmp/worker-lab-build/${APP_BINARY}"
```

以上命令从仓库根目录执行，使用独立 target build 目录。

```bash
QT_QPA_PLATFORM=linuxfb "${APP_BINARY}"
```

运行 30 秒，反复 Start/Stop 并退出；日志中的 thread pointer 应显示 Worker timeout 与 GUI render 不同。接入实际包后使用：

```bash
bash buildscripts/build_and_deploy.sh drv "${PACKAGE}"
```

## 正常、缺失与失败验收

| 场景 | 预期 |
| --- | --- |
| 正常 | 周期 sample 从 Worker queued 到 GUI，sequence 增长 |
| 数据缺失 | `valid=false`、值显示 `--`，不显示 0/false |
| Worker 失败 | error 可见，timer 可停止或按策略继续，GUI 仍可 Stop/退出 |

## 完成标准

- 能画出 QThread 对象、工作线程、Worker、timer 的 affinity 和 owner。
- 可证明所有模拟 I/O 在 Worker 线程，所有 Widget 更新在 GUI 线程。
- Start/Stop 幂等，正常切页不阻塞，进程退出仅有界等待。
- 能解释为何真实 AP3216C/LD2410C 可以共用一个 SensorWorker 线程。

## 复盘问题

1. 为什么继承 `QThread` 并把 slot 写进去常造成 affinity 误解？
2. `moveToThread()` 改变执行位置还是 C++ owner？
3. queued signal 的自定义参数为什么要注册 metatype？
4. timer 为什么应在线程启动后由 Worker 初始化？
5. stop command 若排在一个长阻塞 ioctl 后面，会发生什么，如何限制阻塞时间？
