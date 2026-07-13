TEMPLATE = app
TARGET = imx6-sm-core-test
ROOT = ../../..
include($$ROOT/qt/apps/app_common.pri)

SOURCES += \
    main.cpp \
    core_test_window.cpp \
    $$ROOT/core/monitor_core.cpp

HEADERS += \
    core_test_window.h \
    $$ROOT/core/monitor_core.h
