TEMPLATE = app
TARGET = imx6-qt5-demo

QT += widgets network
CONFIG += c++11

SOURCES += main.cpp \
           monitorpanel.cpp

HEADERS += monitorpanel.h

FORMS += monitorpanel.ui

target.path = /usr/bin
INSTALLS += target
