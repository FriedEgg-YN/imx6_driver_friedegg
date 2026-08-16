# 现有半成品代码审查

## 目标/前置

目标是把现有 `src/app` 当作代码审查练习：识别可保留的方向、编译问题、未完成行为和语义风险，再在原路径内手动替换半成品。权威要求来自 [SPEC](../../SPEC.md)，不是现有实现。

前置：先阅读 [学习路线](00-learning-roadmap.md)，再浏览现有 [qmake 工程](../../app.pro)、[类型](../../application/smart_monitor/smart_monitor_types.h)、[Engine 头文件](../../application/smart_monitor/smart_monitor_engine.h)、[Engine 实现](../../application/smart_monitor/smart_monitor_engine.cpp)、[测试](../../tests/monitor_engine_test.cpp)和[状态表](../../application/smart_monitor/状态转移表.md)。

## 最低必懂模型

代码审查要分三层，不因“方向正确”而忽略“当前不可用”：

- 构建完整性：qmake target、源文件和声明/定义是否闭合。
- 业务完整性：SPEC 中每个状态、输入、timer 和失败语义是否落地。
- 架构完整性：纯 Engine 是否真与 Qt、设备和副作用隔离。

现有代码有一个值得保留的核心方向：类型和 Engine 没有 include Qt、Widget 或 Linux 设备头文件。但它只是一段未闭合的草稿。

## 文件或数据流

当前可见关系：

```text
app.pro -> application/（缺少可见子工程入口）
smart_monitor_engine.cpp -> smart_monitor_engine.h -> smart_monitor_types.h
monitor_engine_test.cpp（空）
状态转移表.md（部分规则）
```

预期关系则应是：

```text
test main -> MonitorEngine public inputs -> state transition
          -> MonitorDecision -> assert(state + timer + desired effects)
```

## 分步手写任务

1. 建立审查表，字段包含“证据位置、问题、后果、目标规则、重写动作”。
2. 检查构建：`app.pro` 是 `subdirs` 工程，但 `SUBDIRS += application/` 尚不能证明存在可构建 `.pro`；测试文件为空。
3. 检查声明/定义：实现中定义了头文件未声明的析构函数；`stop()`、输入处理和 timer timeout 仅声明未实现。
4. 检查命名：`persenceConfirmTimer` 拼写错误，`ActiveMonitoring` 与 SPEC 的 `Active` 不一致；命名应先统一再写测试。
5. 检查状态：`start()` 直接改两个状态，但没有表达 timer 取消，也没有明确旧样本是否清理。
6. 检查 Decision：当前只从状态推导 camera/recording/torch，没有输出 confirm/cooldown timer 命令。
7. 检查输入有效性：类型有 `valid` 和时间戳，但当前实现未处理 stale、Unknown 或 fail-safe Cooldown。
8. 检查文档状态表：补充 timeout、Start、Stop 和 Unknown 后的 timer 行为，不能只写 presence 三值输入。
9. 把结论用于新工程设计，不直接修补或复制半成品文件。

## 关键代码片段或伪代码

用“声明必须有定义，定义必须有声明”检查类边界：

```cpp
class Example {
public:
    Example();
    ~Example();       // 若在 .cpp 中定义，应在类中声明
    Decision stop();  // 若测试调用，链接时必须有定义
};
```

用状态转移表驱动审查，而不是只看分支是否能编译：

```text
当前状态 + 输入 + 输入有效性 + timer状态
    -> 新状态 + timer命令 + camera/recording/torch wanted
```

练习：分别为“Pending 收到 absent”“Cooldown 收到 present”“Active 收到 invalid”写出预期新状态和 timer 命令，再去对照现有代码是否存在对应路径。

## 检查点

- 能明确说出现有代码“可参考的方向”和“不能作为答案的行为”。
- 每项问题都有文件证据和可观察后果，不只写风格意见。
- 能区分编译错误、链接错误、未覆盖规则和架构风险。
- 审查结论不要求修改现有半成品。

## 常见错误

- 看到纯 C++ 类型就判断 Engine 已经完成。
- 只检查 happy path，不检查 Stop、timeout、invalid 和 stale。
- 把拼写或命名差异当作纯美观问题，忽略跨层 API 漂移。
- 通过给所有缺失函数返回默认 Decision 来“修复”链接，掩盖未实现规则。
- 让测试迎合当前实现，而不是以 SPEC 的状态转移为 oracle。

## 最窄验证

本篇以只读审查为主，可在临时构建目录验证构建入口，不修改现有文件：

```bash
mkdir -p /tmp/app-review-build
"${QMAKE}" -o /tmp/app-review-build/Makefile "${SOURCE_DIR}/app.pro"
make -C /tmp/app-review-build
```

以上命令从仓库根目录执行。预期：当前半成品可能在 qmake 子工程发现或后续链接阶段失败；失败日志应被归类为审查证据，不在本篇中绕过。

## 完成标准

- 输出至少覆盖构建、声明/定义、状态转移、timer、有效性/stale、测试六类问题。
- 能解释为何现有代码只保留“纯 Engine + typed Decision”的思想，而不做整文件迁移。
- 已为新手写工程列出最先需要通过的三个测试：Start/Stop、Pending 取消、Active/Cooldown。

## 复盘问题

1. 哪个问题会在编译期出现，哪个会在链接期出现，哪个只能由行为测试发现？
2. `valid=false` 为什么不能通过 `presence=false` 的分支处理？
3. Decision 中 timer 命令为何与 cameraWanted 同样重要？
4. 如果只修复现有 `.cpp`，仍有哪些 SPEC 能力完全没有被验证？
5. 这次审查中，哪些结论来自代码，哪些来自权威规格？
