TEMPLATE = app
TARGET = imx6-sm-ld2410-test
ROOT = ../../..
include($$ROOT/qt/apps/app_common.pri)

SOURCES += \
    main.cpp \
    ld2410_test_window.cpp \
    $$ROOT/sensors/ld2410_device.cpp

HEADERS += \
    ld2410_test_window.h \
    $$ROOT/sensors/ld2410_device.h
