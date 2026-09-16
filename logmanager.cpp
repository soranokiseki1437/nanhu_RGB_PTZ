#include "logmanager.h"
#include <QDir>
#include <QDateTime>
#include <QCoreApplication>
#include <QMutexLocker>
#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <cstdio>

LogManager::LogManager()
    : m_initialized(false)
{
}

LogManager::~LogManager()
{
    stopLogging();
}

LogManager& LogManager::instance()
{
    static LogManager inst;
    return inst;
}

void LogManager::startLogging(const QString& logDir)
{
    QMutexLocker lock(&m_mutex);

    if (m_initialized) return;

    m_logDir = logDir;

    // 确保日志目录存在
    QDir dir;
    if (!dir.exists(m_logDir)) {
        if (!dir.mkpath(m_logDir)) {
            std::fprintf(stderr, "LogManager: 无法创建日志目录: %s\n",
                         m_logDir.toLocal8Bit().constData());
            // 退回到应用程序所在目录
            m_logDir = QCoreApplication::applicationDirPath();
        }
    }

    // 生成日志文件名: YYYYMMDDHHmm.txt
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMddHHmm");
    m_currentLogFile = QString("%1/%2.txt").arg(m_logDir, timestamp);

    // 打开文件（追加模式，文本）
    m_logFile.setFileName(m_currentLogFile);
    if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        std::fprintf(stderr, "LogManager: 无法打开日志文件: %s (error=%d)\n",
                     m_currentLogFile.toLocal8Bit().constData(),
                     static_cast<int>(m_logFile.error()));
        return;
    }

    // 关联到 QTextStream（Qt 6 默认 UTF-8）
    m_logStream.setDevice(&m_logFile);

    // 写入 UTF-8 BOM，使 Windows 记事本正确识别（只在新文件开头写一次）
    if (m_logFile.size() == 0) {
        const char bom[] = { '\xEF', '\xBB', '\xBF' };
        m_logFile.write(bom, 3);
    }

    m_initialized = true;

    // 安装全局消息处理
    qInstallMessageHandler(LogManager::messageHandler);

    // 首条日志
    QString appInfo = QString("%1 %2").arg(QCoreApplication::applicationName(),
                                           QCoreApplication::applicationVersion());
    QString startMsg = QString("==== 启动日志 ==== %1 ==== 应用: %2 ==== 文件: %3")
                           .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz"),
                                appInfo,
                                QDir::toNativeSeparators(m_currentLogFile));
    m_logStream << startMsg << "\n";
    m_logStream.flush();
}

void LogManager::stopLogging()
{
    QMutexLocker lock(&m_mutex);

    if (m_initialized) {
        // 先恢复默认处理，防止析构期间再产生消息
        qInstallMessageHandler(nullptr);

        m_logStream.flush();
        if (m_logFile.isOpen()) {
            m_logFile.close();
        }
        m_initialized = false;
    }
}

QString LogManager::currentLogFile() const
{
    QMutexLocker lock(&m_mutex);
    return m_currentLogFile;
}

void LogManager::messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    LogManager::instance().handleMessage(type, context, msg);
}

void LogManager::handleMessage(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    QMutexLocker lock(&m_mutex);

    if (!m_initialized) return;

    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");

    const char* typeStr = "INFO";
    switch (type) {
        case QtDebugMsg:    typeStr = "DEBUG"; break;
        case QtInfoMsg:     typeStr = "INFO";  break;
        case QtWarningMsg:  typeStr = "WARN";  break;
        case QtCriticalMsg: typeStr = "ERROR"; break;
        case QtFatalMsg:    typeStr = "FATAL"; break;
        default:            typeStr = "?????"; break;
    }

    // 简化文件名（仅保留文件名，不含路径）
    QString shortFile;
    if (context.file != nullptr) {
        QString file = QString::fromUtf8(context.file);
        int idx = file.lastIndexOf('/');
        if (idx < 0) idx = file.lastIndexOf('\\');
        shortFile = (idx >= 0) ? file.mid(idx + 1) : file;
    }

    // 组装文件日志
    QString fileLine;
    if (!shortFile.isEmpty() && context.line > 0) {
        fileLine = QString("[%1] [%2] [%3:%4] %5")
                       .arg(timestamp, QString::fromLatin1(typeStr), shortFile)
                       .arg(context.line).arg(msg);
    } else {
        fileLine = QString("[%1] [%2] %3")
                       .arg(timestamp, QString::fromLatin1(typeStr), msg);
    }

    // 写入日志文件（UTF-8）
    m_logStream << fileLine << "\n";
    m_logStream.flush();

    // 控制台输出：Windows 控制台用 WriteConsoleW 保证中文，其它平台走 fprintf
    QString consoleLine;
    if (!shortFile.isEmpty() && context.line > 0) {
        consoleLine = QString("[%1] [%2] %3\n").arg(timestamp, QString::fromLatin1(typeStr), msg);
    } else {
        consoleLine = QString("[%1] [%2] %3\n").arg(timestamp, QString::fromLatin1(typeStr), msg);
    }

#ifdef Q_OS_WIN
    HANDLE hStdErr = GetStdHandle(STD_ERROR_HANDLE);
    if (hStdErr != INVALID_HANDLE_VALUE && hStdErr != nullptr) {
        DWORD mode = 0;
        if (GetConsoleMode(hStdErr, &mode)) {
            // 真正的控制台窗口 —— 用 Unicode 输出
            DWORD written = 0;
            WriteConsoleW(hStdErr, consoleLine.utf16(),
                          static_cast<DWORD>(consoleLine.size()),
                          &written, nullptr);
        } else {
            // IDE 输出面板或重定向到文件 —— 走 UTF-8
            std::fprintf(stderr, "%s", consoleLine.toUtf8().constData());
        }
    } else {
        std::fprintf(stderr, "%s", consoleLine.toUtf8().constData());
    }
#else
    std::fprintf(stderr, "%s", consoleLine.toUtf8().constData());
#endif

    // 致命错误 —— 刷新文件后 abort
    if (type == QtFatalMsg) {
        m_logStream.flush();
        m_logFile.close();
        std::abort();
    }
}
