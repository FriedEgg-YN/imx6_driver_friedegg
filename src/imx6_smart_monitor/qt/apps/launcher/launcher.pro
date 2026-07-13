TEMPLATE = app
TARGET = imx6-smart-monitor
ROOT = ../../..
include($$ROOT/qt/apps/app_common.pri)

SOURCES += \
    main.cpp \
    launcher_window.cpp \
    $$ROOT/qt/apps/touch_test/touch_test_window.cpp \
    $$ROOT/qt/apps/ap3216c_test/ap3216c_test_window.cpp \
    $$ROOT/qt/apps/ld2410_test/ld2410_test_window.cpp \
    $$ROOT/qt/apps/camera_test/camera_test_window.cpp \
    $$ROOT/qt/apps/storage_test/storage_test_window.cpp \
    $$ROOT/qt/apps/core_test/core_test_window.cpp \
    $$ROOT/sensors/ap3216c_device.cpp \
    $$ROOT/sensors/ld2410_device.cpp \
    $$ROOT/camera/camera_device.cpp \
    $$ROOT/storage/storage_manager.cpp \
    $$ROOT/core/monitor_core.cpp

HEADERS += \
    launcher_window.h \
    $$ROOT/qt/apps/touch_test/touch_test_window.h \
    $$ROOT/qt/apps/ap3216c_test/ap3216c_test_window.h \
    $$ROOT/qt/apps/ld2410_test/ld2410_test_window.h \
    $$ROOT/qt/apps/camera_test/camera_test_window.h \
    $$ROOT/qt/apps/storage_test/storage_test_window.h \
    $$ROOT/qt/apps/core_test/core_test_window.h \
    $$ROOT/sensors/ap3216c_device.h \
    $$ROOT/sensors/ld2410_device.h \
    $$ROOT/camera/camera_device.h \
    $$ROOT/storage/storage_manager.h \
    $$ROOT/core/monitor_core.h
