#include "configmanager.h"
#include <QSettings>
#include <QDebug>

ConfigManager::ConfigManager(QObject *parent) : QObject(parent)
    , m_deviceIP("192.168.1.68")
    , m_deviceUsername("admin")
    , m_devicePassword("nanhu315")
    , m_captureInterval(10)
    , m_captureQuality(80)
    , m_savePath("./captures")
    , m_ptzPortName("COM1")
    , m_ptzBaudRate(9600)
    , m_ptzAddress(1)
    , m_trackKp(0.15f)
    , m_trackKi(0.005f)
    , m_trackKd(0.03f)
    , m_trackKff(0.5f)
    , m_trackTPredict(5)
    , m_trackDeadZone(15)
    , m_trackMaxSpeed(20)
    , m_videoSavePath("./videos")
{
    loadConfig();
}

ConfigManager::~ConfigManager()
{
    saveConfig();
}

QString ConfigManager::getDeviceIP() const
{
    return m_deviceIP;
}

void ConfigManager::setDeviceIP(const QString &ip)
{
    m_deviceIP = ip;
}

QString ConfigManager::getDeviceUsername() const
{
    return m_deviceUsername;
}

void ConfigManager::setDeviceUsername(const QString &username)
{
    m_deviceUsername = username;
}

QString ConfigManager::getDevicePassword() const
{
    return m_devicePassword;
}

void ConfigManager::setDevicePassword(const QString &password)
{
    m_devicePassword = password;
}

int ConfigManager::getCaptureInterval() const
{
    return m_captureInterval;
}

void ConfigManager::setCaptureInterval(int interval)
{
    m_captureInterval = interval;
}

int ConfigManager::getCaptureQuality() const
{
    return m_captureQuality;
}

void ConfigManager::setCaptureQuality(int quality)
{
    m_captureQuality = quality;
}

QString ConfigManager::getSavePath() const
{
    return m_savePath;
}

void ConfigManager::setSavePath(const QString &path)
{
    m_savePath = path;
}

// M5: PTZ 串口配置
QString ConfigManager::getPtzPortName() const { return m_ptzPortName; }
int ConfigManager::getPtzBaudRate() const { return m_ptzBaudRate; }
int ConfigManager::getPtzAddress() const { return m_ptzAddress; }
void ConfigManager::setPtzConfig(const QString &port, int baud, int addr)
{
    m_ptzPortName = port;
    m_ptzBaudRate = baud;
    m_ptzAddress = addr;
}

// M5: 跟踪参数
float ConfigManager::getTrackKp() const { return m_trackKp; }
float ConfigManager::getTrackKi() const { return m_trackKi; }
float ConfigManager::getTrackKd() const { return m_trackKd; }
float ConfigManager::getTrackKff() const { return m_trackKff; }
int ConfigManager::getTrackTPredict() const { return m_trackTPredict; }
int ConfigManager::getTrackDeadZone() const { return m_trackDeadZone; }
int ConfigManager::getTrackMaxSpeed() const { return m_trackMaxSpeed; }
void ConfigManager::setTrackParams(float kp, float ki, float kd, float kff, int tPred, int dz, int maxSpd)
{
    m_trackKp = kp;
    m_trackKi = ki;
    m_trackKd = kd;
    m_trackKff = kff;
    m_trackTPredict = tPred;
    m_trackDeadZone = dz;
    m_trackMaxSpeed = maxSpd;
}

// M5: 录像路径
QString ConfigManager::getVideoSavePath() const { return m_videoSavePath; }
void ConfigManager::setVideoSavePath(const QString &path) { m_videoSavePath = path; }

void ConfigManager::saveConfig()
{
    QSettings settings("RGB_PTZ", "RGB_PTZ_Integrated");
    settings.setValue("device/ip", m_deviceIP);
    settings.setValue("device/username", m_deviceUsername);
    settings.setValue("device/password", m_devicePassword);
    settings.setValue("capture/interval", m_captureInterval);
    settings.setValue("capture/quality", m_captureQuality);
    settings.setValue("capture/savePath", m_savePath);
    // M5: 持久化扩展配置
    settings.setValue("ptz/portName", m_ptzPortName);
    settings.setValue("ptz/baudRate", m_ptzBaudRate);
    settings.setValue("ptz/address", m_ptzAddress);
    settings.setValue("track/kp", m_trackKp);
    settings.setValue("track/ki", m_trackKi);
    settings.setValue("track/kd", m_trackKd);
    settings.setValue("track/kff", m_trackKff);
    settings.setValue("track/tPredict", m_trackTPredict);
    settings.setValue("track/deadZone", m_trackDeadZone);
    settings.setValue("track/maxSpeed", m_trackMaxSpeed);
    settings.setValue("video/savePath", m_videoSavePath);
    qDebug() << "Config saved";
}

void ConfigManager::loadConfig()
{
    QSettings settings("RGB_PTZ", "RGB_PTZ_Integrated");
    m_deviceIP = settings.value("device/ip", "192.168.1.68").toString();
    m_deviceUsername = settings.value("device/username", "admin").toString();
    m_devicePassword = settings.value("device/password", "nanhu315").toString();
    m_captureInterval = settings.value("capture/interval", 10).toInt();
    m_captureQuality = settings.value("capture/quality", 80).toInt();
    m_savePath = settings.value("capture/savePath", "./captures").toString();
    // M5: 加载扩展配置
    m_ptzPortName = settings.value("ptz/portName", "COM1").toString();
    m_ptzBaudRate = settings.value("ptz/baudRate", 9600).toInt();
    m_ptzAddress = settings.value("ptz/address", 1).toInt();
    m_trackKp = settings.value("track/kp", 0.15f).toFloat();
    m_trackKi = settings.value("track/ki", 0.005f).toFloat();
    m_trackKd = settings.value("track/kd", 0.03f).toFloat();
    m_trackKff = settings.value("track/kff", 0.5f).toFloat();
    m_trackTPredict = settings.value("track/tPredict", 5).toInt();
    m_trackDeadZone = settings.value("track/deadZone", 15).toInt();
    m_trackMaxSpeed = settings.value("track/maxSpeed", 20).toInt();
    m_videoSavePath = settings.value("video/savePath", "./videos").toString();
    qDebug() << "Config loaded";
}
