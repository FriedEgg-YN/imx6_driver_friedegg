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
    - [x] 完成VFS挂载，当前/proc、/sys、/dev都未挂载，且驱动modprobe后需要手动挂载VFS后mknod方可使用->增加mdev后解决，**需要了解背后原理**
    - [ ] 当前中断链路有问题，测试app 中断触发模式无法正常使用
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
在使用默认的增量模式开发底层驱动时，工程利用了 Buildroot 的特权覆盖机制。当前实际生效的覆盖文件是 `bsp/local.mk`，其中通过 `LINUX_OVERRIDE_SRCDIR = $(TOPDIR)/../src/linux-imx` 告诉 Buildroot：内核源码不是从下载包解压出来的，而是直接使用工作区里的 `src/linux-imx`。

每当执行增量脚本时，Buildroot 会利用 `rsync` 轻量比对 `src/` 目录的修改并同步至编译工作区，随后唤醒内核或驱动原生的 Makefile（如 Kbuild 体系）进行精准到文件级别的编译更新。这意味着仅当修改单一 `.c` 文件后，再次部署 RootFS 最快仅需数秒。

### 4. `/dev` 管理方式与 rootfs 配置

当前工程的 `bsp/configs/imx6ull_friedegg_emmc_defconfig` 没有显式指定 `/dev` 管理方式，因此 Buildroot 会采用默认的 **devtmpfs only** 方案，也就是 `BR2_ROOTFS_DEVICE_CREATION_DYNAMIC_DEVTMPFS`。这意味着设备节点主要由内核自动创建，rootfs 侧不需要额外安装设备管理守护进程。

这三种方式的区别可以直接理解为“谁来负责创建 `/dev` 节点、谁来处理热插拔和权限”：

1. **devtmpfs**：内核直接创建 `/dev` 节点，最轻量，适合只需要基础设备节点的场景。
2. **mdev**：在 devtmpfs 之上增加 BusyBox 的轻量设备管理，适合需要简单热插拔、权限修正或自动动作的场景。
3. **udev / eudev**：完整的用户态设备管理，规则能力最强，但体积和依赖也最大。

Buildroot 里的配置入口在 `menuconfig -> System configuration -> /dev management`，对应的符号如下：

- `BR2_ROOTFS_DEVICE_CREATION_STATIC`：静态设备表，只有你手工维护设备节点时才用。
- `BR2_ROOTFS_DEVICE_CREATION_DYNAMIC_DEVTMPFS`：devtmpfs only，当前工程默认就是这个方向。
- `BR2_ROOTFS_DEVICE_CREATION_DYNAMIC_MDEV`：devtmpfs + mdev，会自动选中 `BR2_PACKAGE_BUSYBOX`。
- `BR2_ROOTFS_DEVICE_CREATION_DYNAMIC_EUDEV`：devtmpfs + eudev，会自动选中 `BR2_PACKAGE_EUDEV`。

如果你使用的是 systemd，那么 `/dev` 管理会走 systemd 自带的 udev 方案，而不是上面的普通 `/dev management` 选项。

要把机制“编译进 rootfs”，实际就是在 Buildroot 里把对应选项勾上，然后重新构建 rootfs：

1. 进入 `buildroot/menuconfig`。
2. 打开 `System configuration -> /dev management`。
3. 选择你需要的方案：`devtmpfs only`、`devtmpfs + mdev` 或 `devtmpfs + eudev`。
4. 如果选择 `mdev`，确认 BusyBox 已启用并且 rootfs 启动流程会执行 `mdev -s`；如果选择 `eudev`，确认 `BR2_PACKAGE_EUDEV` 可用，且工具链满足 `wchar`、动态库和 MMU 要求。
5. 保存配置后重新编译 rootfs 或整包，再检查 `/dev` 下是否出现目标节点。

内核侧还需要满足两个基础条件：`CONFIG_DEVTMPFS=y` 和 `CONFIG_DEVTMPFS_MOUNT=y`。Buildroot 在动态设备创建模式下会自动尽量帮你打开它们，但如果你手工改过内核配置，还是要确认这两个选项保留开启。

判断是否配置成功的最直接方法是：系统启动后查看 `/dev`，如果 `ap3216c` 这类字符设备能自动出现，说明 runtime 设备管理已经生效；如果驱动已经 `probe success` 但 `/dev/ap3216c` 不在，那就是设备节点管理还没接上，不是驱动注册失败。

### 5. 新增模块与调整编译内容

如果需要给项目增加一个新的外置模块，推荐沿用当前 `ap3216c` 的组织方式：

1. 在 `src/<module_name>/` 下放置源码，至少保留驱动源码、测试程序和对应的 Makefile。
2. 在 `bsp/package/<module_name>/` 下创建 `Config.in` 和 `<module_name>.mk`，并在 `bsp/Config.in` 中通过 `source "$BR2_EXTERNAL_BSP_PATH/package/<module_name>/Config.in"` 引入。
3. 在外部包的 `Config.in` 中用 `depends on BR2_LINUX_KERNEL` 等条件约束它的启用范围，再在目标板 `defconfig` 中勾选该包。
4. 在 `<module_name>.mk` 中保留 `$(eval $(kernel-module))`，用于构建内核 `.ko`；如果还有用户态工具，再额外增加 `BUILD_CMDS` 和 `INSTALL_TARGET_CMDS`，把工具编译并安装到 `$(TARGET_DIR)`。

调整模块的编译内容时，优先改 `src/<module_name>/Makefile`：

1. 用独立变量拆分内核模块和用户态程序，例如 `APP_SRC`、`APP_CFLAGS`、`APP_LDFLAGS`。
2. 让用户态程序通过单独的 `app` 目标编译，避免和 `modules` 目标互相耦合。
3. 如果要增加、删除或替换测试程序，只改 `APP_SRC` 和 `APP_*` 变量即可；如果安装路径要变化，只改 `<module_name>.mk` 里的 `INSTALL_TARGET_CMDS`。
4. 如果只想编译内核模块，不想带测试程序，可在包的 `.mk` 里去掉 `BUILD_CMDS`，保留 `kernel-module` 的默认流程即可。

当前工程里的 AP3216C 包已经按这个模式拆分：`src/ap3216c_drv/Makefile` 负责分别编译 `ap3216c.ko` 和 `ap3216cApp`，而 `bsp/package/ap3216c/ap3216c.mk` 负责把测试程序安装到目标根文件系统的 `/usr/bin/`，因此后续新增同类模块时可以直接复用这套结构。
