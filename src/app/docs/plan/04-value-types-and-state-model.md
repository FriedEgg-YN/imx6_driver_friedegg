# 值类型与状态模型

## 目标/前置

目标是先定义 Engine、Controller、Service 和 Page 共同使用的类型化语言，区分未知、不可用、无效、stale 与合法业务值。前置是完成 [qmake 最小工程](03-qmake-minimal-project.md)，并阅读 SPEC 的“类型化状态与异步结果”和“Smart Monitor 最小类型”。

## 最低必懂模型

值类型是一份自包含快照，不借用生产者内部内存。状态模型至少分四类：

- 输入事实：`PresenceSample`、`LuxSample`，包含 value、valid 和更新时间。
- 业务状态：`PresenceState`、`LightState`，由 Engine 持有。
- 期望输出：`MonitorDecision`，描述 wanted 状态和 timer 意图，不代表 I/O 已成功。
- 实际设备状态：Camera/Storage status，由 Service 发布，Controller 用来收敛 desired/actual。

`false` 是有效的“无人”；`0 lux` 是有效读数；它们都不能表示读取失败。stale 是“曾经有效，但对当前决策已过期”。

## 文件或数据流

建议先集中在一个头文件，职责增长后再拆：

```text
monitor_types.h
  |-- policy and enum classes
  |-- input samples
  |-- state
  `-- decision
```

数据流及语义变化：

```text
backend result -> valid timestamped sample -> Engine validation at nowMs
               -> business state -> Decision -> Controller convergence
```

## 分步手写任务

1. 写 `PresenceState`：`Disabled`、`NoPerson`、`PersonPending`、`Active`、`Cooldown`。
2. 写 `LightState`：`Unknown`、`Normal`、`Dark`；不要把数值 lux 和业务光照状态混成一个字段。
3. 写 `TimerCommand`：`Keep`、`Start`、`Cancel`，并为三者写一句可观察语义。
4. 写 `MonitorPolicy`，包含 confirm/cooldown 时长、presence/lux stale 时长和 dark enter/exit 阈值。
5. 写两个 sample，统一使用 `std::int64_t updatedAtMs`；只使用标准库类型以保持 Engine 纯 C++。
6. 写 `MonitorState`，保留业务状态以及输入 availability/最近有效值，但避免重复存储无法保持一致的数据。
7. 写 `MonitorDecision`，区分 camera/recording/torch wanted 与两个 timer 命令。
8. 为策略写有效性约束：时长非负，`darkEnterLux < darkExitLux`；决定由构造时拒绝还是测试前置保证。
9. 后续跨 queued signal 时，在 Qt adapter 层注册类型；不要因此让 pure Engine include QObject。

## 关键代码片段或伪代码

输入样本的最小形状：

```cpp
struct PresenceSample {
    bool valid = false;
    bool present = false;
    std::int64_t updatedAtMs = 0;
};

bool isFresh(const PresenceSample &sample,
             std::int64_t nowMs,
             std::int64_t staleMs);
```

有效性判断要明确处理未来时间和边界：

```text
fresh = valid
     && updatedAtMs <= nowMs
     && nowMs - updatedAtMs <= staleMs
```

Decision 只表达意图：

```cpp
struct MonitorDecision {
    bool cameraWanted = false;
    bool recordingWanted = false;
    bool torchWanted = false;
    TimerCommand confirmTimer = TimerCommand::Keep;
    TimerCommand cooldownTimer = TimerCommand::Keep;
};
```

练习：构造“有效无人”“有效 0 lux”“presence 过期”“lux 从未有效”四个值，并解释它们为何不能压缩成两个 bool。

## 检查点

- 类型头文件不 include Qt、Widget、Linux 或 backend 头文件。
- enum 和字段命名与 SPEC 一致，没有程序分支依赖字符串。
- Decision 的 wanted 与设备 actual status 明确分开。
- stale 比较的等号边界已经写入测试预期。
- 输入值跨线程时拥有自身数据，不含裸 buffer 指针。

## 常见错误

- 用 `-1`、空字符串或零值同时表示错误和真实值。
- 在 sample 内直接放 UI 展示文案，导致 backend 与 Page 耦合。
- 每个层级重新定义相似 enum，靠强制转换连接。
- 在 Engine 类型中使用 `QString`，使纯 C++ host 测试被迫链接 Qt。
- Decision 只包含动作脉冲，例如 `startRecording`，无法在失败后重新收敛。
- 没有规定 `nowMs < updatedAtMs`，无符号减法造成错误 fresh。

## 最窄验证

先只编译一个值类型测试：

```bash
"${CXX}" -std=c++11 -Wall -Wextra -pedantic \
  -I"${SOURCE_DIR}/application/smart_monitor" \
  "${SOURCE_DIR}/tests/monitor_types_test.cpp" \
  -o /tmp/monitor-types-test
/tmp/monitor-types-test
```

至少断言：默认状态、有效 0 值、恰好等于 stale 阈值、超过阈值、未来时间戳。预期进程返回 0 且无编译警告。

## 完成标准

- 所有 Presence/Light 测试所需类型已定义，且仅依赖 C++11 标准库。
- 能逐字段说明 producer、consumer、单位、默认值和无效语义。
- `MonitorDecision` 足以表达两类 timer 和三类副作用的期望状态。
- 没有引入通用事件总线、Device 基类或 request ID。

## 复盘问题

1. availability、validity 和 freshness 分别回答什么问题？
2. 为什么 Decision 表达 desired state 比表达一次性字符串动作更容易恢复失败？
3. `TimerCommand::Keep` 与 `Start` 有何区别，重复 Start 会造成什么行为？
4. 哪些类型需要跨线程，哪些只应留在 GUI/Engine 内部？
5. 策略非法时应在何处拒绝，理由是什么？
