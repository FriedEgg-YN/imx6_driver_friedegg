# i.MX6 Smart Monitor Skeleton

本目录是 `docs/smart-monitor-project-architecture.md` 对应的第一版项目骨架。
当前实现一个综合 Qt launcher 和各模块测试 app。综合入口用于像手机主页一样打开、返回各测试页；独立测试程序仍保留，便于单模块调试。

## 目录

| 路径 | 作用 |
| --- | --- |
| `include/imx6smartmonitor/` | 公共数据类型。 |
| `camera/` | OV5640/V4L2 轻量探测封装。 |
| `sensors/` | AP3216C、LD2410C 和 SensorHub 入口占位。 |
| `core/` | MonitorCore 状态机骨架。 |
| `storage/` | NFS/session 存储检查骨架。 |
| `qt/common/` | Qt 模块测试 app 共用窗口和启动逻辑。 |
| `qt/apps/launcher/` | 综合测试 launcher，点击打开各模块测试页，页内 Home 返回主页。 |
| `qt/apps/*_test/` | 各模块独立测试 app，同时提供 launcher 复用的测试窗口。 |

## Qt app

| 程序 | 测试对象 |
| --- | --- |
| `imx6-smart-monitor` | 综合 launcher，单进程打开/返回各模块测试页。 |
| `imx6-sm-touch-test` | Qt input/touch 事件链路。 |
| `imx6-sm-ap3216c-test` | AP3216C IIO sysfs 扫描和采样。 |
| `imx6-sm-ld2410-test` | LD2410C OUT/input 和 UART 节点探测。 |
| `imx6-sm-camera-test` | `/dev/videoX` V4L2 capability 和 format 枚举。 |
| `imx6-sm-storage-test` | `<nfs-dir>/smart-monitor` 可写性和 session 文件骨架。 |
| `imx6-sm-core-test` | MonitorCore presence/light/storage 决策骨架。 |

## 构建

板端 Buildroot 包：

```bash
NFS_DIR=<nfs-dir> bash buildscripts/build_and_deploy.sh drv imx6-smart-monitor
```

本目录交叉编译验证：

```bash
cd src/imx6_smart_monitor
../../buildroot/output/host/bin/qmake imx6_smart_monitor.pro
make -j$(nproc)
file bin/imx6-smart-monitor bin/imx6-sm-*-test
```

主机离板 Qt 预览需要安装桌面 Qt5，再用主机 `qmake` 生成 x86 程序。

板端运行示例：

```bash
QT_QPA_PLATFORM=linuxfb imx6-smart-monitor
QT_QPA_PLATFORM=linuxfb imx6-sm-touch-test
QT_QPA_PLATFORM=linuxfb imx6-sm-ap3216c-test
QT_QPA_PLATFORM=linuxfb imx6-sm-ld2410-test
QT_QPA_PLATFORM=linuxfb imx6-sm-camera-test
QT_QPA_PLATFORM=linuxfb imx6-sm-storage-test
QT_QPA_PLATFORM=linuxfb imx6-sm-core-test
```

## 边界

- Qt 测试页只通过对应模块封装访问设备节点。
- 综合 launcher 只负责页面入口和返回，不复制 V4L2/IIO/UART/Storage 逻辑。
- 完整智能监控业务页、回放页和完整 camera streaming 本次不实现。
- Camera 测试 app 当前只做 V4L2 capability 和 format 枚举；MMAP streaming 后续进入 Camera SDK。
- Storage 测试 app 默认写 `/tmp/smart-monitor`，板端验证 NFS 时请改成 `<nfs-dir>/smart-monitor`。
