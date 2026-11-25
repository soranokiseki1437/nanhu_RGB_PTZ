QT += core gui
CONFIG += console c++17 release
CONFIG -= app_bundle debug_and_release debug
TARGET = bench_exp
DESTDIR = $$PWD/../build/release
DEFINES += USE_ONNXRUNTIME
SOURCES += bench_exp.cpp \
    ../dsstcore.cpp \
    ../onnxdetector.cpp \
    ../yolopostprocessor.cpp \
    ../onnxsottracker.cpp \
    ../logging_categories.cpp
HEADERS += ../dsstcore.h \
    ../IDetector.h \
    ../onnxdetector.h \
    ../yolopostprocessor.h \
    ../ISotTracker.h \
    ../onnxsottracker.h \
    ../logging_categories.h
INCLUDEPATH += $$PWD/.. $$PWD/../opencv/install/include $$PWD/../3rdparty/onnxruntime/include
LIBS += -L$$PWD/../opencv/install/x64/mingw/lib -lopencv_core4100 -lopencv_imgproc4100 -lopencv_imgcodecs4100
LIBS += -L$$PWD/../3rdparty/onnxruntime/lib -lonnxruntime -lDirectML -lpsapi
