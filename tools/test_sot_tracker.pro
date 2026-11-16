QT += core
CONFIG += console c++17 release
CONFIG -= app_bundle debug_and_release debug
TARGET = test_sot_tracker
DESTDIR = $$PWD/../build/release
DEFINES += USE_ONNXRUNTIME
SOURCES += test_sot_tracker.cpp ../onnxsottracker.cpp ../amftevaluator.cpp
HEADERS += ../onnxsottracker.h ../ISotTracker.h ../amftevaluator.h
INCLUDEPATH += $$PWD/.. $$PWD/../opencv/install/include $$PWD/../3rdparty/onnxruntime/include
LIBS += -L$$PWD/../opencv/install/x64/mingw/lib -lopencv_core4100 -lopencv_imgproc4100 -lopencv_imgcodecs4100
LIBS += -L$$PWD/../3rdparty/onnxruntime/lib -lonnxruntime -lDirectML -lpsapi

