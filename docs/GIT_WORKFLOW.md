# GIT Workflow

## 一、 当前多仓管理结构

当前项目为 **Super-project（主仓） + 多 Sub-project（子仓）** 的架构。

仓库边界如下：

- 主仓库只保留项目级配置、脚本和少量本地驱动代码。
- 子仓库负责大型源码树，主仓只记录它们的指针。

```
imx6_driver_friedegg/              <-- 【主仓库】只记录这层核心逻辑
├── bsp/                        <-- 主仓库内容（Buildroot 配置与脚本）
├── src/***_drv/                <-- 主仓库内容（移植的本地驱动）
├── buildroot/                  <-- 【独立子仓库】官方编译引擎，以 Gitlink 指向
├── src/linux-friedegg/         <-- 【独立子仓库】移植后的内核，内部自建 git 追踪
└── src/uboot-friedegg/         <-- 【独立子仓库】移植后的 U-Boot，内部自建 git 追踪
```


## 二、 提交信息规范

### 2.1 格式模板

```
<类型>(<模块>): <一行标题 (≤ 50 字符)>

<详细说明（可选，写 what 和 why，不写 how）>
```

### 2.2 标题行规则
- **首字母小写**：`fix:` 不是 `Fix:`
- **不超过 50 个字符**，句末不加句号
- **用祈使句**：`fix bug` 不是 `fixed bug` 或 `fixes bug`

### 2.3 正文规则
- 正文和标题之间空一行
- 正文解释 **what（改了啥）** 和 **why（为什么改）**，不需要写 **how（怎么改的）**——代码本身已经说明 how
- 每行建议 ≤ 72 字符

### 2.4 类型前缀

| 前缀 | 何时用 | 示例 |
|------|--------|------|
| `fix` | 修复 bug | `fix: ap3216c readdata returns wrong ps value` |
| `feat` | 新增功能 | `feat: add ioctl cmd to set ps trigger threshold` |
| `dts` | 设备树变更专用 | `dts: imx6ull-friedegg-emmc: fix ap3216c int pin` |
| `driver` | 驱动代码变更专用 | `driver: ap3216c: fix interrupt not triggering` |
| `docs` | 文档/README | `docs: update GIT_WORKFLOW with commit conventions` |
| `refactor` | 重构，不改行为 | `refactor: extract event mode switch to separate func` |
| `chore` | 构建脚本、子模块指针等杂项 | `chore: update linux-friedegg submodule` |

### 2.5 完整示例

```
dts: imx6ull-friedegg-emmc: fix ap3216c interrupt to GPIO1_IO01

The AP3216C INT pin is connected to GPIO1_IO01 (pin 1), but the device
tree was using the wrong mapping. Correct the 'interrupts' property to
<1 IRQ_TYPE_LEVEL_LOW> so the driver can receive hardware interrupts.
```

### 2.6 子模块指针提交约定

主仓库中更新子模块指针时，用 `chore:` 前缀，并在正文中列出子模块内部的 commit 摘要：

```
chore: update linux-friedegg submodule

- dts: fix ap3216c interrupt to GPIO1_IO01
```
