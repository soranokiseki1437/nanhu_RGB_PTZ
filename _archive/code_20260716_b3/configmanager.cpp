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

void ConfigManager::saveConfig()
{
    QSettings settings("RGB_PTZ", "RGB_PTZ_Integrated");
    settings.setValue("device/ip", m_deviceIP);
    settings.setValue("device/username", m_deviceUsername);
    settings.setValue("device/password", m_devicePassword);
    settings.setValue("capture/interval", m_captureInterval);
    settings.setValue("capture/quality", m_captureQuality);
    settings.setValue("capture/savePath", m_savePath);
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
    qDebug() << "Config loaded";
}
