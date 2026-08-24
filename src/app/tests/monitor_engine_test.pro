CONFIG += console c++11
CONFIG -= app_bundle qt
TEMPLATE = app
TARGET = monitor-engine-test

INCLUDEPATH += ../application/smart_monitor

SOURCES += monitor_engine_test.cpp \
           ../application/smart_monitor/smart_monitor_engine.cpp

HEADERS += ../application/smart_monitor/smart_monitor_engine.h \
           ../application/smart_monitor/smart_monitor_types.h
