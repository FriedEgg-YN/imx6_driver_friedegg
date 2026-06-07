# AP3216C IIO 驱动说明

本目录中的 `ap3216c.ko` 已从 legacy cdev 驱动迁移为 IIO 驱动。加载后不再提供 `/dev/ap3216c`、`read()` 和 AP3216C 私有 ioctl，用户态统一通过 IIO sysfs 与 `/dev/iio:deviceX` event fd 访问。

## 构建

IIO 驱动需要内核打开 `CONFIG_IIO=y`。本项目的 `bsp/configs/linux.config` 已包含该选项。

```sh
bash bsp/build_and_deploy.sh drv ap3216c
bash bsp/build_and_deploy.sh verify ko ap3216c
```

如果是第一次从非 IIO 内核配置切换过来，需要重新部署新的 `zImage`，否则板端内核没有 IIO core：

```sh
bash bsp/build_and_deploy.sh zimage
```

## IIO 设备

板端加载模块：

```sh
rmmod ap3216c 2>/dev/null || true
modprobe ap3216c
```

查找设备：

```sh
for d in /sys/bus/iio/devices/iio:device*; do
    printf '%s: ' "$d"
    cat "$d/name"
done
```

`name` 为 `ap3216c` 的目录就是本传感器，例如 `/sys/bus/iio/devices/iio:device0`。

## Raw 与 Scale

常用 sysfs 文件：

```text
name
operating_mode
operating_mode_available
in_illuminance_raw
in_illuminance_scale
in_illuminance_scale_available
in_illuminance_calibscale
in_intensity_ir_raw
in_proximity_raw
```

`operating_mode` 可写值：

```text
power_down
als
ps_ir
als_ps_ir
```

默认模式是 `als_ps_ir`。如果当前模式未开启某个通道，读取对应 raw 可能返回 `EAGAIN`。

ALS lux 换算由用户态完成：

```text
lux = in_illuminance_raw * in_illuminance_scale * in_illuminance_calibscale
```

## Events

事件阈值在 `events/` 目录下：

```text
events/in_illuminance_thresh_rising_value
events/in_illuminance_thresh_rising_en
events/in_illuminance_thresh_falling_value
events/in_illuminance_thresh_falling_en
events/in_proximity_thresh_rising_value
events/in_proximity_thresh_rising_en
events/in_proximity_thresh_falling_value
events/in_proximity_thresh_falling_en
```

ALS rising 表示光照 raw 高于 high threshold，falling 表示低于 low threshold。PS rising 表示 near/high，falling 表示 away/low。

事件读取使用 `/dev/iio:deviceX` 的 IIO event fd。可以直接用本包安装的测试程序：

```sh
ap3216c_test scan
ap3216c_test read [auto|iio:deviceX|N|/sys/...]
ap3216c_test readloop [auto|iio:deviceX|N|/sys/...] [interval_ms]
ap3216c_test fulltest [auto|iio:deviceX|N|/sys/...]
ap3216c_test eventoff [auto|iio:deviceX|N|/sys/...]
ap3216c_test eventtest [auto|iio:deviceX|N|/sys/...] [seconds]
```

**参数说明**：
- `[auto|iio:deviceX|N|/sys/...]`：设备指定参数。不写或传入 `auto` 则自动寻找设备；也可直接指定设备号 `N` (如 `0`) 或完整路径。
- `[interval_ms]`：`readloop` 获取数据的时间间隔，单位为毫秒。
- `[seconds]`：`eventtest` 持续的测试时间（秒数）。例如：`ap3216c_test eventtest auto 30` 为自动寻找设备并测试 30 秒。

`eventtest` 会先关闭旧事件、等待一轮新样本，再围绕当前 ALS/PS raw 值设置一个窗口。测试时手动改变光照或靠近/远离传感器即可触发事件；结束或 `Ctrl-C` 时会自动关闭事件。如果板端曾经被旧测试程序留下持续中断，先执行：

```sh
ap3216c_test eventoff
```

## 旧接口映射

| 旧 cdev 接口 | 新 IIO 接口 |
| --- | --- |
| `/dev/ap3216c` | `/sys/bus/iio/devices/iio:deviceX` 和 `/dev/iio:deviceX` |
| `read(struct ap3216c_sample)` | `in_illuminance_raw`、`in_intensity_ir_raw`、`in_proximity_raw` |
| `AP3216C_CMD_SET_MODE` | `operating_mode` |
| `AP3216C_CMD_SET_ALS_RANGE` | `in_illuminance_scale` |
| `AP3216C_CMD_SET_ALS_TH` | `events/in_illuminance_thresh_*_value` |
| `AP3216C_CMD_SET_PS_TH` | `events/in_proximity_thresh_*_value` |
| `AP3216C_CMD_SET_EVENT_MASK` | `events/*_en` |
| `AP3216C_CMD_GET_STATS` | debugfs stats，仅调试用途 |

## Debugfs Stats

驱动中保留了可选 stats 代码。只有编译时定义 `CONFIG_AP3216C_STATS` 且内核启用 `CONFIG_DEBUG_FS` 时，IIO debugfs 目录下才会出现 AP3216C stats 文件。stats 不是稳定 ABI，只用于调试和学习。

```sh
mount -t debugfs none /sys/kernel/debug 2>/dev/null || true
find /sys/kernel/debug/iio -maxdepth 3 -type f -name 'stats*'
```
