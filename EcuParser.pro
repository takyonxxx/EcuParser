QT       = core gui widgets
CONFIG  += c++17
CONFIG  -= app_bundle

TARGET   = EcuParser
TEMPLATE = app

# Compile-time warnings - per-compiler flags
*-msvc {
    QMAKE_CXXFLAGS += /W4
    QMAKE_CXXFLAGS += /utf-8
}
*-g++|*-clang* {
    QMAKE_CXXFLAGS += -Wall -Wextra -Wpedantic
}

# qDebug logs: ASCII only as project rule (no emoji)
DEFINES += QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII

INCLUDEPATH += src

HEADERS += \
    src/core/DrtParser.h \
    src/core/BinFile.h \
    src/core/MapData.h \
    src/core/StagePackage.h \
    src/model/AxisDefinition.h \
    src/model/MapDefinition.h \
    src/model/DriverModel.h \
    src/model/MapCategory.h \
    src/model/DriverNames.h \
    src/gui/MainWindow.h \
    src/gui/DriverTreeWidget.h \
    src/gui/MapTableWidget.h \
    src/gui/MapGraphWidget.h \
    src/gui/AppPaths.h

SOURCES += \
    src/core/DrtParser.cpp \
    src/core/BinFile.cpp \
    src/core/MapData.cpp \
    src/core/StagePackage.cpp \
    src/model/MapCategory.cpp \
    src/model/DriverNames.cpp \
    src/gui/MainWindow.cpp \
    src/gui/DriverTreeWidget.cpp \
    src/gui/MapTableWidget.cpp \
    src/gui/MapGraphWidget.cpp \
    src/gui/AppPaths.cpp \
    src/gui/main.cpp

# Per-configuration build layout: exe and intermediates live in
# release/ or debug/ under the build directory, so a single shadow
# build can hold both configurations side-by-side without colliding.
# This matches the "build-EcuParser/release/EcuParser.exe" layout
# Qt Creator users expect when they have separate Release and Debug
# kits.
CONFIG(release, debug|release) {
    DESTDIR     = $$OUT_PWD/release
    OBJECTS_DIR = $$OUT_PWD/release/.obj
    MOC_DIR     = $$OUT_PWD/release/.moc
    RCC_DIR     = $$OUT_PWD/release/.rcc
    UI_DIR      = $$OUT_PWD/release/.ui
}
CONFIG(debug, debug|release) {
    DESTDIR     = $$OUT_PWD/debug
    OBJECTS_DIR = $$OUT_PWD/debug/.obj
    MOC_DIR     = $$OUT_PWD/debug/.moc
    RCC_DIR     = $$OUT_PWD/debug/.rcc
    UI_DIR      = $$OUT_PWD/debug/.ui
}
