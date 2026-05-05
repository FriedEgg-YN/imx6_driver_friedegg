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
假设你修改了 `src/linux-imx/drivers/video/` 下的文件，此时需要：**先进子仓提交 -> 再回主仓绑定最新指针**
```bash
# 1. 深入子仓局部记录修改
cd src/linux-imx
git add .
git commit -m "修改了某个视频驱动支持"

# 2. 退回根目录，让主工作区感知内核的“版本号指针”已经更新
cd ../..
git add src/linux-imx   # 注意这里 add 会提示你绑定了新的内核提交指针
git commit -m "Bump: 更新内核版本引用"
```

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