# qmake 最小工程

## 目标/前置

目标是在 `src/app/` 内手动替换现有半成品，手写两个最小 target：作为主线的 target Qt Widgets 应用，以及可在 host 辅助运行的不依赖 Qt 的 MonitorEngine 断言测试，并证明 C++11 和 i.MX6ULL 交叉编译/板端运行链路。所有构建都使用 source tree 之外的独立 build 目录。前置是理解 [架构与所有权](02-architecture-and-ownership.md)，并能区分 host compiler 与 target compiler。

## 最低必懂模型

qmake 读取 `.pro` 生成 Makefile；make 再调用该 qmake 配套 mkspec 中的编译器。用 host qmake 生成的程序不能拿到 ARM 板运行，用 target qmake 生成的 ARM 程序通常也不能在 x86 主机执行。

第一步不需要 `TEMPLATE = subdirs`。先让一个 target 闭环，再决定是否增加顶层子工程：

```text
app.pro -> Qt Widgets executable for target（开发主线）
tests/monitor_engine_test.pro -> pure C++ console executable for host（辅助验证）
```

## 文件或数据流

最小手写集合：

```text
app.pro
main/main.cpp
application/smart_monitor/monitor_types.h
application/smart_monitor/monitor_engine.{h,cpp}
tests/monitor_engine_test.cpp
tests/monitor_engine_test.pro
```

构建数据流：

```text
.pro + SOURCES/HEADERS -> qmake -> Makefile -> compiler/linker -> executable
```

## 分步手写任务

1. 保持 `src/app/` 路径不变，先审查后手动替换半成品文件；不整文件复制旧类，也不另建脱离交付路径的练习树。
2. 写 `main/main.cpp`，只创建 `QApplication`、显示一个带标题的 `QWidget` 并进入 event loop。
3. 写最小 `app.pro`，声明 `QT += widgets`、`CONFIG += c++11`、`TEMPLATE = app`、target、source 和 header。
4. 用 Buildroot 产出的 target qmake 在独立 build 目录生成 Makefile；使用 `file` 检查二进制架构，并部署到板端运行最小窗口。
5. 如确有需要，可另用 host qmake 和另一 build 目录构建桌面窗口辅助排查工程描述，但它不作为主线验收。
6. 为纯 Engine 测试写独立 console `.pro`，设置 `QT -= gui core` 或 `CONFIG -= qt`，避免测试意外依赖 Qt。
7. 测试 target 只编入 Engine 和 test main，不编入 Controller、Widget、Worker。
8. 当两个 target 都稳定后，再评估顶层 `subdirs`；不要为了目录外观提前引入。

## 关键代码片段或伪代码

应用 `.pro` 的关键形状：

```qmake
QT += widgets
CONFIG += c++11
TEMPLATE = app
TARGET = <app-binary>

SOURCES += main/main.cpp
# 练习：逐阶段添加真实 SOURCES，不使用宽泛通配符。
```

纯 C++ 测试 `.pro` 的关键形状：

```qmake
CONFIG += console c++11
CONFIG -= app_bundle qt
TEMPLATE = app
TARGET = <monitor-engine-test>

SOURCES += monitor_engine_test.cpp \
           ../application/smart_monitor/monitor_engine.cpp
```

练习：故意漏掉 Engine `.cpp`，观察链接错误；再补回 source，说明“头文件声明”和“目标文件定义”分别解决什么问题。

## 检查点

- `.pro` 中列出的每个路径都相对该 `.pro` 所在位置正确。
- 应用使用 Qt5 Widgets 和 C++11，没有 QML/QtMultimedia 等额外模块。
- 测试 binary 不链接 Qt，也不需要 `QApplication`。
- host 与 target 构建目录分离，切换 qmake 后不会复用旧 Makefile。
- `file <app-binary>` 显示目标 ARM 架构后才部署到板端。

## 常见错误

- target qmake 和 host qmake 混用，随后只反复执行 make。
- 修改 `.pro` 后没有重新运行 qmake。
- 在 source tree 内混合 host/target 中间文件。
- 一开始采用 `subdirs`，但子目录没有对应 `.pro`，导致 qmake 无法形成 target。
- 测试链接 Qt Core，只因为值类型误用了 `QString` 或 Qt 整数类型。
- 用通配符自动吸入未完成 `.cpp`，让最小构建失去可控性。

## 最窄验证

主机纯 C++ 测试：

```bash
mkdir -p /tmp/monitor-test-build
"${HOST_QMAKE}" -o /tmp/monitor-test-build/Makefile "${SOURCE_DIR}/tests/monitor_engine_test.pro"
make -C /tmp/monitor-test-build -j2
"/tmp/monitor-test-build/${MONITOR_ENGINE_TEST}"
```

目标应用：

```bash
mkdir -p /tmp/app-target-build
"${BUILDROOT_DIR}/output/host/bin/qmake" -o /tmp/app-target-build/Makefile "${SOURCE_DIR}/app.pro"
make -C /tmp/app-target-build -j2
file "/tmp/app-target-build/${APP_BINARY}"
```

以上命令从仓库根目录执行。`qmake -o` 把 Makefile 写入指定 build 目录，`make -C` 明确在该目录构建，避免 host/target 中间文件进入 `src/app/` 或相互复用。

部署后板端运行：

```bash
QT_QPA_PLATFORM=linuxfb "${APP_BINARY}"
```

预期：测试返回 0；`file` 表明是 ARM ELF；板端出现最小 Widget 且可正常退出。

## 完成标准

- 从清理后的 build 目录可重复生成 host test 和 target app。
- 能解释 qmake、Makefile、compiler、linker 各自职责。
- 测试与 Qt UI target 源文件边界清楚。
- 尚未实现的目录和类没有进入构建。

## 复盘问题

1. 为什么换 qmake 后必须使用新的 build 目录？
2. 编译错误和链接错误分别说明构建链路走到了哪一步？
3. 纯 Engine 使用标准库类型对 host 测试有什么直接收益？
4. 何时 `TEMPLATE = subdirs` 才真正有价值？
5. 板端窗口不显示时，如何区分 binary 架构、Qt plugin 和 framebuffer 环境问题？
