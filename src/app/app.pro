QT += widgets
CONFIG += c++11
TEMPLATE = app
TARGET = app_entrance

INCLUDEPATH += $$PWD

SOURCES += \
    $$PWD/main/main.cpp \
    $$PWD/device/sensor/sensor_service.cpp \
    $$PWD/device/sensor/sensor_worker.cpp \
    $$PWD/application/ap3216c/ap3216c_controller.cpp \
    $$PWD/application/ap3216c/ap3216c_page.cpp

HEADERS += \
    $$PWD/device/sensor/sensor_types.h \
    $$PWD/device/sensor/sensor_service.h \
    $$PWD/device/sensor/sensor_worker.h \
    $$PWD/application/ap3216c/ap3216c_view_state.h \
    $$PWD/application/ap3216c/ap3216c_controller.h \
    $$PWD/application/ap3216c/ap3216c_page.h