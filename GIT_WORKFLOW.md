# Git 工作空间版本管理指南 (GIT Workflow)

## 一、 当前多仓管理结构（解决“10K+ 文件”冗余的核心逻辑）

在嵌入式 Linux/Buildroot 开发中，诸如 Kernel、U-Boot 这类重型源码文件动辄几万个文件。若全盘放入一个主仓库中追踪，会导致仓库快速膨胀（从而引发编辑器提示过载/10K+ 等卡顿）。

为了解决这个痛点以及实现工程“最佳实践”，我们当前把整个项目演变成了一个 **Super-project（主仓） + 多 Sub-project（子仓）** 的架构。

目前的结构层级如下：

```
my_imx6_workspace/              <-- 【主仓库】只记录这层核心逻辑
├── .git/                       
├── bsp/                        <-- 属于主仓库追踪（你的 Buildroot 配置与脚本）
├── src/ap3216c_drv/            <-- 属于主仓库追踪（完全原生的自写驱动）
│
├── buildroot/                  <-- 【独立子仓库】官方编译引擎，以 Gitlink 指向
├── src/linux-imx/              <-- 【独立子仓库】移植后的内核，内部自建 git 追踪
└── src/uboot-imx/              <-- 【独立子仓库】移植后的Uboot，内部自建 git 追踪
```

### 为什么 `src/linux-imx` 和 `src/uboot-imx` 需要独立 git？
即使这是你“移植修改后”的内容，不再与 NXP 官方源码对齐，采用独立局部建仓（内部带有单独 `.git`）再链到外层仓库也是非常有用的：
1. **轻量化主仓**：外层主仓只会记录里面那个独立的 `Commit ID`，无需将六万个源码文件加入 `git add`。
2. **便于调试溯源**：你在针对 Kernel 打驱动补丁时，修改的是 `src/linux-imx` 内部环境，可以单独进行内部提交，保持历史清晰。

---

## 二、 常用 Git 开发命令及场景

既然变成了多个仓库嵌套，平时的提交流程会有所不同。请参考以下常见开发闭环：

### 场景一：只修改了外部配置文件（如 `bsp/` 里的脚本 或 `ap3216c_drv`）
这种修改**只属于主仓库**。
```bash
# 回到工作空间根目录
cd /home/friedegg/my_imx6_workspace

git status
git add bsp/ src/ap3216c_drv/
git commit -m "更新系统打包配置或外置驱动逻辑"
```

### 场景二：修改了 Kernel (内核) 或 U-Boot 下的代码（如移植修改）
假设你修改了 `src/linux-imx/arch/arm/boot/dts/imx6ull-friedegg-emmc.dts`，此时需要：**先进子仓提交 -> 再回主仓绑定最新指针**
```bash
# 1. 进入子模块内部提交修改
cd src/linux-imx
git add arch/arm/boot/dts/imx6ull-friedegg-emmc.dts
git commit -m "dts: imx6ull-friedegg-emmc: fix ap3216c interrupt to GPIO1_IO01"

# 2. 退回主仓库根目录，更新子模块指针
cd /home/friedegg/my_imx6_workspace
git add src/linux-imx   # 此时 stage 的是子模块的新 commit ID 指针
git commit -m "chore: update linux-imx submodule for ap3216c dts fix"
```

### 场景二补充：在 VS Code 中查看子模块的内部变更

VS Code 的 Git 插件默认只显示主仓库的变更（即子模块的**指针变化**，`src/linux-imx` 显示为绿色修改，不展开内部文件）。要查看子模块内部具体改了哪些文件：

**方式一（推荐）—— 终端命令：**
```bash
# 查看子模块最近一次提交的详细 diff
cd src/linux-imx
git log -p -1

# 查看子模块相对于主仓记录的上次指针，有哪些新提交
cd /home/friedegg/my_imx6_workspace
git diff src/linux-imx   # 显示子模块指针的 commit id 变化

# 列出所有子模块最近的 3 条提交（不用手动 cd）
git submodule foreach 'git log --oneline -3'
```

**方式二（VS Code 图形化）：**
1. 在 VS Code 中打开 `src/linux-imx/` 下的文件，文件编辑器内会正常显示 git 行级标注（blame / 修改标记）
2. **源代码管理面板** (Ctrl+Shift+G) → 在 `src/linux-imx` 项上 **右键 → 在集成终端中打开**
3. 在该终端内用 `git log` / `git diff` 查看子模块内部的变更细节
4. 子模块内文件的 `Stage Changes` 和 `Commit` 请在子模块自己的终端上下文中完成，不要在 VS Code 的主仓库 Git 面板中操作

### 场景三：日后在新电脑完整拉取 (Clone) 开发环境
由于包含了嵌套的 Git 项目，如果在远端新建了私有云仓库，未来 clone 必须递归获取：
```bash
# 一次性将主环境及挂载的内核、Uboot统一下载
git clone <主库URL> --recursive 

# 或者如果你已经普通的 clone 了主仓库，想要进一步补全里面空壳的 linux 等文件夹：
git submodule update --init --recursive
```

---

## 三、 本地备份推荐说明
你目前在 `src/linux-imx/` 以及 `src/uboot-imx/` 中的修改是**纯本地版控**的。
如果将来你购买/搭建了如 GitHub、Gitee 或者本地 GitLab 服务，你可以：
1. 在 Gitee 上新建三个仓库：`main-workspace`、`my-linux-imx`、`my-uboot-imx`。
2. 分别进入那三个对应的目录中，去执行各自的 `git remote add origin <各自独立仓库地址>`。
3. 把它们分别推送到云端即可完成完美的工程安全托管。

## 四、提交信息规范

### 4.1 格式模板

```
<类型>(<模块>): <一行标题 (≤ 50 字符)>

<详细说明（可选，写 what 和 why，不写 how）>
```

### 4.2 标题行规则
- **首字母小写**：`fix:` 不是 `Fix:`
- **不超过 50 个字符**，句末不加句号
- **用祈使句**：`fix bug` 不是 `fixed bug` 或 `fixes bug`

### 4.3 正文规则
- 正文和标题之间空一行
- 正文解释 **what（改了啥）** 和 **why（为什么改）**，不需要写 **how（怎么改的）**——代码本身已经说明 how
- 每行建议 ≤ 72 字符

### 4.4 类型前缀

| 前缀 | 何时用 | 示例 |
|------|--------|------|
| `fix` | 修复 bug | `fix: ap3216c readdata returns wrong ps value` |
| `feat` | 新增功能 | `feat: add ioctl cmd to set ps trigger threshold` |
| `dts` | 设备树变更专用 | `dts: imx6ull-friedegg-emmc: fix ap3216c int pin` |
| `driver` | 驱动代码变更专用 | `driver: ap3216c: fix interrupt not triggering` |
| `docs` | 文档/README | `docs: update GIT_WORKFLOW with commit conventions` |
| `refactor` | 重构，不改行为 | `refactor: extract event mode switch to separate func` |
| `chore` | 构建脚本、子模块指针等杂项 | `chore: update linux-imx submodule` |

### 4.5 完整示例

```
dts: imx6ull-friedegg-emmc: fix ap3216c interrupt to GPIO1_IO01

The AP3216C INT pin is connected to GPIO1_IO01 (pin 1), but the device
tree was using the wrong mapping. Correct the 'interrupts' property to
<1 IRQ_TYPE_LEVEL_LOW> so the driver can receive hardware interrupts.
```

### 4.6 子模块指针提交约定

主仓库中更新子模块指针时，用 `chore:` 前缀，并在正文中列出子模块内部的 commit 摘要：

```
chore: update linux-imx submodule

- dts: fix ap3216c interrupt to GPIO1_IO01
```
