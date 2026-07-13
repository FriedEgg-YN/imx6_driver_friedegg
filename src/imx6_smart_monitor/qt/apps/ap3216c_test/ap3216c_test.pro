TEMPLATE = app
TARGET = imx6-sm-ap3216c-test
ROOT = ../../..
include($$ROOT/qt/apps/app_common.pri)

SOURCES += \
    main.cpp \
    ap3216c_test_window.cpp \
    $$ROOT/sensors/ap3216c_device.cpp

HEADERS += \
    ap3216c_test_window.h \
    $$ROOT/sensors/ap3216c_device.h
