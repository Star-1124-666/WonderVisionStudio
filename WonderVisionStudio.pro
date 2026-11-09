QT += core gui widgets

TEMPLATE = app
CONFIG += c++17
TARGET = WonderVisionStudio

SOURCES += \
    main.cpp \
    MainWindow.cpp \
    WonderVisionDevice.cpp \
    ThermalrightLhmReader.cpp

HEADERS += \
    MainWindow.h \
    WonderVisionDevice.h \
    ThermalrightLhmReader.h \
    ThermalrightSystemMetrics.h

win32 {
    LIBS += -lsetupapi -lwinusb -lwinhttp -lshell32 -luser32
}
