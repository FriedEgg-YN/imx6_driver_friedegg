QT += widgets
CONFIG += c++11
TEMPLATE = app
TARGET = app_ap3216c

SOURCES += \
    main.cpp \
    ap3216c_controller.cpp \
    ap3216c_page.cpp

HEADERS += \
    ap3216c_view_state.h \
    ap3216c_controller.h \
    ap3216c_page.h