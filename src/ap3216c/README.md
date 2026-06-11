# AP3216C IIO 驱动与测试程序说明

本目录包含 AP3216C 三合一传感器的 Linux IIO 驱动和板端测试程序：

- `ap3216c.c`：I2C + regmap + IIO 驱动，提供 ALS 环境光、PS 接近检测、IR 原始值读取和阈值事件。
- `ap3216c_test.c`：用户态测试程序，通过 IIO sysfs 和 IIO event fd 验证驱动接口。

驱动注册后的 IIO 设备名为 `ap3216c`，通常出现在：

```bash
/sys/bus/iio/devices/iio:deviceX
/dev/iio:deviceX
```

可以用测试程序扫描：

```bash
ap3216c_test scan
```

## 构建与部署

通过 BSP 包构建：

```bash
bash bsp/build_and_deploy.sh drv ap3216c
```

验证包和模块：

```bash
bash bsp/build_and_deploy.sh verify nfs-pkg ap3216c
bash bsp/build_and_deploy.sh verify ko ap3216c
```

`bsp/package/ap3216c/ap3216c.mk` 会把测试程序安装到板端：

```bash
/usr/bin/ap3216c_test
```

也可以在本目录直接构建，但需要外部传入内核和交叉编译环境：

```bash
make KERNELDIR=<kernel-build-dir> ARCH=arm CROSS_COMPILE=<toolchain-prefix>
make app CC=<target-gcc>
```

## IIO 通道与 sysfs 接口

驱动提供 3 个 IIO 通道。

| 通道 | IIO 类型 | 主要 sysfs | 含义 |
| --- | --- | --- | --- |
| ALS | `IIO_LIGHT` | `in_illuminance_raw` | 16-bit 环境光原始值 |
| ALS | `IIO_LIGHT` | `in_illuminance_input` | 按当前 scale 换算后的 lux 值 |
| ALS | `IIO_LIGHT` | `in_illuminance_scale` | ALS 每 LSB 对应的 lux |
| PS | `IIO_PROXIMITY` | `in_proximity_raw` | 10-bit 接近传感器原始值 |
| IR | `IIO_INTENSITY`, `IIO_MOD_LIGHT_IR` | `in_intensity_ir_raw` | 10-bit 红外原始值 |

部分内核/IIO 命名可能带通道号，例如 `in_illuminance0_raw`。测试程序同时兼容带 `0` 和不带 `0` 的属性名。

ALS 支持 4 个 scale：

```text
0.315000
0.078800
0.019700
0.004900
```

这些值对应 AP3216C ALS 不同量程下的分辨率，单位为 lux/LSB。`in_illuminance_input` 的计算模型为：

```text
lux = in_illuminance_raw * in_illuminance_scale
```

PS 和 IR 读取时，如果硬件报告 overflow，驱动返回 `-EOVERFLOW`。用户态直接 `cat` sysfs 时通常会看到 I/O 错误。

## operating_mode

ALS 通道挂有 shared ext-info：

```bash
cat operating_mode_available
cat operating_mode
echo als_ps_ir > operating_mode
```

可选 mode：

| mode | 硬件模式 | 可读通道 |
| --- | --- | --- |
| `power_down` | 关闭采样 | 无 |
| `als` | 只开启 ALS | ALS |
| `ps_ir` | 开启 PS + IR | PS、IR |
| `als_ps_ir` | 开启 ALS + PS + IR | ALS、PS、IR |

驱动会根据当前 mode 拦截不活跃通道的读取。例如在 `als` mode 下读取 `in_proximity_raw` 会返回 `-EAGAIN`。

### mode 切换时序

当前实现中，`ap3216c_set_mode()` 写 `SYSTEM_CONFIG` 后固定等待 20 ms，然后返回。测试程序在 mode 切换后额外等待 50 ms 再读取。

需要注意：这个等待只表示软件路径延迟，不等价于“所有通道的数据寄存器一定已经刷新为新 mode 下的第一帧样本”。AP3216C 是 free-running 采样模型，mode 或 scale 改变后，数据寄存器可能仍短暂保留上一轮采样结果。

当前驱动的实际语义是：

- `read_raw`/`read_processed` 会检查通道是否在当前 mode 中 active。
- 读取不会保证“每次 read 都是新采样”。
- 连续两次快速读取同一通道，可能读到同一轮硬件采样值，这是正常的 IIO direct read 行为。
- mode 切换后只等待 20 ms，极端情况下首次读取仍可能看到旧样本。
- ALS scale 写入后驱动等待 100 ms，并且写入和读取共用 mutex，可以降低 ALS 正在运行时读到旧 scale 样本的概率。
- 如果 ALS 未运行时修改 scale，随后再切到 `als`，scale 写入阶段的 100 ms 并不代表 ALS 已完成新采样。

更完整的时序模型可以在驱动内部维护 `als_ready_jiffies`、`ps_ready_jiffies`、`ir_ready_jiffies`，在 mode/scale 这类会改变采样语义的配置写入后标记对应通道下一次可读时间。当前版本尚未实现该 ready 时间戳机制。

## 中断与事件语义

驱动支持 IIO threshold event，前提是设备树/I2C client 提供有效 IRQ。没有 IRQ 时，写事件 enable 会返回 `-ENODEV`。

事件 sysfs：

```bash
events/in_illuminance_thresh_rising_value
events/in_illuminance_thresh_falling_value
events/in_illuminance_thresh_rising_en
events/in_illuminance_thresh_falling_en

events/in_proximity_thresh_rising_value
events/in_proximity_thresh_falling_value
events/in_proximity_thresh_rising_en
events/in_proximity_thresh_falling_en
```

PS 还提供中断算法选择：

```bash
cat in_proximity_interrupt_algorithm_available
cat in_proximity_interrupt_algorithm
echo hysteresis > in_proximity_interrupt_algorithm
echo zone > in_proximity_interrupt_algorithm
```

### 阈值编程

驱动内部保存 ALS 和 PS 的 low/high 阈值：

- ALS 默认 low = `0`，high = `65535`。
- PS 默认 low = `100`，high = `200`。
- ALS 阈值合法范围：`0..65535`。
- PS 阈值合法范围：`0..1023`。
- low 不能大于 high。

启用/关闭事件或修改阈值后，驱动会重新写硬件阈值寄存器，并通过读取 ALS/PS 数据寄存器清除旧 pending 状态。中断清除方式配置为 `AP3216C_INT_CLEAR_BY_READ`。

当某个方向未启用时，驱动会把对应硬件阈值放宽，减少无意义中断：

- 未启用 ALS falling 时，ALS low 使用 `0`。
- 未启用 ALS rising 时，ALS high 使用 `65535`。
- PS 任一事件启用时会写入当前 PS low/high。

### IRQ 处理流程

驱动申请 threaded IRQ：

```text
IRQF_ONESHOT | IRQF_TRIGGER_LOW
```

IRQ 线程处理逻辑：

1. 读取 `AP3216C_INT_STATUS`，只关心 ALS/PS 中断位。
2. 根据当前 `operating_mode` 判断 active 通道。
3. 读取 active 通道的数据寄存器。因为硬件配置为 read-to-clear，这一步同时清除 pending 状态。
4. 如果 status 包含 ALS bit 且 ALS active，则根据当前 ALS raw 与阈值比较推送事件：
   - `raw > high` 推送 `IIO_EV_DIR_RISING`
   - `raw < low` 推送 `IIO_EV_DIR_FALLING`
   - 在 `[low, high]` 内不推送事件
5. 如果 status 包含 PS bit 且 PS active，则根据样本中的 object 标志推送事件：
   - `ps_object = true` 推送 `IIO_EV_DIR_RISING`
   - `ps_object = false` 推送 `IIO_EV_DIR_FALLING`
   - PS overflow 时跳过事件并限频打印 warning

因此 ALS 事件方向由驱动对 raw 和阈值的比较决定；PS 事件方向主要由硬件 object 状态决定，阈值用于硬件接近检测判定。

### 中断时序注意点

mode、scale、阈值和 enable 改变后，硬件可能处在新旧采样交界。当前驱动已经在阈值/enable/PS 算法变更后读取数据寄存器来清旧 pending event，但 mode 切换仍只等待 20 ms，没有统一的“数据 ready 时间”概念。

如果测试中发现刚切 mode 或刚改 scale 后第一条事件/读数异常，应优先考虑采样周期造成的旧样本残留，而不是立即判断为阈值比较错误。

## 测试程序用法

测试程序入口：

```bash
ap3216c_test scan
ap3216c_test test1 [auto|iio:deviceX|N|/sys/bus/iio/devices/iio:deviceX]
ap3216c_test test2 [auto|iio:deviceX|N|/sys/bus/iio/devices/iio:deviceX]
ap3216c_test irq [device] [options]
```

设备参数说明：

- 省略或写 `auto`：按 IIO name 自动查找 `ap3216c`。
- `iio:deviceX`：指定 IIO 设备名。
- `N`：指定 `/sys/bus/iio/devices/iio:deviceN`。
- `/sys/...`：直接指定 IIO sysfs 目录。
- `ap3216c`：按 name 查找设备。

测试程序执行前会保存当前 mode、事件 enable、阈值、scale 等状态；结束时尽量恢复。测试过程中按 `Ctrl-C` 可中断 IRQ 等待流程。

### scan

扫描所有 IIO 设备：

```bash
ap3216c_test scan
```

输出示例：

```text
iio:device0  name=ap3216c  *
```

带 `*` 的设备是测试程序识别出的 AP3216C。

### test1：mode 与接口矩阵测试

执行：

```bash
ap3216c_test test1
ap3216c_test mode iio:device0
```

测试内容：

1. 保存当前 `operating_mode` 和事件 enable 状态。
2. 关闭 ALS/PS event，避免测试读取时混入旧中断状态。
3. 依次切换 `power_down`、`als`、`ps_ir`、`als_ps_ir`。
4. 每次切换后等待 50 ms。
5. 读取并打印以下接口矩阵：
   - `name`
   - `operating_mode`
   - `operating_mode_available`
   - ALS raw/input/scale
   - IR raw
   - PS raw
   - PS interrupt_algorithm
   - ALS/PS event value 和 enable
6. 恢复原始 mode 和事件 enable。

这个测试主要用于确认：

- mode 枚举是否正常。
- 不同 mode 下通道 active/inactive 行为是否符合预期。
- IIO sysfs 属性名是否生成正确。
- 事件属性是否存在。

### test2：ALS scale 与 processed 测试

执行：

```bash
ap3216c_test test2
ap3216c_test als 0
```

测试内容：

1. 保存当前 mode、ALS scale 和事件 enable 状态。
2. 关闭事件。
3. 切换到 `als` mode，并等待 50 ms。
4. 对 4 个 ALS scale 重复写入并读取 `in_illuminance_input`。
5. 一共采 10 组，打印 scale x group 表格。
6. 恢复原始 scale、mode 和事件 enable。

这个测试主要用于确认：

- `in_illuminance_scale` 是否接受 4 个合法值。
- 修改 scale 后 `in_illuminance_input` 是否随 scale 变化。
- scale 写入后的 100 ms 等待是否足以避免明显的旧 scale 读数。

注意：测试程序读取的是 processed lux，不读取 raw 后自己换算。若环境光稳定，scale 越小，processed 值通常按比例变化；若环境光变化或刚切 mode，首组数据可能受采样刷新时序影响。

### irq：IIO event 测试

执行：

```bash
ap3216c_test irq
ap3216c_test irq iio:device0 --seconds 60
ap3216c_test irq --als rising --ps off --als-low 0 --als-high 100
ap3216c_test irq --als off --ps both --ps-low 100 --ps-high 200 --ps-algo hysteresis
```

参数：

| 参数 | 含义 | 默认 |
| --- | --- | --- |
| `--als off|rising|falling|both` | ALS 事件方向 | `both` |
| `--ps off|rising|falling|both` | PS 事件方向 | `both` |
| `--als-low N` | ALS falling 阈值，范围 `0..65535` | 当前值或 `0` |
| `--als-high N` | ALS rising 阈值，范围 `0..65535` | 当前值或 `65535` |
| `--ps-low N` | PS falling 阈值，范围 `0..1023` | 当前值或 `0` |
| `--ps-high N` | PS rising 阈值，范围 `0..1023` | 当前值或 `1023` |
| `--ps-algo zone|hysteresis` | PS 中断算法 | 保持当前值 |
| `--seconds N` | 等待中断秒数，范围 `1..3600` | `30` |
| `--event-limit N` | 最多读取事件数 | `128` |

测试内容：

1. 保存当前 mode、PS 算法、事件 enable 和阈值。
2. 打开 `/dev/iio:deviceX`，通过 `IIO_GET_EVENT_FD_IOCTL` 获取 event fd。
3. 关闭所有事件。
4. 切换到 `als_ps_ir` mode，并等待 50 ms。
5. 按参数写入 PS 算法和 ALS/PS 阈值。
6. 启用指定方向的 ALS/PS event。
7. `poll()` event fd，收到事件后打印：
   - source：ALS 或 PS
   - dir：rising 或 falling
   - type：thresh
   - raw：事件发生后即时读取的 ALS/PS raw
   - timestamp
   - event id
8. 超时、达到 event limit 或按 `Ctrl-C` 后恢复原始配置。

输出示例：

```text
event fd opened from /dev/iio:device0
irq config: ALS=both low=0 high=100, PS=off low=0 high=1023
waiting for interrupts for 30 seconds, press Ctrl-C to stop
interrupt: source=ALS dir=rising type=thresh chan=0 raw=123 timestamp=... id=0x...
irq test: received 1 interrupt event(s)
```

IRQ 测试返回值：

- 收到至少 1 个 event：返回 `0`。
- 未收到 event 或配置失败：返回非 `0`。

## 常见手工调试命令

先定位设备：

```bash
ap3216c_test scan
cd /sys/bus/iio/devices/iio:deviceX
```

查看 mode：

```bash
cat operating_mode_available
cat operating_mode
```

读取 ALS：

```bash
echo als > operating_mode
sleep 0.1
cat in_illuminance_raw
cat in_illuminance_scale
cat in_illuminance_input
```

读取 PS/IR：

```bash
echo ps_ir > operating_mode
sleep 0.1
cat in_proximity_raw
cat in_intensity_ir_raw
```

启用 ALS rising event：

```bash
echo als_ps_ir > operating_mode
echo 100 > events/in_illuminance_thresh_rising_value
echo 1 > events/in_illuminance_thresh_rising_en
ap3216c_test irq --als rising --ps off --als-high 100
```

测试 PS 接近事件：

```bash
echo als_ps_ir > operating_mode
echo hysteresis > in_proximity_interrupt_algorithm
ap3216c_test irq --als off --ps both --ps-low 100 --ps-high 200
```

## 当前实现边界

- 驱动只注册 `INDIO_DIRECT_MODE`，没有 IIO buffer/trigger 采样流。
- ALS/PS event 依赖硬件 IRQ；无 IRQ 时只能读取 raw/input/scale。
- mode 切换等待固定 20 ms，不保证第一帧一定是新 mode 下的新样本。
- scale 写入等待固定 100 ms，只覆盖 ALS 已经运行时的大部分场景。
- `read_raw` 是直接寄存器读取，不保证每次用户态读取都对应新的硬件采样周期。
- IRQ 线程读取 active 通道来清 pending event；mode/scale 刚变化时仍可能遇到新旧采样交界。

