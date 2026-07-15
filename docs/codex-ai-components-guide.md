# VSCode + Codex AI 组件使用指南

扫描日期：2026-07-11

本文记录本仓库当前真实存在的 Codex/AI 辅助组件，以及它们在 i.MX6ULL BSP、Buildroot 集成和 Linux 驱动学习中的使用边界。项目文档只写可复现规则和脱敏示例，不写真实 SSH、NFS/TFTP、板卡 IP、token、provider endpoint、私有笔记库路径或用户级运行状态。

## 当前组件清单

| 层级 | 组件 | 路径 | 当前状态 | 作用 |
| --- | --- | --- | --- | --- |
| 项目级 | instruction | `AGENTS.md` | 已启用 | 仓库边界、隐私规则、默认工作流、验证入口和 skill 路由 |
| 项目级 | skill | `.agents/skills/kernel-api-explainer/SKILL.md` | 已启用 | Linux kernel API、宏、枚举、结构体字段和 driver helper 解释 |
| 项目级 | skill | `.agents/skills/driver-callback-walkthrough/SKILL.md` | 已启用 | driver callback、V4L2 ioctl/subdev/VB2、file ops 等回调链路讲解 |
| 项目级 | skill | `.agents/skills/cite-project-docs/SKILL.md` | 已启用 | 从项目 docs、package docs 和 PDF/manual 查找依据、页码和项目笔记证据 |
| 用户级 | skill | `~/.codex/skills/ai-system-architect/SKILL.md` | 用户级 | 设计或维护 Codex/VSCode+Codex AI 组件体系 |

当前没有项目级 `.agents/prompts/`、`.agents/context/`、`.agents/mcp/`、hook 或 subagent 配置。不要在任务中把这些规划型组件当成已经可用；只有实际创建后再补充本文档。

旧的 driver summary notes skill 已删除。驱动学习总结不再走单独 skill：普通学习总结直接由当前回答完成；如果问题聚焦 API，则用 `$kernel-api-explainer`；如果问题聚焦回调触发链路，则用 `$driver-callback-walkthrough`；如果问题要求依据、页码或 manual/RM 证明，则用 `$cite-project-docs`。

## 打开工作区

在 VSCode 中打开仓库根目录，而不是只打开某个 `src/<pkg>/` 子目录。根目录上下文能让 Codex 同时看到：

- `AGENTS.md` 的项目规则。
- `.agents/skills/` 的项目级工作流。
- `buildscripts/` 的构建部署入口。
- `bsp/` 的 Buildroot external、rootfs overlay、package 和配置。
- `src/<pkg>/` 的外置驱动包和用户态测试程序。
- `src/linux-friedegg/`、`src/uboot-friedegg/` 这类大源码树或嵌套源码仓库。

## 默认使用方式

只解释、评审或整理方案时，可以明确写“不要改文件”。允许修改和验证时，直接给目标、症状、相关文件和期望结果。

常用请求示例：

```text
先根据 AGENTS.md 判断这次问题属于 BSP 集成、Buildroot/rootfs、内核 DTS、内核源码、外置驱动包还是用户态测试程序。只选择能证明改动的最窄验证命令，不要默认 full rebuild。
```

涉及板端命令时，如果 Codex 无法实际运行，应要求输出 exact board-side commands 和 expected result。

## Skill 路由

### `$kernel-api-explainer`

使用场景：

- Linux kernel API、内核函数、宏、枚举、结构体字段。
- driver helper 的参数、返回值、调用约束。
- “当前这个 API 为什么这样用”。

示例：

```text
Use $kernel-api-explainer.
解释当前 platform_get_resource(pdev, IORESOURCE_MEM, 0) 的作用、参数含义、可选值，以及这里为什么这样用。
```

期望行为：

- 先用 `rg` 在 `src/linux-friedegg/` 和相关 `src/<pkg>/` 中找定义、声明、flag/enum 和调用点。
- 给可点击本地定义链接，不粘贴函数体源码。
- 用中文 kernel-doc 风格说明作用、参数、返回值、原理和当前上下文。

### `$driver-callback-walkthrough`

使用场景：

- 讲解 driver callback、ioctl callback、`file_operations`、IRQ/workqueue/PM callback。
- 讲解 V4L2 `vidioc_*`、sensor subdev op、VB2 queue op。
- 梳理 userspace 命令如何分发到当前驱动实现。

示例：

```text
Use $driver-callback-walkthrough.
讲解 src/ov5640/mx6s_capture.c 里的 streamon/streamoff 路径，分清 userspace ioctl、host video-node callback、sensor subdev .s_stream 和 VB2 start_streaming。
```

期望行为：

- 先建立层级映射：userspace 操作、ops 表成员、具体实现函数、subsystem 层级。
- 对 V4L2 尤其要区分 `/dev/videoX` 的 ioctl、host `v4l2_ioctl_ops`、sensor `v4l2_subdev_ops` 和 VB2 `vb2_ops`。
- 输出当前实现的执行顺序、状态变化、硬件配置、错误路径和验证命令。

### `$cite-project-docs`

使用场景：

- 用户询问“依据”“官方依据”“datasheet/manual/RM 证明”“页码”。
- 需要从 `docs/`、`src/*/docs/` 或 PDF/manual 核对某个技术结论。
- 需要区分官方手册、项目笔记和当前代码实现证据。

示例：

```text
Use $cite-project-docs.
OV5640 的 DVP/YUV 输出能力有没有 datasheet 依据？请给 PDF 页码，并说明当前驱动是否已经实现。
```

期望行为：

- 优先查当前 package 的 `docs/`，再查项目级 `docs/` 和其他 `src/*/docs/`。
- 用户点名 PDF/manual 路径或文件名时，如果普通 `rg --files` 没找到，必须用 `rg --files -uuu` 或精确路径配合 `file`/`pdftotext` 兜底确认，避免 ignore 规则隐藏资料。
- PDF 依据给 1-based PDF viewer 页码；Markdown 依据给可点击本地行号。
- 如果手册能力和当前驱动实现不一致，分别说明“manual capability”和“current driver behavior”。

### `$ai-system-architect`

使用场景：

- 设计或调整 `AGENTS.md`、skill、prompt、hook、MCP、subagent、memory。
- 判断规则应该放项目级还是用户级。
- 复盘 Codex 工作流摩擦并决定改哪个 AI 组件。

示例：

```text
Use $ai-system-architect.
请评审当前项目的 Codex AI 组件，指出哪些该留在项目层，哪些该放到用户层，并给出下一轮改进计划。
```

该 skill 在用户级，不应复制到项目目录。项目里只记录协作者也需要的可复现规则。

## 当前工程入口

本仓库同时包含 BSP 集成、Buildroot external、外置驱动包、用户态测试程序和大源码树。常见归属判断：

| 问题类型 | 优先检查路径 | 典型验证 |
| --- | --- | --- |
| 外置驱动包代码 | `src/<pkg>/` | `bash buildscripts/build_and_deploy.sh drv <pkg>` |
| Buildroot package 安装 | `bsp/package/<pkg>/` | `bash buildscripts/build_and_deploy.sh drv <pkg>` 或 `rootfs` |
| rootfs overlay 或启动脚本 | `bsp/rootfs_overlay/` | `bash buildscripts/build_and_deploy.sh rootfs` |
| BusyBox/Buildroot 配置 | `bsp/configs/` | `bash buildscripts/build_and_deploy.sh config status` 后按需 `rootfs` |
| 内核 DTS 或内核驱动 | `src/linux-friedegg/` | `dtb` 或 `zimage` |
| U-Boot | `src/uboot-friedegg/` | 按具体启动链路选择，不默认改 |
| 文档和学习材料 | `docs/`、`src/<pkg>/README.md`、`src/<pkg>/docs/` | Markdown 检查或人工阅读 |

当前可见外置包包括 `ap3216c`、`gt9147`、`ov5640`、`imx6_monitor`、`imx6_qt5_demo` 和 `print_chasing_led`。不要假设只有当前打开的 `ov5640` 存在。

## 验证命令

优先选择能证明本次改动的最窄命令：

```bash
bash buildscripts/build_and_deploy.sh dtb
bash buildscripts/build_and_deploy.sh zimage
bash buildscripts/build_and_deploy.sh rootfs
bash buildscripts/build_and_deploy.sh drv <pkg>
bash buildscripts/build_and_deploy.sh config status
```

驱动或 BSP 调试回答应同时给出板端命令，例如 `modprobe`、`dmesg`、`lsmod`、`v4l2-ctl`、`media-ctl`、`cat /proc/interrupts` 或具体测试程序命令，并写清 expected result。

## 组件维护规则

- `AGENTS.md` 放长期项目规则、仓库边界、隐私边界、验证入口和默认路由。
- `.agents/skills/` 放稳定、可复用、多步骤工作流。
- 一次性学习总结、任务复盘、简历整理和面试问答优先在普通回答中完成，不再单独维护 summary-notes skill。
- prompt、context、MCP 模板、hook、subagent 只有创建后才写入“当前组件清单”。
- 删除或重命名 skill 后，必须同步更新 `AGENTS.md` 和本文档，并用 `rg` 检查旧名称残留。
- 用户级配置保留个人偏好、真实路径、provider、token、MCP 实例和运行状态；项目级配置只保留可共享、可复现、脱敏后的规则。

## 隐私占位符

项目文档、prompt、skill 和示例配置统一使用：

```text
<vm-host>
<board-ip>
<nfs-dir>
<tftp-dir>
<obsidian-vault>
<notes-vault>
<user>
```

不要提交真实 SSH 主机、用户名、私钥、密码、token、API key、cookie、真实 NFS/TFTP 路径、真实 board IP、Obsidian vault 绝对路径、私有 provider endpoint 或用户级运行状态文件。

## 维护自检

调整 AI 指导文档后至少运行：

```bash
rg --files .agents
rg -n "<old-skill-or-component-name>" AGENTS.md docs .agents
```

第一条用于核对项目级 AI 组件实际文件；第二条把占位符替换为被删除或重命名的旧组件名，用于确认旧名称没有残留在路由和当前清单中。
