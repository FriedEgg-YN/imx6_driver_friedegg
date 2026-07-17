TEMPLATE = app
TARGET = imx6-sm-camera-test
ROOT = ../../..
include($$ROOT/qt/apps/app_common.pri)

SOURCES += \
    main.cpp \
    camera_test_window.cpp \
    $$ROOT/camera/camera_device.cpp \
    $$ROOT/storage/storage_manager.cpp

HEADERS += \
    camera_test_window.h \
    $$ROOT/camera/camera_device.h \
    $$ROOT/storage/storage_manager.h
