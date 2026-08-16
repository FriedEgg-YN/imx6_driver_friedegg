# QT .pro 项目配置文件与编译

与 Cmake 类似，qmake 读取 .pro 生成 Makefile；make 再调用该 qmake 配套 mkspec 中的编译器进行编译、链接等工作，最后生成可执行文件。

## .pro

在 `.pro` 文件中，基本是对各个变量的配置，常见变量的详细情况如下。

### QT

声明需要的 QT 模块，如当前项目依赖 widgets 构建，因此需要声明 `QT+=widgets`，在 Qt5 中，widgets会连带需要gui和core，因此实际包含
QT_WIDGETS_LIB
QT_GUI_LIB
QT_CORE_LIB

### CONFIG

常用编译配置，当前 CONFIG+=c++11，配置C++11

### TEMPLATE

常见值包括：

- app：表示生成一个可执行程序
- lib：表示生成一个静态库或动态库
- subdirs：表示聚合多个独立子工程

### TARGET

用以配置最后编译出可执行文件的名称

### INCLUDEPATH

### SOURCES

源文件

### HEADERS

头文件

## 编译命令与原理


