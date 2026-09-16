#ifndef LOGMANAGER_H
#define LOGMANAGER_H

#include <QObject>
#include <QString>
#include <QFile>
#include <QTextStream>
#include <QMutex>

class LogManager : public QObject
{
    Q_OBJECT
public:
    static LogManager& instance();

    void startLogging(const QString& logDir = QStringLiteral("日志"));
    void stopLogging();

    QString currentLogFile() const;

private:
    LogManager();
    ~LogManager();
    Q_DISABLE_COPY(LogManager)

    static void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg);
    void handleMessage(QtMsgType type, const QMessageLogContext& context, const QString& msg);

    QFile m_logFile;
    QTextStream m_logStream;
    mutable QMutex m_mutex;
    QString m_logDir;
    QString m_currentLogFile;
    bool m_initialized;
};

#endif // LOGMANAGER_H
