# Qt5/C++11 HMI 手写学习路线

## 目标/前置

目标是在 `src/app/` 内手动替换现有半成品，手写一个可在 i.MX6ULL 上运行的 Qt Widgets HMI，并能解释从 Linux 驱动 ABI、跨线程值传递、业务状态机到 UI/媒体文件的完整链路。本路线以 [SPEC](../../SPEC.md) 为权威；现有半成品只用于识别问题，不作为复制模板。开发和验收主线在目标板，host 只辅助运行不依赖 Qt/设备的纯逻辑测试。

前置知识控制在最低范围：能编写 C++11 类和结构体，理解头文件/实现文件、引用和 `enum class`，会运行 qmake、make 和一个交叉编译器。暂不要求掌握模板元编程、QML、QtMultimedia 或完整测试框架。

命令约定：先在当前 shell 设置所需变量（例如 `HOST_QMAKE`、`TARGET_QMAKE`、`SOURCE_DIR`、`APP_BINARY` 和 `PACKAGE`）；变量未确定时不要运行对应命令，尤其不要运行 Buildroot 命令。

## 最低必懂模型

先记住五层单向依赖：

```text
Page -> Controller -> pure Engine / typed Service API
Service facade -> Worker -> Linux backend
```

- Page 只表达用户意图和渲染状态。
- Controller 把 Qt signal、timer 和设备快照转换成 Engine 输入，并执行决策。
- Engine 是纯 C++ 规则，不知道 QObject、设备节点或文件。
- Worker 在专用线程做阻塞 I/O，Service 是窄 QObject 门面。
- backend 只理解某一种 Linux ABI，不生成 UI 文案。

学习顺序遵循“先纯逻辑，后设备；先单点，后组合；先同步模型，后异步执行”。

## 文件或数据流

建议边写边形成以下文件，不要预先创建空目录树：

```text
app.pro
application/smart_monitor/monitor_types.h
application/smart_monitor/monitor_engine.{h,cpp}
tests/monitor_engine_test.cpp
```

后续纵向数据流：

```text
driver ABI -> backend -> Worker -> typed sample
             -> Controller -> Engine -> desired decision
             -> CameraService/MediaWriter -> typed status -> Page
```

本计划按 SPEC 阶段 0-8 导航；编号是学习顺序，不代表每篇都是独立实施阶段：

- [x] 阶段 0，旧闭环和 ABI 基线：[01 现有代码审查](01-current-code-review.md)、[02 架构与所有权](02-architecture-and-ownership.md)、[03 qmake 最小工程](03-qmake-minimal-project.md)。
- [x] 阶段 1，MonitorEngine：[04 值类型与状态模型](04-value-types-and-state-model.md)、[05 Presence Engine](05-monitor-engine-presence.md)、[06 Light Engine](06-monitor-engine-light.md)、[07 Engine 测试](07-monitor-engine-tests.md)。
- [ ] 阶段 2，AP3216C 全链路：[08 Qt 对象模型](08-qt-object-model.md)、[09 Qt Worker 线程](09-qt-worker-thread.md)、[10 AP3216C backend](10-ap3216c-backend.md)、[11 AP3216C App](11-ap3216c-app.md)。其中 08-09 是后续所有 Service/Worker 的共同基础。
- [ ] 阶段 3，LD2410C 读取链路：[12 LD2410C ABI](12-ld2410c-abi.md)、[13 LD2410C App](13-ld2410c-app.md)。
- [ ] 阶段 4，Camera 预览链路：[14 Camera 能力发现](14-camera-capability.md)、[15 Camera MMAP 预览](15-camera-mmap-preview.md)、[16 Camera 帧转换](16-camera-frame-conversion.md)、[17 Camera controls](17-camera-controls.md)。
- [ ] 阶段 5，MediaWriter、截图和录像：[18 MediaWriter](18-media-writer.md)。
- [ ] 阶段 6，Smart Monitor 闭环：[19 Smart Monitor 集成](19-smart-monitor-integration.md)。
- [ ] 阶段 7，简化 Shell 和资源切换：[20 Page 与 Shell](20-page-and-shell.md)。
- [ ] 阶段 8，LD2410C 少量配置、异常和交付：[21 异常、交付与复盘](21-failure-delivery-review.md)。

## 分步手写任务

1. 阶段 0 先调查旧闭环：只读梳理 presence 输入、Engine decision、Camera capture、MediaWriter 和 UI 的实际路径，记录代码证据、缺口和不可照搬之处。
2. 在目标板逐项调查 AP3216C、LD2410C、Camera、Storage 的真实节点、ABI、输入输出、正常/缺失/失败行为；不凭旧代码推断驱动契约。
3. 形成阶段 0 的四项产物：presence 状态转移表；四类能力的 ABI/输入输出表；旧实现“可参考/必须修改”清单；第一版/后续范围清单。另画 Page 到 backend 的依赖、线程和 owner 图。
4. 在 `src/app/` 内手动替换半成品，先创建最小 Qt Widgets target；使用目标 qmake 做 out-of-source 构建并在板端启动，证明主工具链可用。
5. 手写 `MonitorPolicy`、输入样本、状态和 Decision；每增加一个字段，都写出“谁生产、谁消费、无效时含义”。
6. 只实现 Presence 状态转移并运行断言测试，不 include Qt 或设备头文件；host 可辅助快速运行这类纯 C++ 测试。
7. 加入 lux 双阈值滞回，验证中间区间保持状态、无效样本关闭 Auto torch。
8. 在 Engine 稳定后，按 AP3216C、LD2410C、Camera、MediaWriter、Controller/Page、Shell 的顺序做板端单能力闭环。
9. 每阶段记录最窄板端命令、预期输出、线程/资源所有权和一个失败场景。

## 关键代码片段或伪代码

每次输入都采用同一思考模板：

```cpp
// 伪代码：不要直接扩展成通用事件总线。
Decision onInput(const OwningValue &input, int64_t nowMs)
{
    validate(input, nowMs);
    transitionState();
    return deriveDesiredState();
}
```

每阶段只允许在上一阶段通过后增加一个依赖：

```text
types -> engine -> assert tests -> Qt adapter -> Linux backend -> integrated HMI
```

练习：为“presence 读取失败”分别写出错误设计和正确设计。错误设计把它写成 `false`；正确设计保留 `valid/availability/error/updatedAt`，让业务知道数据未知。

## 检查点

- 能在一分钟内解释 Page、Controller、Engine、Service、Worker、backend 的差别。
- 能指出 GUI、Sensor I/O、Camera Capture、Media Writer 分别在哪个线程。
- 每阶段有可执行结果，且不因后续功能提前引入平台框架。
- Engine 测试不启动 `QApplication`，不需要板卡或真实设备。
- 单设备验证完成前，不接入 Smart Monitor 组合闭环。

## 常见错误

- 从旧项目整类复制，再通过改名制造“新架构”。
- 一开始创建所有目录、基类、接口、Fake 和事件总线，却没有可运行闭环。
- 把设备失败解释成合法业务值 `0` 或 `false`。
- 在 GUI 线程读取 sysfs、执行 ioctl、编码 JPEG 或写文件。
- 为了赶进度跳过测试，直到板端联调才发现状态转移错误。
- 把 SPEC 的后续扩展误当第一版任务，导致范围持续扩大。

## 最窄验证

纯 Engine 等无平台依赖逻辑可先用 host 做辅助快速验证；这不替代 target 构建和板端验收：

```bash
mkdir -p /tmp/monitor-test-build
"${HOST_QMAKE}" -o /tmp/monitor-test-build/Makefile "${SOURCE_DIR}/tests/${TEST_PROJECT}.pro"
make -C /tmp/monitor-test-build -j2
"/tmp/monitor-test-build/${MONITOR_ENGINE_TEST}"
```

形成 Qt 可执行程序后再交叉编译并部署：

```bash
mkdir -p /tmp/app-target-build
"${TARGET_QMAKE}" -o /tmp/app-target-build/Makefile "${SOURCE_DIR}/app.pro"
make -C /tmp/app-target-build -j2
file "/tmp/app-target-build/${APP_BINARY}"
QT_QPA_PLATFORM=linuxfb "${APP_BINARY}"
```

预期：测试进程返回 0；板端 UI 可操作，设备缺失时显示 unavailable 而非伪造数值。

## 完成标准

- 01-21 全部专题按 SPEC 阶段 0-8 完成，并保留每阶段验证记录。
- 能脱离代码讲清 `presence -> decision -> capture -> writer -> UI` 主线。
- 能说明关键 QObject、fd、MMAP buffer、图像副本和媒体文件由谁拥有、在哪个线程释放。
- 第一版范围与 [SPEC](../../SPEC.md) 一致，没有提前引入 lease、request bus、插件或后台 App。

## 复盘问题

1. 为什么纯 Engine 必须先于 Qt Controller 和真实设备完成？
2. 哪些边界是为当前真实问题服务，哪些可能只是过度设计？
3. 当设备输入失效时，系统如何避免把未知状态误判成无人或正常光照？
4. 如果板端失败，本地测试、交叉编译、部署和运行环境中哪一层最先排查？
5. 你能否用三分钟讲清当前完成阶段的数据流、线程和所有权？
