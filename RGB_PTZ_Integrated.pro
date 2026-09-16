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
    fftutils.cpp \
    logmanager.cpp

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
    fftutils.h \
    logmanager.h

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

# 避免C语言相关警告
DEFINES += _CRT_SECURE_NO_WARNINGS

# 修复P3#30: 添加版本信息宏
DEFINES += PROJECT_VERSION=\\\"1.0.0\\\"
DEFINES += PROJECT_NAME=\\\"RGB_PTZ_Integrated\\\"

# 修复P3#29: 生产环境下禁用qDebug（通过qmake配置控制）
CONFIG(debug, debug|release) {
    DEFINES += QT_MESSAGELOGCONTEXT
} else {
    # Release模式下，将qDebug替换为void以避免输出
    DEFINES += QT_NO_DEBUG_OUTPUT
}