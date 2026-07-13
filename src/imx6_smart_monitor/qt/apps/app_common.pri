QT += widgets
CONFIG += c++11 warn_on

DESTDIR = $$ROOT/bin
INCLUDEPATH += $$ROOT $$ROOT/include $$ROOT/qt/common

SOURCES += \
    $$ROOT/common/types.cpp \
    $$ROOT/qt/common/app_runner.cpp \
    $$ROOT/qt/common/module_test_window.cpp

HEADERS += \
    $$ROOT/include/imx6smartmonitor/types.h \
    $$ROOT/qt/common/app_runner.h \
    $$ROOT/qt/common/module_test_window.h

target.path = /usr/bin
INSTALLS += target

