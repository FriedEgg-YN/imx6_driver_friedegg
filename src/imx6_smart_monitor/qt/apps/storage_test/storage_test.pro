TEMPLATE = app
TARGET = imx6-sm-storage-test
ROOT = ../../..
include($$ROOT/qt/apps/app_common.pri)

SOURCES += \
    main.cpp \
    storage_test_window.cpp \
    $$ROOT/storage/storage_manager.cpp

HEADERS += \
    storage_test_window.h \
    $$ROOT/storage/storage_manager.h
