# 08 Qt 对象模型：先把线程与所有权说清楚

> 权威范围见 [SPEC](../../SPEC.md)。本阶段只练 Qt 5 Widgets/C++11 的对象关系，不访问真实设备。

## 目标与前置

目标：能解释 `QObject` parent ownership、thread affinity、事件循环和 signal/slot，并手写一个 GUI 线程内的最小 Page/Controller 闭环。

前置：会写 C++ 类、构造/析构、引用和 `enum class`；了解进程与线程。无需掌握 QML、智能指针大全或模板元编程。

## 最低必懂模型

- `QObject` 是“身份对象”：不可复制，通常由 parent 或明确生命周期 owner 管理。
- `new QLabel(page)` 使 `page` 成为 label 的 parent；parent 析构时递归删除子对象。
- 没有 parent 的栈上 `QObject` 由作用域管理；不要再给它安排另一个 owner。
- thread affinity 是 `QObject::thread()` 指向的线程，不是“哪个线程拿着指针”。对象默认属于创建它的线程。
- event loop 处理 queued signal、timer、socket notifier 和 `deleteLater()`；线程没有运行事件循环时，这些事件不会按预期执行。
- `Qt::DirectConnection` 在发射 signal 的线程立即调用；`Qt::QueuedConnection` 把调用投递到 receiver affinity 所在线程；`Qt::AutoConnection` 在发射时按线程关系选择。
- 跨线程 queued signal 的参数必须可复制并拥有自身数据；自定义类型需 `Q_DECLARE_METATYPE`，必要时启动前 `qRegisterMetaType<T>()`。
- Widget 只能在 GUI 线程创建和操作。Page 发意图，Controller 组装 ViewState，Worker/backend 不生成 UI 文案。

`QObject` 不等于线程，`QThread` 对象也不等于它管理的工作线程。先掌握对象 affinity，再进入 [09 Worker 线程](09-qt-worker-thread.md)。

## 数据流、线程与所有权

```text
GUI thread / event loop
  QPushButton --clicked--> DemoController --viewStateChanged--> DemoPage
       parent: Page          parent: Page or Shell             owns Widgets
```

| 对象 | affinity | owner | 可做什么 |
| --- | --- | --- | --- |
| `DemoPage`、Widgets | GUI 线程 | Shell/Qt parent | 接收操作、渲染 |
| `DemoController` | GUI 线程 | Page 或 CompositionRoot | 轻量状态转换 |
| 值类型 `DemoViewState` | 无 affinity | 按值传递 | 描述 UI，不持有 Widget 指针 |

本阶段所有对象都在 GUI 线程。不要为了“练线程”提前把 Controller 移走；业务 timer 和纯状态转换本来就应留在 GUI 线程。

## 分步手写任务

1. 定义 `DemoViewState { bool running; QString status; }`，不要让 Page 自己拼业务状态。
2. 写 `DemoController : public QObject`，提供 `start()`、`stop()` slot 和 `viewStateChanged(DemoViewState)` signal。
3. 写 `DemoPage : public QWidget`，包含 Start、Stop、状态 label；按钮只表达用户意图。
4. 用函数指针语法 `connect` Page 与 Controller，不使用字符串版 `SIGNAL/SLOT`。
5. 在构造后发布一次初始 ViewState，观察 signal 在连接前发射会丢失；改为由 Page 主动请求刷新或在连接后调用。
6. 给 Page/Controller 打印 `QThread::currentThread()` 与 `object->thread()`，确认二者均为 GUI 线程。
7. 关闭页面，确认 parent ownership 自动释放；对“栈上 QObject 又被短命 parent 管理”的析构顺序只做纸面推演。需要观察析构日志时，使用安全作用域：先构造 parent、后构造栈上 child，确保 child 先析构并将自身从 parent 的 children 列表移除；不要实际运行 parent 先删除栈对象的双重所有权场景。

## 关键 Qt/C++ 片段

```cpp
struct DemoViewState {
    bool running = false;
    QString status;
};
Q_DECLARE_METATYPE(DemoViewState)

class DemoController : public QObject {
    Q_OBJECT
public slots:
    void start() {
        state_.running = true;
        state_.status = QStringLiteral("Running");
        emit viewStateChanged(state_);
    }
signals:
    void viewStateChanged(const DemoViewState &state);
private:
    DemoViewState state_;
};
```

```cpp
connect(startButton, &QPushButton::clicked,
        controller, &DemoController::start);
connect(controller, &DemoController::viewStateChanged,
        page, &DemoPage::render);
```

练习：给 `DemoViewState` 增加 `updatedAtMs`；Page 只根据字段渲染，不访问 Controller 内部成员。

## 检查点

- 能说出 parent ownership 与 thread affinity 是两个正交概念。
- 能预测同线程和跨线程 `Qt::AutoConnection` 的执行位置。
- `render()` 只在 GUI 线程运行，且只处理 ViewState。
- 没有 `shared_ptr<QObject>`，没有复制 QObject，没有 Worker/backend 头文件进入 Page。

## 常见错误

- 误以为 sender 属于哪个线程就决定 slot 在哪个线程；queued slot 看 receiver affinity。
- 给已有 parent 的 QObject 再用 `unique_ptr` 独占，形成双 owner。
- 在子线程直接 `label->setText()`。
- 连接到临时 lambda 时捕获悬空裸指针；优先给 `connect` 提供 context QObject。
- 认为 emit 会保存“最近状态”；signal 是事件，不是状态存储。
- 用字符串 `"failed:..."` 驱动分支；程序分支应使用 enum/struct，字符串只展示和记录。

## 交叉构建与板端最窄验证

当前 `src/app/` 尚未形成可部署 package，先只验证新建的最小 qmake target；不要沿用旧应用 binary 名。

```bash
mkdir -p /tmp/qt-object-model-build
"${BUILDROOT_DIR}/output/host/bin/qmake" -o /tmp/qt-object-model-build/Makefile "${SOURCE_DIR}/${QT_OBJECT_MODEL_PROJECT}.pro"
make -C /tmp/qt-object-model-build -j2
file "/tmp/qt-object-model-build/${APP_BINARY}"
```

以上命令从仓库根目录执行，源目录不产生 Makefile 或中间文件。

部署后在板端运行：

```bash
QT_QPA_PLATFORM=linuxfb "${APP_BINARY}"
```

最窄观察：反复点击 Start/Stop，关闭页面；状态正确切换，Widget 始终响应，退出无崩溃。package 接入后才使用 `bash buildscripts/build_and_deploy.sh drv <package>`。

## 正常、缺失与失败验收

| 场景 | 预期 |
| --- | --- |
| 正常 | Start/Stop 更新 typed ViewState，GUI 不阻塞 |
| 缺失输入 | 尚无设备输入时显示 `Unknown/--`，不显示 `0` |
| 操作失败 | ViewState 有独立 error/code，不能把失败解释为 `running=false` 的正常停止 |

## 完成标准

- 能画出 Page、Controller、Widget 的 owner 和 affinity。
- 能不看代码解释 direct/queued/auto connection。
- 最小页面可交叉编译并在板端启动、操作、退出。
- 代码没有设备 I/O；下一阶段再引入 `QThread/Worker/QTimer`。

## 复盘问题

1. 为什么 QObject 有 parent 后通常不再需要 `shared_ptr`？
2. receiver 被 `deleteLater()` 后，尚未处理的 queued 调用会怎样？
3. signal 在连接前发射，为什么后来连接的 Page 看不到初始状态？
4. Widget 为什么必须留在 GUI 线程？
5. thread affinity 与 C++ 对象所有权分别回答什么问题？
