# LD2410Device Qt 封装学习笔记

本文只讲 `src/imx6_smart_monitor/sensors/ld2410_device.*` 这一层用户态实现。内核驱动、TTY line discipline、misc UAPI 的完整链路已经放在 [`../../ld2410c/docs/ld2410c-driver-and-attach.md`](../../ld2410c/docs/ld2410c-driver-and-attach.md)。

目标读者是假设只懂少量 C++ 和 Qt 基础的新手。本文先搭骨架，并先填入最容易卡住的点；后续追问时可以按章节继续补全。

## 阅读入口

| 文件 | 作用 |
| --- | --- |
| [`../sensors/ld2410_device.h`](../sensors/ld2410_device.h) | 声明 `Ld2410Probe`、`Ld2410State`、`Ld2410Config` 和 `Ld2410Device`。 |
| [`../sensors/ld2410_device.cpp`](../sensors/ld2410_device.cpp) | 实现节点探测、设备打开、UART attach、状态读取和配置 ioctl。 |
| [`../qt/apps/ld2410_test/ld2410_test.pro`](../qt/apps/ld2410_test/ld2410_test.pro) | `imx6-sm-ld2410-test` 的 qmake 工程入口。 |
| [`../qt/apps/app_common.pri`](../qt/apps/app_common.pri) | 各 Qt 测试程序共用的 include path、Qt 模块、输出目录和公共源码。 |
| [`../qt/apps/ld2410_test/ld2410_test_window.cpp`](../qt/apps/ld2410_test/ld2410_test_window.cpp) | 测试窗口如何调用 `Ld2410Device`。 |
| [`../../ld2410c/include/friedegg/ld2410c.h`](../../ld2410c/include/friedegg/ld2410c.h) | 内核和用户态共享的 UAPI 结构体、flag、ioctl 编号。 |

## 最低必懂模型

这一层不是驱动，也不是 UI 控件本身，而是夹在二者之间的“设备访问小封装”：

```text
Qt 测试窗口 / 主程序控制器
  -> Ld2410Device
      -> /dev/ld2410c0 misc fd
          -> ioctl/read/poll 获取状态或配置
      -> /dev/ttymxc2 UART fd
          -> TIOCSETD(LD2410C_LDISC) 维持 line discipline 绑定
  -> Ld2410Probe / Ld2410State / Ld2410Config
      -> Qt UI 或 MonitorCore 使用的值对象快照
```

`Ld2410Device` 自己不创建线程，也不是 `QObject`，没有 signal/slot。调用侧在 UI 线程里周期读取 `readState()` 是可以接受的；但配置、校准、重启这类可能等待 ACK 或设备响应的操作，后续如果变复杂，应考虑移到工作线程。

## 第一层：C++ 语法骨架

### 1.1 `class Ld2410Device` 是什么

`Ld2410Device` 是一个普通 C++ 类，负责管理两个 Linux 文件描述符：

- `miscFd`：打开 `/dev/ld2410c0` 后得到，用于 `ioctl()`、`poll()`、`read()`。
- `uartFd`：打开 `/dev/ttymxc2` 后得到，用于保持 `LD2410C_LDISC` 绑定。

它的构造函数把 fd 初始化成 `-1`，表示“当前没有打开”。析构函数负责关闭还活着的 fd。这是 RAII 思路：对象活着时资源有效，对象销毁时自动释放资源。

待补：

- 构造函数初始化列表 `: miscFd(-1), uartFd(-1)` 逐句解释。
- `public` / `private` 的访问控制。
- 命名空间 `namespace imx6sm`。
- 头文件 include guard。

### 1.2 为什么禁止拷贝构造

代码中有这一句：

```cpp
Ld2410Device(const Ld2410Device &) = delete;
```

它删除的是“拷贝构造函数”。典型触发方式是：

```cpp
Ld2410Device a;
Ld2410Device b(a);      // 禁止
Ld2410Device c = a;     // 也是拷贝构造，禁止
```

这里必须禁止的核心原因是：`Ld2410Device` 拥有 fd 资源。如果默认拷贝，C++ 只会把 `miscFd` 和 `uartFd` 这两个整数原样复制到另一个对象里。这样两个对象会以为自己都拥有同一个 fd，析构时可能关闭两次，或者一个对象先关闭后，另一个对象还拿着已经失效的 fd 继续 `ioctl()`。

所以这里不是“C++ 类都要禁止拷贝”，而是“拥有独占资源的类，不能接受默认浅拷贝”。

待补：

- 什么是浅拷贝和深拷贝。
- fd、指针、堆内存、锁这类资源为什么常常需要自定义拷贝语义。
- 如果未来要支持移动语义，`Ld2410Device(Ld2410Device &&)` 应该如何设计。

### 1.3 为什么禁止赋值操作符

代码中还有这一句：

```cpp
Ld2410Device &operator=(const Ld2410Device &) = delete;
```

它删除的是“拷贝赋值操作符”。典型触发方式是：

```cpp
Ld2410Device a;
Ld2410Device b;
b = a;                  // 禁止
```

它和拷贝构造的区别是：

- 拷贝构造：创建新对象时，用旧对象初始化它。
- 拷贝赋值：两个对象都已经存在，再把右边对象的内容赋给左边。

这两个操作都可能复制 fd 整数，所以都要禁掉。只禁一个是不完整的。

待补：

- `operator=` 的返回值为什么是 `Ld2410Device &`。
- `const Ld2410Device &` 参数为什么用引用加 const。
- “Rule of three/five/zero” 在这个类里的体现。

### 1.4 类成员函数末尾的 `const` 起什么作用

例如：

```cpp
QString findMiscPath() const;
Ld2410Probe probe(const QString &miscPath,
                  const QString &outPath,
                  const QString &uartPath) const;
```

函数末尾的 `const` 表示：这个成员函数承诺不修改当前对象的成员状态。也就是说，在这些函数内部不能直接改 `miscFd`、`uartFd`、`openedMiscPath`。

为什么这里要加：

- `findMiscPath()` 只是检查 `/dev/ld2410c0` 是否存在，不需要改变对象。
- `findInputHint()` 只是读 `/proc/bus/input/devices`，不需要改变对象。
- `defaultUartPath()` 只是返回默认路径。
- `probe()` 虽然会生成一个 `Ld2410Probe` 结果，但结果是局部变量，不是对象成员；它只读取 `miscFd >= 0` 来判断是否 attached。

好处有两个：

- 读代码的人一眼知道这些函数是“观察/查询”，不是“打开/关闭/配置”。
- 如果以后有 `const Ld2410Device &device`，仍然可以调用这些查询函数。

待补：

- 成员函数末尾 `const` 和参数里的 `const QString &` 有什么区别。
- `mutable` 是什么，为什么这里不需要。
- `static` 成员函数为什么没有末尾 `const`。

### 1.5 指针参数 `QString *error = nullptr`

很多函数写成：

```cpp
bool readState(Ld2410State *state, QString *error = nullptr);
```

这是 C/C++ 常见风格：函数用 `bool` 返回成功或失败，详细错误文本通过可选输出参数带出来。调用者关心错误时传 `&error`，不关心时可以不传。

`setError(QString *target, const QString &message)` 里先判断 `if (target)`，就是为了允许 `error == nullptr`。

待补：

- 输出参数和返回值怎么配合。
- 为什么这里没有直接抛 C++ exception。
- `nullptr` 和旧式 `NULL` 的区别。


### 1.6 本轮补充：C++ 待补内容集中填坑

构造函数初始化列表：

```cpp
Ld2410Device::Ld2410Device()
    : miscFd(-1)
    , uartFd(-1)
{
}
```

`:` 后面的部分叫“成员初始化列表”。它在进入构造函数 `{}` 函数体之前初始化成员。这里把两个 fd 设成 `-1`，表示“当前没有打开任何文件”。这样析构函数和 `closeDevice()` 只要判断 `fd >= 0`，就知道是否需要关闭。对基础类型如 `int` 来说，也可以在函数体里写 `miscFd = -1;`，但初始化列表更直接；对引用、`const` 成员、没有默认构造函数的对象，初始化列表是必须的。

`public` / `private`：

- `public` 是外部调用者能使用的接口，例如 `openDevice()`、`readState()`、`attachUart()`。
- `private` 是类内部实现细节，例如 `ensureOpen()`、`convertState()`、`setError()`、`miscFd`、`uartFd`。

这种分层让调用者只依赖稳定接口，不直接改 fd。比如外部不能随手写 `device.miscFd = 100;`，避免破坏资源生命周期。

`namespace imx6sm`：

`namespace` 是命名空间，作用是给一组名字加前缀，避免和别的模块撞名。`imx6sm::Ld2410Device` 表示 Smart Monitor 模块里的 LD2410 设备类。以后如果另一个库也有 `Ld2410Device`，只要命名空间不同，就不会冲突。

头文件 include guard：

```cpp
#ifndef IMX6SMARTMONITOR_LD2410_DEVICE_H
#define IMX6SMARTMONITOR_LD2410_DEVICE_H
...
#endif
```

这是防止同一个头文件在一次编译里被重复包含。第一次包含时宏还没定义，于是进入文件内容并定义宏；第二次包含时宏已经定义，预处理器会跳过文件内容，避免重复定义 `struct` / `class`。

浅拷贝和深拷贝：

- 浅拷贝：只复制成员的表面值。对 `int miscFd` 来说，就是复制 fd 数字。
- 深拷贝：复制资源本身，或创建语义上独立的新资源。

fd、裸指针、堆内存、锁、线程句柄这类成员通常不能接受默认浅拷贝。`Ld2410Device` 的 fd 是独占资源；两个对象共享同一个 fd 数字，会导致双重关闭、关闭后误用、状态错乱。

如果未来支持移动语义，思路是“转移所有权”：

```cpp
Ld2410Device::Ld2410Device(Ld2410Device &&other) noexcept
    : miscFd(other.miscFd)
    , uartFd(other.uartFd)
    , openedMiscPath(other.openedMiscPath)
{
    other.miscFd = -1;
    other.uartFd = -1;
    other.openedMiscPath.clear();
}
```

重点不是复制 fd，而是把 fd 从旧对象搬到新对象，并把旧对象置成“不再拥有 fd”。本项目当前没有移动需求，所以只删除拷贝，保持类更简单。

`operator=` 为什么返回 `Ld2410Device &`：

赋值操作符通常返回左侧对象引用，支持链式赋值：

```cpp
a = b = c;
```

虽然当前 `operator=` 被 `= delete` 禁止了，签名仍写成常规形态，表示“如果存在，它本应是拷贝赋值操作符”。返回引用还能避免返回对象副本。

`const Ld2410Device &` 参数为什么用引用加 `const`：

- `&`：不复制整个对象，提高效率。
- `const`：承诺函数不会修改传入对象。

在这个类里即使引用很高效，也仍然禁止拷贝，因为引用参数只是“借看对象”，不是“复制拥有资源的新对象”。

Rule of three/five/zero：

- Rule of three：如果类自定义析构、拷贝构造、拷贝赋值中的一个，通常要考虑另外两个。
- Rule of five：C++11 后还要考虑移动构造、移动赋值。
- Rule of zero：如果资源都交给标准库对象管理，最好一个都不自定义。

`Ld2410Device` 自定义析构函数关闭 fd，所以必须显式考虑拷贝。当前选择是：析构函数自己管资源，拷贝构造和拷贝赋值删除，移动暂不提供。

成员函数末尾 `const` 和参数 `const QString &`：

- `QString findMiscPath() const` 里的末尾 `const` 修饰“当前对象 `this`”，表示函数内不能改成员。
- `const QString &miscPath` 里的 `const` 修饰“参数”，表示函数内不能改调用者传进来的字符串。

一个管对象自己，一个管传入参数，位置不同，含义不同。

`mutable`：

`mutable` 可以让某个成员即使在 `const` 成员函数里也能被修改，常用于缓存、统计计数、懒加载。这里不需要，因为 `probe()`、`findMiscPath()` 等查询函数没有必要更新缓存；保持纯查询更容易理解。

`static` 成员函数为什么没有末尾 `const`：

`static` 成员函数不绑定具体对象，没有 `this` 指针。末尾 `const` 的本质是修饰 `this`，所以 `static` 函数不能写末尾 `const`。例如 `static Ld2410State convertState(...)` 只是一个和类放在一起的转换 helper，不读取 `miscFd`、`uartFd`。

`::close` 前为什么有两个冒号：

`::close(uartFd)` 里的 `::` 是 C++ 的作用域解析运算符。放在最前面表示“从全局命名空间找 `close` 函数”。这里调用的是 POSIX/Linux 的全局 C 函数 `close()`，不是类成员函数。

为什么要这样写？因为类里也有一个成员函数 `closeDevice()`，将来如果类或命名空间里出现同名 `close`，`::close` 仍然明确指向系统调用封装函数。它是一种很常见的 C++ 写法：调用全局 C API 时加 `::`，让读代码的人知道这是系统函数。

`QString *error = nullptr` 的精确含义：

这句话同时表达两层意思：

- `= nullptr` 是默认实参：调用时可以少传这个参数。
- 类型是指针，而且函数内部检查 `if (error)`：说明这个参数允许为 `nullptr`。

所以这两个效果都成立。比如：

```cpp
device.readState(&state);          // 少传 error，等价于 error = nullptr
device.readState(&state, nullptr); // 明确表示不接收错误文本
device.readState(&state, &error);  // 接收错误文本
```

如果函数内部没有判空，虽然语法上仍能传 `nullptr`，但运行时解引用会崩。当前实现用 `setError()` 统一判空，所以这个可选输出参数是安全的。

## 第二层：Qt 类型和构建机制骨架

### 2.1 `.pro`、`.pri`、Makefile 的关系

这个工程使用 qmake。

```text
imx6_smart_monitor.pro
  -> TEMPLATE = subdirs
  -> SUBDIRS 逐个进入 qt/apps/*

qt/apps/ld2410_test/ld2410_test.pro
  -> TARGET = imx6-sm-ld2410-test
  -> include($$ROOT/qt/apps/app_common.pri)
  -> SOURCES 加入 ld2410_test_window.cpp 和 sensors/ld2410_device.cpp
  -> HEADERS 加入 ld2410_test_window.h 和 sensors/ld2410_device.h

qt/apps/app_common.pri
  -> QT += widgets
  -> CONFIG += c++11 warn_on
  -> DESTDIR = $$ROOT/bin
  -> INCLUDEPATH 加入 ROOT、include、qt/common、ld2410c UAPI include
  -> SOURCES/HEADERS 加入公共 Qt 窗口代码

qmake
  -> 生成 Makefile

make
  -> 按 Makefile 编译链接出 bin/imx6-sm-ld2410-test
```

`.pro` 是一个目标程序的工程描述，`.pri` 是被多个 `.pro` 复用的片段，`Makefile` 是 qmake 根据这些描述生成的构建脚本。通常维护 `.pro/.pri`，不手改生成出来的 Makefile。

待补：

- `$$ROOT` 的含义。
- `SOURCES` 和 `HEADERS` 对编译有什么影响。
- `INCLUDEPATH` 为什么能让 `#include "sensors/ld2410_device.h"` 找到文件。
- `QT += widgets` 会带来哪些 Qt 模块。

### 2.2 `QString` 和 `std::string` 有什么差异

`QString` 是 Qt 自己的字符串类，和 Qt UI、文件、文本、国际化接口深度配合。它不是 `std::string` 的简单别名。

在本文件里，`QString` 主要用于：

- 保存设备路径，例如 `/dev/ld2410c0`。
- 拼接错误信息，例如 `QStringLiteral("open %1: %2").arg(...)`。
- 从系统文本转换成人可读信息，例如 `QString::fromLocal8Bit(std::strerror(errno))`。
- 和 Qt 控件交互，例如 `QLineEdit::text()` 返回 `QString`。

但 Linux 的 `open()` 接收的是 `const char *`，所以要先转成字节数组：

```cpp
const QByteArray pathBytes = path.toLocal8Bit();
miscFd = ::open(pathBytes.constData(), O_RDWR | O_CLOEXEC);
```

这里的注意点是：`pathBytes` 必须在 `open()` 调用期间活着，不能写成一个已经销毁的临时对象再拿 `constData()`。

待补：

- `QStringLiteral()` 为什么常用于固定字符串。
- `QLatin1Char(' ')` 和普通 `' '` 的差异。
- `QString::arg()` 的链式替换规则。
- `toLocal8Bit()`、`fromLocal8Bit()`、`fromLatin1()` 的使用边界。

### 2.3 `quint8`、`quint16`、`quint32`、`quint64` 是什么

这些类型来自 Qt 的 `<QtGlobal>`：

- `quint8`：无符号 8 位整数。
- `quint16`：无符号 16 位整数。
- `quint32`：无符号 32 位整数。
- `quint64`：无符号 64 位整数。

它们和标准 C++ 的 `std::uint8_t`、`std::uint16_t`、`std::uint32_t`、`std::uint64_t` 目标很像，都是为了明确位宽。这里选择 Qt 类型，主要是为了和 Qt 代码风格一致，也让 UI 层、Qt 容器、Qt 元类型生态里读起来更统一。

需要注意：

- `quint8` 本质上通常接近 `unsigned char`，直接打印时可能被当作字符，所以 UI 显示常用 `QString::number()` 或先转成 `int`。
- 从 `QSpinBox::value()` 转成 `quint8` 前，要保证 spin box range 已限制在 0 到 255 或更小。
- 和内核 UAPI 结构体交互时，要确认位宽一致，不要用普通 `int` 猜大小。

待补：

- Qt 整数类型和 `<cstdint>` 标准类型如何互转。
- 为什么 UAPI 头文件里用 `__u8`、`__u16`、`__u32`，Qt 层用 `quint8`。
- signed/unsigned 混用的常见坑。

### 2.4 本文件用到的 Qt 类

| Qt 类 | 在当前实现中的作用 |
| --- | --- |
| `QFileInfo` | 判断 `/dev/ld2410c0`、input event、UART 节点是否存在。 |
| `QFile` | 打开 `/proc/bus/input/devices`。 |
| `QTextStream` | 逐行读取 `/proc/bus/input/devices`。 |
| `QStringList` | 拆分 `H:` 行，寻找 `eventX`。 |
| `QByteArray` | 把 `QString` 路径转换成 `open()` 可用的字节缓冲。 |

待补：

- `QIODevice::ReadOnly | QIODevice::Text` 是什么。
- `Qt::SkipEmptyParts` 是什么。
- Qt 的隐式共享会不会影响这里的性能和生命周期。


### 2.5 本轮补充：Qt 和 qmake 待补内容集中填坑

`$$ROOT` 的含义：

在 `ld2410_test.pro` 里有：

```qmake
ROOT = ../../..
include($$ROOT/qt/apps/app_common.pri)
```

`ROOT` 是 qmake 变量，`$$ROOT` 是取变量值。因为 `ld2410_test.pro` 位于 `qt/apps/ld2410_test/`，向上三级就是 `src/imx6_smart_monitor/`。所以后面可以用 `$$ROOT/sensors/ld2410_device.cpp` 指到模块根目录下的源码。

`SOURCES` 和 `HEADERS`：

- `SOURCES += xxx.cpp`：告诉 qmake 这些 `.cpp` 要参与编译，最终变成 `.o` 并链接到程序。
- `HEADERS += xxx.h`：告诉 qmake 这些头文件属于工程。对普通 C++ 编译来说，头文件不是单独编译单元；但 qmake 会用它们做依赖跟踪。若头文件里有 `Q_OBJECT`，还可能触发 moc 生成代码。

`INCLUDEPATH`：

`INCLUDEPATH += $$ROOT $$ROOT/include ...` 会传给编译器，类似 `-I<path>`。这样源码里写：

```cpp
#include "sensors/ld2410_device.h"
```

编译器会在 `$$ROOT` 下寻找 `sensors/ld2410_device.h`。

`QT += widgets`：

这表示当前程序链接 Qt Widgets 模块，并启用相关 include/link 配置。`Ld2410TestWindow` 用到了 `QWidget`、`QPushButton`、`QLabel`、`QTabWidget`、`QSpinBox` 等控件，所以需要 widgets。`QString`、`QFile`、`QTextStream` 属于 QtCore，通常 Qt Widgets 会间接依赖 QtCore；但从概念上要分清：字符串/文件是 Core，窗口控件是 Widgets。

`QStringLiteral()` 是什么：

`QStringLiteral("/dev/ld2410c0")` 是 Qt 提供的宏/工具，用来把编译期固定字符串高效地构造成 `QString`。它适合用于不会变化的字面量，比如设备路径、UI 固定文本、错误模板。

为什么不直接写 `QString("/dev/ld2410c0")`？直接从窄字符串构造 `QString` 往往涉及运行时编码转换；`QStringLiteral` 让 Qt 在编译期准备更接近 `QString` 内部格式的数据，减少运行时开销，也避免编码意图不清。

`Literal` 后缀的含义：

`literal` 在编程语境里通常译作“字面量”，意思是源码里直接写出来的固定值。例如：

- `123` 是整数字面量。
- `' '` 是字符字面量。
- `"auto"` 是字符串字面量。

所以 `QStringLiteral` 可以理解成“把源码里的字符串字面量变成 QString”。这里的 `Literal` 不是 C++ 语法后缀，而是 Qt 给这个宏起的名字。

`QLatin1Char(' ')` 和普通 `' '`：

普通 `' '` 是 C++ 的 `char` 字符字面量。`QLatin1Char(' ')` 是 Qt 的轻量字符包装，明确表示这是 Latin-1 范围内的字符。和 `QString` 拼接、比较时，用 `QLatin1Char` 可以减少不必要的编码转换，也让读者知道这个字符不是 Unicode 复杂字符。

`QString::arg()` 链式替换：

`QStringLiteral("open %1: %2").arg(path, error)` 会把 `%1` 替换成 `path`，`%2` 替换成 `error`。也可以链式写：

```cpp
QStringLiteral("gate %1: %2").arg(i).arg(error)
```

要注意占位符编号和参数顺序，尤其是错误信息、路径、数值混合时，不要把 `%1/%2` 写反。

`toLocal8Bit()`、`fromLocal8Bit()`、`fromLatin1()`：

- `toLocal8Bit()`：把 `QString` 转成本机 locale 字节串，常用于传给 Linux C API，如 `open(const char *)`。
- `fromLocal8Bit()`：把本机 locale 字节串转回 `QString`，这里用于 `strerror(errno)` 的错误文本。
- `fromLatin1()`：按 Latin-1 解释字节。`getVersion()` 里 `raw.text` 来自驱动/UART 的 ASCII 风格版本字符串，用 `fromLatin1()` 足够直接。

注意生命周期：

```cpp
const QByteArray pathBytes = path.toLocal8Bit();
::open(pathBytes.constData(), ...);
```

`constData()` 返回的是 `pathBytes` 内部缓冲指针。必须保证 `pathBytes` 这个对象在 C API 使用期间仍然活着。当前写法是安全的。

`QIODevice::ReadOnly | QIODevice::Text`：

`QIODevice::ReadOnly` 表示只读打开，`QIODevice::Text` 表示按文本模式处理换行等文本细节。`|` 是位或运算，把多个 flag 合在一起。这里读 `/proc/bus/input/devices`，只需要逐行文本读取，所以用这两个 flag。

`Qt::SkipEmptyParts`：

`line.split(QLatin1Char(' '), Qt::SkipEmptyParts)` 按空格拆字符串，并跳过空字段。比如多个连续空格不会产生一堆空字符串，后面遍历 `parts` 找 `eventX` 更干净。

Qt 隐式共享：

很多 Qt 值类型如 `QString`、`QByteArray` 使用隐式共享，也叫 copy-on-write。复制时先共享底层数据，只有某一方要修改时才真正拷贝。好处是按值传递和返回通常不贵；但涉及 `constData()` 指针时，要记住一旦对象被修改或销毁，旧指针可能失效。当前代码把 `QByteArray` 局部变量保持到 `open()` 调用结束，没有悬空指针问题。

## 第三层：LD2410C 状态和调用实现骨架

### 3.1 三个值对象

`Ld2410Probe` 是节点探测结果：

- `miscPath`：主路径，优先读 `/dev/ld2410c0`。
- `outPath`：OUT input event 节点提示。
- `uartPath`：UART 节点，当前默认 `/dev/ttymxc2`。
- `miscAvailable/outAvailable/uartAvailable`：对应路径是否存在。
- `attached`：当前 `Ld2410Device` 是否已经打开 misc fd。
- `error`：探测失败原因。

`Ld2410State` 是实时状态快照：

- `presence`：主闭环最关心的人体存在 gate。
- `targetState`：雷达上报的目标类型。
- `movingDistanceCm/staticDistanceCm/detectDistanceCm`：距离信息。
- `movingEnergy/staticEnergy`：能量信息。
- `movingGateEnergy/staticGateEnergy`：工程模式每个 gate 的能量。
- `frameCount/errorCount/sequence`：驱动侧统计和状态版本。

`Ld2410Config` 是可配置参数镜像：

- 最大 gate、运动/静止 gate、无人持续时间。
- 每个 gate 的运动/静止灵敏度。
- 分辨率、波特率、辅助光控、OUT 默认电平。

待补：

- 每个字段和 UAPI `struct ld2410c_state/config` 的对应关系。
- 哪些字段来自 OUT，哪些字段来自 UART report。
- 哪些字段会影响 Smart Monitor 主闭环，哪些只用于测试页。

### 3.2 设备探测路径

```text
Ld2410TestWindow::refreshProbe()
  -> device.probe(...)
      -> auto misc path 时调用 findMiscPath()
      -> auto out path 时调用 findInputHint()
      -> auto uart path 时调用 defaultUartPath()
      -> QFileInfo::exists() 判断节点是否存在
```

`probe()` 不打开设备，它只做路径推断和存在性检查。因此它被声明成 `const` 是合理的。

待补：

- `findInputHint()` 如何解析 `/proc/bus/input/devices`。
- 为什么 OUT event 只是 hint，不是主状态读取路径。
- `auto` 这个字符串在当前 UI 中如何传递。

### 3.3 打开和关闭 misc 设备

```text
readState()/readConfig()/writeConfig()/...
  -> ensureOpen()
      -> 如果 miscFd >= 0，直接复用
      -> 否则 openDevice("auto")
          -> findMiscPath()
          -> ::open(path, O_RDWR | O_CLOEXEC)
          -> 保存 openedMiscPath
```

这里的设计让调用者不用每次手动 open。第一次读取状态时如果设备存在，会自动打开 `/dev/ld2410c0`。

待补：

- `::open` 前面的 `::` 表示调用全局命名空间函数。
- `O_CLOEXEC` 的作用。
- 为什么失败时要 `openedMiscPath.clear()`。

### 3.4 UART attach 路径

```text
Ld2410TestWindow 的 Attach UART 按钮
  -> device.attachUart(uartPath, LD2410C_DEFAULT_BAUD, &error)
      -> 打开 /dev/ttymxc2
      -> TCGETS2 读取 termios2
      -> 设置 raw 串口参数和自定义 baud
      -> TCSETS2 写回 termios2
      -> TIOCSETD 切到 LD2410C_LDISC
      -> 保存 uartFd，维持绑定
```

这个函数和 `ld2410c_attach` 工具做的是同一件事。重点是 `uartFd` 不能马上关闭，因为 line discipline 绑定跟 TTY/fd 生命周期相关。

待补：

- `termios2`、`BOTHER`、`CS8`、`CLOCAL`、`CREAD` 的含义。
- 为什么 `VMIN = 1`、`VTIME = 0`。
- `ld2410c_attach` 和 Qt `attachUart()` 为什么不要同时抢同一个 UART。


`termios2`、`BOTHER`、`CS8`、`CLOCAL`、`CREAD`：

`termios2` 是 Linux TTY/UART 配置结构体，里面有输入 flag、输出 flag、控制 flag、本地 flag、控制字符数组，以及输入/输出速度。当前代码用它配置 LD2410C 串口：

- `BOTHER`：表示使用 `c_ispeed/c_ospeed` 里的自定义 baud，而不是只用传统 `B9600/B115200` 这类枚举值。
- `CS8`：8 个数据位。
- `CLOCAL`：忽略 modem 控制线，本地连接设备常用。
- `CREAD`：使能接收。
- 清掉 `PARENB`：无校验。
- 清掉 `CSTOPB`：1 个停止位。
- 清掉 `CRTSCTS`：不用硬件流控。

`VMIN = 1`、`VTIME = 0`：

这两个是非 canonical/raw 读取时的控制字符。`VMIN = 1` 表示读操作至少等 1 字节，`VTIME = 0` 表示不使用读超时。对这里的 line discipline attach 来说，用户态不真正读 UART 数据，数据会进入内核 ldisc；但保持 raw/明确的控制字符配置，可以避免默认终端行规对字节流产生干扰。

为什么不要同时运行 `ld2410c_attach` 和 Qt `attachUart()`：

两者都会打开同一个 `/dev/ttymxc2` 并执行 `TIOCSETD(LD2410C_LDISC)`。当前驱动侧同一时间只接受一个 TTY 绑定；一个进程保持 fd 时，另一个进程再抢可能失败，或者导致 line discipline 生命周期变得难判断。实验时二选一：要么运行独立 `ld2410c_attach` 常驻，要么让 Qt 测试页点击 `Attach UART` 并保持窗口/对象存活。

### 3.4.1 `ioctl(uartFd, TCGETS2, &tio)` 作用和驱动侧路径

先分清三条 ioctl：

```text
TCGETS2  -> 读取当前 termios2 到用户态，不改硬件
TCSETS2  -> 把用户态修改后的 termios2 写回内核，并触发 UART 重新配置
TIOCSETD -> 切换 TTY line discipline，让 LD2410C ldisc 接收串口字节
```

#### `TCGETS2`：读取当前串口配置

当前调用点：

```cpp
struct termios2 tio;
::ioctl(uartFd, TCGETS2, &tio);
```

`uartFd` 是 `::open("/dev/ttymxc2", ...)` 得到的 fd。`TCGETS2` 在 UAPI 里定义成“从内核读一个 `struct termios2` 到用户态”的 ioctl 编号。`&tio` 是用户态缓冲区地址，内核会把当前 TTY 的配置拷贝进去。

调用路径：

```text
用户态 ::ioctl(uartFd, TCGETS2, &tio)
  -> /dev/ttymxc2 的 file_operations.unlocked_ioctl = tty_ioctl
  -> tty_ioctl()
  -> 当前 line discipline 的 ioctl，常见默认是 n_tty_ioctl()
  -> n_tty_ioctl_helper()
  -> tty_mode_ioctl()
  -> case TCGETS2
      -> copy_termios(real_tty, &kterm)
      -> kernel_termios_to_user_termios((struct termios2 __user *)arg, &kterm)
```

这一步只是“先把现有配置读出来”。为什么要先读？因为后面只想修改和 LD2410C 有关的字段，比如 baud、8N1、raw、`VMIN/VTIME`。先读再改，可以保留内核已有的其它状态，避免构造一个缺字段的全新配置。

#### `TCSETS2`：应用修改后的串口配置

当前代码在 `TCGETS2` 之后修改 `tio`，再调用：

```cpp
::ioctl(uartFd, TCSETS2, &tio);
```

调用路径：

```text
用户态 ::ioctl(uartFd, TCSETS2, &tio)
  -> tty_ioctl()
  -> n_tty_ioctl_helper()
  -> tty_mode_ioctl()
  -> case TCSETS2
      -> set_termios(real_tty, p, 0)
          -> user_termios_to_kernel_termios(&tmp_termios, user_arg)
          -> 计算 c_ispeed/c_ospeed
          -> tty_set_termios(tty, &tmp_termios)
              -> tty->ops->set_termios(tty, &old_termios)
                  -> serial_core 的 uart_set_termios()
                      -> uart_change_speed(...)
                          -> uart_port->ops->set_termios(...)
                              -> i.MX UART 驱动 imx_set_termios()
              -> 当前 line discipline 的 .set_termios，如存在也会被通知
```

这里真正进入 i.MX UART 驱动的是 `TCSETS2`，不是 `TCGETS2`。在本仓库内核里，serial core 的 `tty_operations.set_termios` 指向 `uart_set_termios()`，i.MX UART 的 `uart_ops.set_termios` 指向 `imx_set_termios()`。

`imx_set_termios()` 会根据 `CS8/PARENB/CSTOPB/CRTSCTS/CREAD` 和 baud 计算 UART 寄存器配置，更新超时，暂停收发，写 `UFCR/UBIR/UBMR/UCR2` 等寄存器，再恢复收发。对 LD2410C 来说，这一步把 `/dev/ttymxc2` 配成 256000 baud、8N1、无硬件流控、接收使能。

#### `TIOCSETD`：绑定 LD2410C line discipline

配置好串口参数后，当前代码调用：

```cpp
int ldisc = LD2410C_LDISC;
::ioctl(uartFd, TIOCSETD, &ldisc);
```

调用路径：

```text
用户态 ::ioctl(uartFd, TIOCSETD, &ldisc)
  -> tty_ioctl()
  -> case TIOCSETD
      -> tiocsetd(tty, p)
          -> get_user(ldisc, p)
          -> tty_set_ldisc(tty, ldisc)
              -> tty_ldisc_get(tty, LD2410C_LDISC)
              -> 关闭旧 ldisc
              -> tty->ldisc = new_ldisc
              -> tty_ldisc_open(tty, new_ldisc)
                  -> ld2410c_ldisc_ops.open = ld2410c_ldisc_open()
```

`TIOCSETD` 成功后，LD2410C 驱动的 `ld2410c_ldisc_open()` 会保存 TTY 引用，后续 UART 收到的字节会走：

```text
UART RX 中断/缓冲
  -> TTY flip buffer
  -> 当前 line discipline receive_buf
  -> ld2410c_ldisc_receive()
  -> ld2410c_consume_frame()
```

所以 `attachUart()` 的完整意图是：先用 `TCGETS2/TCSETS2` 把 UART 物理通信参数调对，再用 `TIOCSETD` 把这路字节流接到 LD2410C 帧解析器。

源码路标：

| 层级 | 文件 |
| --- | --- |
| 用户态调用点 | [`../sensors/ld2410_device.cpp`](../sensors/ld2410_device.cpp) |
| ioctl 编号和 `termios2` | [`../../linux-friedegg/include/uapi/asm-generic/ioctls.h`](../../linux-friedegg/include/uapi/asm-generic/ioctls.h)、[`../../linux-friedegg/include/uapi/asm-generic/termbits.h`](../../linux-friedegg/include/uapi/asm-generic/termbits.h) |
| TTY file ops / `tty_ioctl()` / `TIOCSETD` | [`../../linux-friedegg/drivers/tty/tty_io.c`](../../linux-friedegg/drivers/tty/tty_io.c) |
| `TCGETS2/TCSETS2` mode ioctl | [`../../linux-friedegg/drivers/tty/tty_ioctl.c`](../../linux-friedegg/drivers/tty/tty_ioctl.c) |
| serial core `uart_set_termios()` | [`../../linux-friedegg/drivers/tty/serial/serial_core.c`](../../linux-friedegg/drivers/tty/serial/serial_core.c) |
| i.MX UART `imx_set_termios()` | [`../../linux-friedegg/drivers/tty/serial/imx.c`](../../linux-friedegg/drivers/tty/serial/imx.c) |
| LD2410C ldisc ops | [`../../ld2410c/ld2410c.c`](../../ld2410c/ld2410c.c) |

### 3.5 状态读取路径

```text
Ld2410TestWindow::refreshState()
  -> device.readState(&state, &error)
      -> ensureOpen()
      -> ioctl(miscFd, LD2410C_IOC_GET_STATE, &raw)
      -> convertState(raw, openedMiscPath)
  -> updateStateLabels(state)
```

`readState()` 读的是快照，不等待新事件。测试页的 Live 模式只是用 `QTimer` 每 500 ms 调一次 `refreshState()`。

`convertState()` 里 `presence` 的判断有一个兜底：

```text
优先：LD2410C_STATE_F_OUT_ACTIVE
兜底：target_state 是 moving/static/moving+static
```

这样做的原因是 OUT 是主闭环的低延迟 gate，但 UART report 里也有目标状态。当 OUT 没有 active 时，仍可根据 UART report 判断 moving/static 是否存在。

待补：

- `raw.flags & FLAG` 为什么能判断 flag。
- `pollEvent()` 和 `readState()` 的区别。
- `sequence` 在事件读取中的意义。

### 3.6 配置读取和写入路径

```text
Read Config 按钮
  -> refreshConfig()
      -> device.readConfig(&config, &error)
      -> updateConfigLabels(config)

Apply Config 按钮
  -> collectConfigFromUi()
  -> writeConfig()
      -> LD2410C_IOC_SET_MAX_GATE
      -> 每个 gate 调 LD2410C_IOC_SET_GATE_SENSITIVITY
  -> setResolution()
  -> setAuxControl()
  -> setBaud()
  -> setEngineeringMode()
```

这个实现把“多个 UI 控件值”拆成多个 ioctl 调用。内核驱动再把这些 ioctl 翻译成 LD2410C 串口命令，并等待 ACK。

待补：

- 为什么 `writeConfig()` 先设置 gate，再逐 gate 设置灵敏度。
- 配置类 ioctl 为什么依赖 UART attach。
- 改 baud 后，后续 attach/通信需要注意什么。

### 3.7 维护功能路径

维护页目前包括：

- `getVersion()`：读模块版本字符串。
- `startNoiseCalibration()`：启动噪声校准，并返回状态。
- `getNoiseStatus()`：读取噪声校准状态。
- `factoryReset()`：恢复出厂设置。
- `reboot()`：重启雷达模块。

这些操作都通过 `/dev/ld2410c0` 的 ioctl 进入内核驱动，最终转成 UART 命令。

待补：

- 哪些操作有破坏性或会导致雷达短暂不可用。
- 为什么测试页对 noise/reset/reboot 加确认框。
- 板端验证时如何区分命令失败、ACK 超时和 UI 没刷新。

## 后续追问建议

可以按下面粒度继续补本文：

- “补 1.2：拷贝构造和 fd 双重 close，画一下对象生命周期。”
- “补 1.4：成员函数末尾 const 和参数 const 引用的区别。”
- “补 2.1：qmake 从 `.pro` 到 Makefile 的具体变量流。”
- “补 2.2：QString、QByteArray、constData 生命周期。”
- “补 3.5：`convertState()` 每一行解释。”
- “补 3.6：配置 ioctl 为什么必须 attach UART。”

## 最窄验证

本文是只读讲解文档，不改变代码行为。最窄本地验证是确认文档链接和 Markdown 基本格式。

板端理解链路时可用：

```bash
QT_QPA_PLATFORM=linuxfb imx6-sm-ld2410-test
```

预期：

- Probe 页能看到 `/dev/ld2410c0`、input hint 和 `/dev/ttymxc2` 是否存在。
- Realtime 页能周期刷新 presence、距离、能量、sequence。
- Attach UART 后，工程模式和配置类功能才更可能完整工作。
