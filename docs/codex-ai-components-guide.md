# VSCode + Codex AI 组件使用指南

扫描日期：2026-05-20

本文档基于当前项目目录和用户级 `~/.codex` 目录的实际扫描结果，说明在 VSCode + Codex 插件中如何使用这些组件辅助本 i.MX6ULL BSP 与 Linux 驱动开发。项目文档只记录脱敏后的结构、用途和操作方法，不写入真实 token、认证文件、私有 MCP 路径、SSH/NFS/TFTP 路径、板卡 IP 或个人数据库内容。

## 本次扫描到的组件

| 层级 | 组件 | 路径 | 作用 |
| --- | --- | --- | --- |
| 项目级 | instruction | `AGENTS.md` | 本仓库长期工程规则：i.MX6ULL BSP 定位、仓库边界、验证入口、隐私边界、默认 skill 路由 |
| 项目级 | prompts | `.agents/prompts/*.md` | VSCode 聊天框可复制的标准任务模板 |
| 项目级 | skills | `.agents/skills/*/SKILL.md` | 可复用工作流：内核 API 解释、BSP 工程、开发学习闭环、调试卡片、学习总结、Git 提交 |
| 项目级 | context | `.agents/context/*.md` | 给 skill 按需读取的稳定事实：项目地图、构建部署命令、学习输出契约、调试卡片模板 |
| 项目级 | MCP 模板 | `.agents/mcp/*` | 只保存示例和占位符，真实 MCP 配置留在用户级 |
| 用户级 | instruction | `~/.codex/AGENTS.md` | 个人级规则：AI 系统设计任务使用 `$ai-system-architect` |
| 用户级 | config | `~/.codex/config.toml` | 模型、provider、网络、项目 trust 等本机配置，文档中只做脱敏说明 |
| 用户级 | command rules | `~/.codex/rules/default.rules` | 当前已批准的命令前缀规则，例如 `sort` |
| 用户级 | skills | `~/.codex/skills/` | 系统 skill 和个人 skill，包括 `$ai-system-architect` |
| 用户级 | runtime state | `~/.codex/auth.json`、SQLite、session、logs、shell snapshots | 认证和运行状态，不进入项目文档 |

OpenAI 官方说明中，Codex 可在 IDE 中使用打开文件和选中代码作为上下文，支持本地读写、运行命令和云端任务衔接；公开的 AGENTS.md 格式用于给 agent 提供仓库内工作规则；OpenAI 的 Codex skills 说明把 skill 定义为由说明、资源和脚本组成的可复用能力。本文把这些能力映射到本项目的 BSP/驱动开发工作流。

## VSCode + Codex 聊天框怎么用

### 打开正确的工作区

在 VSCode 中打开仓库根目录：

```text
/home/<user>/imx6_driver_friedegg
```

不要只打开 `src/ap3216c_drv/` 或 `bsp/` 子目录。打开根目录后，Codex 才能同时读取：

- `AGENTS.md` 的项目规则。
- `.agents/prompts/` 的任务模板。
- `.agents/skills/` 的工作流。
- `.agents/context/` 的稳定项目事实。
- `bsp/`、`src/`、`docs/` 的实际工程文件。

### 选择 Chat 还是 Agent

推荐选择方式：

- 只解释、只评审、只整理方案：用 Chat 或在请求中明确“不要改文件”。
- 允许 Codex 编辑文件、运行验证命令：用 Agent。
- 需要访问网络、真实部署目录或工作区外路径：只在确认无隐私风险后使用更高权限。

聊天框可直接写：

```text
先检查仓库归属和当前状态，再决定是否编辑。不要修改 buildroot/、src/linux-friedegg/ 或 src/uboot-friedegg/，除非任务明确需要。
```

### 调用项目 prompt

prompt 文件是“任务输入模板”。在聊天框第一行写：

```text
Use .agents/prompts/learning-loop-task.md
```

然后粘贴并填写模板字段。Codex 会按该 prompt 的结构收集目标、症状、硬件路径、验证条件和学习输出要求。

### 调用 skill

skill 是“工作流”。在聊天框第一行写：

```text
Use $imx6-bsp-engineer.
```

或：

```text
Use $imx6-dev-learning-loop.
```

效果是让 Codex 按对应 `SKILL.md` 的步骤工作，例如先判断仓库归属、读取 context、选择最窄验证命令、输出学习材料。

### 使用当前文件和选区

当你在 VSCode 里打开或选中某段代码时，可以直接写：

```text
Use $imx6-bsp-engineer.
基于当前打开文件和选中代码，解释这个 probe 函数的资源申请、错误回滚、IRQ 注册和 device node 创建路径。先不要改代码。
```

适合：

- 解释 Linux driver 函数。
- 局部 review。
- 小范围修复。
- 把当前日志或代码片段整理成学习材料。

## Instruction：`AGENTS.md`

### 项目级 `AGENTS.md`

路径：

```text
AGENTS.md
```

效果：

- 把本仓库定位为 i.MX6ULL embedded Linux BSP 和驱动工程。
- 要求优先保 bootability、reproducibility、仓库边界和隐私边界。
- 明确 `bsp/`、`src/*_drv/`、`docs/`、`.agents/` 属于 super-project。
- 明确 `buildroot/`、`src/linux-friedegg/`、`src/uboot-friedegg/` 是大源码树或子模块，不能随便改。
- 规定验证入口是 `bash bsp/build_and_deploy.sh <mode>`。
- 内核 API 问题默认路由到 `$kernel-api-explainer`，先用 `rg` 找定义，再给中文 kernel-doc 风格解释。
- 要求开发后给学习总结，调试后给知识卡或建议。

聊天框具体操作：

```text
根据 AGENTS.md 的规则，帮我判断这个 OV5640 修改应该落在 BSP 层、内核 DTS、外置驱动包还是用户态测试程序。先给仓库归属和验证命令，不要直接改文件。
```

什么时候更新：

- 反复发生仓库边界误判。
- 反复遗漏最窄验证命令。
- 反复遗漏隐私占位符。
- 反复遗漏内核 API 定义链接、参数取值或当前上下文分析。
- 新增全项目都应遵守的构建、部署或学习输出规则。

### 用户级 `~/.codex/AGENTS.md`

当前扫描到的用户级规则是：AI 辅助开发系统设计、Codex 组件选择、用户级/项目级边界、长期工作流迭代任务使用 `$ai-system-architect`。

效果：

- 这是跨项目个人规则。
- 不应该直接复制进本仓库，除非它变成协作者也必须遵守的项目规则。

聊天框具体操作：

```text
Use $ai-system-architect.
请评审本仓库的 AGENTS.md、prompt、skill、MCP 模板和 docs，判断哪些规则应该放项目级，哪些应该放用户级。
```

## Prompts：`.agents/prompts/`

prompt 的价值是让需求输入稳定，不靠每次临场发挥。使用时可以打开对应 prompt 文件，也可以直接在聊天框写 `Use .agents/prompts/<name>.md`。

| Prompt | 用途 | 主要效果 |
| --- | --- | --- |
| `learning-loop-task.md` | 端到端开发学习闭环 | 路由到 `$imx6-dev-learning-loop`，覆盖归属、验证、学习总结、调试卡片、AI 组件复盘 |
| `driver-development.md` | Linux 驱动开发 | 判断 char/IIO/V4L2/input/platform/I2C/SPI 等子系统模型，解释 subsystem contract |
| `linux-port.md` | Linux/DTS/驱动绑定调试 | 定位 DTS、Kconfig/config、driver、Buildroot integration |
| `uboot-port.md` | U-Boot 移植或启动问题 | 检查 SPL/DDR、pinmux、environment、boot command、DTB handoff |
| `buildroot-config.md` | Buildroot/BusyBox/rootfs 配置 | 判断改动属于 defconfig、BusyBox config、rootfs overlay 还是 local package |
| `debug-note.md` | 调试过程转知识卡 | 路由到 `$embedded-dev-notes`，输出 Obsidian-friendly Markdown |
| `learning-summary.md` | 完成任务后的学习总结 | 路由到 `$driver-learning-coach`，输出总结、简历、架构、权衡、面试题 |
| `ai-system-retro.md` | AI 工作流复盘 | 判断应改 instruction、prompt、skill、hook、MCP、subagent 还是 memory |

### 最推荐入口：`learning-loop-task.md`

聊天框复制：

```text
Use .agents/prompts/learning-loop-task.md

Run an end-to-end development learning loop for <roadmap item or issue>.

Goal:
Current behavior or symptom:
Target hardware path:
- board/peripheral:
- bus/subsystem:
- pins/clocks/resets/regulators/interrupts if known:
Relevant files or logs:
Expected user-space behavior:
Verification available:
- build command:
- board-side command:
Learning output needed:
- docs note:
- Obsidian/private note:
- resume/interview summary:
AI component friction to watch for:
```

实际效果：

- Codex 先判断任务属于 BSP、Buildroot、AP3216C、OV5640、U-Boot、Linux、rootfs 还是 AI 组件维护。
- 技术实现交给 `$imx6-bsp-engineer`。
- 验证命令会收敛到 `dtb`、`zimage`、`rootfs`、`drv <pkg>`、`verify ...` 之一。
- 完成后生成学习总结。
- 如果有症状、日志、假设和根因，会生成或建议调试知识卡。

### 驱动开发入口：`driver-development.md`

聊天框复制：

```text
Use .agents/prompts/driver-development.md

Develop or modify a Linux driver for AP3216C.
Device facts:
- bus/subsystem: I2C + GPIO IRQ
- datasheet/registers: AP3216C ALS/PS/IR registers
- DTS binding: 请检查当前 DTS
- user-space API: /dev/ap3216c + ap3216cApp
- interrupt/polling requirements: 支持轮询和中断模式
- current code path: src/ap3216c_drv/ap3216c.c
```

实际效果：

- Codex 会先判断应该使用字符设备、IIO、V4L2、input、platform、I2C、SPI 或其他模型。
- 会解释为什么选择该 subsystem contract。
- 会检查源码、Makefile、DTS、Buildroot package、rootfs 和用户态测试是否匹配。

### Linux/DTS 入口：`linux-port.md`

聊天框复制：

```text
Use .agents/prompts/linux-port.md

Port or debug Linux support for OV5640 camera.
Hardware facts:
- bus/interface: I2C control + CSI/parallel data path
- pins: 请从 DTS 检查
- clock/reset/regulator: 请从 DTS 检查
- interrupt: unknown
- expected userspace node or subsystem: /dev/video0 and V4L2 controls
- current symptom: v4l2-ctl 无法枚举摄像头
```

实际效果：

- Codex 会判断问题属于硬件描述、内核配置、驱动 binding、runtime device management 还是用户态验证。
- 会优先定位 DTS、Kconfig/config、driver、Buildroot 集成点。

### Buildroot/rootfs 入口：`buildroot-config.md`

聊天框复制：

```text
Use .agents/prompts/buildroot-config.md

Change Buildroot/BusyBox/rootfs behavior for AP3216C test app install and mdev startup.
Current behavior: rootfs 中需要手动处理设备节点。
Desired behavior: 启动后具备稳定的 /dev 管理路径，并能运行 /usr/bin/ap3216cApp。
Target packages/config symbols if known: BR2_ROOTFS_DEVICE_CREATION_DYNAMIC_MDEV
Runtime validation: ls /dev/ap3216c, ps, mount, ap3216cApp
```

实际效果：

- Codex 会检查 `bsp/configs/imx6ull_friedegg_emmc_defconfig`、`bsp/configs/busybox.config`、`bsp/rootfs_overlay` 和相关 `bsp/package/*`。
- 会说明 persisted config 是否需要更新。
- 会选择 `config status`、`rootfs`、`drv <pkg>` 或更窄命令验证。

### 调试卡片入口：`debug-note.md`

聊天框复制：

```text
Use .agents/prompts/debug-note.md

Turn this development/debug process into an Obsidian-friendly knowledge card.
Topic:
Raw timeline/logs:
Final root cause if known:
Files changed:
Commands that proved the result:
```

实际效果：

- Codex 会按 `.agents/context/debug-note-template.md` 的结构输出。
- 长日志会被压缩为能证明结论的关键证据。
- 如果你要求写入仓库，会放到 `docs/`，并使用占位符脱敏。

## Skills：项目级和用户级工作流

### `$kernel-api-explainer`

路径：

```text
.agents/skills/kernel-api-explainer/SKILL.md
```

用途：

- 解释 Linux kernel API、宏、枚举、结构体字段、driver helper 或当前调用点的 API 使用。
- 先用 `rg` 找定义和相关 flag/enum，再给可跳转定义链接。
- 不贴源码，用中文按 Linux kernel-doc 风格说明作用、参数、返回值、原理和当前上下文为何这样用。
- 常规 API 问答优先快速响应，可使用轻量模型或低推理强度。

聊天框具体操作：

```text
Use $kernel-api-explainer.
解释当前 platform_get_resource(pdev, IORESOURCE_MEM, 0) 的作用、参数含义、可选值，以及这里为什么这样用。
```

效果：

- 输出定义位置链接，例如 `drivers/base/platform.c` 或 `include/linux/platform_device.h`。
- 对每个参数说明它代表什么、当前实参的含义、可选值或常见取值。
- 结合当前驱动上下文解释为什么使用该 API，而不是泛泛讲内核概念。

### `$imx6-bsp-engineer`

路径：

```text
.agents/skills/imx6-bsp-engineer/SKILL.md
```

用途：

- U-Boot、Linux、DTS、Buildroot、BusyBox、rootfs、本地外置驱动包。

聊天框具体操作：

```text
Use $imx6-bsp-engineer.
帮我修复 AP3216C 驱动无法创建设备节点的问题。先检查 src/ap3216c_drv、bsp/package/ap3216c、bsp/rootfs_overlay 和 BusyBox/mdev 配置。确认归属后再改文件。
```

效果：

- 先判断修改归属。
- 优先检查 DTS/config/package/rootfs。
- 只在硬件描述和集成路径一致后改驱动逻辑。
- 输出最窄验证命令和板端命令。

### `$imx6-dev-learning-loop`

路径：

```text
.agents/skills/imx6-dev-learning-loop/SKILL.md
```

用途：

- 完整开发闭环：实现、验证、学习总结、调试卡片、AI 组件迭代。

聊天框具体操作：

```text
Use $imx6-dev-learning-loop.
推进 README 中 OV5640 V4L2 bring-up 的下一步。请完成：仓库归属判断、最小修改建议、最窄验证命令、板端 v4l2-ctl 命令、学习总结和是否需要更新 prompt。
```

效果：

- 技术任务会路由到 `$imx6-bsp-engineer`。
- 完成后会用 `$driver-learning-coach` 风格输出学习内容。
- 调试证据充足时会用 `$embedded-dev-notes` 风格输出知识卡。
- 如果发现流程摩擦，会建议改 prompt、skill、AGENTS、MCP 模板或 memory。

### `$driver-learning-coach`

路径：

```text
.agents/skills/driver-learning-coach/SKILL.md
```

聊天框具体操作：

```text
Use $driver-learning-coach.
基于刚才 AP3216C mdev/rootfs 修复，生成当前工作总结、简历描述、架构学习说明、技术权衡和面试官深挖。
```

效果：

- 输出五段：当前工作总结、简历描述、架构学习说明、技术权衡、面试官深挖。
- 用硬件总线、DTS、内核子系统、Buildroot/rootfs、用户态验证的顺序解释。

### `$embedded-dev-notes`

路径：

```text
.agents/skills/embedded-dev-notes/SKILL.md
```

聊天框具体操作：

```text
Use $embedded-dev-notes.
把下面 AP3216C 中断调试过程写成 docs/AP3216C_irq_debug.md。日志只保留能证明结论的关键行，真实路径和 board IP 用占位符。
<粘贴日志>
```

效果：

- 输出 Obsidian-friendly Markdown。
- 保留现象、假设、验证、根因、修改、可复用经验、关联。
- 使用 `[[Device Tree]]`、`[[I2C]]`、`[[Buildroot]]`、`[[mdev]]` 等稳定链接词。

### `$git-commit-assistant`

路径：

```text
.agents/skills/git-commit-assistant/SKILL.md
```

聊天框具体操作：

```text
Use $git-commit-assistant.
检查当前 Git 变更，按意图拆分提交组并草拟 commit message。不要直接 commit，先给计划。
```

效果：

- 检查 super-project 和嵌套仓库状态。
- 识别 submodule pointer、kernel tree、U-Boot tree、BSP 和 docs 的边界。
- 避免把无关修改混在一个提交里。

### `$ai-system-architect`

路径：

```text
~/.codex/skills/ai-system-architect/SKILL.md
```

聊天框具体操作：

```text
Use $ai-system-architect.
请评审当前项目的 Codex AI 组件，指出哪些该留在项目层，哪些该放到用户层，并给出下一轮 prompt/skill/hook/MCP 改进计划。
```

效果：

- 设计或改进 VSCode+Codex AI 辅助开发系统。
- 选择 instruction、prompt、skill、hook、MCP、subagent、memory。
- 判断项目级和用户级边界。
- 输出目标、成功标准、当前状态、组件计划、验证 prompt 和隐私假设。

## Context：`.agents/context/`

| 文件 | 作用 | Codex 使用效果 |
| --- | --- | --- |
| `project.md` | 目标板、工具链、仓库地图、ownership rules | 先判断文件属于 super-project、Buildroot、kernel tree 还是 U-Boot tree |
| `build-deploy.md` | `bsp/build_and_deploy.sh` 模式和环境变量 | 选择 `dtb`、`zimage`、`rootfs`、`drv <pkg>`、`verify ...` 等最窄命令 |
| `learning-output.md` | 学习输出契约 | 开发后输出总结、简历 bullet、架构解释、权衡、面试题 |
| `debug-note-template.md` | 调试卡片形状 | 输出短知识卡，而不是长流水账 |

聊天框具体操作：

```text
Use $imx6-bsp-engineer.
请按 .agents/context/project.md 和 .agents/context/build-deploy.md 的规则，判断这次 rootfs 修改应该怎么验证。
```

## MCP：`.agents/mcp/`

已发现：

- `.agents/mcp/README.md`
- `.agents/mcp/codex.example.toml`

效果：

- 说明如何给 Codex 增加文件系统或笔记库上下文。
- 项目只保存模板，真实路径留在 `~/.codex/config.toml` 或通过 `codex mcp add` 添加。

项目工作区示例：

```bash
codex mcp add imx6-workspace -- npx -y @modelcontextprotocol/server-filesystem /home/<user>/imx6_driver_friedegg
```

私人笔记库示例：

```bash
codex mcp add imx6-notes -- npx -y @modelcontextprotocol/server-filesystem /home/<user>/imx6_driver_friedegg /home/<user>/<obsidian-vault>
```

检查：

```bash
codex mcp list
```

聊天框具体操作：

```text
Use $embedded-dev-notes.
如果 MCP 中有 imx6-notes，只读取与 AP3216C/mdev 相关的笔记作为参考；输出到本项目 docs/ 时必须脱敏，不写真实 vault 路径。
```

## Hooks、Subagents、Memory

### Hooks

当前项目没有提交可执行 hook。建议先设计只读提醒，不自动改文件：

```text
Use $ai-system-architect.
为本项目设计只读 hook 检查，不要自动修改文件。目标是提醒缺少验证命令、学习总结、调试卡片和隐私占位符。
```

适合检查：

- 开发任务是否缺最窄验证命令。
- 调试任务是否缺知识卡。
- 学习闭环是否缺总结。
- 文档是否出现真实 SSH、token、NFS/TFTP、board IP、Obsidian vault 绝对路径。

### Subagents

当前项目没有专门提交 subagent 配置。只有大范围搜索或并行 review 明显有价值时再请求：

```text
请使用一个只读 explorer 子代理检查 OV5640 相关 DTS、Kconfig、驱动和 Buildroot 包路径。主会话继续整理验证方案；子代理不要修改文件。
```

效果：

- 子代理负责并行探索或复核。
- 主会话负责最终工程判断、编辑和整合。

### Memory

适合放 memory：

- 默认用中文解释 Linux driver 概念。
- 学习总结要包含简历 bullet。
- 面试问题要给 expected discussion points。

不适合放 memory：

- 项目构建规则。
- 真实 board IP。
- 私有 vault 路径。
- SSH 用户、host、token。
- 必须随仓库复现的 BSP 规则。

聊天框具体操作：

```text
请把“驱动学习总结默认用中文，并包含简历 bullet 和面试深挖问题”作为我的个人偏好记住。不要把任何项目路径、board IP 或 SSH 信息写入 memory。
```

## 典型任务一键输入

### AP3216C 中断调试

```text
Use $imx6-dev-learning-loop.

Run an end-to-end development learning loop for AP3216C interrupt debug.

Goal: modprobe 后 /dev/ap3216c 存在，并确认中断触发路径。
Current behavior or symptom: 驱动 probe 成功，但用户态中断测试无输出。
Target hardware path:
- board/peripheral: i.MX6ULL + AP3216C
- bus/subsystem: I2C + GPIO IRQ + character device
- pins/clocks/resets/regulators/interrupts if known: 请从 DTS 检查
Relevant files or logs: 我会粘贴 dmesg、/proc/interrupts、测试程序输出
Expected user-space behavior: ap3216cApp 可读数据，中断模式有事件
Verification available:
- build command: bash bsp/build_and_deploy.sh drv ap3216c
- board-side command: 请给出
Learning output needed:
- docs note: yes
- Obsidian/private note: yes
- resume/interview summary: yes
AI component friction to watch for: 是否需要 prompt 默认要求 /proc/interrupts
```

### OV5640 V4L2 bring-up

```text
Use .agents/prompts/linux-port.md

Port or debug Linux support for OV5640 camera.
Hardware facts:
- bus/interface: I2C control + CSI/parallel data path
- pins: 请从 DTS 检查
- clock/reset/regulator: 请从 DTS 检查
- interrupt: unknown
- expected userspace node or subsystem: /dev/video0 and V4L2 controls
- current symptom: v4l2-ctl 无法枚举摄像头
```

### 提交前整理

```text
Use $git-commit-assistant.
检查当前 super-project 和子仓库状态，按意图拆分提交组并草拟 commit message。不要直接提交，先给计划。
```

## 组件选择规则

| 需求 | 优先组件 | 原因 |
| --- | --- | --- |
| 长期项目规则 | `AGENTS.md` | 自动生效，适合协作者和未来机器 |
| 标准化任务输入 | `.agents/prompts/` | 减少聊天框遗漏字段 |
| 多步可复用流程 | `.agents/skills/` | 让 Codex 按固定步骤读取上下文、执行和输出 |
| 稳定项目事实 | `.agents/context/` | 避免把大段事实复制到每个 prompt |
| 外部资料或私有笔记 | MCP | 连接外部上下文，真实路径留用户级 |
| 自动提醒 | hook/check | 适合缺验证、缺总结、隐私泄露检查 |
| 并行搜索/复核 | subagent | 适合大任务，主会话负责整合 |
| 个人偏好 | memory 或 `~/.codex/AGENTS.md` | 不污染项目级规则 |

## 验证命令选择

按变更范围选择最窄命令：

```bash
bash bsp/build_and_deploy.sh dtb
bash bsp/build_and_deploy.sh zimage
bash bsp/build_and_deploy.sh rootfs
bash bsp/build_and_deploy.sh drv <pkg>
bash bsp/build_and_deploy.sh verify all|dtb|zimage|nfs-pkg <pkg>|ko <module-or-pkg>
bash bsp/build_and_deploy.sh config status
```

聊天框可直接要求：

```text
请只选择能证明这次改动的最窄验证命令。不要默认 full rebuild。如果需要板端验证但无法运行，请给 exact board-side commands 和 expected result。
```

## 用户级配置边界

本次扫描到 `~/.codex/config.toml` 中包含模型、review 模型、reasoning effort、网络访问、provider 和项目 trust 配置。它们会影响 Codex 默认使用的模型、权限和本项目是否被视为可信工作区。

项目文档只记录这些配置的“类别和影响”，不记录：

- 真实 provider endpoint。
- `~/.codex/auth.json`。
- session/log SQLite。
- shell snapshots。
- installation id。

聊天框可这样要求脱敏分析：

```text
Use $ai-system-architect.
请只基于脱敏信息解释我的用户级 Codex config 对本项目开发体验的影响。不要把真实 provider endpoint、auth、session 或 logs 写入仓库。
```

## 隐私占位符

项目文档、prompt、skill 和 MCP 示例统一使用：

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

## 官方参考

- OpenAI Codex 产品页：https://openai.com/codex
- OpenAI Codex IDE extension 更新说明：https://openai.com/index/introducing-upgrades-to-codex/
- OpenAI Help Center Codex 使用概览：https://help.openai.com/en/articles/11369540/
- OpenAI Codex skills 说明：https://openai.com/index/introducing-the-codex-app/
- OpenAI Docs MCP 说明：https://platform.openai.com/docs/docs-mcp
- AGENTS.md 开放格式说明：https://agents.md/
