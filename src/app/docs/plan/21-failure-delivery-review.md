# 21. 阶段 8：异常、交付与全链路复盘

> 对应 [SPEC 阶段 8](../../SPEC.md#阶段-8ld2410c-少量配置异常和交付)及 [Buildroot 和交付策略](../../SPEC.md#16-buildroot-和交付策略)。如本文与规格冲突，以 `SPEC.md` 为准。

## 目标与前置

### 目标

- 只迁移少量已经板端验证、能够可靠回读的 LD2410C 配置。
- 用异常矩阵验收设备缺失、I/O 失败、storage 不可写、资源竞争和退出写入等场景。
- 实现普通切页异步停止和进程退出有界等待，不用析构函数无限阻塞。
- 在闭环通过后再接入 Buildroot local package，不假定现有包名、binary 名或 package 目录已经存在。
- 形成可部署产物、板端演示步骤、证据记录、交付清单和三分钟总口述。

### 前置

- 阶段 1-7 已完成，单设备 App、Smart Monitor 主闭环和 Shell 切换均可板端运行。
- 已有类型化 availability/status/result，错误不会伪装成正常值。
- Worker、fd、MMAP、Writer queue、Page/Controller owner 和线程归属均明确。
- 已阅读 [测试与验收](../../SPEC.md#15-测试与验收)和[完成定义](../../SPEC.md#19-完成定义)。

## 最低必懂模型

阶段 8 不是继续堆功能，而是把“能跑一次”收紧为“可解释地失败、可重复退出、可复现交付”。工作分成四条线：

| 工作线 | 核心问题 | 结束条件 |
| --- | --- | --- |
| 少量配置 | 写入是否真的生效 | 写后逐项回读，只更新成功字段 |
| 异常矩阵 | 故障是否被错误解释 | unavailable/error 与业务值分离 |
| 有界退出 | 资源能否按期限释放 | stop 顺序明确，超时可记录并结束 |
| Buildroot 交付 | 别人能否复现构建运行 | package、binary、依赖和命令与实际一致 |

异常处理的最低原则：

```text
错误发生
  -> 保留错误所属层级和 operation
  -> 发布 typed unavailable/error
  -> 业务状态只按自己的规则变化
  -> 相关能力降级，其他链路继续
  -> 可重试则有限重试
  -> 退出时停止接收新工作并有界清理
```

## 数据与控制流

### 配置写入与回读

```text
Ld2410cPage intent
  -> Controller validates input
  -> SensorService queued command
  -> SensorWorker / UART backend write one verified field
  -> backend read back same field
  -> typed ConfigResult(requested, applied, code, error)
  -> Controller updates only confirmed fields
  -> ViewState -> Page
```

### 进程退出

```text
QApplication aboutToQuit / Shell exit
  -> stop accepting page operations
  -> deactivate foreground Controller
  -> torch off and await Off
  -> stop recording and drain/close Writer within deadline
  -> await Recording Stopped, then STREAMOFF Camera
  -> stop sensor polling / UART work
  -> request worker event loops quit
  -> bounded wait per thread
  -> log timeout and final status
  -> process returns
```

### 交付链路

```text
src/app qmake project
  -> candidate Buildroot local package
  -> cross compile against target Qt5
  -> install actual binary into target rootfs
  -> board linuxfb launch
  -> execute normal + failure acceptance matrix
  -> archive commands, versions and results
```

## 线程和生命周期

- LD2410C UART/misc 配置仍在 Sensor I/O 线程执行，GUI 只提交命令和接收结果。
- 同一设备的采样与配置需要串行化；第一版可让同一 Worker event loop 顺序执行，避免 GUI 线程加锁等待。
- 配置命令必须有 pending 状态；pending 时禁用同字段重复写，最终结果由 Worker signal 返回。
- 同步提交结果 `Accepted` 只表示请求被接受；写后回读一致的异步 completion 才是 `Succeeded`。失败使用 `IoError`、`InvalidArgument`、`Timeout` 等具体错误码，不定义泛称 `Failed`。
- 普通切页继续使用异步 deactivate，不等待 QThread。
- 进程退出可以等待线程，但每一步必须有 deadline；任何 `wait()` 都提供有限 timeout 并记录超时对象。
- MediaWriter 停止前拒绝新帧，处理策略必须明确：在 deadline 内 drain 已接收帧，或丢弃剩余帧并报告 dropped；无论哪种都要关闭文件并发布结果。
- QObject 应在其 affinity 线程仍运行时执行清理/`deleteLater()`；先停 event loop 再依赖 queued cleanup 会丢失清理调用。

## 分步任务

### 任务 1：冻结第一版配置白名单

先从实际驱动/UART 能力和板端证据列出候选字段，每个字段必须满足：

- 有清楚的单位、范围和编码；
- 写命令已在目标板验证；
- 有可靠读回路径；
- 写失败不会破坏主 presence 读取闭环；
- 确实服务于当前学习或演示。

不满足任一条件的高级配置、工程模式、恢复出厂和波特率切换不迁移。

**练习**：建立配置白名单表，包含 field、范围、写 ABI、读 ABI、超时、失败码和板端证据；不能回读的字段标为 out of scope。

### 任务 2：实现单字段写后回读

流程必须是“校验 -> 写一个字段 -> 回读同字段 -> 比较 -> 只提交确认值”。不要把整份 UI 配置对象一次写回，也不要在部分失败后把 requested 值显示成 actual。

**练习**：注入第二个字段失败，确认第一个成功字段保留、失败字段仍显示旧 actual，并单独展示错误。

### 任务 3：建立异常矩阵

至少覆盖本篇“异常验收”中的场景。对每一项记录：

- 注入方法；
- 预期 typed state/result；
- 哪些功能应停止；
- 哪些功能必须继续；
- 是否自动重试及频率；
- 恢复后的预期；
- 日志和 UI 证据。

**练习**：先写 expected，再注入故障，避免根据实际错误临时修改验收标准。

### 任务 4：实现退出协调

建议由 CompositionRoot 或独立的窄 shutdown coordinator 管理进程级顺序，不把全局退出塞进 Page 析构。

1. 设置 exiting，Shell 拒绝创建新页面。
2. 请求当前 Controller deactivate。
3. 请求 Camera torch off，确认 Off 后继续。
4. 请求 Writer 停止接收并关闭录像，确认 Recording Stopped 后继续。
5. 请求 Camera STREAMOFF、unmap、close。
6. 请求 SensorWorker 停止 timer 和设备操作。
7. 各 Worker 发 stopped 后退出线程 event loop。
8. 对未完成线程执行有限 `wait(timeoutMs)`，记录 timeout。

**练习**：分别在 Idle、Camera Streaming、Recording、配置 pending 时退出，记录每个 completion 的顺序和耗时。

### 任务 5：为阻塞点设置预算

列出所有可能拖慢退出的点：`select()`、UART read、sysfs/ioctl、JPEG 编码、存储 flush/close、Writer drain。通过有限 poll timeout、可取消标志或关闭设备唤醒阻塞；不要依赖无限等待最终返回。

**练习**：人为让 storage 变慢，确认 GUI 先进入 exiting 状态，进程在总 deadline 内结束并留下 dropped/timeout 记录。

### 任务 6：执行本地最小测试

至少重新运行：

- MonitorEngine 全部状态转移；
- stale 判断；
- RGB565 行复制/转换；
- JPEG marker 裁剪；
- 媒体路径生成。

具体测试 binary 以实际 qmake 工程生成结果为准，不在文档中虚构命令。

### 任务 7：判断 Buildroot 接入时机

只有以下条件全部满足才创建或切换 package：

- 独立交叉编译通过；
- 单设备页面可运行；
- Smart Monitor 与 Shell 主闭环通过；
- 资源切换和退出通过；
- binary 名、Qt 模块和运行参数已稳定；
- 不再需要通过频繁改 package 掩盖应用自身问题。

在此之前使用手动部署 candidate binary。不要假定 `<package>` 已存在，也不要提前替换旧演示 package。

### 任务 8：接入 Buildroot local package

接入时再根据仓库实际模式确定：

- 新建 package 还是切换已有 package；
- package symbol、目录名和 `build_and_deploy.sh drv` 参数；
- qmake 工程入口；
- Qt5 Widgets、GUI 等真实依赖；
- target 安装路径和实际 binary 名；
- 是否保留旧程序并存。

完成后才把占位命令替换成真实值，并同步对应 README；本篇不预设名称。

### 任务 9：执行板端总验收并整理交付证据

先单 App，再 Smart Monitor，再页面切换，最后异常和退出。保存脱敏后的命令、预期、实际结果和关键日志，不记录真实板卡 IP、主机、用户名或私有路径。

## 关键接口与不变量

同步请求与异步结果分开建模；配置结果同时保留 requested 与 confirmed actual：

```cpp
struct Ld2410ConfigResult {
    OperationCode code;
    Ld2410Field field;
    int requestedValue = 0;
    int appliedValue = 0;
    bool readBackValid = false;
    QString error;
};
```

Worker 接口与结果不变量：

```cpp
OperationCode SensorService::setLd2410Field(Ld2410Field field, int value);
void SensorWorker::setLd2410Field(Ld2410Field field, int value);
```

```text
Service 同步接受请求 -> Accepted
Worker 写入并回读一致 -> Succeeded
写入或回读失败       -> IoError
回读成功但值不一致   -> IoError（readBackValid=true，保留实际值）
```

**TODO 练习**：实现单字段写后回读，保证只有一致回读产生 `Succeeded`，并为每条失败路径选择具体错误码。

有界等待接口必须显式处理 timeout：

```cpp
bool stopThread(QThread *thread, int timeoutMs, const char *name);
```

不变量：先 queued 请求 Worker 清理 fd、MMAP 和文件并等待 completion，再退出 event loop；`wait(timeoutMs)` 只作最后兜底，超时必须记录，不能用 `terminate()` 代替资源清理。

## 检查点

- 配置字段有明确白名单、范围和回读证据。
- 配置在 Sensor I/O 线程执行，GUI 不同步等待 UART/ioctl。
- UI 只把 `readBackValid=true` 的值更新为 actual。
- 写后回读一致的最终结果为 `Succeeded`；`Accepted` 不更新 actual，失败使用具体错误码。
- 每个故障都映射为 typed unavailable/error，不伪装成 false/0。
- retry 有频率和上限，退出会取消 retry。
- Writer queue 有界，退出时不再接收新帧。
- 普通切页完全异步；进程退出的每个 wait 都有 timeout。
- Buildroot 接入发生在闭环稳定之后，package/binary 名来自实际实现。
- package 依赖、安装路径和板端命令经 clean build 或等价窄验证确认。
- 交付材料不含真实主机、用户、板卡 IP、token 或私有绝对路径。

## 常见错误

- 把 requested 配置立即显示为 actual，不等回读。
- 一次写整份 LD2410C 配置，部分失败后无法判断生效字段。
- 为迁移“完整能力”加入尚未验证的工程模式和恢复出厂。
- presence read error 继续发布 `present=false`，触发错误 Cooldown/停止。
- storage 不可写导致整个 monitoring 停止。
- Camera open 失败进行无间隔无限重试。
- 在 QObject 析构中无界 `thread->wait()`。
- 先 `quit()` Worker event loop，再投递 queued stop/cleanup。
- Writer 尚未关闭文件就宣告 deactivated。
- 使用 `QThread::terminate()` 作为正常退出路径。
- 闭环未稳定就替换旧 Buildroot package，导致应用问题和集成问题混在一起。
- 文档先写死一个不存在的 package 或 binary 名。

## 最窄板端验证

### 接入前

手动部署实际 candidate binary 后：

```bash
QT_QPA_PLATFORM=linuxfb "${APP_BINARY}"
```

最窄验证顺序：

1. LD2410C Page 修改一个白名单字段，写后回读一致。
2. Smart Monitor 完成一次 NoPerson -> Pending -> Active -> Cooldown -> NoPerson。
3. Active 中截图并生成一段可关闭的 MJPEG 文件。
4. 录像中切到 Camera App，确认异步释放后可重新打开 Camera。
5. 在 Recording 和配置 pending 时分别退出，确认在 deadline 内结束。

### 接入后

只有实际 package 参数确定后执行：

```bash
bash buildscripts/build_and_deploy.sh drv "${PACKAGE}"
QT_QPA_PLATFORM=linuxfb "${APP_BINARY}"
```

预期：Buildroot 使用 `src/app/` 的 qmake 工程完成交叉编译并安装实际 binary；板端无需依赖开发机私有路径即可启动。

## 异常验收

| 故障注入 | 预期状态/行为 | 不应发生 |
| --- | --- | --- |
| AP3216C 缺失或属性读取失败 | lux unavailable，Auto torch off；presence/Camera 主链可继续 | 把 lux 显示为 0 且当作暗光 |
| LD2410C 缺失 | presence unavailable；Active 按 fail-safe Cooldown；UI 可 Stop | 直接发布无人或 GUI 卡死 |
| LD2410C 配置写失败 | 保留旧 actual，返回字段级 IoError | 整份配置显示为已应用 |
| LD2410C 写成功但回读不一致 | `readBackValid=true` 且 mismatch error，显示实际回读值 | 强行显示 requested 值 |
| Camera open/ioctl 失败 | Camera Error，有限 retry，其他传感器状态继续 | 高频无限 retry 或 UI 阻塞 |
| Camera 采集中设备异常 | 停止/清理当前 stream，发布 Error，可 Stop/切页 | 继续使用失效 MMAP 指针 |
| storage 启动时不可写 | monitoring 和预览继续，不启动录像 | Engine 被 stop |
| 录像中写失败/空间耗尽 | Writer 停止并关闭文件，发布 IoError/dropped；预览继续 | Capture 线程阻塞写盘 |
| Writer 队列满 | 丢帧并累计 dropped，队列不无限增长 | 内存持续增长 |
| 录像中切页 | Shell busy，等待 Writer/Camera idle 后销毁 | 两个 owner 同时 open Camera |
| 录像中退出进程 | 拒绝新帧、关闭文件、STREAMOFF；有界等待 | 无限 hang |
| Worker 超过退出 deadline | 记录具体线程和 timeout，进程按既定失败策略结束 | 静默无限等待 |
| 旧 owner 晚到结果 | 不更新新页面、不控制新 session | 串改当前资源状态 |
| 故障恢复 | 新样本/状态明确恢复 Available，再按 desired 收敛 | 继续永久使用旧 error 或瞬间命令风暴 |

## 交付清单

- [ ] `src/app/SPEC.md` 中的第一版范围与实际功能一致。
- [ ] 实际 qmake 工程能使用目标 Buildroot Qt5/C++11 toolchain 编译。
- [ ] Buildroot package 名、symbol、source path、依赖和安装步骤来自实际文件，不是占位假设。
- [ ] 实际 binary 名和 target 安装路径已确认。
- [ ] `bash buildscripts/build_and_deploy.sh drv <package>` 已用真实参数验证。
- [ ] `QT_QPA_PLATFORM=linuxfb <app-binary>` 已在板端验证。
- [ ] AP3216C、LD2410C、Camera、Storage 的 ABI/输入输出表齐全。
- [ ] MonitorEngine 本地最小测试通过。
- [ ] 单设备正常、缺失、I/O 失败和恢复均有记录。
- [ ] Smart Monitor 正常状态矩阵通过。
- [ ] storage 不可写、Camera 失败和 Writer 队列满等异常矩阵通过。
- [ ] Camera App 与 Smart Monitor 往返切换无重复 open。
- [ ] Idle、Streaming、Recording、配置 pending 四种退出场景有界完成。
- [ ] 录像文件关闭结果、dropped 计数和失败日志可解释。
- [ ] 页面销毁后不再处理结果，OwnerId 行为有证据。
- [ ] 板端演示步骤、预期输出和恢复步骤清楚。
- [ ] 文档和日志样例已脱敏，使用 `<board-ip>`、`<user>` 等占位符。
- [ ] 旧演示程序保留或替换的决定及原因已记录。

## 完成标准

- 只迁移已验证可回读的少量 LD2410C 配置，部分失败不会污染其他字段。
- 异常矩阵逐项通过，设备 unavailable、业务 false/0 和 stale 语义没有混淆。
- storage 不可写不影响 Smart Monitor 状态机和 Camera 预览。
- 切页异步释放，进程退出有界；不存在析构无界等待和正常路径 `terminate()`。
- Buildroot 接入基于实际 package/binary，能从构建脚本到板端 linuxfb 启动形成闭环。
- 交付清单中的命令、依赖、文件名和验收结果可由另一位开发者复现。
- 能脱离代码讲清驱动 ABI、跨线程值、状态机、V4L2 buffer、Writer、Page/Shell 和交付路径。

## 复盘

### 总复盘

### 架构理解

1. 从 AP3216C/LD2410C 驱动 ABI 到 Page，中间每层的数据类型和线程是什么？
2. 为什么 Engine 只产生 desired state，而 Controller 维护 actual/pending？
3. V4L2 buffer 在哪个线程拥有，何时复制，为什么 `QBUF` 后不能继续引用？
4. 为什么 Preview、Snapshot、Recording 可以共享采集源，却不能都在 Capture 线程处理？
5. Page、Controller、Service facade、Worker/backend 和 Shell 各自不能做什么？

### 技术权衡

1. 为什么第一版共用一个 Sensor I/O 线程，什么实测信号会促使拆线程？
2. 为什么使用单前台 + OwnerId，而未实现 lease token 和抢占？
3. 为什么 request ID、Fake backend、AppCatalog 和后台 session 暂缓？
4. 为什么 MJPEG 足以验证第一版写入链路，而未引入 MP4/H.264？
5. 为什么 Buildroot 接入晚于主闭环，而不是项目开始时就替换旧包？

### 故障与证据

1. 哪个故障最容易把 unavailable 错当业务值？代码如何阻止？
2. 哪个退出阻塞点耗时最长，最终 timeout 如何确定？
3. storage 失败时哪些状态继续、哪些能力降级，日志如何证明？
4. 配置写后回读不一致时，UI、actual 和错误分别是什么？
5. 哪条板端证据能证明同一时刻只有一个 Camera owner？

### 后续边界

只有真实出现多长期 Controller、同类并发请求、后台监控、第二 backend 或 App 数量显著增长时，才分别考虑 lease、request ID、session、窄 interface/Fake 或 AppCatalog。复盘应记录触发条件，而不是把这些机制补进第一版。

## 三分钟面试口述

> 这个项目从 i.MX6ULL 驱动 ABI 做到 Qt5 Widgets HMI。SensorWorker 发布带 validity、timestamp 和 error 的值类型；CameraWorker 管理 V4L2 buffer 生命周期；预览、截图和有界 MediaWriter 队列避免 GUI 与采集线程被慢存储阻塞。Engine 只输出 desired，Controller 对照 actual/pending 异步收敛。storage 不可写只禁用录像。Page 只发意图和 render ViewState，Shell 用单前台、OwnerId 和异步 deactivate 管理资源。停止严格按 torch off、Writer/recording stop、Camera stop，进程退出再停止 Sensor 并对线程有界等待。交付阶段只迁移写后回读可确认的 LD2410C 配置，最终成功返回 `Succeeded`，故障返回具体错误码；闭环稳定后才按实际名称接入 Buildroot local package。
