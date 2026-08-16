# QT 基础

## 最小 Wiget 程序讲解

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

### QApplication

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

### QWidget

没有父对象的 QWidget 是顶层窗口。

这里使用栈对象即可，因为：

- window 的生命周期覆盖整个 app.exec()；
- 退出事件循环后，window 自动析构；
- 不需要手动 delete。

#### 标题和尺寸

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
