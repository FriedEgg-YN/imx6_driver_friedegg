# MonitorEngine Presence 状态机

## 目标/前置

目标是手写不依赖 Qt 的 Presence 状态机，正确处理 confirm、Cooldown、Stop、invalid 和 stale，并输出 timer 与副作用的期望状态。前置是完成 [值类型与状态模型](04-value-types-and-state-model.md)，先画转移表再写 `if/switch`。

## 最低必懂模型

五个状态表达的是业务阶段，不是设备线程状态：

```text
Disabled -> NoPerson -> PersonPending -> Active -> Cooldown
              ^             |             ^          |
              +-------------+             +----------+
```

- Pending 过滤短脉冲，不能启动 Camera。
- Cooldown 保留 Camera/录像，避免短暂离开切断文件。
- Unknown 不是 absent：Pending 回 NoPerson；Active 进入 fail-safe Cooldown；已在 Cooldown 则保持原 timer。
- timeout 是独立输入，触发时必须再次检查最新 sample 是否仍 fresh 且符合条件。

## 文件或数据流

```text
PresenceSample + nowMs -> MonitorEngine
confirm/cooldown timeout -> MonitorEngine
start/stop -> MonitorEngine
MonitorEngine -> MonitorState + MonitorDecision
```

建议文件：

```text
application/smart_monitor/monitor_engine.{h,cpp}
application/smart_monitor/monitor_types.h
tests/monitor_engine_test.cpp
```

## 分步手写任务

1. 写构造函数和只读 `state()`；构造后必须为 `Disabled`。
2. 写 `start()`：进入 `NoPerson`，取消可能遗留的 confirm/cooldown timer，副作用均不 wanted。
3. 写 `stop()`：从任何状态进入 `Disabled`，取消两个 timer，并让 camera/recording/torch 均不 wanted。
4. 写 sample freshness 判断，把 invalid、未来时间戳和 stale 统一映射为 Unknown 输入语义。
5. 处理 `NoPerson`：fresh present 进入 Pending 并 Start confirm；absent/Unknown 保持。
6. 处理 `PersonPending`：fresh absent 或 Unknown 回 NoPerson 并 Cancel confirm；present 保持且不重复 Start timer。
7. 处理 confirm timeout：只有最新 presence 仍 fresh/present 才进入 Active，否则回 NoPerson；两条路径都 Cancel confirm。
8. 处理 `Active`：fresh absent 或 Unknown 进入 Cooldown 并 Start cooldown；present 保持。
9. 处理 `Cooldown`：fresh present 直接 Active 并 Cancel cooldown；absent/Unknown 保持，不能重启 timer。
10. 处理 cooldown timeout：仍 fresh present 则 Active，否则 NoPerson；Cancel cooldown。
11. 从最终状态统一派生 `cameraWanted` 和 `recordingWanted`，避免每个分支手工赋值漂移。

## 关键代码片段或伪代码

入口函数的结构应把“分类输入”和“状态转移”分开：

```cpp
MonitorDecision MonitorEngine::onPresenceChanged(
    const PresenceSample &sample, std::int64_t nowMs)
{
    latestPresence_ = sample;
    const PresenceInput input = classify(sample, nowMs);

    // 练习：按 current state 完成 switch，只修改业务状态和 timer intent。
    switch (state_.presenceState) {
    case PresenceState::Disabled:
        break;
    case PresenceState::NoPerson:
        // ...
        break;
    default:
        // ...
        break;
    }
    return makeDecision(timerIntent);
}
```

关键转移表：

| 当前状态 | fresh present | fresh absent | Unknown |
| --- | --- | --- | --- |
| NoPerson | Pending + Start confirm | 保持 | 保持 |
| Pending | 保持 | NoPerson + Cancel confirm | NoPerson + Cancel confirm |
| Active | 保持 | Cooldown + Start cooldown | Cooldown + Start cooldown |
| Cooldown | Active + Cancel cooldown | 保持 | 保持原 timer |

练习：补全 Disabled 行、两个 timeout 行以及每行的 camera/recording wanted。

## 检查点

- Pending 期间 camera/recording 始终为 false。
- Cooldown 收到 present 直接 Active，不经过 Pending，录像不中断。
- `Keep` 不会被误写为 `Start`，避免每个 absent sample 延长 Cooldown。
- timeout 使用最新 sample 和当前 `nowMs` 复核，不相信 timer 启动时的旧事实。
- Stop 从五种状态都取消 timer 和副作用。
- Camera/storage 错误不改变 PresenceState。

## 常见错误

- 把 invalid/stale 转换为 `present=false`，错误结束 Active。
- 每次 present sample 都重新 Start confirm，导致永远无法确认。
- Cooldown 每次 absent 都重启 timer，导致永远无法到期。
- Cooldown 返回先进入 Pending，造成录像或 Camera 抖动。
- timeout 回调无条件转移，不复核最新样本。
- 在 Engine 内直接启动 QTimer 或调用 CameraService。

## 最窄验证

只构建 Engine 和断言测试：

```bash
"${CXX}" -std=c++11 -Wall -Wextra -pedantic \
  -I"${SOURCE_DIR}/application/smart_monitor" \
  "${SOURCE_DIR}/application/smart_monitor/monitor_engine.cpp" \
  "${SOURCE_DIR}/tests/monitor_engine_test.cpp" \
  -o /tmp/monitor-engine-test
/tmp/monitor-engine-test
```

最窄场景：Start 后无人、短脉冲、confirm 成功、Active absent、Cooldown 返回、Cooldown 到期、Active 收到 stale、五种状态 Stop。预期全部断言通过且进程返回 0。

## 完成标准

- 转移表的每个单元和 timeout 分支至少有一个断言。
- Engine 不依赖 Qt、设备、文件系统或 wall-clock API；`nowMs` 由调用者传入。
- 所有 Decision 同时描述最终 desired state 和本次 timer intent。
- 能口述为什么 Unknown 在 Pending、Active、Cooldown 中采用不同策略。

## 复盘问题

1. confirm timeout 到达时为什么仍要检查 sample freshness？
2. Active 收到 Unknown 为何进入 fail-safe Cooldown，而不是立即 NoPerson？
3. Cooldown 中 Unknown 为什么不能重启 timer？
4. desired state 从最终状态统一派生解决了什么一致性问题？
5. Controller 收到 cameraWanted=true 但 Camera 打开失败时，PresenceState 是否应变化？
