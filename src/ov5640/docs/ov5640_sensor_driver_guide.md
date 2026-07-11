# OV5640 sensor 驱动架构与状态机导读

本文面向 `src/ov5640/ov5640.c` 这份 OV5640 sensor 驱动，目标是帮助阅读者快速把握：

- 这个驱动在当前 camera 栈中的位置。
- 源码按功能可以拆成哪些模块。
- 每个模块维护哪些状态，状态如何迁移。
- 用户态 V4L2 ioctl 最终如何影响 sensor subdev 回调。

本文只基于当前仓库源码整理。未直接从源码确认的内容会标为“由上下文推测”“待查 core 源码”或“需板端验证”。

## 驱动功能概述

`ov5640.c` 是一个 I2C camera sensor 驱动，把 OV5640 作为 V4L2 sub-device 暴露给 CSI host 驱动。它负责 sensor 侧的 I2C 寄存器配置、MCLK/GPIO 电源时序、media-bus 格式协商、帧率/分辨率模式切换、stream 开关、V4L2 controls 和 AF 固件命令。

当前 `/dev/videoX` video node、VB2 buffer 队列、CSI DMA 和 IRQ 完成路径在 `mx6s_capture.c` 中实现。用户态并不会直接打开 `ov5640.c`，而是通过 video node ioctl 进入 host，再由 host 通过 `v4l2_subdev_call()` 调用 OV5640 subdev 回调。

```text
userspace open/ioctl/mmap/poll
  -> /dev/videoX
  -> mx6s_capture.c: v4l2_file_operations / v4l2_ioctl_ops / vb2_ops
  -> v4l2_subdev_call(sd, ...)
  -> ov5640.c: v4l2_subdev_ops
  -> regmap I2C writes/reads OV5640 registers
```

## 1 目录 & 概括

### 1.1 总线类型与设备树

- **总线类型**：I2C sensor subdev。源码入口是 `struct i2c_driver ov5640_i2c_driver` 和 `module_i2c_driver(ov5640_i2c_driver)`，见 [`../ov5640.c`](../ov5640.c)。
- **驱动入口**：`ov5640_probe()` / `ov5640_remove()`。
- **ID 表**：源码中有 `static const struct i2c_device_id ov5640_id[] = { {"ov5640", 0}, {} };`。
- **DTS compatible 字符串**：板级 DTS 中有 `compatible = "ovti,ov5640"`，见 [`../../linux-friedegg/arch/arm/boot/dts/imx6ull-friedegg-emmc.dts`](../../linux-friedegg/arch/arm/boot/dts/imx6ull-friedegg-emmc.dts)。
- **源码缺口**：当前 `src/ov5640/ov5640.c` 未看到 `of_match_table`。DTS 到 I2C driver 的 OF 匹配细节需继续查当前内核 I2C core 或模块 alias 生成路径。
- **对应硬件设备**：OV5640 camera sensor，通过 I2C 地址 `0x3c` 配置寄存器，通过并行 CSI/DVP 信号接入 i.MX6ULL CSI。

板级 DTS 中和 sensor 相关的关键资源：

| DTS 资源 | 当前值 | 驱动使用位置 | 说明 |
| --- | --- | --- | --- |
| `reg` | `0x3c` | I2C core | sensor I2C 地址 |
| `clocks` / `clock-names` | `IMX6UL_CLK_CSI` / `csi_mclk` | `devm_clk_get(dev, "csi_mclk")` | sensor 输入 MCLK |
| `pwn-gpios` | `gpio1 4 GPIO_ACTIVE_HIGH` | `devm_gpiod_get(dev, "pwn", ...)` | power-down GPIO |
| `rst-gpios` | `gpio1 2 GPIO_ACTIVE_LOW` | `devm_gpiod_get(dev, "rst", ...)` | reset GPIO |
| `mclk` | `24000000` | `of_property_read_u32()` | 目标 MCLK 频率 |
| `mclk_source` | `0` | `of_property_read_u32()` | legacy 字段，当前主要缓存 |
| `csi_id` | `0` | `of_property_read_u32()` | legacy CSI 编号，当前 sensor 驱动内未实际参与路由 |
| `port/endpoint` | `remote-endpoint = <&csi1_ep>` | media graph / host 侧 | sensor endpoint 接到 CSI endpoint |

probe 的关键任务：

```text
ov5640_probe()
  -> devm_kzalloc()
  -> devm_gpiod_get("pwn"), devm_gpiod_get("rst")
  -> devm_regmap_init_i2c()
  -> devm_clk_get("csi_mclk")
  -> read DT: mclk / mclk_source / csi_id
  -> ov5640_set_clk_rate()
  -> ov5640_init_default_state()
  -> ov5640_power_on()
  -> read chip id: 0x300a == 0x56, 0x300b == 0x40
  -> init_device()
  -> v4l2_i2c_subdev_init()
  -> ov5640_init_controls()
  -> v4l2_async_register_subdev()
  -> pm_runtime_enable() + autosuspend
```

remove 的关键任务：

```text
ov5640_remove()
  -> pm_runtime_disable()
  -> v4l2_async_unregister_subdev()
  -> v4l2_ctrl_handler_free()
  -> lock
  -> ov5640_power_off()
  -> force pwdn if already powered off
  -> unlock
  -> pm_runtime_set_suspended()
  -> disable optional regulators
```

### 1.2 用户接口与回调总表

`ov5640.c` 本身没有 `file_operations`，不直接创建 `/dev/videoX`。用户接口来自 `mx6s_capture.c` 的 video node。下表把用户态操作、host 转发和 sensor 回调放在一起看。

| 用户接口 | 操作/ioctl 命令 | Core / host 行为 | OV5640 回调 | 功能说明 | 证据状态 |
| --- | --- | --- | --- | --- | --- |
| `/dev/videoX` | `open()` | `mx6s_csi_open()` 初始化 VB2 queue、CSI host，并调用 sensor `core.s_power(1)` | `ov5640_s_power(1)` | 增加 runtime PM 使用计数，上电并重新初始化 sensor | host 路径已查源码 |
| `/dev/videoX` | `close()` | `mx6s_csi_close()` 释放队列并调用 sensor `core.s_power(0)` | `ov5640_s_power(0)` | 释放 runtime PM 使用计数，允许 autosuspend | host 路径已查源码 |
| `/dev/videoX` | `VIDIOC_ENUM_FMT` | host 枚举 mbus code 后映射为 V4L2 fourcc | `ov5640_enum_fmt()` | 当前只暴露 `MEDIA_BUS_FMT_RGB565_2X8_LE` | 已查源码 |
| `/dev/videoX` | `VIDIOC_ENUM_FRAMESIZES` | host 把 fourcc 转成 mbus code，调用 pad ops | `ov5640_enum_framesizes()` | 枚举 `ov5640_modes[]` 中的离散分辨率 | 已查源码 |
| `/dev/videoX` | `VIDIOC_ENUM_FRAMEINTERVALS` | host 转发 width/height/code/index | `ov5640_enum_frameintervals()` | 枚举某分辨率支持的 15/30 fps 组合 | 已查源码 |
| `/dev/videoX` | `VIDIOC_TRY_FMT` | host 协商格式但不提交 active state | `ov5640_try_fmt()` | 规整 code/width/height，不写寄存器 | 已查源码 |
| `/dev/videoX` | `VIDIOC_S_FMT` | host 提交 active format 并配置 CSI | `ov5640_s_fmt()` | streaming 前切换 sensor 模式、写 RGB565 输出寄存器 | 已查源码 |
| `/dev/videoX` | `VIDIOC_G_FMT` | host 返回当前 active video format | `ov5640_g_fmt()` 或 host cache | sensor 返回 cached `mbus_fmt`；host 是否调用需看具体路径 | 部分已查 |
| `/dev/videoX` | `VIDIOC_G_PARM` | host 转发 stream 参数请求 | `ov5640_g_parm()` | 返回 cached `streamcap`，重点是 `timeperframe` | 已查源码 |
| `/dev/videoX` | `VIDIOC_S_PARM` | host 转发 stream 参数请求 | `ov5640_s_parm()` | 归一到 15/30 fps，必要时切换模式 | 已查源码 |
| `/dev/videoX` | `VIDIOC_STREAMON` | VB2/host 启动 CSI，再调用 sensor 出流 | `ov5640_s_stream(1)` | runtime get，写 `0x4202 = 0x00`，置 `streaming=true` | 已查源码 |
| `/dev/videoX` | `VIDIOC_STREAMOFF` | host 停 CSI，调用 sensor 停流 | `ov5640_s_stream(0)` | 写 `0x4202 = 0x0f`，置 `streaming=false`，runtime put | 已查源码 |
| `/dev/videoX` | `VIDIOC_QUERYCTRL` / `VIDIOC_QUERYMENU` | V4L2 control core 查询 `ctrl_handler` | `ov5640_init_controls()` 创建的 controls | 枚举 H/V flip、防频闪、闪光灯、focus/AF controls | 已查源码 |
| `/dev/videoX` | `VIDIOC_S_CTRL` | V4L2 control core 分发 | `ov5640_s_ctrl()` | 设置 flip、防频闪、闪光灯、手动 focus、AF zone/start/stop | 已查源码 |
| `/dev/videoX` | `VIDIOC_G_CTRL` volatile controls | V4L2 control core 分发 | `ov5640_g_volatile_ctrl()` | 读取 AF status / zone result | 已查源码 |
| debug ioctl | `VIDIOC_DBG_G_REGISTER` / `VIDIOC_DBG_S_REGISTER` | 仅 `CONFIG_VIDEO_ADV_DEBUG` 下可用 | `ov5640_get_register()` / `ov5640_set_register()` | 直接读写 16-bit 地址、8-bit 值的 sensor 寄存器 | 条件编译 |
| runtime PM | autosuspend/resume | PM core 调用 driver pm ops | `ov5640_runtime_suspend()` / `ov5640_runtime_resume()` | 空闲掉电、重新上电并按缓存重配 sensor | 已查源码 |

## 2 接口 / 回调展开讲解

### 2.1 绑定与初始化：`ov5640_probe()`

- **触发来源**：I2C 设备匹配到 `ov5640_i2c_driver` 后，I2C core 调用 `probe`。DTS compatible 与当前 driver 的完整 OF 匹配链路待继续查内核 core。
- **内核调用路径**：`module_i2c_driver()` 注册 I2C driver；I2C core match 后调用 `ov5640_probe()`。
- **Core 的关键处理**：device model 负责创建 `struct device` 和 `struct i2c_client`，devres 负责 `devm_*` 资源释放。V4L2 subdev 注册后，host 可通过 async notifier 或 subdev lookup 建立拓扑关系。
- **驱动回调实现分析**：
  - 分配 `struct ov5640`，保存 `dev` 和 `i2c_client`。
  - 获取 `pwn`、`rst` GPIO。
  - 初始化 `regmap`，配置为 16-bit register address、8-bit value。
  - 获取 `csi_mclk` 并按 DTS `mclk` 设置速率。
  - 初始化默认状态：800x480、RGB565、30fps、powered=false、streaming=false、AF 未加载。
  - 临时上电，读 chip id，确认 `0x56 0x40`。
  - `init_device()` 写 sensor 初始化表并按默认/缓存状态关流。
  - 初始化 subdev ops 和 controls，最后 async register。
- **硬件操作与寄存器路径**：MCLK、GPIO reset/pwdn、I2C 读 chip id、写全局初始化表和模式表。
- **要点 / 限制**：
  - `v4l2_i2c_subdev_init()` 在 `init_device()` 之后调用，所以 probe 前半段不能依赖 `i2c_get_clientdata()` 找回 subdev。
  - optional regulator 代码存在，但当前被注释；板级模块供电由外部 3.3V 或模块内部电源处理，属于当前实现假设。
- **待补充项**：
  - 查当前内核 I2C OF 匹配路径，确认 `compatible = "ovti,ov5640"` 如何匹配到这个外置 driver。

### 2.2 卸载：`ov5640_remove()`

- **触发来源**：模块卸载、设备解绑或 I2C device 移除。
- **内核调用路径**：I2C core 调用 driver `.remove`。
- **Core 的关键处理**：device model 释放 devres 资源；V4L2 async unregister 解除 subdev 注册。
- **驱动回调实现分析**：
  - 先 `pm_runtime_disable()`，防止新的 runtime PM transition。
  - unregister subdev，释放 ctrl handler。
  - 加锁后 `ov5640_power_off()`，保证 stream 停止、GPIO 进入 power-down、MCLK 关闭。
  - 若 optional regulators 曾启用，则 disable。
- **硬件操作与寄存器路径**：可能写 stream-off 寄存器；拉 PWDN；关闭 clock；disable regulators。
- **要点 / 限制**：`ov5640_power_off()` 会使 AF 状态失效，因此下次重新上电后 AF 固件需重新加载。
- **待补充项**：板端卸载时观察 `dmesg` 是否存在 runtime PM 竞态或 busy 情况。

### 2.3 电源引用：`ov5640_s_power()`

- **用户态行为**：通常由 `/dev/videoX` 的 `open()` / `close()` 间接触发；当前 host 在 `mx6s_csi_open()` / `mx6s_csi_close()` 中调用 sensor `core.s_power`。
- **内核调用路径**：`open/close -> mx6s_capture.c -> v4l2_subdev_call(sd, core, s_power, on) -> ov5640_s_power()`。
- **Core 的关键处理**：V4L2 subdev call 做空指针/ops 检查；runtime PM 做引用计数、resume/suspend 分发。
- **驱动回调实现分析**：
  - `on != 0`：调用 `ov5640_runtime_get()`，最终可能进入 `runtime_resume()`。
  - `on == 0`：调用 `ov5640_runtime_put_autosuspend()`，延迟进入 suspend。
- **硬件操作与寄存器路径**：`s_power()` 自己不直接写寄存器；硬件动作在 runtime PM callback 中完成。
- **要点 / 限制**：这是 legacy host open/close 契约和 runtime PM 的桥接层，不再直接 toggle power。
- **待补充项**：查 host 多进程 open 时引用计数策略，确认多 fd 场景下 power 引用是否配平。

### 2.4 runtime resume：`ov5640_runtime_resume()`

- **触发来源**：`pm_runtime_get_sync()`，可能由 `s_power(1)` 或 `s_stream(1)` 引起。
- **内核调用路径**：PM core 调用 `dev_pm_ops.runtime_resume`。
- **Core 的关键处理**：PM core 管理 usage count 和设备 active/suspended 状态。
- **驱动回调实现分析**：
  - 加 `sensor->lock`。
  - `ov5640_power_on()` 打开 MCLK、执行 reset/pwdn 时序、置 `powered=true`。
  - `init_device()` 软复位 sensor，写全局初始化表和当前缓存请求。
  - 若初始化失败，调用 `ov5640_power_off()` 回滚。
- **硬件操作与寄存器路径**：MCLK enable、GPIO reset/pwdn、软复位、初始化寄存器表、模式寄存器表、RGB565、controls、stream off。
- **要点 / 限制**：掉电后的所有运行态硬件配置都会丢失，因此 resume 不是简单上电，而是完整重放 `init_device()`。
- **待补充项**：可板端开动态调试观察 autosuspend 后再次 `STREAMON` 是否重新写表。

### 2.5 runtime suspend：`ov5640_runtime_suspend()`

- **触发来源**：`pm_runtime_put_autosuspend()` 后空闲超时。
- **内核调用路径**：PM core 调用 `dev_pm_ops.runtime_suspend`。
- **Core 的关键处理**：PM core 保证只有 usage count 归零后才尝试 suspend。
- **驱动回调实现分析**：
  - 加锁。
  - 如果 `state.streaming == true`，直接返回 `-EBUSY`，拒绝 suspend。
  - 否则 `ov5640_power_off()`。
- **硬件操作与寄存器路径**：stream off、PWDN、clock disable、AF invalidate。
- **要点 / 限制**：streaming 中不能 autosuspend；如果调用顺序不当，会看到 `-EBUSY`。
- **待补充项**：结合 PM core 日志验证 autosuspend delay 是否符合 `OV5640_AUTOSUSPEND_DELAY_MS`。

### 2.6 出流控制：`ov5640_s_stream()`

- **用户态行为**：`ioctl(fd, VIDIOC_STREAMON, &type)` 或 `VIDIOC_STREAMOFF`。
- **内核调用路径**：用户态 ioctl 进入 host `mx6s_vidioc_streamon/streamoff()`，再通过 `v4l2_subdev_call(sd, video, s_stream, enable)` 转给 sensor。
- **Core 的关键处理**：VB2/host 负责 buffer 队列、CSI DMA 启停、IRQ 路径；sensor 只负责让 OV5640 是否输出像素流。
- **驱动回调实现分析**：
  - enable：
    - 若已 streaming，直接返回 0。
    - 先 runtime get，确保 powered。
    - 加锁后二次检查 streaming。
    - `ov5640_set_stream(true)` 写硬件寄存器并置 `state.streaming=true`。
  - disable：
    - 若未 streaming，直接返回 0。
    - `ov5640_set_stream(false)`。
    - 成功后 runtime put autosuspend。
- **硬件操作与寄存器路径**：写 `OV5640_REG_STREAM_CTRL`，值为 `OV5640_STREAM_ON` 或 `OV5640_STREAM_OFF`。
- **要点 / 限制**：
  - `ov5640_set_stream(true)` 要求 `state.powered == true`，否则返回 `-EPIPE`。
  - 这个函数只控制 sensor 输出，不管理 VB2 buffer 生命周期。
- **待补充项**：确认 host 中 streamon 的顺序是先准备 CSI 还是先 sensor 出流；不同顺序会影响前几帧稳定性。

状态机：

```text
streaming=false
  -- VIDIOC_STREAMON -->
runtime_get()
  -> powered=true
  -> write 0x4202 = 0x00
  -> streaming=true

streaming=true
  -- VIDIOC_STREAMOFF -->
write 0x4202 = 0x0f
  -> streaming=false
  -> runtime_put_autosuspend()
```

### 2.7 查询格式：`ov5640_try_fmt()` / `ov5640_g_fmt()` / `ov5640_enum_fmt()`

- **用户态行为**：`VIDIOC_ENUM_FMT`、`VIDIOC_TRY_FMT`、部分 host 路径下的 `VIDIOC_G_FMT`。
- **内核调用路径**：video node ioctl 进入 host，host 在需要 sensor 能力时调用 `video.enum_mbus_fmt`、`video.try_mbus_fmt`、`video.g_mbus_fmt`。
- **Core 的关键处理**：V4L2 ioctl core 负责参数拷贝和 ioctl 分发；host 负责 fourcc 与 mbus code 之间映射。
- **驱动回调实现分析**：
  - `ov5640_enum_fmt()`：按 index 枚举 `ov5640_colour_fmts[]`，当前只有 RGB565 mbus code。
  - `ov5640_try_fmt()`：如果 code 不支持，则修正为默认 RGB565；分辨率按当前 `state.frame_rate` 找最近支持模式。
  - `ov5640_g_fmt()`：返回 cached `state.mbus_fmt`。
- **硬件操作与寄存器路径**：无寄存器写入。
- **要点 / 限制**：`TRY_FMT` 是纯协商路径，不应改变 active state；真正写寄存器的是 `S_FMT` 或 resume 时 `init_device()`。
- **待补充项**：host 的 `G_FMT` 是否每次查询 sensor cache，还是只返回 host cache，需要继续细读对应函数。

### 2.8 设置格式：`ov5640_s_fmt()`

- **用户态行为**：`ioctl(fd, VIDIOC_S_FMT, &fmt)`。
- **内核调用路径**：`video_ioctl2() -> mx6s_vidioc_s_fmt_vid_cap() -> mx6s_negotiate_format(apply=true) -> v4l2_subdev_call(sd, video, s_mbus_fmt, &mbus_fmt) -> ov5640_s_fmt()`。
- **Core 的关键处理**：V4L2 core 分发 ioctl；host 侧完成 video capture format 结构和 CSI 接收格式配置。
- **驱动回调实现分析**：
  - 先调用 `ov5640_try_fmt()` 规整请求。
  - 找到对应 `ov5640_datafmt` 和 `ov5640_mode_info`。
  - 加锁。
  - 如果 streaming，返回 `-EBUSY`。
  - 如果 powered，立即：
    - `ov5640_change_mode()`
    - `ov5640_apply_format()`
    - `ov5640_apply_controls()`
    - `ov5640_hw_set_stream(false)`
  - 最后更新 `state.frame_size` 和 `state.mbus_fmt`。
- **硬件操作与寄存器路径**：
  - 模式寄存器表。
  - `OV5640_REG_FORMAT_MUX_CONTROL`。
  - `OV5640_REG_FORMAT_CONTROL00`。
  - controls 涉及 flip、防频闪、flash/focus 等寄存器。
- **要点 / 限制**：
  - streaming 中禁止改格式。
  - powered=false 时只缓存请求，下一次 `init_device()` 会重放。
  - 先写模式再写格式和 controls，是因为模式表可能覆盖部分寄存器。
- **待补充项**：确认 host 在 `S_FMT` 后是否一定同步更新 `sizeimage/bytesperline`，避免用户态 mmap buffer 尺寸错误。

状态机：

```text
request fmt
  -> try_fmt normalize
  -> if streaming: -EBUSY
  -> if powered:
       change_mode()
       apply_format()
       apply_controls()
       force hardware stream off
  -> cache state.frame_size + state.mbus_fmt
```

### 2.9 设置帧率：`ov5640_s_parm()`

- **用户态行为**：`ioctl(fd, VIDIOC_S_PARM, &streamparm)`。
- **内核调用路径**：host `mx6s_vidioc_s_parm()` 转发给 `video.s_parm`。
- **Core 的关键处理**：V4L2 streamparm 结构由 ioctl core 处理，host 不直接解释 sensor 的帧率表。
- **驱动回调实现分析**：
  - 只支持 `V4L2_BUF_TYPE_VIDEO_CAPTURE`。
  - `timeperframe` 为空时使用默认 30fps。
  - 目标 fps 被夹到 15 到 30 之间。
  - 只接受离散 15fps 或 30fps。
  - 检查当前 `state.frame_size` 是否支持该 fps。
  - 如果正在 streaming 且帧率变化，返回 `-EBUSY`。
  - 如果 powered 且帧率变化，立即切模式、重写 RGB565、重放 controls、强制硬件停流。
  - 更新 `state.streamcap` 和 `state.frame_rate`。
- **硬件操作与寄存器路径**：和 `S_FMT` 类似，核心是对应 fps 的模式寄存器表。
- **要点 / 限制**：
  - 帧率不是任意值，当前设计只有 15/30 两档。
  - 有些分辨率只提供 15fps 或 30fps 表；例如大分辨率表以 15fps 为主。
- **待补充项**：结合 `ov5640_modes[]` 生成一张“分辨率 -> 支持 fps”速查表，可作为后续文档补充。

### 2.10 模式切换：`ov5640_change_mode()`

- **触发来源**：`S_FMT`、`S_PARM`、`init_device()`。
- **内核调用路径**：这是 sensor 内部 helper，不是直接 subdev callback。
- **Core 的关键处理**：无 core 参与。
- **驱动回调实现分析**：
  - 校验 frame rate 和 frame size。
  - 查 `ov5640_mode_info` 和对应 `ov5640_mode_reg_table`。
  - 根据 `mode_info->downsize` 分流：
    - `OV5640_DOWNSIZE_SUBSAMPLING`：走 `ov5640_change_mode_direct()`。
    - `OV5640_DOWNSIZE_SCALING`：走 `ov5640_change_mode_exposure_calc()`。
  - 成功后更新 `state.frame_rate`、`state.frame_size`、`state.mbus_fmt.width/height`，并通知 AF mode changed。
- **硬件操作与寄存器路径**：
  - direct 路径：写模式表、打开 AE/AG、重算 banding filter、写 AE target、night mode、等待若干帧。
  - exposure_calc 路径：读取预览 shutter/gain/average，写 capture 表，关闭 AE/AG，按 sysclk/HTS/VTS/banding 换算 capture shutter/gain。
- **要点 / 限制**：
  - 大分辨率切换比小分辨率复杂，因为要尽量维持曝光连续性。
  - `state.prev_sysclk` 和 `state.prev_hts` 是曝光换算的重要历史状态，由 `ov5640_set_bandingfilter()` 更新。
- **待补充项**：寄存器表来自原厂/NXP 经验值，具体含义需要 datasheet 或寄存器手册补充。

模式切换状态机：

```text
mode request: frame_size + frame_rate
  -> validate enum range
  -> get mode_info
  -> get mode_reg_table
  -> if downsize == SUBSAMPLING:
       write table directly
       enable AE/AG
       recalc banding + AE target + night mode
  -> if downsize == SCALING:
       read preview exposure/gain
       write capture table
       disable AE/AG
       calculate capture shutter/gain
  -> update state
  -> AF note mode change
```

### 2.11 读取流参数：`ov5640_g_parm()`

- **用户态行为**：`ioctl(fd, VIDIOC_G_PARM, &streamparm)`。
- **内核调用路径**：host `mx6s_vidioc_g_parm()` 转发给 sensor `video.g_parm`。
- **Core 的关键处理**：V4L2 ioctl core 做参数结构处理。
- **驱动回调实现分析**：
  - 只支持 `V4L2_BUF_TYPE_VIDEO_CAPTURE`。
  - 清空返回结构后填回 `capability`、`timeperframe` 和 `capturemode`。
  - 非 capture 类型返回 `-EINVAL`。
- **硬件操作与寄存器路径**：无，读取的是 driver cache。
- **要点 / 限制**：这不是实时读取 sensor 寄存器；如果硬件被其他路径改动，cache 可能和硬件不一致。
- **待补充项**：无。

### 2.12 枚举分辨率和帧间隔：`ov5640_enum_framesizes()` / `ov5640_enum_frameintervals()`

- **用户态行为**：`VIDIOC_ENUM_FRAMESIZES` / `VIDIOC_ENUM_FRAMEINTERVALS`。
- **内核调用路径**：host 将用户态 fourcc 转换到 mbus code 后调用 sensor pad ops。
- **Core 的关键处理**：V4L2 ioctl core 负责分发；pad ops 表示这是 media graph pad 级能力枚举。
- **驱动回调实现分析**：
  - `enum_framesizes()`：要求 code 支持；按 index 遍历 `ov5640_modes[]`，返回离散尺寸。
  - `enum_frameintervals()`：要求 code、width、height 有效；查 exact mode；按 index 枚举该 mode 支持的 frame rate。
- **硬件操作与寄存器路径**：无。
- **要点 / 限制**：
  - frameinterval 只对 exact width/height 有效，不做 nearest 匹配。
  - framesize 枚举不按当前 fps 过滤，而 frameinterval 会按 mode 的 reg table 过滤。
- **待补充项**：确认 host 是否把 unsupported fourcc 正确拒绝，而不是传入错误 mbus code。

### 2.13 V4L2 controls：`ov5640_s_ctrl()` / `ov5640_g_volatile_ctrl()`

- **用户态行为**：`VIDIOC_QUERYCTRL`、`VIDIOC_QUERYMENU`、`VIDIOC_G_CTRL`、`VIDIOC_S_CTRL`。
- **内核调用路径**：V4L2 control core 根据 `sensor->subdev.ctrl_handler` 调用 `v4l2_ctrl_ops`。
- **Core 的关键处理**：
  - control core 负责保存 control 当前值、范围检查、菜单项查询、volatile control 读取分发。
  - driver 只实现 `.s_ctrl` 和 `.g_volatile_ctrl`。
- **驱动回调实现分析**：
  - `ov5640_init_controls()` 创建 14 个 controls。
  - `ov5640_s_ctrl()` 加锁后按 control id 分发。
  - AF zone、touch x/y、AF stop/start 有特殊路径。
  - 普通 controls 如果 `state.powered == false`，多数只保留 control core 缓存，不写硬件。
  - powered 时写 HFLIP/VFLIP、防频闪、flash、focus absolute 等寄存器。
  - `ov5640_g_volatile_ctrl()` 读取 AF status 和 AF zone result。
- **硬件操作与寄存器路径**：
  - flip：`OV5640_REG_TIMING_TC_REG20/21`。
  - anti-banding：`OV5640_REG_BANDING_FILTER_CTRL/MAN`。
  - flash：`OV5640_REG_PAD_SELECT00`、`OV5640_REG_PAD_OUTPUT_ENABLE00`、`OV5640_REG_STROBE_CTRL` 等。
  - focus absolute：`OV5640_REG_VCM_CONTROL2/3`。
  - AF：`0x3022` 到 `0x3029` 一组 MCU command/status 寄存器。
- **要点 / 限制**：
  - 控制项缓存和硬件寄存器是两层状态；掉电后硬件丢失，但 control 值仍在 handler 中，resume 时通过 `ov5640_apply_controls()` 重放。
  - `AUTO_FOCUS_START` 要求 powered 且 streaming，否则返回 `-EPIPE`。
- **待补充项**：用 `v4l2-ctl --list-ctrls` 在板端确认 control 名称、范围和 flags 是否符合预期。

### 2.14 自动对焦状态机：AF firmware + command

- **用户态行为**：通过 `VIDIOC_S_CTRL` 设置 `V4L2_CID_AUTO_FOCUS_START/STOP`，通过 `VIDIOC_G_CTRL` 读取 `V4L2_CID_AUTO_FOCUS_STATUS` 和自定义 `af_zone_result`。
- **内核调用路径**：V4L2 control core -> `ov5640_s_ctrl()` / `ov5640_g_volatile_ctrl()` -> AF helper。
- **Core 的关键处理**：control core 做 range/flags 处理；firmware framework 负责从 rootfs firmware path 加载 `ov5640_af.bin`。
- **驱动回调实现分析**：
  - `ov5640_af_invalidate()`：清除 `firmware_loaded`、置 `zone_pending=true`、清 `zone_result`。
  - `ov5640_af_load_firmware()`：请求 `ov5640_af.bin`，写入 `0x8000` 起始区域，释放 MCU reset，等待 firmware idle。
  - `ov5640_af_apply_zone()`：根据 default 或 touch 模式下发 AF zone command。
  - `ov5640_af_start()`：要求 streaming，加载固件，应用 pending zone，发送 trigger auto focus。
  - `ov5640_af_get_status()`：读 firmware status，映射到 V4L2 AF status；focused 时读取当前单 AF zone 结果。
  - `ov5640_af_stop()`：release focus。
- **硬件操作与寄存器路径**：
  - firmware block：`regmap_raw_write()` 到 `OV5640_AF_FIRMWARE_START`。
  - MCU reset：`OV5640_REG_SYSTEM_RESET00`。
  - command：`OV5640_REG_AF_CMD_MAIN/ACK/PARA0/PARA1`。
  - status：`OV5640_REG_AF_FW_STATUS`。
- **要点 / 限制**：
  - AF 依赖 `state.streaming == true`，因为对焦需要实时图像统计。
  - power off 后 AF firmware 状态失效；下次 start 会重新加载。
  - touch 坐标按当前 `state.mbus_fmt.width/height` 转成 AF VV（80x60）坐标系。
- **待补充项**：
  - `ov5640_af.bin` 具体固件协议需 datasheet 或原厂资料确认。
  - 板端需要确认 `/lib/firmware/ov5640_af.bin` 是否实际安装。

AF 状态机：

```text
power off / init
  -> firmware_loaded=false
  -> zone_pending=true
  -> zone_result=0

set zone/touch
  -> update zone cache
  -> zone_pending=true
  -> if firmware_loaded && streaming: apply zone

AUTO_FOCUS_START
  -> require powered && streaming
  -> load firmware if needed
  -> wait firmware idle
  -> apply pending zone
  -> send TRIG_AUTO_FOCUS

G_CTRL AUTO_FOCUS_STATUS
  -> if not firmware_loaded: IDLE
  -> read AF_FW_STATUS
  -> IDLE / BUSY / FOCUSED
  -> if focused: get focus result -> REACHED or FAILED

power_off
  -> invalidate firmware state
```

### 2.15 debug register：`ov5640_get_register()` / `ov5640_set_register()`

- **用户态行为**：需要 `CONFIG_VIDEO_ADV_DEBUG`，通常由 debug ioctl 或 `v4l2-ctl --get-register/--set-register` 类工具触发。
- **内核调用路径**：V4L2 debug ioctl -> subdev core debug register ops。
- **Core 的关键处理**：debug ioctl 权限和命令分发由 V4L2 core 处理，具体待查 core 源码。
- **驱动回调实现分析**：
  - 只接受 16-bit register address。
  - 读写 8-bit value。
- **硬件操作与寄存器路径**：直接走 `ov5640_read_reg()` / `ov5640_write_reg()`。
- **要点 / 限制**：这是调试口，不适合作为正常控制接口；随意写寄存器可能破坏当前状态缓存。
- **待补充项**：确认当前内核配置是否打开 `CONFIG_VIDEO_ADV_DEBUG`。

## 3 核心模块与状态机设计

### 3.1 私有对象：`struct ov5640`

`struct ov5640` 是贯穿整份驱动的中心对象。阅读时可以按四类字段理解：

| 字段类别 | 代表字段 | 作用 |
| --- | --- | --- |
| 子系统对象 | `v4l2_subdev subdev`、`v4l2_ctrl_handler` | 把 sensor 接入 V4L2 subdev 和 control core |
| 设备资源 | `dev`、`i2c_client`、`regmap`、`sensor_clk`、GPIO、regulator | 访问设备模型、I2C、时钟、电源和管脚 |
| 运行状态 | `struct ov5640_state state` | 保存格式、帧率、电源、stream、AE 相关状态 |
| AF 状态 | `struct ov5640_af_state af` | 保存 AF 固件、区域、结果等状态 |

从回调拿回私有对象的常用路径：

```text
subdev callback:
  sd -> container_of(sd, struct ov5640, subdev)

i2c/pm callback:
  i2c_client -> i2c_get_clientdata(client) -> subdev -> struct ov5640

ctrl callback:
  ctrl->handler -> container_of(handler, struct ov5640, ctrls.ctrl_handler)
```

### 3.2 `struct ov5640_state`

关键字段：

| 字段 | 含义 | 谁写它 | 谁读它 |
| --- | --- | --- | --- |
| `mbus_fmt` | 当前 active media-bus 格式缓存 | default init、`s_fmt()`、`change_mode()` | `g_fmt()`、AF touch 坐标换算、resume |
| `streamcap` | 当前 capture stream 参数缓存 | default init、`s_parm()` | `g_parm()`、`init_device()` |
| `frame_rate` | 当前离散 fps enum | default init、`s_parm()`、`change_mode()` | `try_fmt()`、`s_fmt()`、`init_device()` |
| `frame_size` | 当前 sensor mode enum | default init、`s_fmt()`、`change_mode()` | `s_parm()`、`init_device()` |
| `powered` | 硬件是否处于可访问状态 | `power_on/off()` | `s_ctrl()`、`set_stream()`、PM |
| `streaming` | sensor 是否正在输出像素流 | `set_stream()`、`power_off()`、`init_device()` | `s_stream()`、`s_fmt()`、`s_parm()`、PM、AF |
| `prev_sysclk` / `prev_hts` | 预览模式曝光换算历史值 | `set_bandingfilter()` | `change_mode_exposure_calc()` |
| `ae_target` / `ae_high` / `ae_low` | AE 目标和稳定区间 | default init、`set_AE_target()` | mode switch / exposure calc |
| `night_mode` | 夜景模式缓存 | default init、mode init | mode switch |

普通视频流可以抓住这个状态机：

```text
default cache
  -> powered=false, streaming=false, 800x480 RGB565 30fps
  -> runtime_resume/init_device applies cache to hardware
  -> S_FMT/S_PARM modifies cache and maybe hardware
  -> STREAMON sets streaming=true
  -> STREAMOFF sets streaming=false
  -> runtime_suspend powers off, keeps cache
```

### 3.3 `struct ov5640_af_state`

关键字段：

| 字段 | 含义 |
| --- | --- |
| `firmware_loaded` | AF MCU 固件是否已经加载并 idle |
| `zone_pending` | zone/touch 设置是否还没下发到 firmware |
| `zone_mode` | default 或 touch |
| `touch_x_q16` / `touch_y_q16` | 按当前图像尺寸归一化后的触摸坐标 |
| `zone_result` | 单 AF 区域结果，1 表示 focused，0 表示 failed 或未完成 |

AF 状态和普通 stream 状态有两个强依赖：

- 没有 `powered`，不能访问 AF 寄存器。
- 没有 `streaming`，`AUTO_FOCUS_START` 返回 `-EPIPE`。

### 3.4 锁设计

当前驱动有一个 `mutex lock`，主要保护：

- `state.powered` / `state.streaming`。
- `state.frame_rate` / `state.frame_size` / `state.mbus_fmt` / `state.streamcap`。
- control 写硬件和 AF command。
- runtime PM resume/suspend 中的 power 和 init sequence。

典型模式：

```text
subdev/control/pm callback
  -> mutex_lock(&sensor->lock)
  -> check state
  -> write registers / update cache
  -> mutex_unlock()
```

`ov5640_s_stream()` 在 enable 路径里先无锁 runtime get，再加锁二次检查，是为了避免持锁进入 runtime PM 后和 PM callback 锁顺序冲突。这个地方是阅读竞态设计时最值得多看几遍的点。

## 4 主要数据表

### 4.1 格式表

当前 `ov5640_colour_fmts[]` 只有一个格式：

| mbus code | colorspace | 用户态常见 fourcc |
| --- | --- | --- |
| `MEDIA_BUS_FMT_RGB565_2X8_LE` | `V4L2_COLORSPACE_SRGB` | host 映射为 `V4L2_PIX_FMT_RGB565` |

因此所有格式协商最终都会被规整到 RGB565。

### 4.2 模式表

`ov5640_modes[]` 把每个离散分辨率绑定到：

- `frame_size` enum。
- `width` / `height`。
- `downsize` 类型：subsampling 或 scaling。
- analog crop / crop。
- timing 参数：`htot`、`vts_def`。
- 默认 fps。
- 该模式支持的 `ov5640_mode_reg_table[]`。

理解模式表的三个层次：

```text
ov5640_modes[]
  -> one mode: width/height/downsize/default_fps
  -> reg_tables: 15fps table, 30fps table, or only one of them
  -> reg_value[]: concrete sensor register writes
```

### 4.3 初始化表和模式表的关系

`init_device()` 的硬件重放顺序：

```text
ov5640_init_mode()
  -> soft reset
  -> global init table
  -> default VGA 30fps init table
  -> driver capability
  -> banding filter
  -> AE target
  -> night mode

then if cached mode != VGA 30fps:
  -> ov5640_change_mode(cached fps, cached frame_size)

then:
  -> ov5640_apply_format(cached mbus code)
  -> ov5640_apply_controls()
  -> ov5640_hw_set_stream(false)
```

这个顺序说明：**驱动把 VGA init 当作基础状态，再叠加用户缓存的目标模式**。

## 5 子系统专用 API 补充

| API / 结构体 | 出现位置 | 在本驱动中的作用 | 学习要点 |
| --- | --- | --- | --- |
| `struct v4l2_subdev` | `struct ov5640` | sensor 的 V4L2 子设备对象 | sensor driver 通常不直接建 `/dev/videoX`，而是由 host 组合成 video node |
| `v4l2_i2c_subdev_init()` | `ov5640_probe()` | 绑定 I2C client、subdev 和 ops | 它会设置 subdev data 和 clientdata |
| `struct v4l2_subdev_ops` | `ov5640_subdev_ops` | 总回调表，分 core/video/pad | host 通过 `v4l2_subdev_call()` 按类别调用 |
| `struct v4l2_subdev_video_ops` | `ov5640_subdev_video_ops` | 格式、帧率、stream 控制 | 当前使用旧式 `.s_mbus_fmt/.g_mbus_fmt/.try_mbus_fmt` |
| `struct v4l2_subdev_pad_ops` | `ov5640_subdev_pad_ops` | 枚举 frame size / interval | pad ops 更接近 media graph 端口能力 |
| `struct v4l2_ctrl_handler` | `ov5640_init_controls()` | 管理 controls 集合 | control 值由 core 缓存，driver 在 `.s_ctrl` 写硬件 |
| `struct v4l2_ctrl_ops` | `ov5640_ctrl_ops` | control set/get 回调 | volatile control 每次 G_CTRL 可能读硬件 |
| `request_firmware()` | `ov5640_af_load_firmware()` | 加载 AF firmware blob | rootfs 需安装 `ov5640_af.bin` |
| runtime PM ops | `ov5640_pm_ops` | 空闲自动掉电和按需恢复 | sensor 掉电后需要完整重放寄存器配置 |

## 6 通用 API 索引

| 分类 | API / 机制 | 出现位置 | 本驱动用途 | 建议追问 |
| --- | --- | --- | --- | --- |
| 设备模型/资源管理 | `devm_kzalloc` | `ov5640_probe()` | 分配私有对象，随 device 生命周期释放 | 是 |
| GPIO descriptor | `devm_gpiod_get` / `gpiod_set_value_cansleep` | probe、power/reset helpers | 获取并控制 PWDN/RESET GPIO | 是 |
| regmap | `devm_regmap_init_i2c` / `regmap_write/read/update_bits/raw_write` | register helpers、AF firmware | I2C register 访问抽象 | 是 |
| clock framework | `devm_clk_get` / `clk_set_rate` / `clk_prepare_enable` | probe、power_on/off | 管理 sensor MCLK | 是 |
| runtime PM | `pm_runtime_get_sync` / `pm_runtime_put_autosuspend` | power、stream、probe/remove | 延迟掉电和引用计数 | 是 |
| 并发保护 | `mutex` | state/control/PM paths | 保护缓存状态和寄存器写序列 | 是 |
| C 结构模式 | `container_of` | `sd_to_ov5640()`、ctrl callback | 从内嵌对象找回私有对象 | 是 |
| firmware | `request_firmware` / `release_firmware` | AF load | 从 rootfs 加载固件 | 视情况 |
| 延时 | `msleep` | reset、mode switch、poll AF ack | 满足硬件时序和等待稳定帧 | 视情况 |
| 错误码 | `-EINVAL` / `-EBUSY` / `-EPIPE` / `-ETIMEDOUT` | 多处 | 表达参数非法、运行中不可改、未上电/未出流、等待超时 | 视情况 |

## 7 典型用户态流程与 sensor 状态变化

### 7.1 发现能力

```text
open /dev/videoX
  -> host open
  -> sensor s_power(1)
  -> runtime_resume
  -> powered=true, streaming=false

VIDIOC_QUERYCAP
VIDIOC_ENUM_FMT
  -> sensor enum_mbus_fmt
VIDIOC_ENUM_FRAMESIZES
  -> sensor enum_frame_size
VIDIOC_ENUM_FRAMEINTERVALS
  -> sensor enum_frame_interval
VIDIOC_QUERYCTRL / G_CTRL
  -> ctrl core / sensor volatile ctrl

close
  -> sensor s_power(0)
  -> autosuspend
  -> powered=false
```

### 7.2 配置格式和帧率

```text
VIDIOC_TRY_FMT
  -> sensor try_mbus_fmt
  -> no hardware write

VIDIOC_S_FMT
  -> sensor s_mbus_fmt
  -> if powered: write mode + RGB565 + controls
  -> cache mbus_fmt/frame_size

VIDIOC_G_PARM
  -> read streamcap cache

VIDIOC_S_PARM
  -> normalize fps to 15/30
  -> if powered and changed: write mode + RGB565 + controls
  -> cache streamcap/frame_rate
```

### 7.3 MMAP streaming

```text
VIDIOC_REQBUFS
VIDIOC_QUERYBUF
mmap
VIDIOC_QBUF *
VIDIOC_STREAMON
  -> host starts VB2/CSI path
  -> sensor s_stream(1)
  -> streaming=true

poll
VIDIOC_DQBUF
consume frame
VIDIOC_QBUF

VIDIOC_STREAMOFF
  -> sensor s_stream(0)
  -> streaming=false
  -> runtime autosuspend allowed
```

### 7.4 AF 操作

```text
STREAMON must already be active

optional:
  S_CTRL af_touch_x
  S_CTRL af_touch_y
  S_CTRL af_zone_mode=touch

S_CTRL AUTO_FOCUS_START
  -> load firmware if needed
  -> apply zone
  -> trigger AF

loop:
  G_CTRL AUTO_FOCUS_STATUS
  G_CTRL af_zone_result

S_CTRL AUTO_FOCUS_STOP
  -> release focus
```

## 8 阅读缺口与验证建议

### 8.1 源码缺口

- 当前 `ov5640.c` 未看到 `of_match_table`，需要查当前内核 I2C core、module alias 和外置模块装载路径，确认 DTS compatible 如何触发绑定。
- V4L2 ioctl core 的参数复制和 control core 细节未在本文展开，可继续读 `drivers/media/v4l2-core/`。
- `mx6s_capture.c` 的 VB2/CSI IRQ 状态机不是本文主对象，但它决定 buffer 何时完成，建议另写一篇 host 驱动导读。
- OV5640 寄存器表来源、各寄存器 bit 语义和 AF firmware 协议需要 datasheet 或原厂资料补证。

### 8.2 本机最窄构建验证

```bash
bash buildscripts/build_and_deploy.sh drv ov5640
```

预期：

- `ov5640.ko` 和 `mx6s_capture.ko` 能完成交叉编译。
- 若脚本包含部署动作，按当前工程配置同步到目标 rootfs/板端位置。

### 8.3 板端功能验证

以下命令中的 `/dev/videoX` 按实际 video node 替换。

```bash
dmesg | grep -i ov5640
ov5640_interface_demo /dev/videoX --list
ov5640_interface_demo /dev/videoX --configure --width 800 --height 480 --fps 30
ov5640_interface_demo /dev/videoX --mmap --count 30
```

预期：

- `dmesg` 能看到 `camera ov5640, is found` 或相关 probe 成功日志。
- list 能枚举 RGB565、分辨率和帧间隔。
- configure 能完成 `TRY_FMT/S_FMT/G_PARM/S_PARM`。
- mmap capture 能连续 `DQBUF/QBUF`，画面或日志无明显错误。

AF 验证：

```bash
ls -l /lib/firmware/ov5640_af.bin
ov5640_test /dev/videoX
```

预期：

- firmware 文件存在。
- streaming 后触发 AF 不返回 `-EPIPE`。
- `AUTO_FOCUS_STATUS` 能从 BUSY 变化到 REACHED 或 FAILED。
- `af_zone_result` 只返回单 bit 结果：1 表示当前 default/touch zone focused，0 表示 failed 或未完成。

### 8.4 下一步阅读顺序

1. `ov5640_probe()`：先确认资源、默认状态和 subdev 注册。
2. `ov5640_init_default_state()` + `init_device()`：理解 cache 如何重放到硬件。
3. `ov5640_s_fmt()` / `ov5640_s_parm()` / `ov5640_change_mode()`：理解格式、帧率和寄存器表。
4. `ov5640_s_stream()` + runtime PM callbacks：理解出流和电源引用。
5. `ov5640_s_ctrl()` + AF helpers：理解 controls 和 firmware command。
6. `mx6s_capture.c` 的 `v4l2_ioctl_ops` / `vb2_ops`：把 sensor 状态机接到 video node 和 buffer 状态机。

## 9 快速记忆版

读这份驱动时，先把它压缩成五句话：

1. `ov5640.c` 是 I2C V4L2 subdev，不直接创建 `/dev/videoX`。
2. `mx6s_capture.c` 是 host/video node/VB2/CSI，用户 ioctl 通过它转发到 sensor。
3. `state.powered` 和 `state.streaming` 是最重要的两个运行态开关。
4. `S_FMT/S_PARM` 在未上电时改 cache，在已上电且未 streaming 时写寄存器。
5. AF 是独立固件状态机，必须 powered + streaming，掉电后固件状态失效。
