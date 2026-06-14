TEMPLATE = app
TARGET = imx6-qt5-demo

QT += widgets network
CONFIG += c++11

SOURCES += main.cpp

target.path = /usr/bin
INSTALLS += target
