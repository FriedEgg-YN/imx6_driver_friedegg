# MonitorEngine 纯 C++ 断言测试

## 目标/前置

目标是用 C++11 `assert` 建立可在主机快速运行的状态机回归测试，不启动 QApplication、不连接设备。前置是完成 [值类型](04-value-types-and-state-model.md)、[Presence Engine](05-monitor-engine-presence.md)和[Light Engine](06-monitor-engine-light.md)。

## 最低必懂模型

每个测试遵循 Arrange、Act、Assert：构造策略和 Engine，提交一个最短输入序列，同时断言内部业务状态与外部 Decision。测试的 oracle 是 [SPEC](../../SPEC.md) 和手写状态转移表，不是当前实现。

`assert` 适合第一阶段，因为零框架、交叉工具链负担小；它的限制是失败信息弱，且定义 `NDEBUG` 后断言会被移除。因此测试构建不得开启 `NDEBUG`，用小函数名和分步断言定位失败。

## 文件或数据流

```text
tests/monitor_engine_test.cpp
  -> monitor_types.h
  -> monitor_engine.h/.cpp
  -> process exit 0 or assertion failure
```

测试数据流：

```text
fixed nowMs + explicit sample -> Engine input
                              -> state() + Decision -> assert
```

不调用系统时钟，避免测试等待真实 confirm/cooldown 时间。

## 分步手写任务

1. 写 `main()`，依次调用具名测试函数，最后返回 0。
2. 写少量样本工厂函数，例如 `presentAt(ms)`、`absentAt(ms)`、`invalidPresenceAt(ms)`；不要建立通用测试 DSL。
3. 先测默认状态和 Start：Disabled -> NoPerson，所有副作用 false，timer 意图明确。
4. 测短脉冲：present 进入 Pending/Start confirm，随后 absent 回 NoPerson/Cancel confirm。
5. 测 confirm：timeout 前保持 Pending；timeout 且样本 fresh/present 后进入 Active。
6. 测 Cooldown：Active absent 后 Start cooldown；再次 absent 只能 Keep；present 直接 Active/Cancel。
7. 测 cooldown 到期：仍 absent 或 Unknown 后到 NoPerson；重新 present 时按规则处理。
8. 对 Disabled、NoPerson、Pending、Active、Cooldown 分别调用 Stop。
9. 测 invalid、恰好 fresh 边界、超过 stale 一毫秒和未来时间戳。
10. 测 lux enter/exit 等号、中间区间保持、invalid 后 Unknown、恢复。
11. 测组合：Dark + Pending 不开 torch；Dark + Active 开；Dark + Cooldown 保持；Stop 后关。
12. 每修复一个状态机 bug，先添加能失败的最小断言，再修改 Engine。

## 关键代码片段或伪代码

单个测试只表达一条规则：

```cpp
void pending_is_cancelled_by_absence()
{
    MonitorEngine engine(testPolicy());
    engine.start();

    MonitorDecision decision = engine.onPresenceChanged(presentAt(100), 100);
    assert(engine.state().presenceState == PresenceState::PersonPending);
    assert(decision.confirmTimer == TimerCommand::Start);

    decision = engine.onPresenceChanged(absentAt(150), 150);
    assert(engine.state().presenceState == PresenceState::NoPerson);
    assert(decision.confirmTimer == TimerCommand::Cancel);
    assert(!decision.cameraWanted);
}
```

测试列表可直接作为 `main()` 清单：

```cpp
int main()
{
    start_enters_no_person();
    pending_is_cancelled_by_absence();
    cooldown_return_does_not_restart_recording();
    stale_presence_is_unknown();
    light_thresholds_are_hysteretic();
    stop_clears_every_state();
    return 0;
}
```

练习：不要复制上述实现，手写 `cooldown_return_does_not_restart_recording()`，同时断言 PresenceState、cooldown timer、cameraWanted 和 recordingWanted。

## 检查点

- 测试 binary 的链接依赖不含 Qt、设备库或 GUI platform plugin。
- 时间全部显式传入，无 `sleep()`、真实 timer 或 wall clock。
- 每个测试从新 Engine 开始，测试间无共享可变状态。
- 同时断言状态和 Decision，避免内部正确但输出错误。
- `Keep/Start/Cancel` 三种 timer 意图都有覆盖。
- 编译命令没有 `-DNDEBUG`。

## 常见错误

- 只测最终状态，不测 timer 和 wanted 输出。
- 一个测试覆盖十几个规则，失败后无法定位。
- 用真实等待模拟 confirm/cooldown，导致测试慢且不稳定。
- 从 Engine 当前返回值反推预期，形成自证测试。
- helper 过度封装，把关键输入时间和状态隐藏起来。
- Release 配置定义 `NDEBUG`，测试看似全部通过但断言未执行。

## 最窄验证

直接用 host C++ 编译器验证：

```bash
"${CXX}" -std=c++11 -Wall -Wextra -Werror -pedantic \
  -I"${SOURCE_DIR}/application/smart_monitor" \
  "${SOURCE_DIR}/application/smart_monitor/monitor_engine.cpp" \
  "${SOURCE_DIR}/tests/monitor_engine_test.cpp" \
  -o /tmp/monitor-engine-test
/tmp/monitor-engine-test
```

再做一次负向验证：临时改变一个测试期望或输入，确认进程确实因 assertion 非零退出，然后恢复测试。交叉编译不是纯 Engine 正确性的前置；需要验证目标工具链时可额外编译 ARM binary，但无需在每次逻辑迭代部署板卡。

## 完成标准

- SPEC 阶段 1 列出的 Start、短 presence、Pending absent、confirm、Active/Cooldown、返回、到期、Stop、lux 滞回、invalid/stale 均有测试。
- 测试在普通主机上秒级运行且退出码为 0。
- 任意关键转移被故意破坏时，至少一个断言失败。
- 测试代码仍能让读者直接看出输入序列和业务规则。

## 复盘问题

1. 为什么测试既要观察 State 又要观察 Decision？
2. 显式 `nowMs` 如何替代真实 QTimer，并提高可重复性？
3. 哪些边界最容易出现 off-by-one，如何用一毫秒差构造测试？
4. `assert` 方案何时不再够用，应升级到 Qt Test 或其他框架？
5. 哪个测试最能证明 Cooldown 返回时录像文件不应中断？
