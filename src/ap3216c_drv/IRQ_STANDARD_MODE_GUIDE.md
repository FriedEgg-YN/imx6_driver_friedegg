# AP3216C 标准中断响应与可切换轮询说明（面试导向）

本文档解释本轮改造：在 AP3216C 驱动中实现“标准线程化中断响应模式”，并支持运行时切换到轮询模式，且触发条件统一为“PS 接近或 ALS 变化超阈值”。

## 1. 改造目标

- DTS 补齐 AP3216C 中断资源（interrupt-parent/interrupts/pinctrl）
- 驱动支持运行时切换 `HW_IRQ` 与 `POLL_SIM`
- 触发判定统一：`PS >= ps_th` 或 `|ALS(k)-ALS(k-1)| >= als_delta_th`
- 保留统计与日志口径，方便定位中断线异常下拉问题

## 2. 为什么要这样做

- 仅依赖轮询无法体现完整中断框架能力
- 仅依赖硬件中断在板级异常时调试成本高
- 运行时可切换让你可以快速做 A/B：
  - 同一驱动、同一 App、同一统计口径
  - 切模式后马上观察计数行为差异

## 3. 关键实现点

### 3.1 DTS 侧

在 AP3216C 设备节点增加：

- `interrupt-parent = <&gpio5>`
- `interrupts = <0 8>`（GPIO5_IO00，低电平触发）
- `pinctrl-names` 与 `pinctrl-0`

并新增 `pinctrl_ap3216c`，将中断脚配置为 GPIO 输入并启用上拉。

### 3.2 驱动侧

- 增加 ioctl：
  - `AP3216C_CMD_SET_EVENT_MODE`
  - `AP3216C_CMD_SET_PS_TRIGGER_TH`
  - `AP3216C_CMD_SET_ALS_DELTA_TH`
  - `AP3216C_CMD_SET_POLL_INTERVAL_MS`
- 新增统一判定函数，IRQ 线程与轮询任务复用
- 模式切换时保证时序安全：
  - 切到 `HW_IRQ`：先停轮询，再申请 IRQ
  - 切到 `POLL_SIM`：释放 IRQ，再启动轮询

### 3.3 App 侧

新增 `irqcfg` 模式：

```bash
./ap3216cApp /dev/ap3216c irqcfg <event_mode> <ps_th> <als_delta_th> [poll_interval_ms]
```

结合 `irqdemo` 观察模式与统计：

```bash
./ap3216cApp /dev/ap3216c irqdemo 30
```

## 4. 验证与判读

1. 切到 `POLL_SIM` 后，`poll_sim_events` 应持续增长。
2. 切到 `HW_IRQ` 后，触发传感器事件时 `hw_irq_events` 应增长。
3. 若切到 `HW_IRQ` 失败，先检查 DTS 中断属性是否生效。
4. 若中断脚长期低电平，按“DTS配置 -> GPIO输入上拉 -> 外部上拉电阻 -> 硬件连线”顺序排查。

## 5. 面试表达模板

“我把 AP3216C 的事件处理做成统一入口，中断线程和轮询模拟共用同一触发判定。通过 ioctl 可以在运行时切换 HW_IRQ 与 POLL_SIM，并动态调阈值。这样即便板级中断线有异常，也能先用同一套业务链路做可重复验证，等硬件恢复后无缝切回真实中断。”
