QT += widgets
CONFIG += c++11
TEMPLATE = app
TARGET = app_ap3216c

INCLUDEPATH += $$PWD/../..

SOURCES += \
    main.cpp \
    ../../main/composition_root.cpp \
    ap3216c_controller.cpp \
    ap3216c_page.cpp \
    ../../device/sensor/sensor_service.cpp \
    ../../device/sensor/sensor_worker.cpp \
    ../../device/sensor/ap3216c_backend.cpp

HEADERS += \
    ap3216c_view_state.h \
    ../../main/composition_root.h \
    ap3216c_controller.h \
    ap3216c_page.h \
    ../../device/sensor/sensor_types.h \
    ../../device/sensor/sensor_service.h \
    ../../device/sensor/sensor_worker.h \
    ../../device/sensor/ap3216c_backend.h
