QT += widgets
CONFIG += c++11 debug
CONFIG -= release
# Keep source-level symbols even when the Buildroot qmake spec supplies -g0.
QMAKE_CXXFLAGS += -O0 -g3
TEMPLATE = app
TARGET = app_entrance

INCLUDEPATH += $$PWD

SOURCES += \
    $$PWD/main/main.cpp \
    $$PWD/device/sensor/sensor_service.cpp \
    $$PWD/device/sensor/sensor_worker.cpp \
    $$PWD/application/ap3216c/ap3216c_controller.cpp \
    $$PWD/application/ap3216c/ap3216c_page.cpp \
    $$PWD/device/sensor/ap3216c_backend.cpp

HEADERS += \
    $$PWD/device/sensor/sensor_types.h \
    $$PWD/device/sensor/sensor_service.h \
    $$PWD/device/sensor/sensor_worker.h \
    $$PWD/application/ap3216c/ap3216c_view_state.h \
    $$PWD/application/ap3216c/ap3216c_controller.h \
    $$PWD/application/ap3216c/ap3216c_page.h \
    $$PWD/device/sensor/ap3216c_backend.h
