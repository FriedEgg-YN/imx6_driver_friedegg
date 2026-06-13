# GT9147 触摸驱动

本目录维护 GT9147 触摸控制器的外置内核模块。

| 项 | 内容 |
| --- | --- |
| Buildroot 包 | `bsp/package/gt9147/` |
| 源码目录 | `src/gt9147/` |
| 内核模块 | `gt9147.ko` |
| 构建类型 | `kernel-module` |
| 模块工具链 | `BSP_KERNEL_CROSS_COMPILE` 指向的 Linaro 4.9 |

## 构建与部署

只改本目录驱动源码：

```bash
NFS_DIR=<nfs-dir> bash buildscripts/build_and_deploy.sh drv gt9147
```

如果同时改了触摸相关 DTS/DTSI：

```bash
TFTP_DIR=<tftp-dir> bash buildscripts/build_and_deploy.sh dtb
```

删除或重命名模块后，显式清理 NFS 旧文件：

```bash
NFS_DIR=<nfs-dir> bash buildscripts/pkg_clean_stale.sh gt9147
```

## 板端验证

```bash
dmesg | grep -i -E 'gt9147|goodix|input|touch'
modprobe gt9147
cat /proc/bus/input/devices
ls -l /dev/input/event*
```

如果能看到触摸 input 设备，再根据当前 rootfs 中可用工具读取对应 `/dev/input/eventX` 验证触摸事件。
