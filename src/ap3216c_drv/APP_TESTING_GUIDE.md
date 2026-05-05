# AP3216C App 测试使用总览

本文档统一收敛本项目所有 App 测试方法，避免各功能文档重复维护命令示例。

## 1. App 模式总览

当前 `ap3216cApp` 支持以下模式：

- `read`：默认连续读取 IR/ALS/PS 数据
- `atomic2`：父子双进程并发 open，验证独占打开原子策略
- `ioctldemo`：通过 ioctl 设置模式与采样率，再读取验证
- `lockrace`：双线程并发 read + ioctl，验证锁策略正确性
- `lockstress`：高频循环 ioctl + read 压测稳定性
- `irqdemo`：读取事件模式与统计，演示中断框架链路
- `irqcfg`：运行时切换事件模式并配置 PS/ALS 阈值与轮询周期

统一命令格式：

```bash
./ap3216cApp /dev/ap3216c [mode] [args...]
```

新增模式参数：

- `lockrace [loops]`：`loops` 默认 200
- `lockstress [loops]`：`loops` 默认 200
- `irqdemo [loops]`：`loops` 默认 200
- `irqcfg <event_mode> <ps_th> <als_delta_th> [poll_interval_ms]`

## 2. read 模式

用途：

- 验证基础读链路是否通畅
- 观察实时传感器数值变化

命令：

```bash
./ap3216cApp /dev/ap3216c
./ap3216cApp /dev/ap3216c read
```

预期：

- 持续输出 `ir, als, ps`
- 若设备被占用，提示 `device busy`

## 3. atomic2 模式

用途：

- 验证驱动独占打开策略是否生效
- 验证 `-EBUSY` 行为是否符合预期

命令：

```bash
./ap3216cApp /dev/ap3216c atomic2 20
```

参数说明：

- `20` 为测试轮数（正整数）

预期：

- 每轮恰好一个进程 open 成功
- 汇总输出 `pass/fail`，理想状态 `fail=0`

## 4. ioctldemo 模式

用途：

- 验证 ioctl 配置接口（模式切换、ALS/PS 采样率）
- 验证“配置后读取”链路

命令：

```bash
./ap3216cApp /dev/ap3216c ioctldemo <mode> <als_rate> <ps_rate>
```

示例：

```bash
./ap3216cApp /dev/ap3216c ioctldemo 3 2 1
```

参数范围与意义：

- `mode`：`0~3`
- `0` 关断，`1` 仅 ALS，`2` 仅 PS+IR，`3` ALS+PS+IR
- `als_rate`：`0x0~0xF`（或 `0~15`），写入 ALS 配置低 4 位
- `ps_rate`：`0x0~0xF`（或 `0~15`），写入 PS 配置低 4 位

### 4.1 位级别解释（重点）

下面用“参数 -> 寄存器位”的方式解释每一位在本项目里的作用。

#### A. mode（写入 SYSTEMCONG）

`mode` 会直接写入 `AP3216C_SYSTEMCONG`，本项目实际使用低 2 位组合值：

- `mode=0`（二进制 `00`）：关断
- `mode=1`（二进制 `01`）：仅 ALS
- `mode=2`（二进制 `10`）：仅 PS+IR
- `mode=3`（二进制 `11`）：ALS+PS+IR

理解方式：

- bit0 可理解为 ALS 通道开关位
- bit1 可理解为 PS+IR 通道开关位
- `mode=3` 表示 bit1/bit0 都为 1，因此两类通道都开启

示例：

- `ioctldemo 1 ... ...`：只看环境光（ALS）
- `ioctldemo 2 ... ...`：只看接近/红外（PS+IR）

#### B. als_rate（写入 ALSCONFIG 低 4 位）

`als_rate` 使用掩码 `0x0F`，只更新寄存器低 4 位：`bit[3:0]`。

位域语义：

- `als_rate = b3 b2 b1 b0`
- 例如 `als_rate=0x2`，二进制是 `0010`
- 最终写入 ALSCONFIG 的低四位为 `0010`

驱动实现是“读改写”：

1) 先读原寄存器
2) 只替换低 4 位
3) 其他位保持不变

这能避免误改同一寄存器的非采样率位。

#### C. ps_rate（写入 PSCONFIG 低 4 位）

`ps_rate` 同样使用掩码 `0x0F`，只更新 `bit[3:0]`。

位域语义与 ALS 类似：

- `ps_rate = b3 b2 b1 b0`
- 例如 `ps_rate=0x6`，二进制是 `0110`
- 最终写入 PSCONFIG 的低四位为 `0110`

#### D. 为什么限制在 0x0~0xF

因为本项目只允许修改低 4 位：

- 合法范围：`0000` 到 `1111`
- 若传入 `0x10`（二进制 `1 0000`）就超出 4 位位域，会被判定为非法参数

### 4.2 参数到驱动的对应关系

你在命令行输入参数后，链路是：

1) App 解析参数（`atoi` / `strtol`）
2) `ioctl(fd, CMD, value)` 把 value 传给驱动
3) 驱动在 `unlocked_ioctl` 校验范围
4) 驱动写寄存器或读改写目标位域
5) 返回值回到 App，由 App 打印成功或失败

### 4.3 面试可直接复述的话术

“我把 mode 和采样率都当成位域配置来处理。mode 直接控制 SYSTEMCONG 的通道开关组合，als_rate/ps_rate 只更新各自配置寄存器的低 4 位。驱动里采用读改写，保证只改目标位不破坏其他位；参数越界直接返回 -EINVAL，未知命令返回 -ENOTTY。”

### 4.4 说明

本项目当前使用的是“位值直传”测试模型，便于学习 ioctl 与寄存器位域映射。
若后续做产品化，可在 App 层把“档位/毫秒”映射成位值，再调用 ioctl。

非法参数示例（预期失败）：

```bash
./ap3216cApp /dev/ap3216c ioctldemo 9 2 1
./ap3216cApp /dev/ap3216c ioctldemo 3 16 1
./ap3216cApp /dev/ap3216c ioctldemo 3 2 17
```

## 5. 双终端联调建议

终端 A：

```bash
./ap3216cApp /dev/ap3216c ioctldemo 3 2 1
```

终端 B：

```bash
dmesg -w | grep ap3216c
```

用途：

- 同步观察用户态调用结果与驱动日志
- 快速定位参数越界、寄存器写失败等问题

## 6. lockrace 模式（并发读写）

用途：

- 验证“线程A read + 线程B ioctl”并发下是否稳定

命令：

```bash
./ap3216cApp /dev/ap3216c lockrace
./ap3216cApp /dev/ap3216c lockrace 300
```

参数说明：

- `loops`：每个线程循环次数，正整数，默认 `200`

预期：

- 输出 `lockrace summary`
- `read_fail=0` 且 `ioctl_fail=0` 表示并发路径稳定

## 7. lockstress 模式（高频压测）

用途：

- 单线程持续执行 `ioctl + read`，用于压力验证

命令：

```bash
./ap3216cApp /dev/ap3216c lockstress
./ap3216cApp /dev/ap3216c lockstress 1000
```

参数说明：

- `loops`：压测轮数，正整数，默认 `200`

预期：

- 输出 `lockstress summary`
- `fail=0` 表示本轮压测通过

## 8. irqdemo 模式（中断框架兜底验证）

用途：

- 查询驱动当前事件模式（`HW_IRQ` 或 `POLL_SIM`）
- 持续读取事件统计，验证事件处理链路是否可达
- 在轮询兜底模式下，验证“轮询注入 -> 中断核心处理”流程

命令：

```bash
./ap3216cApp /dev/ap3216c irqdemo
./ap3216cApp /dev/ap3216c irqdemo 30
```

参数说明：

- `loops`：统计轮数，正整数，默认 `200`

预期：

- 输出当前 `event mode`
- 周期打印 `total/hw/poll/manual` 统计和 `last_ps/last_src`
- 周期打印 `irqdiag`：`entries/no_status/filtered`
- 在 `POLL_SIM` 下，`poll` 与 `manual` 统计应随时间增长
- 每 5 轮会注入一次手动事件，便于验证事件处理链路和统计更新

## 9. irqcfg 模式（运行时切换 IRQ/POLL）

用途：

- 运行时切换事件模式：`HW_IRQ` 与 `POLL_SIM`
- 配置 PS 接近阈值与 ALS 变化阈值
- 配置轮询周期（仅在 `POLL_SIM` 下生效）

命令：

```bash
./ap3216cApp /dev/ap3216c irqcfg <event_mode> <ps_th> <als_delta_th> [poll_interval_ms]
```

示例：

```bash
# 切到轮询模式，PS阈值200，ALS变化阈值200，轮询间隔10ms
./ap3216cApp /dev/ap3216c irqcfg 2 200 200 10

# 切回硬件中断模式，阈值保持200/200
./ap3216cApp /dev/ap3216c irqcfg 1 200 200
```

参数说明：

- `event_mode`：`1=HW_IRQ`，`2=POLL_SIM`
- `ps_th`：PS触发阈值，建议 `0~1023`
- `als_delta_th`：ALS变化阈值，建议 `0~65535`
- `poll_interval_ms`：轮询周期，默认 `10ms`，建议 `5~1000ms`

预期：

- 驱动日志可看到 `switch event mode` 与阈值更新信息
- `irqdemo` 统计中，切模式后对应来源计数增长
- 在 `POLL_SIM` 下调整 `poll_interval_ms` 能观察到统计变化速度变化

## 10. 常见问题

- 报 `device busy`：说明设备已被其他进程占用，先结束占用进程再重试
- 报 `Invalid argument`：通常是 ioctl 参数超出允许范围
- 报 `No such file or directory`：检查设备节点和驱动加载状态
- `lockrace` 出现 `ioctl_fail`：优先检查驱动是否已加载最新模块
- `lockstress` 出现失败：结合 `dmesg -w | grep ap3216c` 查看驱动日志定位
- `irqdemo` 模式总是 `UNKNOWN`：说明驱动未加载到含事件框架的新版本
- `irqdemo` 统计不增长：检查设备是否已 open、模式是否为 `POLL_SIM`、以及传感器读链路是否正常
- `irqcfg` 切到 `HW_IRQ` 失败：优先检查 DTS 的 `interrupt-parent`、`interrupts` 与 `pinctrl` 是否生效

## 11. 文档分工

- 本文档：只负责 App 测试命令与参数说明
- `LOGGING_GUIDE.md`：日志分级原理与最佳实践
- `ATOMIC_OPEN_GUIDE.md`：原子并发策略原理与面试问答
- `IOCTL_CONFIG_GUIDE.md`：ioctl 设计与实现原理
- `LOCK_CONCURRENCY_GUIDE.md`：原子变量与各类锁的实践对比及选型
- `INTERRUPT_FALLBACK_GUIDE.md`：中断框架兜底方案与面试表达
