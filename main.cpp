#include "mainwindow.h"
#include "capturemanager.h"
#include "devicemanager.h"
#include "objecttracker.h"
#include "logmanager.h"

#include <QApplication>
#include <QLoggingCategory>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 日志分级默认策略：不再用 QT_NO_DEBUG_OUTPUT 一刀切，
    // 默认关闭 debug 级输出（保持安静），现场排障时用环境变量打开，例如：
    //   set QT_LOGGING_RULES=*.debug=true          全部打开
    //   set QT_LOGGING_RULES=tracker.debug=true    只打开跟踪逐帧日志
    if (qEnvironmentVariableIsEmpty("QT_LOGGING_RULES")) {
        QLoggingCategory::setFilterRules(QStringLiteral("*.debug=false"));
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