TEMPLATE = app
TARGET = imx6-sm-touch-test
ROOT = ../../..
include($$ROOT/qt/apps/app_common.pri)

SOURCES += \
    main.cpp \
    touch_test_window.cpp

HEADERS += \
    touch_test_window.h
