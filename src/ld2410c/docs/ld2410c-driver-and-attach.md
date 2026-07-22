# LD2410C 驱动与 attach 实现讲解

本文沉淀 `src/ld2410c` 模块的最低必懂模型：内核驱动如何接收 LD2410C 雷达数据、如何把状态暴露给用户态，以及 `ld2410c_attach`/Qt `attachUart()` 为什么是这条链路的关键开关。

## 阅读入口

| 文件 | 作用 |
| --- | --- |
| [`../ld2410c.c`](../ld2410c.c) | 内核模块：platform driver、OUT GPIO/input、TTY line discipline、misc UAPI、命令 ACK 等待。 |
| [`../ld2410c_attach.c`](../ld2410c_attach.c) | 用户态 attach 工具：配置 UART baud，然后用 `TIOCSETD` 绑定 line discipline。 |
| [`../include/friedegg/ld2410c.h`](../include/friedegg/ld2410c.h) | 内核和用户态共享的 UAPI：line discipline 编号、状态结构体、配置结构体、ioctl 编号。 |
| [`../../imx6_smart_monitor/sensors/ld2410_device.cpp`](../../imx6_smart_monitor/sensors/ld2410_device.cpp) | Qt 应用侧封装：打开 `/dev/ld2410c0`、attach UART、读状态、读写配置。 |
| [`../../../bsp/package/ld2410c/ld2410c.mk`](../../../bsp/package/ld2410c/ld2410c.mk) | Buildroot 包集成：构建内核模块、安装 attach/rawdump 工具、导出 UAPI 头文件。 |
| [`../../linux-friedegg/arch/arm/boot/dts/imx6ull-friedegg-emmc.dts`](../../linux-friedegg/arch/arm/boot/dts/imx6ull-friedegg-emmc.dts) | 板级 DTS：声明 `friedegg,ld2410c` 设备、OUT GPIO、UART3 pinctrl。 |

## 最低必懂模型

LD2410C 有两条信号路径：

```text
OUT 引脚
  -> GPIO1_IO10
  -> ld2410c_out_irq()
  -> ld2410c_update_out()
  -> input SW_FRONT_PROXIMITY + /dev/ld2410c0 state

UART3 数据帧
  -> /dev/ttymxc2
  -> 用户态 TIOCSETD(LD2410C_LDISC)
  -> 内核 n_ld2410c line discipline
  -> ld2410c_ldisc_receive()
  -> ld2410c_consume_frame()
  -> report 更新 state / ACK 唤醒 ioctl
  -> /dev/ld2410c0 read/poll/ioctl
```

这个驱动不是传统的 “probe 里直接拿 UART 设备” 模型。DTS 只让 platform driver 拿到 OUT GPIO；UART 是普通 TTY 设备，必须由用户态进程把该 TTY 切到 `LD2410C_LDISC` 后，内核驱动才开始收到串口字节。这个设计的好处是驱动很轻，不需要把 UART 控制器和雷达节点强绑定；代价是板端必须有一个常驻 attach 过程，或由 Qt 应用持有 UART fd。

## 驱动装载与 probe

模块初始化分两步：

1. `ld2410c_init()` 注册 TTY line discipline：`LD2410C_LDISC` 等于 `29`，名字是 `n_ld2410c`。
2. 继续注册 `platform_driver`，匹配 DTS 中的 `compatible = "friedegg,ld2410c"`。

`ld2410c_probe()` 做几件事：

- 分配 `struct ld2410c_dev`，初始化 `tty_lock`、`tx_lock`、`state_lock`、`read_wq`、`ack_wq`。
- 从 DTS 读取 `out-gpios`，当前板级节点配置为 GPIO1_IO10、active high。
- 如果 OUT GPIO 存在，注册 input 设备 `LD2410C presence`，能力是 `EV_SW / SW_FRONT_PROXIMITY`。
- 把 OUT GPIO 转成 IRQ，同时注册上升沿和下降沿中断，任何电平变化都会刷新 presence。
- 注册 misc 设备 `/dev/ld2410c0`，用户态通过它读状态、poll 事件、发 ioctl 配置雷达。
- 设置全局 `ld2410c_global`。当前实现只支持一个 LD2410C 实例；如果已经存在实例，第二个 probe 会返回 `-EBUSY`。
- probe 末尾主动调用一次 `ld2410c_update_out()`，让初始 OUT 电平进入状态快照。

`remove()` 只清理全局指针和 misc 设备；GPIO、input、IRQ、内存由 devm 资源自动释放。

## OUT GPIO 路径

OUT 是最低延迟、最简单的 presence gate。

当 GPIO 中断触发时：

1. `ld2410c_out_irq()` 被调用。
2. `ld2410c_update_out()` 读取 GPIO 原始电平。
3. 如果 DTS 标成 active low，就取反；当前 DTS 是 active high。
4. 在 `state_lock` 保护下更新：
   - `state.out_level`
   - `LD2410C_STATE_F_OUT_VALID`
   - `LD2410C_STATE_F_OUT_ACTIVE`
   - `state.sequence`
   - `state.timestamp_ns`
5. 如果 input 设备已注册，同步上报 `SW_FRONT_PROXIMITY`。
6. 唤醒 `read_wq`，让阻塞在 `/dev/ld2410c0` 的 `read()`/`poll()` 用户拿到新状态。

这条路径不依赖 UART attach。也就是说，即使串口尚未绑定 line discipline，OUT 引脚仍然可以提供 presence 开关量。

## UART line discipline 路径

TTY line discipline 是串口字节进入驱动的核心。

`ld2410c_ldisc_ops` 只实现了三个回调：

| 回调 | 时机 | 当前行为 |
| --- | --- | --- |
| `open` | 用户态对 TTY 执行 `TIOCSETD(29)` 后，TTY 核心切换 line discipline 时调用。 | 检查是否有 `ld2410c_global`，检查是否已有 UART 绑定，分配 `ld2410c_ldisc`，保存 `tty` 引用，设置 `tty->disc_data`。 |
| `close` | line discipline 被替换、TTY 关闭或 fd 生命周期结束时调用。 | 清空 `ld->tty` 和 `tty->disc_data`，释放 tty 引用和上下文。 |
| `receive_buf` | UART 驱动收到字节后交给当前 line discipline。 | 把字节追加到 256 字节 RX buffer，并调用帧消费逻辑。 |

这里有两个互斥点要抓住：

- `ld2410c_global_lock` 保护全局实例查找，避免 line discipline open 时拿到不完整或已移除的实例。
- `ld->tty_lock` 保护 `ld->tty`，并限制同一时刻只有一个 UART fd 能绑定到驱动。

## 帧解析

`ld2410c_consume_frame()` 是一个简单的流式帧同步器。

它支持两类帧：

| 类型 | 头 | 尾 | 进入函数 |
| --- | --- | --- | --- |
| 上报帧 report | `f4 f3 f2 f1` | `f8 f7 f6 f5` | `ld2410c_parse_report()` |
| 命令 ACK 帧 | `fd fc fb fa` | `04 03 02 01` | `ld2410c_parse_ack()` |

解析策略是：

- RX buffer 至少有 4 字节时开始找头。
- 如果当前 buffer 开头不是 report 头，也不是 ACK 头，就丢 1 字节重试。这是典型的串口失步恢复。
- 读出 little-endian `len`，计算总长度 `4 + 2 + len + 4`。
- 如果长度超出 256 字节 buffer，计一次 parse error，丢 1 字节重同步。
- 如果数据还没收齐，就等待下一次 `receive_buf()`。
- 收齐后检查尾部；尾不对同样计错并重同步。
- report 更新状态，ACK 更新命令等待条件。

普通 report 会更新目标状态、运动距离、静止距离、检测距离、能量和帧计数。工程模式 report 长度更大，会额外更新每个 gate 的运动/静止能量、最大 gate、光照和 OUT 电平。

## misc UAPI

驱动把用户态主接口做成 `/dev/ld2410c0`。

### `read()` 和 `poll()`

每个打开文件都有一个 `struct ld2410c_file`，里面记录 `last_sequence`。

- `open()` 时把当前 `state.sequence` 保存下来，避免一打开就读到旧状态。
- `read()` 要求用户 buffer 至少能容纳 `struct ld2410c_state`。
- 如果没有新状态，阻塞 fd 会睡在 `read_wq`；非阻塞 fd 返回 `-EAGAIN`。
- 有新状态后，把完整 `ld2410c_state` 拷给用户态，并更新该 fd 的 `last_sequence`。
- `poll()` 同样通过 sequence 判断是否可读。

这个设计让多个用户态 reader 互不干扰：每个 fd 都有自己的“上次读到哪一版状态”。

### `ioctl()`

`LD2410C_IOC_GET_STATE` 是纯快照读取，不等待新事件。

配置类 ioctl 会走命令链路：

```text
用户态 ioctl
  -> copy_from_user / 参数检查
  -> ld2410c_run_config_cmd()
      -> tx_lock 串行化配置命令
      -> ENABLE_CONFIG
      -> 目标命令
      -> 可选 END_CONFIG
  -> ld2410c_send_cmd_wait_locked()
      -> 组 UART 命令帧
      -> ld2410c_write_tty()
      -> wait_event_interruptible_timeout(ack_wq, ...)
      -> 检查 ack_status / 拷贝 ack_payload
```

这里 `tx_lock` 很重要：LD2410C 命令是“发一条、等对应 ACK”的串行协议。如果多个线程同时发配置命令，ACK 会互相穿插，驱动就难以判断谁等到了谁。当前用一把互斥锁把配置命令线性化。

ACK 等待依赖 `ack_seq`：

- 发送前记录当前 `ack_seq`。
- `ld2410c_parse_ack()` 收到 ACK 后更新 `ack_cmd`、`ack_status`、`ack_payload`、`ack_len`，再递增 `ack_seq` 并唤醒 `ack_wq`。
- 等待条件要求 `ack_seq` 变化，并且 `ack_cmd` 等于当前命令。
- 超时是 `700 ms`，超时返回 `-ETIMEDOUT`。

### 命令装配、发送和 ACK 回收

配置类 ioctl 的真正发送点是 `ld2410c_send_cmd_wait_locked()`。它只在 `tx_lock` 已经持有时调用，因此函数名带 `locked`：这里的 locked 不是它自己上锁，而是提醒调用者“已经在串行化命令上下文里”。

一条命令的完整流向：

```text
用户态 ioctl(/dev/ld2410c0, LD2410C_IOC_*, arg)
  -> ld2410c_misc_ioctl()
  -> copy_from_user / 参数检查
  -> 具体 helper，如 ld2410c_set_resolution()
  -> ld2410c_run_config_cmd()
      -> mutex_lock(tx_lock)
      -> 发送 ENABLE_CONFIG(0x00ff)
      -> 发送目标命令
      -> 如需要，发送 END_CONFIG(0x00fe)
      -> mutex_unlock(tx_lock)
  -> ld2410c_send_cmd_wait_locked()
      -> 装配 UART 命令帧
      -> ld2410c_write_tty()
      -> 等 ack_wq
      -> 检查 ack_status
```

命令帧格式和协议手册一致：

```text
fd fc fb fa
  -> 命令帧头

len_le16
  -> 帧内数据长度，当前驱动设置为 2 + payload_len
  -> 这 2 字节是命令字本身

cmd_le16
  -> 命令字，小端序

payload
  -> 命令值，可为空

04 03 02 01
  -> 命令帧尾
```

例如 `LD2410C_IOC_SET_RESOLUTION` 进入 `ld2410c_set_resolution()` 后，只允许 `index` 为 `0` 或 `1`。驱动把 `index` 写成 2 字节小端 payload，再用命令字 `0x00aa` 发出：

```text
cmd = 0x00aa
payload = index_le16
```

`ld2410c_write_tty()` 不直接访问 UART 寄存器，它通过 line discipline open 时保存的 `ld->tty` 调用 `ld->tty->ops->write()`。也就是说，命令发送方向是：

```text
ld2410c 驱动
  -> tty->ops->write()
  -> serial core
  -> i.MX UART 驱动
  -> UART TX 引脚
  -> LD2410C 模块
```

发送后，驱动不会忙等，而是睡在 `ack_wq` 上。睡眠条件由 `ld2410c_ack_ready()` 判断：

```text
ack_seq != 发送前记录的 seq
并且 ack_cmd == 当前等待的 cmd
```

这样可以避免读到“旧 ACK”。`ack_seq` 是 ACK 版本号，每解析到一个 ACK 就递增。`ack_cmd` 是刚收到的 ACK 对应的命令字。两者同时匹配，才认为这次命令等到了自己的回复。

ACK 的接收路径和普通 report 共用 UART RX 入口：

```text
LD2410C 模块 ACK 字节
  -> UART RX
  -> TTY core
  -> n_ld2410c.receive_buf
  -> ld2410c_ldisc_receive()
  -> ld2410c_consume_frame()
      -> 识别 fd fc fb fa 命令帧头
      -> 校验 04 03 02 01 命令帧尾
      -> ld2410c_parse_ack()
```

`ld2410c_parse_ack()` 做四件关键事：

1. 从 ACK payload 前 2 字节读出 `ack_word`，再用 `ack_word & ~0x0100` 还原命令字。协议 ACK 示例里命令字常表现为 `A9 01`、`AA 01` 这类形式，驱动清掉 bit8 后得到原命令 `0x00a9`、`0x00aa`。
2. 从后 2 字节读取 `status`。当前约定 `0` 为成功，非 `0` 在等待侧转成 `-EIO`。
3. 如果 ACK 还有返回 payload，就拷贝到 `ld->ack_payload`，长度上限是 `LD2410C_ACK_MAX`。
4. 更新 `ack_seq`、设置 `LD2410C_STATE_F_ACK_VALID`，唤醒 `ack_wq` 和 `read_wq`。

特殊情况是 `LD2410C_CMD_READ_CONFIG`。这个命令的 ACK payload 里带当前配置，`ld2410c_parse_ack()` 在 ACK 成功后会继续调用 `ld2410c_parse_read_config_ack()`，把最大距离门、运动/静止距离门、每个 gate 灵敏度、无人持续时间解析到 `ld->config` 缓存里。随后 `ld2410c_read_config()` 再通过 `ld2410c_copy_config()` 把缓存拷给用户态。

注意：普通 report 和 ACK 都由 `ld2410c_consume_frame()` 解析，但它们语义不同。report 是模块主动周期上报，用来更新 `ld->state`；ACK 是命令回复，用来唤醒正在等待的 ioctl。两者共用 RX buffer、帧头同步、帧尾校验和 parse error 计数。

协议依据：[`docs/LD2410C 串口通信协议 V1.09.pdf`](LD2410C%20串口通信协议%20V1.09.pdf) p.15 给出了距离分辨率命令 `0x00AA` 的发送帧和 ACK 示例；p.19-p.20 给出了上报帧头尾、目标基本信息和工程模式追加字段。当前实现依据见 [`../ld2410c.c`](../ld2410c.c) 中 `ld2410c_send_cmd_wait_locked()`、`ld2410c_write_tty()`、`ld2410c_consume_frame()`、`ld2410c_parse_ack()` 和 `ld2410c_parse_report()`。

### 距离门分辨率和距离字段语义

协议里的“距离分辨率”不是所有距离字段的显示小数位，而是“一个距离门代表多远”。LD2410C 可配置每个距离门为 `0.75 m` 或 `0.2 m`，最大距离门个数都是 8。也就是说：

```text
resolution index 0 -> 每个距离门 0.75 m，默认值
resolution index 1 -> 每个距离门 0.2 m
```

这主要影响按 gate 配置的参数，例如：

- 运动探测最远距离门。
- 静止探测最远距离门。
- 每个距离门的运动/静止灵敏度。
- 工程模式里每个距离门的能量数组应该如何换算空间区间。

它不等于“上报距离只能是 0.75 m 或 0.2 m 的整数倍”。协议上报帧中的 `运动目标距离`、`静止目标距离`、`探测距离` 字段单位是厘米，驱动也按 `u16` 厘米值保存到：

```text
motion_distance_cm
static_distance_cm
detect_distance_cm
```

所以会看到一个看起来更“细”的厘米级数值。正确理解是：

- 距离门分辨率：配置和能量门控的空间分桶粒度。
- 上报距离厘米值：模块算法基于回波/能量/目标状态估算出来的目标距离结果。

说明书也提醒过：雷达理论距离精度是在物理分辨率 `0.75 m` 基础上通过算法处理得到，目标体型、状态、RCS 等不同会导致距离精度波动。因此厘米字段适合用于趋势、显示和粗略位置判断，不应理解成毫米波雷达真的具备严格厘米级绝对测距精度。

三个距离字段可以这样理解：

| 字段 | 协议字段 | 当前驱动字段 | 含义 |
| --- | --- | --- | --- |
| 运动目标距离 | `运动目标距离（厘米）` | `motion_distance_cm` | 当前被算法判为“运动目标”的距离。目标状态为运动或运动&静止时更有意义。 |
| 静止目标距离 | `静止目标距离（厘米）` | `static_distance_cm` | 当前被算法判为“静止/微动存在目标”的距离。目标状态为静止或运动&静止时更有意义。 |
| 探测距离 | `探测距离（厘米）` | `detect_distance_cm` | 模块综合输出的检测距离，可理解为当前 presence 结果对应的总体目标距离。实际使用时比运动/静止分类更适合作为 UI 的单一距离显示，但仍受目标状态和算法影响。 |

`target_state` 决定如何读这些距离：

| `target_state` | 协议含义 | 距离理解 |
| --- | --- | --- |
| `0x00` | 无目标 | 距离字段通常不应作为有效目标距离使用。 |
| `0x01` | 运动目标 | 优先看 `motion_distance_cm` 和 `motion_energy`。 |
| `0x02` | 静止目标 | 优先看 `static_distance_cm` 和 `static_energy`。 |
| `0x03` | 运动&静止目标 | 两套距离/能量都可能有意义，可能代表同一区域内运动和静止特征同时存在。 |
| `0x04`-`0x06` | 底噪检测状态 | 这是校准流程状态，不是普通 presence 距离结果。 |

工程模式会追加每个 gate 的运动/静止能量、最大运动/静止 gate、光敏值和 OUT 状态。这里的 gate 能量更适合调灵敏度和分析干扰源；普通主链路只需要 `presence`、`target_state` 和几个距离/能量摘要。

协议依据：[`docs/LD2410C 串口通信协议 V1.09.pdf`](LD2410C%20串口通信协议%20V1.09.pdf) p.5 说明最远探测距离按距离门设置，且每门可配置 `0.2 m` 或 `0.75 m`；p.15 说明距离分辨率命令和索引；p.19-p.20 说明上报字段包含运动目标距离、静止目标距离、探测距离，单位为厘米，并列出目标状态值。说明书 [`docs/HLK LD2410C生命存在感应模组说明书V1.09.pdf`](HLK%20LD2410C生命存在感应模组说明书V1.09.pdf) p.13 对物理分辨率和算法距离精度的关系做了提醒。

## attach 工具的实现

`ld2410c_attach` 做的事很少，但它决定 UART 字节是否能进入驱动。

流程如下：

```text
open("/dev/ttymxc2", O_RDWR | O_NOCTTY | O_CLOEXEC)
  -> TCGETS2 读取 termios2
  -> 设置 raw UART：BOTHER、CS8、CLOCAL、CREAD、无校验、无硬件流控
  -> 设置输入/输出 baud，默认 256000
  -> TCSETS2 应用串口参数
  -> TIOCSETD，把 line discipline 切到 LD2410C_LDISC(29)
  -> pause() 常驻，直到 SIGINT/SIGTERM
```

为什么要常驻？因为 line discipline 绑定跟 TTY/fd 生命周期相关。`TIOCSETD` 成功后，内核会调用 `ld2410c_ldisc_open()`，驱动保存 `tty` 引用并开始收串口数据；进程退出或 fd 被关闭时，TTY 可能触发 line discipline close，驱动侧会清掉 `ld->tty`。所以 attach 工具进入 `pause()`，本质上是在“保持这根 UART 线接到驱动上”。

Qt 侧的 `Ld2410Device::attachUart()` 是同一件事的库内实现：它打开 UART、配置 `termios2`、执行 `TIOCSETD`，然后把 fd 保存在成员 `uartFd` 里。只要 `Ld2410Device` 没析构、没关闭该 fd，绑定就维持存在。这样 `imx6-sm-ld2410-test` 可以不依赖单独的 `ld2410c_attach` 进程完成实验。

## 用户态消费关系

Smart Monitor 不直接读 UART 字节，而是消费驱动抽象后的状态：

- `Ld2410Device::probe()` 查 `/dev/ld2410c0`、input event hint 和 UART 节点。
- `openDevice()` 打开 `/dev/ld2410c0`。
- `readState()` 用 `LD2410C_IOC_GET_STATE` 拉取快照。
- `pollEvent()` 用 `poll()` 等新状态，再 `read()` 取 `ld2410c_state`。
- `readConfig()`/`writeConfig()` 等配置入口通过 misc ioctl 间接走 UART 命令通道。
- `convertState()` 把内核 UAPI 转成 Qt 内部 `Ld2410State`；presence 优先使用 OUT active，如果 OUT 不 active，则根据 UART report 的目标状态兜底判断 moving/static presence。

因此主闭环看到的是稳定的 `presence / distance / energy / sequence`，不是裸串口协议。

## 常见故障定位

| 现象 | 优先检查 |
| --- | --- |
| `/dev/ld2410c0` 不存在 | 模块是否加载、DTS 是否有 `friedegg,ld2410c` 节点、`misc_register()` 是否成功。 |
| OUT presence 不变化 | DTS `out-gpios`、GPIO 电平极性、IRQ 是否触发、`/proc/bus/input/devices` 是否有 `LD2410C presence`。 |
| UART report 不更新 | 是否运行 `ld2410c_attach -d /dev/ttymxc2 -b 256000`，或 Qt 侧是否执行 `attachUart()` 并保持 fd。 |
| ioctl 配置返回 `-ENODEV` | 通常表示还没有 UART 绑定到 line discipline，`ld->tty` 为空。 |
| ioctl 配置返回 `-ETIMEDOUT` | 命令帧发出后没有等到匹配 ACK；检查 baud、TX/RX 线、雷达是否在对应模式、是否有其他进程抢占 UART。 |
| `error_count` 增长 | 串口失步、baud 不匹配、帧尾不对或 buffer 溢出。可先用 `ld2410c_rawdump` 看裸字节。 |

## 最窄验证

本地只验证文档和代码阅读，不做驱动构建。板端验证建议按最窄链路分层：

```bash
# 1. 模块和节点
ls -l /dev/ld2410c0
dmesg | grep -i ld2410c

# 2. OUT/input 链路
cat /proc/bus/input/devices | grep -A5 -i "ld2410"

# 3. UART 裸数据，确认 baud 和线序
ld2410c_rawdump -d /dev/ttymxc2 -b 256000 -n 128 -t 3000

# 4. 绑定 line discipline，另开终端观察状态或运行测试程序
ld2410c_attach -d /dev/ttymxc2 -b 256000

# 5. Qt 测试页
QT_QPA_PLATFORM=linuxfb imx6-sm-ld2410-test
```

预期结果：

- `/dev/ld2410c0` 存在，`dmesg` 能看到驱动 ready 日志。
- input 设备列表里能看到 `LD2410C presence` 和对应 `eventX`。
- `ld2410c_rawdump` 能打印以 report 头 `f4 f3 f2 f1` 开始的裸字节。
- attach 后，`/dev/ld2410c0` 的 `sequence/frame_count` 会随雷达上报增长。
- 在 LD2410 Test 里切换工程模式、读配置、改灵敏度时，应能看到 ACK 成功和状态刷新。

## 当前边界

- 当前实现是单实例模型：一个 `ld2410c_global`，一个 `/dev/ld2410c0`，同一时间只绑定一个 UART。
- DTS 里 LD2410C 节点没有把 UART 作为 phandle 传给驱动；UART 节点和雷达驱动之间靠用户态 attach 连接。
- OUT GPIO 可独立工作；完整距离、能量、工程 gate 数据和配置命令依赖 UART attach。
- `ld2410c_attach` 和 Qt `attachUart()` 不应该同时抢同一个 `/dev/ttymxc2`。
