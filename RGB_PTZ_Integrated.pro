QT       += core gui serialport concurrent

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    ptzcontroller.cpp \
    devicemanager.cpp \
    capturemanager.cpp \
    imageprocessor.cpp \
    configmanager.cpp \
    videodecoder.cpp \
    DecodeThread.cpp \
    RecordThread.cpp \
    lensmanager.cpp \
    datarecorder.cpp \
    objecttracker.cpp \
    trackingcontroller.cpp \
    dsstcore.cpp \
    logmanager.cpp \
    logging_categories.cpp \
    imagematconvert.cpp

HEADERS += \
    mainwindow.h \
    ptzcontroller.h \
    devicemanager.h \
    capturemanager.h \
    imageprocessor.h \
    configmanager.h \
    videodecoder.h \
    DecodeThread.h \
    ThreadSafeQueue.h \
    RecordThread.h \
    lensmanager.h \
    datatypes.h \
    datarecorder.h \
    objecttracker.h \
    trackingcontroller.h \
    dsstcore.h \
    logmanager.h \
    logging_categories.h \
    imagematconvert.h

FORMS += \
    mainwindow.ui



# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

INCLUDEPATH += $$PWD/sdk
LIBS += -L$$PWD/sdk -lunivisionsdk

# FFmpeg 依赖
FFMPEG_DIR = $$PWD/ffmpeg-8.1-full_build-shared
INCLUDEPATH += $$FFMPEG_DIR/include
LIBS += -L$$FFMPEG_DIR/lib \
        -lavcodec \
        -lavformat \
        -lavutil \
        -lswscale \
        -lswresample

# ===== OpenCV 依赖（DSST 跟踪器，源码编译的最小集 core+imgproc，见 待执行方案/03） =====
OPENCV_DIR = $$PWD/opencv/install
INCLUDEPATH += $$OPENCV_DIR/include

win32: LIBS += -L$$OPENCV_DIR/x64/mingw/lib \
    -lopencv_core4100 \
    -lopencv_imgproc4100
# 运行时需把 libopencv_core4100.dll / libopencv_imgproc4100.dll 放到 exe 同目录

# 避免C语言相关警告
DEFINES += _CRT_SECURE_NO_WARNINGS

# 修复P3#30: 添加版本信息宏
DEFINES += PROJECT_VERSION=\\\"1.0.0\\\"
DEFINES += PROJECT_NAME=\\\"RGB_PTZ_Integrated\\\"

# Release 下不再一刀切关闭 qDebug（QT_NO_DEBUG_OUTPUT 会让 qCDebug 在编译期整体失效），
# 改为运行时用 QLoggingCategory 分类开关，默认策略见 main.cpp
CONFIG(debug, debug|release) {
    DEFINES += QT_MESSAGELOGCONTEXT
}