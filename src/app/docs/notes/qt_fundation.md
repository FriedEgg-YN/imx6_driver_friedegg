# QT 基础

- [1. 最小 Wiget 程序讲解](#1-最小-wiget-程序讲解)
  - [1.1. QApplication](#11-qapplication)
  - [1.2. QWidget](#12-qwidget)
    - [1.2.1. 标题和尺寸](#121-标题和尺寸)
- [2. QObject](#2-qobject)
- [QThread](#qthread)
- [3. signals/slot/emit](#3-signalsslotemit)
  - [3.1. signals](#31-signals)
  - [3.2. slots](#32-slots)
  - [3.3. emit](#33-emit)
  - [3.4. connect](#34-connect)
  - [3.5. 跨线程connect与thread affinity](#35-跨线程connect与thread-affinity)
- [QByteArray](#qbytearray)
- [QFile](#qfile)
- [QString](#qstring)
- [4. QVBoxLayout](#4-qvboxlayout)
- [5. QString](#5-qstring)


## 1. 最小 Wiget 程序讲解

```cpp
#include <QApplication>
#include <QWidget>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QWidget window;
    window.resize(800, 480);
    window.setWindowTitle("Qt Application");
    window.show();

    return app.exec();
}
```

### 1.1. QApplication

每个 Qt Widgets 程序都需要一个 QApplication 对象。它负责：

- 初始化 Qt GUI 环境；
- 读取命令行参数；
- 加载 QPA 平台插件，例如 linuxfb；
- 接收键盘、鼠标、触摸等事件；
- 管理 Qt 主事件循环。

一个进程中只能有一个 QApplication 对象，而且必须在创建任何 QWidget 之前创建。

exec() 启动 Qt 主事件循环：

输入设备事件
    -> QPA/linuxfb
    -> QApplication
    -> QWidget 事件分发
    -> 重绘 framebuffer

调用后会一直运行，直到窗口关闭或程序调用 quit()。返回值作为进程退出码返回给 Linux。

如果没有 app.exec()，程序会创建窗口后马上离开 main()，窗口几乎不会被看到。

### 1.2. QWidget

没有父对象的 QWidget 是顶层窗口。

这里使用栈对象即可，因为：

- window 的生命周期覆盖整个 app.exec()；
- 退出事件循环后，window 自动析构；
- 不需要手动 delete。

#### 1.2.1. 标题和尺寸

```cpp
window.setWindowTitle("i.MX6ULL Qt Widgets Test");
window.resize(480, 272);
window.show();
```

`setWindowTitle()` 设置窗口标题。

需要注意：linuxfb 没有桌面窗口管理器，通常不会绘制桌面系统中的标题栏。因此板端不一定能直接看到标题文字，但标题属性仍然设置成功。当前阶段主要观察 framebuffer 是否被 Qt 窗口正常接管。

`resize()` 给窗口一个明确的初始尺寸。这里的 480 × 272 只是最小测试尺寸，不要求必须等于 LCD 分辨率。你也可以根据实际 LCD 改成 800 × 480。

如果普通 show() 在你的 linuxfb 环境下没有铺满屏幕，可以在排查时将`window.show();`暂时换成：`window.showFullScreen();`

构造 QWidget 不等于显示窗口。只有调用 `show()`、`showFullScreen()` 等函数，Qt 才会创建对应的平台窗口并安排绘制。

## 2. QObject

`QObject` 是 Qt 元对象系统的入口，没有它，signals/slots/emit这些机制就不能正常工作

Qt 通过`QObject` 实现标准父对象机制，如果传入父对象，在父对象析构时会递归调用子对象析构函数

## QThread

```cpp
/** 
 * 只在GUI线程中创建了线程对象本身
 * 启动后会执行 QThread::run()，可以重载
 */
QThread thread;
/**
 * worker 中QTimer不是在构造函数中初始化，而是通过strat之类函数初始化，这样QTimer在 worker affinity所在线程 
 * 修改worker的thread affinity
 * worker 需继承 QObject，且不能有 parent，否则不能执行此调用
 */
worker->moveToThread(thread);
/**
 * 把worker放入其所属线程的 deferred-delete 队列，会在线程收尾阶段析构
 */
QObject::connect(&thread, &QThread::finished, &worker, &QObject::deleteLater)
/**
 * 真正创建并启动底层线程
 * 在默认的 QThread::run() 中，会执行事件循环
 */
thread.start();
/**
 * 退出事件循环，事件循环返回后，会发出 QThread::finished 信号
 */
thread.quit();
```

thread 对象本身还属于 GUI 线程，但它管理的工作函数运行在另一个线程。

推荐使用 worker-object 模式：

QThread       负责管理线程
SensorWorker  负责实际业务
moveToThread  改变 Worker 的 affinity
queued signal 把命令投递给 Worker
QTimer        在 Worker 线程中周期触发

注意跨线程不要共享可变容器、Widget 指针、fd 或 backend 指针

## 3. signals/slot/emit

### 3.1. signals

```cpp
signals:
    void dataReady(int value);
```

Signal 表示事件或状态发生变化：

特点：

- 类必须继承 QObject 并包含 Q_OBJECT；
- 只声明，不手写普通函数实现；
- 由 moc 生成元对象支持代码；
- signal 没有返回值语义，通常使用 void；
- 一个 signal 可以连接多个 slot；
- signal 也可以连接另一个 signal；
- signal 本质上是公开的元对象接口，技术上可从外部调用，但通常只由对象自身发射，以保持封装。

适用场景：

- 用户操作；
- 状态变化；
- 异步操作完成；
- 数据到达；
- 错误或生命周期通知。

### 3.2. slots

Slot 是接收 signal 的成员函数：

```cpp
public slots:
    void handleData(int value);
```

权限含义与普通 C++ 访问权限一致：

- public slots：外部代码可直接调用；
- protected slots：类及派生类可直接调用；
- private slots：仅类内部可直接调用。

无论 slot 是 public、protected 还是 private，连接建立后 Qt 都能调用它。访问权限主要限制 C++ 代码能否直接取得并使用该成员。
使用函数指针版 connect 时，普通成员函数也可以作为接收函数：

```cpp
class Receiver : public QObject {
    Q_OBJECT
public:
    void handleData(int value);
};
```

不一定必须写在 slots: 下。写成 slot 的意义是明确它是 Qt 事件接口，并将其纳入元对象信息。

### 3.3. emit

发射 signal：

```cpp
emit dataReady(42);
```

emit 是可读性标记，不负责创建线程，也不会保存最新状态。发射时，Qt 查找当前连接并派发调用。

如果发射时没有任何连接者，该事件直接结束；以后建立连接的对象不会收到历史 signal。

### 3.4. connect

建立长期的事件连接：
```cpp
// 发送对象地址，信号地址，接受对象地址，接受slot函数地址
QObject::connect(sender,
                 &Sender::dataReady,
                 receiver,
                 &Receiver::handleData);
```

它不是立即调用 slot，而是规定：

sender 以后每次发射 dataReady
-> Qt 调用 receiver 的 handleData

常用完整形式：

```cpp
QObject::connect(sender,
                 &Sender::dataReady,
                 receiver,
                 &Receiver::handleData,
                 Qt::AutoConnection);
```

函数指针语法提供编译期类型检查。Signal 参数必须能提供 slot 所需参数；slot 可以忽略 signal 尾部多余参数。

QObject 销毁后，Qt 会自动断开以它作为 sender 或 receiver 的连接。

连接类型：

|       类型          |       执行方式         |
|------------------------|-----------------------|

| Qt::AutoConnection           | 默认;同线程 Direct,跨线程 Queued        |
| Qt::DirectConnection         | 在发射 signal 的线程立即执行 slot       |
| Qt::QueuedConnection         | 将调用投递到 receiver所在线程的事件队列 |
| Qt::BlockingQueuedConnection | 投递后阻塞 sender,直到 slot 完成        |
| Qt::UniqueConnection         | 防止同一成员函数连接被重复建立          |

*BlockingQueuedConnection 不能在同一线程使用，否则会死锁；GUI 程序中也应谨慎使用，避免冻结界面。*

需要说明的是，`QueuedConnection` 不是立刻调用slot，而是把一次调用包装成事件放进 receiver 线程的事件队列。Qt 必须知道参数的类型、大小、复制方式，因此跨线程参数必须：

- 可复制
- 自己拥有数据
- 不包含 Widget 指针
- 不包含临时栈对象引用
- 不包含不能包装生命周期的裸 buffer

### 3.5. 跨线程connect与thread affinity

跨线程通常使用默认 Auto 或显式 Queued：

```cpp
QObject::connect(sender,
                 &Sender::dataReady,
                 worker,
                 &Worker::handleData,
                 Qt::QueuedConnection);
```

核心规则：

Queued slot 在 receiver 的 thread affinity 所在线程执行。不是由 sender 所在线程决定，也不是由持有 receiver 指针的线程决定。

对象默认属于创建它的线程，如一对象在GUI主线程创建，则它们具有GUI线程affinity，其中

- queued signal/slot
- QTimer
- deleteLater()
- 事件

会由此线程的 event loop 处理。

跨线程参数必须：

- 可复制；
- 拥有自身数据；
- 不携带栈对象引用、Widget 指针、裸设备 buffer 等短生命周期数据；
- 自定义类型需要声明并注册元类型。

```cpp
Q_DECLARE_METATYPE(Sample)

// QApplication 创建后、建立连接前：
qRegisterMetaType<Sample>("Sample");
```

QueuedConnection 依赖 receiver 所在线程运行 event loop。没有事件循环时，已投递的 slot 不会正常得到执行。

常见跨线程形状：

```text
GUI thread                       Worker thread
Controller --command signal----> Worker slot
Controller <---result signal---- Worker
              QueuedConnection
```

GUI 不直接调用 Worker 普通方法，而是通过 queued signal 提交命令；Worker 再通过 typed signal 把结果按值返回。

## QByteArray

```cpp
// QByteArray 构造
QByteArray ba;  // empty
QByteArray ba("abc");  // 从 const char* 构造
QByteArray ba = QByteArrayLiteral("abc");  // 字面量，适合常量
QByteArray ba(10, '\0');  // 指定长度并填充
QByteArray ba = QByteArray::fromRawData(ptr, len);  // 不拷贝，引用外部数据

// 属性接口
ba.isEmpty();  // 是否为空
ba.size(); ba.length(); ba.count();  // 字节数
ba.capacity();  // 容量
ba.reserve(100);  // 预留容量
ba.clear();  // 清空
ba.isNull();  // 是否是 null 状态，通常不常用
// 取字节/子串
ba.at(i); ba[i];  // 取单个字节
ba.left(n);  // 左边 n 个字节
ba.right(n);  // 右边 n 个字节
ba.mid(pos, len);  // 子串
ba.first(n);  // 前 n 个字节
ba.last(n);  // 后 n 个字节
ba.chop(n);  // 删除末尾 n 个字节
ba.truncate(n);  // 截断到指定长度

// 查找
ba.contains("abc");  // 是否包含
ba.indexOf("abc");  // 第一次出现位置
ba.lastIndexOf("abc");  // 最后一次出现位置
ba.startsWith("abc");  // 是否以某串开头
ba.endsWith("abc");  // 是否以某串结尾

// 修改
ba.append("x");  // 追加
ba += "x";  // 追加
ba.prepend("x");  // 前插
ba.replace("a", "b");  // 替换
ba.remove("a");  // 删除匹配内容
ba.fill('x', 5);  // 填充为指定字符
ba.resize(20);  // 调整大小

// 去空白
ba.trimmed();  // 去掉首尾空白
ba.simplified();  // 去掉首尾空白，并压缩中间空白
ba.trimmed();  // 常用于处理文本字节数据

// 比较
ba == "abc";
ba != "abc";
ba.compare("abc");  // 返回 <0, 0, >0

// 编码转换
ba.toStdString();  // 转 std::string
ba.toHex();  // 转十六进制字节串
ba.toBase64();  // 转 Base64
QString::fromUtf8(ba);  // 转 QString
QString::fromLocal8Bit(ba);  // 转 QString
QString::fromLatin1(ba);  // 转 QString
QString text = QString::fromUtf8(ba);
QByteArray utf8 = text.toUtf8();

// 数字相关
QByteArray::number(123);
QByteArray::number(3.14);

// 原始数据
ba.constData();  // const char*
ba.data();  // char*
ba.data() + i;  // 指针运算
// QByteArray 常用例子

QByteArray data = file.readAll();
QString text = QString::fromUtf8(data);

QByteArray line = file.readLine();
if (line.endsWith('\n')) {
    line.chop(1);
}

QByteArray hex = data.toHex();
QByteArray b64 = data.toBase64();

QByteArray joined = "a" + QByteArray("b") + "c";
```

## QFile

QFile 继承自 QIODevice，QIODevice 析构会关闭已打开的设备，实现了对 fd 的 oop管理

```cpp
// QFile 构造/绑定
QFile file;  // empty
QFile file(path);  // 绑定文件路径
file.setFileName(path);  // 后续再设置路径

// 打开/关闭
file.open(QIODevice::ReadOnly | QIODevice::Text);  // 文本模式
file.close();  // 关闭
file.isOpen();  // 是否已打开
file.isReadable();  // 是否可读
file.isWritable();  // 是否可写

// 读
file.readAll();  // 读全部，返回 QByteArray
file.read(n);  // 读 n 字节，返回 QByteArray
file.readLine();  // 读一行，返回 QByteArray
file.readLine(maxlen);  // 限长读一行
file.atEnd();  // 是否到末尾
file.bytesAvailable();  // 还能读多少字节

// 写
file.write(data);  // 写入 QByteArray / const char*
file.write(data, len);  // 写入指定长度
file.flush();  // 刷新缓冲
file.resize(size);  // 调整文件大小

// 定位
file.pos();  // 当前读写位置
file.seek(offset);  // 跳到指定位置
file.size();  // 文件大小
file.exists();  // 文件是否存在
file.remove();  // 删除文件
file.rename(newName);  // 重命名
file.copy(newName);  // 复制文件

// 信息
file.fileName();  // 路径
file.errorString();  // 错误描述
file.permissions();  // 权限
file.setPermissions(perms);  // 设置权限
file.fileTime(QFileDevice::FileModificationTime);  // 文件时间
```

- QIODevice::ReadOnly  只读打开。
- QIODevice::WriteOnly  只写打开。
- QIODevice::ReadWrite  读写都允许，等价于 ReadOnly | WriteOnly。
- QIODevice::Append  追加写入，写操作总是在文件末尾。
- QIODevice::Truncate  打开时清空原文件内容。
- QIODevice::Text  文本模式。Qt 会做一些平台相关处理，最典型的是换行符转换（windows中"\r\n"转"\n"）。读写都可用。
- QIODevice::Unbuffered  尽量不走 Qt 缓冲，直接操作底层设备。
- QIODevice::NewOnly  只在文件不存在时创建，已存在就失败。
- QIODevice::ExistingOnly  只能打开已存在的文件，不允许新建。

## QString

```cpp
// 构造
QString s;  // empty
QString s = "abc";  // 从 c 字符串构造
QString s = QStringLiteral("abc");  // 更适合字符串常量
s = "hello";  // 赋值

// 属性接口
s.isEmpty();  // 是否为空
s.size(); s.length(); s.count();  // number of char
s.capacity();  // 当前容量
s.reserve(100);  // 预留容量
s.clear();  // 清空

// 取字符/子串
s.at(i); s[i];  // cpp 基础接口
s.left(n); s.right(n);  // 左/右 n 个字符
s.mid(pos, len);  // 类似 substr
s.first(n);  // 前 n 个字符，Qt 里新版本可用时很方便
s.chopped(n);  // 返回去掉末尾 n 个字符的结果
s.chop(n);  // 直接删除末尾 n 个字符
s.truncate(n);  // 直接截断到长度 n

// 查找
s.contains("abc");  // 是否包含
s.indexOf("abc");  // 第一次出现位置
s.lastIndexOf("abc");  // 最后一次出现位置
s.startsWith("abc");  // 是否以某串开头
s.endsWith("abc");  // 是否以某串结尾

// 比较
s == "abc";
s != "abc";
s.compare("abc");  // 返回 <0, 0, >0
s.compare("abc", Qt::CaseInsensitive);  // 忽略大小写比较

// 修改
s.append("x");  // 追加
s += "x";  // 追加
s.prepend("x");  // 前插
s.replace("a", "b");  // 替换
s.remove("a");  // 删除匹配内容
s.replace(i, len, "x");  // 从位置 i 开始替换 len 个字符
s.fill('x', 5);  // 填充成指定字符

// 去空白
s.trimmed();  // 去掉首尾空白
s.simplified();  // 去掉首尾空白，并把中间多个空白压成一个

// 大小写
s.toLower();
s.toUpper();

// 拆分/拼接
s.split(",");  // 按分隔符拆分成 QStringList
list.join(",");  // QStringList 拼接成 QString

// 格式化
QString("%1").arg(value);  // Qt 风格模板替换
QString("%1 %2").arg(a).arg(b);  // 多参数格式化
QString::number(123);  // 数字转字符串
QString::number(3.14, 'f', 2);  // 指定格式和精度

// 类型转换
s.toInt();
s.toLongLong();
s.toDouble();
s.toBool();
s.toUtf8();  // 转 QByteArray
s.toLocal8Bit();
s.toLatin1();
QString::fromUtf8(bytes);
QString::fromLocal8Bit(bytes);
QString::fromLatin1(bytes);

// 视图/引用相关
s.constData();  // const QChar*
s.data();  // 可写数据，注意版本和使用场景
s.cbegin(); s.cend();  // 迭代
```

## 4. QVBoxLayout

QVBoxLayout 是 Qt 里的垂直布局管理器，作用是把多个控件按从上到下的顺序排列，并自动处理间距、对齐和窗口缩放时的重排。

`auto *layout = new QVBoxLayout(this);`

通常直接把它设置给一个窗口或容器控件，也可以先创建再 setLayout()。

使用场景: 

- 表单类界面中，控件需要纵向排列
- 设置页、属性面板、对话框
- 一组按钮、标签、输入框需要从上到下堆叠时
- 希望界面随窗口大小变化自动调整时

使用方式:

1. 创建布局对象
2. 往里面添加控件或子布局
3. 设置边距和间距
4. 交给父控件管理

常用接口：

- addWidget()：添加控件
- addLayout()：添加子布局
- addStretch()：添加弹簧，推动控件位置
- setSpacing()：设置控件间距
- setContentsMargins()：设置四周边距

## 5. QString

`QString` 是 Qt 用来表示 Unicode 字符串的类，具有隐式共享机制，`QString b = a;` 时不会立即复制数据，只有其中一个字符串被真正修改才进行复制。

`QStringLiteral` 是一个用于创建字符串常量的宏，专门优化代码中的固定文本。优势在于：

- 字符串内容在编译期处理
- 避免运行时进行字符编码转换
- 通常不需要为字符串内容重新分配内存
- 明确表达这是一个固定文本
- 对包含非 ASCII 字符的文本更安全

```cpp
// 涉及从C字符串字面量到Qt字符串的转换
QString title = QString("Hello")
// Qt的Unicode字符串字面量机制
QString title = QStringLiteral("Hello")
// 非 ASCII 字符建议指定编码
QString text = QString::fromUtf8("你好");
```
