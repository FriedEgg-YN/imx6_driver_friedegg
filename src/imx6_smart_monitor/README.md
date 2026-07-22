# i.MX6 Smart Monitor

本目录实现面向 i.MX6ULL 板端的 Qt 触摸屏 Smart Monitor 与硬件测试程序。当前主应用 `imx6-smart-monitor` 的核心闭环已经精简为：

```text
AP3216C proximity/lux + LD2410C presence -> MonitorCore -> CameraDevice record -> /smart-monitor/videos
```

主页面保留 RGB565 预览、摄像头遮挡报警、自动/关闭/常亮补光、分辨率/帧率选择和 LD2410C 配置入口。AP3216C 用于 `Auto` strobe 的 lux 判断，也用于基于 proximity/lux 的遮挡报警；Camera Test 与 LD2410 Test 继续作为完整硬件实验台保留。

## 目录概览

| 路径 | 内容 |
| --- | --- |
| `include/imx6smartmonitor/` | 公共类型和枚举。 |
| `common/` | 公共类型实现。 |
| `camera/` | V4L2 camera 封装、MMAP streaming、snapshot/record worker。 |
| `sensors/` | AP3216C、LD2410C 等传感器访问封装。 |
| `storage/` | `/smart-monitor/frames` 与 `/smart-monitor/videos` 路径生成和可写性检查。 |
| `core/` | MonitorCore 纯决策状态机。 |
| `qt/common/` | Qt 测试窗口和 app runner 复用层。 |
| `qt/apps/launcher/` | `imx6-smart-monitor` 主页面、controller 和 Tools。 |
| `qt/apps/*_test/` | 可独立运行、也可被 launcher 复用的模块测试页。 |
| `docs/` | 模块细节文档和验证记录。 |

## 程序入口

| 程序 | 作用 |
| --- | --- |
| `imx6-smart-monitor` | Smart Monitor 主页面：presence 触发录制、预览、遮挡报警、strobe 和 Tools。 |
| `imx6-sm-touch-test` | 触摸 input/Qt 事件链路测试。 |
| `imx6-sm-ap3216c-test` | AP3216C IIO sysfs 扫描和采样。 |
| `imx6-sm-ld2410-test` | LD2410C OUT/input、misc ioctl、UART 配置和工程数据测试。 |
| `imx6-sm-camera-test` | OV5640 V4L2 枚举、RGB565 预览、RGB565/JPEG snapshot、短录制实验。 |
| `imx6-sm-storage-test` | `/smart-monitor` 可写性以及 frames/videos 目录检查。 |
| `imx6-sm-core-test` | MonitorCore presence/light/storage/record 决策骨架测试。 |

## docs 索引

| 文档 | 内容 |
| --- | --- |
| [`docs/smart-monitor-v1-closed-loop.md`](docs/smart-monitor-v1-closed-loop.md) | Smart Monitor record-first 闭环、状态机、存储产物和验收命令。 |
| [`docs/ld2410-device-qt-wrapper-learning-notes.md`](docs/ld2410-device-qt-wrapper-learning-notes.md) | LD2410Device Qt 用户态封装的 C++/Qt/状态调用分层学习笔记。 |
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

## 验收重点

```bash
find /smart-monitor -maxdepth 2 -type f | sort
```

预期 Smart Monitor 主页面只因 presence 录制生成 `videos/*.mjpeg`；手动截图功能已移除。Camera Test 仍可单独验证 `frames/*.jpg`。不再生成 `sessions/`、`latest/`、`session.json`、`events.jsonl` 或 `index.jsonl`。

## 边界

- Smart Monitor 主页面只通过 `MonitorController` 发命令和订阅状态；Qt UI 不直接访问 V4L2/IIO/UART/Storage。
- 各测试页只通过对应模块封装访问设备节点；launcher 不复制 V4L2/IIO/UART/Storage 逻辑。
- Storage 默认写 `/smart-monitor`，只维护 `frames/` 和 `videos/` 两个子目录。
- “视频”继续使用当前 CameraDevice 的连续 JPEG byte stream，扩展名为 `.mjpeg`，不引入 MP4 容器。
- Smart Monitor 不提供 Playback，不引入 GStreamer、QtMultimedia、libcamera、服务化或 AI 视觉算法。
