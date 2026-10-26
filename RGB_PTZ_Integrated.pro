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
    imagematconvert.cpp \
    yolopostprocessor.cpp \
    onnxdetector.cpp \
    amftevaluator.cpp

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
    imagematconvert.h \
    IDetector.h \
    yolopostprocessor.h \
    onnxdetector.h \
    amftevaluator.h

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
unix: LIBS += -L$$OPENCV_DIR/lib -lopencv_core -lopencv_imgproc
# 运行时需把 libopencv_core4100.dll / libopencv_imgproc4100.dll 放到 exe 同目录

# ===== ONNX Runtime (DirectML / C-API 硬件加速) =====
win32 {
    DEFINES += USE_ONNXRUNTIME
    INCLUDEPATH += $$PWD/3rdparty/onnxruntime/include
    LIBS += -L$$PWD/3rdparty/onnxruntime/lib -lonnxruntime -lDirectML
}

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

# ===== 构建后自动复制运行时 DLL 到输出目录 =====
win32 {
    # 目标输出目录
    DESTDIR_WIN = $$shell_path($$OUT_PWD/release)

    # FFmpeg DLLs
    FFMPEG_BIN = $$shell_path($$PWD/ffmpeg-8.1-full_build-shared/bin)
    QMAKE_POST_LINK += copy /Y $$quote($$FFMPEG_BIN\\avcodec-62.dll)   $$quote($$DESTDIR_WIN) &
    QMAKE_POST_LINK += copy /Y $$quote($$FFMPEG_BIN\\avformat-62.dll)  $$quote($$DESTDIR_WIN) &
    QMAKE_POST_LINK += copy /Y $$quote($$FFMPEG_BIN\\avutil-60.dll)    $$quote($$DESTDIR_WIN) &
    QMAKE_POST_LINK += copy /Y $$quote($$FFMPEG_BIN\\swscale-9.dll)    $$quote($$DESTDIR_WIN) &
    QMAKE_POST_LINK += copy /Y $$quote($$FFMPEG_BIN\\swresample-6.dll) $$quote($$DESTDIR_WIN) &

    # SDK DLL
    SDK_BIN = $$shell_path($$PWD/sdk)
    QMAKE_POST_LINK += copy /Y $$quote($$SDK_BIN\\univisionsdk.dll) $$quote($$DESTDIR_WIN) &

    # OpenCV DLLs
    OPENCV_BIN = $$shell_path($$PWD/opencv/install/x64/mingw/bin)
    QMAKE_POST_LINK += copy /Y $$quote($$OPENCV_BIN\\libopencv_core4100.dll)     $$quote($$DESTDIR_WIN) &
    QMAKE_POST_LINK += copy /Y $$quote($$OPENCV_BIN\\libopencv_imgproc4100.dll)  $$quote($$DESTDIR_WIN) &
    QMAKE_POST_LINK += copy /Y $$quote($$OPENCV_BIN\\libopencv_imgcodecs4100.dll) $$quote($$DESTDIR_WIN) &

    # ONNX Runtime & DirectML DLLs
    ORT_BIN = $$shell_path($$PWD/3rdparty/onnxruntime/bin)
    QMAKE_POST_LINK += copy /Y $$quote($$ORT_BIN\\onnxruntime.dll)                  $$quote($$DESTDIR_WIN) &
    QMAKE_POST_LINK += copy /Y $$quote($$ORT_BIN\\DirectML.dll)                     $$quote($$DESTDIR_WIN) &
    QMAKE_POST_LINK += copy /Y $$quote($$ORT_BIN\\onnxruntime_providers_shared.dll) $$quote($$DESTDIR_WIN) &

    # Models ONNX 文件自动同步到输出目录
    MODELS_DIR = $$shell_path($$PWD/models)
    QMAKE_POST_LINK += if not exist $$quote($$DESTDIR_WIN\\models) mkdir $$quote($$DESTDIR_WIN\\models) &
    QMAKE_POST_LINK += copy /Y $$quote($$MODELS_DIR\\*.onnx) $$quote($$DESTDIR_WIN\\models)
}