QT += core gui
CONFIG += console c++17 release
CONFIG -= app_bundle debug_and_release debug
TARGET = test_amft
DESTDIR = $$PWD/../build/release
DEFINES += USE_ONNXRUNTIME
SOURCES += test_amft.cpp \
    ../objecttracker.cpp \
    ../dsstcore.cpp \
    ../amftevaluator.cpp \
    ../onnxdetector.cpp \
    ../yolopostprocessor.cpp \
    ../imagematconvert.cpp \
    ../logging_categories.cpp \
    ../onnxsottracker.cpp
HEADERS += ../objecttracker.h \
    ../dsstcore.h \
    ../amftevaluator.h \
    ../IDetector.h \
    ../onnxdetector.h \
    ../yolopostprocessor.h \
    ../imagematconvert.h \
    ../logging_categories.h \
    ../ISotTracker.h \
    ../onnxsottracker.h
INCLUDEPATH += $$PWD/.. $$PWD/../opencv/install/include $$PWD/../3rdparty/onnxruntime/include
LIBS += -L$$PWD/../opencv/install/x64/mingw/lib -lopencv_core4100 -lopencv_imgproc4100 -lopencv_imgcodecs4100
LIBS += -L$$PWD/../3rdparty/onnxruntime/lib -lonnxruntime -lDirectML -lpsapi
