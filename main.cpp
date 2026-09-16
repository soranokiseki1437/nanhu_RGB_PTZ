#include "mainwindow.h"
#include "capturemanager.h"
#include "devicemanager.h"
#include "objecttracker.h"
#include "logmanager.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

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