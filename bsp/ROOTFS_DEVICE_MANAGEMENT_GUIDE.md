# Rootfs 设备管理修复说明（devtmpfs + mdev）

本文档说明本次针对 AP3216C 目标系统的 rootfs 修复：从仅依赖 devtmpfs 的方式，升级为 devtmpfs + mdev 的动态设备管理方案。

## 问题现象

在板端测试时，执行 `modprobe ap3216c` 后没有自动生成对应的 `/dev/ap3216c`，只能手工挂载虚拟文件系统并使用 `mknod` 创建设备节点。

## 根因判断

当前 Buildroot rootfs 只启用了 `devtmpfs`，没有启用 `mdev` 启动链路：

- `devtmpfs` 依赖内核自动挂载和设备创建事件。
- `mdev` 负责在用户态处理冷插拔、补充节点创建和设备权限管理。
- 当目标系统没有完整的动态设备管理链路时，`modprobe` 成功并不保证 `/dev` 节点一定可见。

对于本项目，这个问题会直接影响应用层测试，尤其是 `ap3216cApp` 依赖 `/dev/ap3216c` 打开设备。

## 本次修改

在 [bsp/configs/imx6ull_friedegg_emmc_defconfig](bsp/configs/imx6ull_friedegg_emmc_defconfig) 中开启：

```text
BR2_ROOTFS_DEVICE_CREATION_DYNAMIC_MDEV=y
```

这样 Buildroot 在生成 rootfs 时会自动：

- 安装 BusyBox 的 `S10mdev` 启动脚本
- 安装默认 `mdev.conf`
- 在启动阶段执行 `mdev -df`
- 完成冷插拔扫描和节点补全

## 为什么这样改

### 1. 保留 devtmpfs 的低开销优势

devtmpfs 仍然是基础设备节点来源，保留了内核侧的自动创建设备能力。

### 2. 用 mdev 补齐启动和冷插拔

`mdev` 的作用不是替代驱动 probe，而是把“设备出现后该如何在 `/dev` 中可见”这件事收口到统一链路。

### 3. 降低环境差异造成的误判

如果 rootfs 只靠手工挂载和 `mknod`，很容易把“设备节点管理问题”和“驱动 probe / IRQ 问题”混在一起。启用 `mdev` 后，节点问题更容易被排除。

## 验证方式

在目标板上执行以下检查：

1. 查看 `/dev` 是否已经由 `devtmpfs` 挂载：

```sh
mount | grep ' on /dev '
```

2. 查看 `mdev` 启动脚本是否存在：

```sh
ls -l /etc/init.d/S10mdev /etc/mdev.conf
```

3. 加载模块并检查节点：

```sh
modprobe ap3216c
ls -l /dev/ap3216c
```

4. 查看驱动日志：

```sh
dmesg | grep -i ap3216c
```

## 最佳实践

- 保持 kernel 的 `CONFIG_DEVTMPFS` 和 `CONFIG_DEVTMPFS_MOUNT` 打开。
- rootfs 侧优先用动态设备管理，不要长期依赖手工 `mknod`。
- 先确认 `/dev` 节点链路，再判断 IRQ、I2C、DTS 是否有问题。
- 如果后续还有更多热插拔设备，可以继续在 `mdev.conf` 中补充权限规则。

## 和 AP3216C IRQ 的关系

这个修改只负责设备节点创建，不会修复真实的中断连线、GPIO 触发类型或驱动内的 `request_threaded_irq()` 失败。

因此后续排查顺序应是：

1. 先确认 `/dev/ap3216c` 自动出现
2. 再看 `probe` 和 `request irq` 日志
3. 最后判断中断是硬件链路问题还是 DTS 配置问题

这样可以把 rootfs 问题和 IRQ 问题分离开，避免重复试错。