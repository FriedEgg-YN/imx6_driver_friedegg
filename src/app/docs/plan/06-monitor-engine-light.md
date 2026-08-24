# MonitorEngine Light 与 Auto Torch

## 目标/前置

目标是在 Presence 状态机稳定后加入 lux 有效性、双阈值滞回和 Auto torch 决策，同时保持业务规则与 Camera capability/actual status 分层。前置是完成 [Presence 状态机](05-monitor-engine-presence.md)及其测试。

## 最低必懂模型

双阈值避免光照在单一阈值附近波动时 torch 频繁切换：

```text
lux <= enter -> Dark
lux >= exit  -> Normal
enter < lux < exit -> 保持原 LightState
invalid/stale -> Unknown
```

Engine 可以决定业务上是否“希望开灯”，但只有 Controller 同时观察到 Camera Streaming、支持 torch 且 owner 正确时，才能提交实际 control。第一版 Auto torch 的必要条件包括 monitoring、Active/Cooldown、fresh lux 和 Dark。

## 文件或数据流

```text
Ap3216cSample -> Controller -> LuxSample + nowMs -> MonitorEngine
PresenceState ------------------------------------+
                                                   v
                                         torchWanted
                                                   |
CameraStatus/capability -> Controller convergence -+-> CameraService
```

Engine 不接触 V4L2 control；Camera 的实际 torch status 通过 typed status 回到 Controller/Page。

## 分步手写任务

1. 在 Policy 中确认 `darkEnterLux < darkExitLux`，为非法策略写失败测试。
2. 实现 lux freshness 分类；invalid、stale、未来时间戳进入 Unknown。
3. 从 `LightState::Unknown` 收到 fresh lux：小于等于 enter 进入 Dark，大于 enter 时进入 Normal，以获得确定初始状态。
4. 从 Normal 收到 `lux <= enter` 才进入 Dark；中间区间保持 Normal。
5. 从 Dark 收到 `lux >= exit` 才进入 Normal；中间区间保持 Dark。
6. 收到 invalid/stale 时进入 Unknown，并保证 Auto torch wanted=false。
7. 从 PresenceState 派生 monitoring/active-window，再与 LightState 组合得到 Engine 层的 torch wanted。
8. 在 Controller 练习表中加入 Camera actual/capability 条件，但先不要在 Engine 中实现设备调用。
9. 分别测试阈值等号、区间内波动、无效后恢复、非 Active 状态下 Dark。

## 关键代码片段或伪代码

滞回伪代码：

```cpp
switch (state_.lightState) {
case LightState::Unknown:
    state_.lightState = lux <= policy_.darkEnterLux
        ? LightState::Dark : LightState::Normal;
    break;
case LightState::Normal:
    if (lux <= policy_.darkEnterLux)
        state_.lightState = LightState::Dark;
    break;
case LightState::Dark:
    if (lux >= policy_.darkExitLux)
        state_.lightState = LightState::Normal;
    break;
}
```

Auto torch 分两级收敛：

```text
Engine desired = enabled && activeWindow && freshLux && Dark
Controller executable = desired && cameraStreaming && torchSupported && ownerMatches
```

练习：给出 lux 序列 `50, 19, 25, 39, 40, invalid, 10`，手工写出每一步 LightState 和 torchWanted，再用测试验证。

## 检查点

- enter 和 exit 的等号方向与 SPEC 一致。
- 中间区间保持旧状态，而不是重新按平均阈值判断。
- invalid/stale 后 LightState 为 Unknown，Auto torch 关闭且 UI 可显示原因。
- NoPerson/Pending/Disabled 即使 Dark 也不请求 torch。
- Cooldown 保持与 Active 相同的 Auto torch 资格。
- Engine 不知道 Camera control ID、ioctl 或 capability 结构。

## 常见错误

- 用一个阈值判断开关，板端环境噪声导致灯闪烁。
- 在 Unknown 收到中间区间时继续保持 Unknown，导致有有效 lux 却永远没有确定状态。
- lux 失败时继续沿用 Dark 并保持 torch，违反 fail-safe 规则。
- 把 Camera Streaming/support 条件放进纯 LightState 转移，混淆业务与设备实际状态。
- 手动 Torch 隐式启动 Camera；第一版不允许这种隐藏所有权变化。
- 忽略浮点阈值等号，边界测试与实现不一致。

## 最窄验证

复用同一个纯 C++ Engine test binary，只运行/观察 light 相关用例：

```bash
"${CXX}" -std=c++11 -Wall -Wextra -pedantic \
  -I"${SOURCE_DIR}/application/smart_monitor" \
  "${SOURCE_DIR}/application/smart_monitor/monitor_engine.cpp" \
  "${SOURCE_DIR}/tests/monitor_engine_test.cpp" \
  -o /tmp/monitor-engine-test
/tmp/monitor-engine-test
```

板端集成后仅验证阈值附近序列：

```bash
QT_QPA_PLATFORM=linuxfb "${APP_BINARY}"
```

预期：低于 enter 开启资格，高于 exit 关闭资格，中间波动不反复切换；AP3216C 不可用时 UI 显示 Unknown 且 Auto torch off。

## 完成标准

- LightState 的 Unknown/Normal/Dark 转移和所有阈值边界有断言。
- Presence 与 Light 的组合决策覆盖 Disabled、Pending、Active、Cooldown。
- 能清楚区分 `torchWanted`、Camera 是否支持、Camera 是否 Streaming 和实际 torch 状态。
- lux 无效不会被解释成 0 lux 或继续使用过期值。

## 复盘问题

1. 双阈值如何减少输出抖动，代价是什么？
2. Unknown 初次收到中间区间值时为何应得到确定状态？
3. Engine 和 Controller 各自掌握哪些 torch 条件？
4. Cooldown 为什么仍允许 Auto torch？
5. 若未来增加 ManualOn/ManualOff，哪些规则属于 Engine，哪些仍属于 Camera owner？
