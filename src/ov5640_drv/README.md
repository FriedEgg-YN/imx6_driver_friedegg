# OV5640 V4L2 驱动与调试记录

本目录维护 OV5640 摄像头相关外置驱动和板端测试程序。

| 项 | 内容 |
| --- | --- |
| Buildroot 包 | `bsp/package/ov5640_drv/` |
| 源码目录 | `src/ov5640_drv/` |
| 内核模块 | `ov5640.ko`、`mx6s_capture.ko` |
| 用户态测试 | `/usr/bin/ov5640_test` |
| 模块工具链 | `BSP_KERNEL_CROSS_COMPILE` 指向的 Linaro 4.9 |
| 测试程序工具链 | Buildroot target toolchain |

## 构建与部署

只改本目录驱动或测试程序：

```bash
NFS_DIR=<nfs-dir> bash buildscripts/build_and_deploy.sh drv ov5640_drv
```

如果同时改了摄像头相关 DTS/DTSI：

```bash
TFTP_DIR=<tftp-dir> bash buildscripts/build_and_deploy.sh dtb
```

删除或重命名模块、测试程序后，显式清理 NFS 旧文件：

```bash
NFS_DIR=<nfs-dir> bash buildscripts/pkg_clean_stale.sh ov5640_drv
```

## 板端验证

```bash
dmesg | grep -i -E 'ov5640|csi|video'
modprobe ov5640
modprobe mx6s_capture
ls -l /dev/video*
ov5640_test
```

如果 `modprobe` 顺序或依赖在当前 rootfs 中不稳定，可以临时使用 `insmod` 指定模块路径验证，再回到 Buildroot 包和模块依赖上修正。

## 移植调试记录

早期移植时反复出现 `reg write/read error`。排查过程如下：

1. 对照 datasheet 检查报错寄存器含义，并根据 `probe` 路径定位失败位置。
2. 多次复现后发现报错寄存器地址会变化，问题不像单个寄存器配置错误，更像 I2C 通信不稳定。
3. `i2cdetect -y 1` 中 OV5640 地址 `0x3c` 显示为 `UU`，说明内核驱动已经绑定该地址。
4. 继续检查各路电压、I2C 频率和模块加载顺序。

当时还怀疑过 BusyBox `mdev` 自动加载顺序：

- 自启动日志中能看到 `camera ov5640, is found`。
- 随后卸载 `mx6s_capture` 时曾出现 `send error`、`camera ov5640 is not found`。
- 再次加载 `mx6s_capture` 和 `ov5640` 时偶发 `camera ov5640 not found`。
- 临时通过 overlay 关闭 mdev autoload 后，多次卸载/加载模块更稳定；重新打开 autoload 后又出现过异常。

后续重新复现时，这个 autoload 问题没有稳定再现：

1. 临时禁用 `S90imx6-monitor`，避免服务自动加载摄像头相关模块。
2. 恢复默认 `S10mdev`。
3. 恢复默认 `mdev.conf`。
4. 手动加载 `mx6s_capture.ko` 和 `ov5640.ko` 后，`ov5640_test` 可以显示摄像头内容。
5. 默认 `S10mdev` 和默认 `mdev.conf` 下再次验证也正常。

当时关键启动日志示例：

```text
Starting mdev... OK
1-003c supply DOVDD not found, using dummy regulator
1-003c supply DVDD not found, using dummy regulator
1-003c supply AVDD not found, using dummy regulator
camera ov5640, is found
CSI: Registered sensor subdevice: ov5640 1-003c
```

当前结论：mdev autoload 可能曾经放大了模块加载顺序或电源/复位时序问题，但现阶段没有稳定复现。后续如果再次出现，应优先保留完整启动日志、模块加载顺序、`i2cdetect` 结果和 `dmesg` 时间线，再决定是修 mdev 规则、模块依赖还是驱动 probe/remove 时序。

## 参考资料

- [移植 ov5640 摄像头到 imx6ull 开发板（一）](https://blog.csdn.net/qq_55389904/article/details/132538449)
- [移植 ov5640 摄像头到 imx6ull 开发板（二）](https://blog.csdn.net/qq_55389904/article/details/132568689)
- [移植 ov5640 摄像头到 imx6ull 开发板（三）](https://blog.csdn.net/qq_55389904/article/details/132561962)
