# i.MX6 Smart Monitor

本目录实现面向 i.MX6ULL 板端的 Qt 触摸屏 Smart Monitor 与硬件测试程序。当前 `imx6-smart-monitor` 首屏是 Smart Monitor v1 主页面：单进程 Qt 应用内由 controller 串联 AP3216C、LD2410C、MonitorCore、CameraDevice 和 StorageManager，完成 presence gate、RGB565 预览、自动/手动 snapshot、torch、storage/session 状态和静态 Playback。Tools 页面继续保留触摸、IIO、LD2410、OV5640 camera、存储和核心状态机单点测试入口。

Camera Test 仍覆盖 RGB565 预览/截图/录制路径和 OV5640 sensor JPEG 采集路径。Smart Monitor v1 的实时预览与自动 snapshot 走 RGB565 预览帧编码成 JPEG；Camera Test 的 Capture 菜单继续用于验证 sensor JPEG 专项路径。

## 目录概览

| 路径 | 内容 |
| --- | --- |
| `include/imx6smartmonitor/` | 公共类型和枚举。 |
| `common/` | 公共类型实现。 |
| `camera/` | V4L2 camera 封装、MMAP streaming、snapshot/record worker。 |
| `sensors/` | AP3216C、LD2410C 等传感器访问封装。 |
| `storage/` | `/smart-monitor` session、latest、index 文件管理。 |
| `core/` | MonitorCore 纯决策状态机。 |
| `qt/common/` | Qt 测试窗口和 app runner 复用层。 |
| `qt/apps/launcher/` | `imx6-smart-monitor` 主页面、controller、Tools 和 Playback。 |
| `qt/apps/*_test/` | 可独立运行、也可被 launcher 复用的模块测试页。 |
| `docs/` | 模块细节文档和验证记录。 |

## 程序入口

| 程序 | 作用 |
| --- | --- |
| `imx6-smart-monitor` | Smart Monitor v1 主页面，含 Tools 和 Playback。 |
| `imx6-sm-touch-test` | 触摸 input/Qt 事件链路测试。 |
| `imx6-sm-ap3216c-test` | AP3216C IIO sysfs 扫描和采样。 |
| `imx6-sm-ld2410-test` | LD2410C OUT/input 与 UART 节点探测。 |
| `imx6-sm-camera-test` | OV5640 V4L2 枚举、RGB565 预览、RGB565/JPEG snapshot、record。 |
| `imx6-sm-storage-test` | `/smart-monitor` 可写性与 session 文件测试。 |
| `imx6-sm-core-test` | MonitorCore presence/light/storage 决策骨架测试。 |

## docs 索引

| 文档 | 内容 |
| --- | --- |
| [`docs/smart-monitor-v1-closed-loop.md`](docs/smart-monitor-v1-closed-loop.md) | Smart Monitor v1 架构、状态机、session 产物和验收命令。 |
| [`docs/camera-test-performance-notes.md`](docs/camera-test-performance-notes.md) | Camera Test RGB565/JPEG 采集性能、验证命令、已知限制。 |
| [`docs/camera-data-flow-and-format-path.md`](docs/camera-data-flow-and-format-path.md) | OV5640 后的数据流、RGB565/JPEG 格式转换、buffer 与 SOI/EOI 裁剪语义。 |

## 构建与运行

Buildroot 包构建和部署：

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

板端运行示例：

```bash
QT_QPA_PLATFORM=linuxfb imx6-smart-monitor
QT_QPA_PLATFORM=linuxfb imx6-sm-camera-test
QT_QPA_PLATFORM=linuxfb imx6-sm-core-test
```

## 边界

- Smart Monitor 主页面只通过 `MonitorController` 发命令和订阅状态；Qt UI 不直接访问 V4L2/IIO/UART/Storage。
- 各测试页只通过对应模块封装访问设备节点；launcher 不复制 V4L2/IIO/UART/Storage 逻辑。
- Camera Test 的 JPEG 第一阶段不提供 quality UI，不做 RGB565/JPEG 双路并行采集。
- Storage 默认写 `/smart-monitor`，在 NFS root 模式下可从 `<nfs-dir>/smart-monitor` 查看 session 产物。
- Smart Monitor v1 不引入 GStreamer、QtMultimedia、libcamera、服务化或 AI 视觉算法；Playback 第一版只查看事件和 JPEG 帧。
