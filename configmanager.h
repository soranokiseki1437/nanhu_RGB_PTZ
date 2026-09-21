#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <QObject>
#include <QString>
#include <QMutex>

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

    // M5: PTZ 串口配置
    QString getPtzPortName() const;
    int getPtzBaudRate() const;
    int getPtzAddress() const;
    void setPtzConfig(const QString &port, int baud, int addr);

    // M5: 跟踪参数
    float getTrackKp() const;
    float getTrackKi() const;
    float getTrackKd() const;
    float getTrackKff() const;
    int getTrackTPredict() const;
    int getTrackDeadZone() const;
    int getTrackMaxSpeed() const;
    void setTrackParams(float kp, float ki, float kd, float kff, int tPred, int dz, int maxSpd);

    // M5: 录像路径
    QString getVideoSavePath() const;
    void setVideoSavePath(const QString &path);

    void saveConfig();

private:
    mutable QMutex m_mutex;  // P7：保护全部配置字段的并发读写
    QString m_deviceIP;
    QString m_deviceUsername;
    QString m_devicePassword;
    int m_captureInterval;
    int m_captureQuality;
    QString m_savePath;

    // M5: 扩展配置
    QString m_ptzPortName;
    int m_ptzBaudRate;
    int m_ptzAddress;
    float m_trackKp, m_trackKi, m_trackKd, m_trackKff;
    int m_trackTPredict, m_trackDeadZone, m_trackMaxSpeed;
    QString m_videoSavePath;

    void loadConfig();
};

#endif // CONFIGMANAGER_H