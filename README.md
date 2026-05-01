# i.MX6ULL 嵌入式 Linux 系统构建工程

## 项目架构分析 (Project Structure)

- **buildroot**：采用 Buildroot 作为系统构建系统（负责交叉编译工具链、根文件系统统筹）。
- **bsp**：Board Support Package（板级支持包）。依托于 Buildroot 的 `BR2_EXTERNAL` 机制独立维护项目的板级定制参数、设备包配置（Package Config）及一键编译部署脚本。
- **src**：本地外置源码目录（Local Source Tree），便于脱离 Buildroot 系统进行独立驱动开发及内核迭代：
    - `ap3216c_drv`：基于正点原子基础例程重构并完善的 AP3216C I2C 环境光及接近传感器（ALS+PS）字符设备驱动，包含对应的用户态测试程序（APP）。
    - `linux-imx`：基于 NXP 官方源码栈，结合正点原子教程移植的 Linux 内核工程（含定制化 Device Tree 设备树）。
    - `uboot-imx`：基于 NXP 官方源码栈，针对本地开发板进行适配的 U-Boot 芯片平台引导程序。

## 项目实施路径 (Roadmap)

- [x] 完成 u-boot、Linux kernel 移植及 DTS 编写，基于 Buildroot 构建并成功运行 RootFS（根文件系统）。
- [x] AP3216C 环境传感器 I2C 驱动编写并跑通应用层测试。
- [ ] OV5640 摄像头模组 V4L2 驱动适配与联调。
- [ ] 基于轻量级 Web 服务器（如 lighttpd/Nginx）实现 OV5640 Web 端远程监控功能。
- [ ] 将 AP3216C 驱动由传统字符设备模型向 IIO（Industrial I/O）子系统模型迁移。
- [ ] 应用层功能联动应用：实现读取 AP3216C 光强数据动态调节 OV5640 曝光率，完成目标异动抓拍的系统级开发验证。

## 编译与部署指南 (Build & Deploy)

本项目内聚了基于 Buildroot 封装的一键编译与网络分发脚本 `bsp/build_and_deploy.sh`。

### 1. 编译模式选择
- **增量编译 (快速迭代)**：在工程根目录执行 `bash bsp/build_and_deploy.sh`。此模式跳过全局清理（保留 Stamp 印记文件），仅通过文件时间戳及同步机制编译修改过的源码，耗时极短。
- **全量编译 (完全重构)**：执行 `bash bsp/build_and_deploy.sh all`。触发前置 `make clean`，清空所有构建产物和 Stamp 标记从零开始构建。适用于刚更换 Toolchain 或引发重大 `.config` 变更时。

### 2. 编译内容控制与溯源
- **宏观组件开关 (`.config`)**：具体的系统包含哪些包、是否编译 Linux 内核（`BR2_LINUX_KERNEL=y`）等由 `buildroot/.config` 全权定夺。
- **自定义环境挂载 (`BR2_EXTERNAL`)**：脚本向 Make 传入了 `BR2_EXTERNAL` 参数，将 `bsp/` 路径作为独立模块树嵌入到 Buildroot，令系统能顺藤摸瓜检索到 `bsp/external.mk` 和外部驱动包（如 ap3216c_drv）。

### 3. 微观本地代码增量编译原理 (`OVERRIDE_SRCDIR`)
在使用默认的增量模式开发底层驱动时，工程利用了 Buildroot 特权覆盖机制。在 `buildroot/local.mk` 中通过指定 `LINUX_OVERRIDE_SRCDIR = $(TOPDIR)/../src/linux-imx` 让系统知道开发者正在本地维护代码：
每当执行增量脚本时，Buildroot 会利用 `rsync` 轻量比对 `src/` 目录的修改并同步至编译工作区，随后唤醒内核或驱动原生的 Makefile（如 Kbuild 体系）进行精准到文件级别的编译更新。这意味着仅当修改单一 `.c` 文件后，再次部署 RootFS 最快仅需数秒。
