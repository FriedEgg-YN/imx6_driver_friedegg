# alias 与 mdev 自动加载 .ko

本文说明 Linux 模块自动加载中两类 alias 的来源、生成链路和板端排查路径。

## 两类 alias

模块自动加载涉及两个方向：

- 设备侧 modalias：内核为已经枚举出来的设备生成的匹配字符串，出现在 `/sys/.../modalias` 或 uevent 环境变量 `MODALIAS=...` 中。
- 模块侧 alias：驱动模块声明自己能匹配哪些设备，最终由 `depmod` 汇总到 `/lib/modules/$(uname -r)/modules.alias`。

自动加载成立的条件是：

```text
设备侧 modalias 能匹配 modules.alias 中某一条模块侧 alias 规则
```

例如：

```text
/sys/.../modalias: i2c:ov5640
modules.alias:      alias i2c:ov5640 ov5640
结果:               modprobe i2c:ov5640 会加载 ov5640.ko
```

## 驱动源码到 modules.alias

以 I2C 驱动为例，驱动里写：

```c
static const struct i2c_device_id xxx_id[] = {
    { "xxx", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, xxx_id);
```

`MODULE_DEVICE_TABLE()` 定义在 `src/linux-friedegg/include/linux/module.h`。它的作用不是直接写出 `modules.alias`，而是在模块目标文件中生成一个特殊符号：

```text
__mod_i2c__xxx_id_device_table
```

Kbuild 的 `modpost` 阶段会扫描这类 `__mod_<bus>__..._device_table` 符号。源码入口在：

```text
src/linux-friedegg/scripts/Makefile.modpost
src/linux-friedegg/scripts/mod/file2alias.c
```

`file2alias.c` 会按总线类型把匹配表转换为模块 alias：

```text
i2c_device_id   -> i2c:<name>
of_device_id    -> of:N<name>T<type>C<compatible>*
input_device_id -> input:...
pci_device_id   -> pci:...
usb_device_id   -> usb:...
```

对 I2C 来说，`do_i2c_entry()` 根据 `struct i2c_device_id.name` 生成：

```text
alias=i2c:<name>
```

对 OF/Device Tree 来说，`do_of_entry()` 根据 `struct of_device_id.compatible` 生成：

```text
alias=of:N*T*C<compatible>*
```

这些 `alias=` 字符串进入 `.ko` 的 `.modinfo` 区。可以在主机或板端用 `modinfo` 看到：

```sh
modinfo ov5640.ko
modinfo mx6s_capture.ko
modinfo ap3216c.ko
modinfo gt9147.ko
```

之后 `depmod` 扫描 `/lib/modules/$(uname -r)` 下的 `.ko`，读取 `.modinfo` 中的 `alias=` 和 `depends=`，生成：

```text
/lib/modules/$(uname -r)/modules.alias
/lib/modules/$(uname -r)/modules.alias.bin
/lib/modules/$(uname -r)/modules.dep
/lib/modules/$(uname -r)/modules.dep.bin
```

`modules.alias` 中每行格式是：

```text
alias <alias-pattern> <module-name>
```

例如：

```text
alias i2c:ov5640 ov5640
alias of:N*T*Cfsl,imx6s-csi* mx6s_capture
```

含义是：

```text
modprobe i2c:ov5640          -> 加载 ov5640.ko
modprobe of:N*T*Cfsl,...     -> 加载 mx6s_capture.ko
```

## 设备侧 modalias 的来源

### I2C + Device Tree

I2C 设备由 DTS 子节点枚举时，生成链路是：

```text
DTS compatible
  -> of_modalias_node()
  -> i2c_board_info.type
  -> i2c_client.name
  -> /sys/bus/i2c/devices/.../modalias
  -> uevent MODALIAS=i2c:<client->name>
```

关键源码：

```text
src/linux-friedegg/drivers/i2c/i2c-core.c
src/linux-friedegg/drivers/of/base.c
src/linux-friedegg/include/linux/mod_devicetable.h
```

以 OV5640 为例，DTS 是：

```dts
compatible = "ovti,ov5640";
```

`of_modalias_node()` 会去掉厂商前缀 `ovti,`，留下：

```text
ov5640
```

I2C core 把它保存为 `client->name`，I2C 的 sysfs `modalias` 再拼接 `I2C_MODULE_PREFIX`：

```text
i2c: + ov5640 = i2c:ov5640
```

所以板端会看到：

```sh
cat /sys/bus/i2c/devices/*-003c/modalias
```

预期：

```text
i2c:ov5640
```

AP3216C 和 GT9147 同理：

```text
compatible = "alientek,ap3216c" -> 设备侧 modalias: i2c:ap3216c
compatible = "goodix,gt9147"    -> 设备侧 modalias: i2c:gt9147
```

因此 I2C 驱动的 `i2c_device_id` 通常应该使用去掉厂商前缀后的名字：

```c
{ "ap3216c", 0 }
{ "gt9147", 0 }
```

如果写成：

```c
{ "alientek,ap3216c", 0 }
{ "goodix,gt9147", 0 }
```

`depmod` 会生成：

```text
alias i2c:alientek,ap3216c ap3216c
alias i2c:goodix,gt9147 gt9147
```

这和板端设备侧的 `i2c:ap3216c`、`i2c:gt9147` 不匹配，mdev 开机自动加载就会失败。

### Platform/OF 设备

platform 设备如果带 `of_node`，会优先生成 OF 风格 modalias。

以 MX6S CSI 为例：

```dts
compatible = "fsl,imx6ul-csi", "fsl,imx6s-csi";
```

驱动中有：

```c
static const struct of_device_id mx6s_csi_dt_ids[] = {
    { .compatible = "fsl,imx6s-csi" },
    { }
};
MODULE_DEVICE_TABLE(of, mx6s_csi_dt_ids);
```

模块侧 alias 会包含：

```text
alias of:N*T*Cfsl,imx6s-csi* mx6s_capture
```

platform bus 的 modalias 逻辑在：

```text
src/linux-friedegg/drivers/base/platform.c
src/linux-friedegg/drivers/of/device.c
```

## uevent 与热插拔通知

内核 uevent 是 kernel object event：当设备、总线、驱动等内核对象发生新增、删除或状态变化时，内核向用户态广播一组 `key=value` 环境变量。用户态的 `mdev` 或 `udev` 监听这些事件，完成创建设备节点、加载固件、根据 `MODALIAS` 自动 `modprobe` 等工作。

典型热插拔事件内容类似：

```text
ACTION=add
DEVPATH=/devices/platform/...
SUBSYSTEM=i2c
MODALIAS=i2c:ap3216c
SEQNUM=123
```

内核发出设备新增事件的主链路是：

```text
device_add()
  -> kobject_uevent(&dev->kobj, KOBJ_ADD)
  -> kobject_uevent_env()
  -> devices_kset 的 dev_uevent()
  -> dev->bus->uevent()
  -> dev->class->dev_uevent()
  -> dev->type->uevent()
  -> add_uevent_var(env, "MODALIAS=...")
  -> NETLINK_KOBJECT_UEVENT 广播给用户态
```

关键源码：

```text
src/linux-friedegg/drivers/base/core.c
src/linux-friedegg/lib/kobject_uevent.c
src/linux-friedegg/include/uapi/linux/netlink.h
```

`dev_uevent()` 会先加入通用字段，例如 `MAJOR`、`MINOR`、`DEVNAME`、`DEVTYPE`、`DRIVER` 和 DT 通用信息；然后调用 bus/class/type 自己的 uevent 回调，让具体总线补充自己的字段。

对 I2C 设备，这个 4.1 内核中 `MODALIAS` 是由 `i2c_client_type.uevent = i2c_device_uevent` 补充的。`i2c_device_uevent()` 根据 `client->name` 生成：

```text
MODALIAS=i2c:<client->name>
```

对 platform/OF 设备，`platform_bus_type.uevent = platform_uevent`，`platform_uevent()` 优先调用 `of_device_uevent_modalias()`，根据 `dev->of_node` 的 `compatible` 生成：

```text
MODALIAS=of:N...T...C...
```

因此“设备热插拔时 bus 如何知道 modalias”的答案是：设备枚举阶段已经把设备名、地址、`of_node`、`compatible` 等信息保存到 bus 私有设备结构里；发 uevent 时，驱动核心调用对应 bus/type 的 uevent 回调，该回调从这些结构中取值，并用 `add_uevent_var()` 把 `MODALIAS=...` 加进事件环境变量。

## mdev 启动时如何触发 modprobe

当前 rootfs 的启动脚本是：

```text
/etc/init.d/S10mdev
```

仓库中对应：

```text
buildroot/output/target/etc/init.d/S10mdev
```

启动时先运行 mdev daemon：

```sh
start-stop-daemon -S -b -m -p /var/run/mdev.pid -x /sbin/mdev -- -df
```

这个命令由 BusyBox init 脚本调用，用 `start-stop-daemon` 启动 `/sbin/mdev`：

```text
-S: start，启动服务。
-b: background，把进程放到后台运行。
-m: make pidfile，创建 pid 文件。
-p /var/run/mdev.pid: 指定 pid 文件路径。
-x /sbin/mdev: 指定要启动的可执行程序。
--: 后面的参数不再属于 start-stop-daemon，而是传给 /sbin/mdev。
-d: mdev daemon 模式，监听内核 uevent netlink。
-f: mdev 前台运行；这里配合 start-stop-daemon -b，由 start-stop-daemon 负责后台化和记录 pid。
```

也就是：

```text
start-stop-daemon 负责把 mdev 启成后台服务并维护 pid 文件；
mdev -df 负责以前台 daemon 形式监听 NETLINK_KOBJECT_UEVENT。
```

daemon 模式监听内核 uevent。内核热插拔事件中如果带有：

```text
ACTION=add
DEVPATH=...
SUBSYSTEM=...
MODALIAS=i2c:ov5640
```

mdev 会读取：

```text
/etc/mdev.conf
```

仓库中对应：

```text
buildroot/output/target/etc/mdev.conf
```

当前规则是：

```text
$MODALIAS=.* root:root 660 @modprobe "$MODALIAS"
```

含义是：

```text
如果环境变量 MODALIAS 存在并匹配 .*
并且当前是 add 事件，因为命令前缀是 @
就执行 modprobe "$MODALIAS"
```

BusyBox mdev 的 `$ENV=regex` 规则匹配和命令执行在：

```text
buildroot/output/build/busybox-1.37.0/util-linux/mdev.c
```

## coldplug 扫描

开机时很多设备可能已经被内核枚举完成，不一定会重新产生热插拔事件。因此 `S10mdev` 还做了一次 coldplug：

```sh
find /sys/ -name modalias -print0 |
    xargs -0 sort -u |
    tr '\n' '\0' |
    xargs -0 modprobe -abq
```

作用是：

```text
扫描所有 /sys/**/modalias
去重
逐个执行 modprobe -a -b -q <modalias>
```

这样即使某个设备的 add uevent 早于用户态 mdev daemon 启动，也可以在启动脚本阶段补加载模块。

## modprobe 如何匹配 modules.alias

BusyBox `modprobe` 启动后会进入：

```text
/lib/modules/$(uname -r)
```

然后读取：

```text
modules.dep
modules.alias
modules.builtin
```

当执行：

```sh
modprobe i2c:ov5640
```

时，`modprobe` 会：

```text
1. 先把 i2c:ov5640 当作待解析名字
2. 读取 modules.dep，尝试按真实模块名解析
3. 如果没有解析出来，再读取 modules.alias
4. 用 fnmatch() 匹配 alias pattern
5. 找到 alias i2c:ov5640 ov5640
6. 把真实模块名 ov5640 加入加载队列
7. 根据 modules.dep 找到 ov5640.ko 路径并 init_module
```

BusyBox modprobe 源码在：

```text
buildroot/output/build/busybox-1.37.0/modutils/modprobe.c
```

## 板端 alias 路径

板端模块索引路径：

```text
/lib/modules/$(uname -r)/modules.alias
/lib/modules/$(uname -r)/modules.alias.bin
/lib/modules/$(uname -r)/modules.dep
/lib/modules/$(uname -r)/modules.dep.bin
/lib/modules/$(uname -r)/modules.builtin
```

板端外置驱动模块常见路径：

```text
/lib/modules/$(uname -r)/extra/ap3216c.ko
/lib/modules/$(uname -r)/extra/gt9147.ko
/lib/modules/$(uname -r)/extra/ov5640.ko
/lib/modules/$(uname -r)/extra/mx6s_capture.ko
```

板端设备侧 modalias 路径：

```text
/sys/bus/i2c/devices/<bus>-001e/modalias    # AP3216C
/sys/bus/i2c/devices/<bus>-0014/modalias    # GT9147
/sys/bus/i2c/devices/<bus>-003c/modalias    # OV5640
/sys/bus/platform/devices/<device>/modalias # platform/OF 设备
```

实际 bus 编号可能不同，可用通配符查看：

```sh
cat /sys/bus/i2c/devices/*-001e/modalias
cat /sys/bus/i2c/devices/*-0014/modalias
cat /sys/bus/i2c/devices/*-003c/modalias
find /sys/bus/platform/devices -maxdepth 2 -name modalias -exec sh -c 'echo "$1: $(cat "$1")"' sh {} \;
```

查看当前 rootfs 模块 alias：

```sh
grep -E 'ap3216c|gt9147|ov5640|mx6s|evbug' /lib/modules/$(uname -r)/modules.alias
```

查看模块自身导出的 alias：

```sh
modinfo ap3216c
modinfo gt9147
modinfo ov5640
modinfo mx6s_capture
```

查看 modprobe 解析结果：

```sh
modprobe -D i2c:ap3216c
modprobe -D i2c:gt9147
modprobe -D i2c:ov5640
```

## 本项目排查要点

OV5640 能自动加载的原因：

```text
DTS compatible = "ovti,ov5640"
设备侧 modalias = i2c:ov5640
模块侧 alias = alias i2c:ov5640 ov5640
二者匹配
```

MX6S CSI 能自动加载的原因：

```text
DTS compatible 包含 "fsl,imx6s-csi"
模块侧 alias 包含 of:N*T*Cfsl,imx6s-csi*
二者匹配
```

AP3216C 和 GT9147 如果不能通过 mdev 自动加载，优先检查：

```sh
cat /sys/bus/i2c/devices/*-001e/modalias
cat /sys/bus/i2c/devices/*-0014/modalias
grep -E 'ap3216c|gt9147' /lib/modules/$(uname -r)/modules.alias
```

期望匹配关系是：

```text
i2c:ap3216c -> alias i2c:ap3216c ap3216c
i2c:gt9147  -> alias i2c:gt9147 gt9147
```

如果 `modules.alias` 中只有：

```text
alias i2c:alientek,ap3216c ap3216c
alias i2c:goodix,gt9147 gt9147
```

说明驱动的 `i2c_device_id` 使用了带厂商前缀的名字。应把 I2C id table 改为去厂商前缀的设备名，同时保留 OF match table 中的标准 `compatible`。

## 总结链路

```text
驱动 MODULE_DEVICE_TABLE
 -> modpost/file2alias 生成 .ko 的 alias=
 -> depmod 生成 /lib/modules/$(uname -r)/modules.alias
 -> 内核为设备生成 /sys/.../modalias 或 uevent MODALIAS
 -> mdev 把 modalias 交给 modprobe
 -> modprobe 用 modules.alias 反查 .ko 并加载
```
