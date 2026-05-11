# Project specific details for my_imx6_workspace

### Architecture & Submodules
- Buildroot: Managed as a Git submodule in `/buildroot/`. Only pulling stable/latest source, no direct package modifications inside the submodule if possible.
- Kernel/U-Boot: Source maintained in `/src/linux-imx/` and `/src/uboot-imx/` as submodules.
- BSP: Custom configurations, scripts, and local packages are in `/bsp/`.
- You can use `friedegg` as a keyword to search for project-specific details in the codebase, like dts files.

### Build System & Coordination
- The project orchestrates builds via `/bsp/build_and_deploy.sh`.
- Local package source overrides are defined via `BR2_PACKAGE_OVERRIDE_FILE="$BSP_DIR/local.mk"`.
- Do not manually modify files inside `/buildroot/` directly stringently; adhere to `BR2_EXTERNAL` and `local.mk` override paradigms.

### 开发环境

- 正点原子i.MX6ULL开发板，底板V2.4，核心板V2.0
- src/linux-imx: Linux kernel 4.1.15, 在 NXP 官方基础上进行移植
- src/uboot-imx: U-Boot 2016.11, 同样基于 NXP 官方版本
- 交叉编译工具链: gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf，来自正点原子
