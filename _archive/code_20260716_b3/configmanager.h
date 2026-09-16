#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <QObject>
#include <QString>

class ConfigManager : public QObject
{
    Q_OBJECT

public:
    explicit ConfigManager(QObject *parent = nullptr);
    ~ConfigManager();

    QString getDeviceIP() const;
    void setDeviceIP(const QString &ip);

    QString getDeviceUsername() const;
    void setDeviceUsername(const QString &username);

    QString getDevicePassword() const;
    void setDevicePassword(const QString &password);

    int getCaptureInterval() const;
    void setCaptureInterval(int interval);

    int getCaptureQuality() const;
    void setCaptureQuality(int quality);

    QString getSavePath() const;
    void setSavePath(const QString &path);

    void saveConfig();

private:
    QString m_deviceIP;
    QString m_deviceUsername;
    QString m_devicePassword;
    int m_captureInterval;
    int m_captureQuality;
    QString m_savePath;

    void loadConfig();
};

#endif // CONFIGMANAGER_H