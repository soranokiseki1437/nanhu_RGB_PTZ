#include "mainwindow.h"
#include "capturemanager.h"
#include "devicemanager.h"
#include "objecttracker.h"
#include "logmanager.h"

#include <QApplication>
#include <QLoggingCategory>

#include <opencv2/core.hpp>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // Step6：限制 OpenCV 内部线程池，避免与 Qt/解码线程过度争抢 CPU
    cv::setNumThreads(2);

    // 默认开启跟踪与控制排障日志，流媒体网络包保持静音
    if (qEnvironmentVariableIsEmpty("QT_LOGGING_RULES")) {
        QLoggingCategory::setFilterRules(QStringLiteral("*.debug=false\ndefault.debug=true\ntracker.debug=true\nptz.debug=true\nstream.debug=false"));
    }

    // 启动日志（输出到"日志"目录）
    LogManager::instance().startLogging(QStringLiteral("日志"));

    // 注册自定义类型，以便在信号槽中使用
    qRegisterMetaType<ProcessResult>("ProcessResult");
    qRegisterMetaType<LoginResult>("LoginResult");
    qRegisterMetaType<TrackResult>("TrackResult");
    
    MainWindow w;
    w.show();
    return a.exec();
}