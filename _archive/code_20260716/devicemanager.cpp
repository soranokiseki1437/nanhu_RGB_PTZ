#include "devicemanager.h"
#include <QDebug>
#include <QtConcurrent>
#include "sdk/sdk.h"
#include "ptzcontroller.h"

// 静态实例指针
DeviceManager *DeviceManager::instance = nullptr;
QMutex DeviceManager::s_instanceMutex;

DeviceManager::DeviceManager(QObject *parent) : QObject(parent)
    , m_sdkInitialized(false)
    , m_connected(false)
    , m_userID(0)
    , m_playHandle(0)
    , m_streaming(false)
    , m_decodeThread(nullptr)
    , m_recordThread(nullptr)
    , m_isRecording(false)
    , m_loginWatcher(new QFutureWatcher<LoginResult>(this))
    , m_lensManager(new LensManager(this))
    , m_dataRecorder(new DataRecorder(this))
{
    QMutexLocker lock(&s_instanceMutex);
    instance = this;
    m_decodeThread = new DecodeThread(this);
    connect(m_decodeThread, &DecodeThread::frameDecoded, this, &DeviceManager::onFrameDecoded);
    connect(m_loginWatcher, &QFutureWatcher<LoginResult>::finished, this, &DeviceManager::onLoginFinished);
    connect(m_lensManager, &LensManager::errorOccurred, this, &DeviceManager::errorOccurred);
    
    // 连接数据记录器信号
    connect(m_dataRecorder, &DataRecorder::recordingStarted, this, &DeviceManager::onDataRecordStarted);
    connect(m_dataRecorder, &DataRecorder::recordingStopped, this, &DeviceManager::onDataRecordStopped);
    connect(m_dataRecorder, &DataRecorder::errorOccurred, this, &DeviceManager::errorOccurred);
}

LensManager* DeviceManager::getLensManager()
{
    return m_lensManager;
}

DataRecorder* DeviceManager::getDataRecorder()
{
    return m_dataRecorder;
}

DeviceManager::~DeviceManager()
{
    // 1. 停止录像
    stopRecord();
    
    // 2. 停止流播放
    stopStream();
    
    // 3. 先设置 instance = nullptr，阻止新的 SDK 回调进入
    {
        QMutexLocker lock(&s_instanceMutex);
        if (instance == this) {
            instance = nullptr;
        }
    }
    
    // 4. 确保 QFutureWatcher 完成或取消正在进行的任务
    if (m_loginWatcher) {
        m_loginWatcher->cancel();
        m_loginWatcher->waitForFinished();
    }
    
    // 5. 清理 SDK 相关资源
    cleanupSDK();
    
    // 6. 停止并清理解码线程
    if (m_decodeThread) {
        m_decodeThread->stop();
        m_decodeThread->wait();
        m_decodeThread = nullptr;
    }
    
    // 7. 清理录像线程
    if (m_recordThread) {
        m_recordThread->stopRecord();
        m_recordThread->wait();
        delete m_recordThread;
        m_recordThread = nullptr;
    }
}

void DeviceManager::onFrameDecoded(const QImage &frame)
{
    emit frameReceived(frame);
}

bool DeviceManager::initSDK()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_sdkInitialized) {
        return true;
    }
    
    // 初始化SDK
    int ret = UNIV_SDK_Init();
    if (ret != SUCCESS) {
        emit errorOccurred(QString("SDK初始化失败: %1").arg(ret));
        return false;
    }
    
    // 设置日志级别
    UNIV_SDK_SetLogLevel(2);
    
    // 设置异常回调
    UNIV_SDK_SetExceptionCallBack((UNIV_ExceptionCallBack)OnException);
    
    m_sdkInitialized = true;
    qDebug() << "SDK初始化成功";
    return true;
}

void DeviceManager::cleanupSDK()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_connected) {
        logout();
    }
    
    if (m_sdkInitialized) {
        UNIV_SDK_Cleanup();
        m_sdkInitialized = false;
        qDebug() << "SDK清理成功";
    }
}

// 静态后台登录函数
LoginResult DeviceManager::loginInBackground(QString ip, QString username, QString password)
{
    LoginResult result;
    result.success = false;
    result.userID = 0;
    
    try {
        qDebug() << "[登录] 后台线程开始执行";
        qDebug() << "[登录] 准备登录参数";
        
        // 准备登录参数
        UNIV_DEV_LOGIN_PARAM dev;
        memset(&dev, 0, sizeof(dev));
        strncpy(dev.ip, ip.toUtf8().constData(), sizeof(dev.ip) - 1);
        dev.port = 80;
        strncpy(dev.username, username.toUtf8().constData(), sizeof(dev.username) - 1);
        strncpy(dev.password, password.toUtf8().constData(), sizeof(dev.password) - 1);
        
        qDebug() << "[登录] 尝试激活设备";
        // 尝试激活设备
        int activate_ret = UNIV_DEV_Activate(&dev);
        if (activate_ret == 0) {
            qDebug() << "[登录] 设备激活成功";
        } else {
            qDebug() << "[登录] 设备激活失败或无需激活，错误码:" << activate_ret;
        }
        
        qDebug() << "[登录] 尝试登录设备";
        // 登录设备
        uint64_t userID = 0;
        int ret = UNIV_DEV_Login(&dev, &userID);
        qDebug() << "[登录] 登录结果，错误码:" << ret;
        
        if (ret != SUCCESS) {
            // 分析错误原因 - 需要在后台完成分析
            QString errorMsg;
            switch (ret) {
            case -1:
                errorMsg = "登录失败: 网络连接失败，请检查设备IP是否正确或网络是否通畅";
                break;
            case -2:
                errorMsg = "登录失败: 用户名或密码错误";
                break;
            case -3:
                errorMsg = "登录失败: 设备不在线或无法访问";
                break;
            case -4:
                errorMsg = "登录失败: 设备已被其他用户登录";
                break;
            case -5:
                errorMsg = "登录失败: 设备权限不足";
                break;
            case -6:
                errorMsg = "登录失败: 设备超时无响应";
                break;
            case -7:
                errorMsg = "登录失败: 设备版本不兼容";
                break;
            case -8:
                errorMsg = "登录失败: 设备激活失败";
                break;
            case -9:
                errorMsg = "登录失败: 设备存储空间不足";
                break;
            case -10:
                errorMsg = "登录失败: 设备网络配置错误";
                break;
            default:
                errorMsg = QString("登录失败: 未知错误 (错误码: %1)").arg(ret);
                break;
            }
            result.errorMsg = errorMsg;
            qDebug() << "[登录] 登录失败，错误信息:" << errorMsg;
            return result;
        }
        
        qDebug() << "[登录] 登录成功，用户ID:" << userID;
        result.success = true;
        result.userID = userID;
        
    } catch (const std::exception& e) {
        QString errorMsg = QString("登录过程发生异常: %1").arg(e.what());
        qDebug() << "[登录] 捕获到std::exception:" << errorMsg;
        result.errorMsg = errorMsg;
    } catch (...) {
        QString errorMsg = "登录过程发生未知异常";
        qDebug() << "[登录] 捕获到未知异常";
        result.errorMsg = errorMsg;
    }
    
    return result;
}

void DeviceManager::login(const QString &ip, const QString &username, const QString &password)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_connected) {
        logout();
    }
    
    m_ip = ip;
    m_username = username;
    m_password = password;
    
    // 首先初始化 SDK（在主线程中安全地完成）
    if (!m_sdkInitialized) {
        qDebug() << "[登录] 初始化SDK";
        locker.unlock();
        if (!initSDK()) {
            return;
        }
        locker.relock();
    }
    
    // 重置连接状态
    m_connected = false;
    m_userID = 0;
    qDebug() << "[登录] 开始连接设备，IP:" << ip << "用户名:" << username;
    
    // 立即发送连接开始的信号，更新 UI 状态
    qDebug() << "[登录] 发送连接开始信号";
    locker.unlock();
    emit connectionStatusChanged(false);
    
    // 使用 QFutureWatcher 安全地执行后台登录
    QFuture<LoginResult> future = QtConcurrent::run(
        loginInBackground, ip, username, password);
    m_loginWatcher->setFuture(future);
}

void DeviceManager::onLoginFinished()
{
    // 在主线程中安全地处理登录结果
    LoginResult result = m_loginWatcher->result();
    
    QMutexLocker locker(&m_mutex);
    
    if (result.success) {
        // 登录成功
        m_userID = result.userID;
        m_connected = true;
        qDebug() << "[登录] 更新连接状态为已连接";
        
        // 设置镜头管理器的用户ID
        m_lensManager->setUserID(m_userID);
        
        locker.unlock();
        emit connectionStatusChanged(true);
        qDebug() << "[登录] 登录成功处理完成";
        
        // 获取设备信息（在主线程中执行）
        locker.relock();
        qDebug() << "[登录] 尝试获取设备信息";
        UNIV_DEV_DEVICE_INFO_PARAM cfg;
        memset(&cfg, 0, sizeof(cfg));
        if (!UNIV_DEV_GetConfig(m_userID, UNIV_CFG_DEVICE_INFO, &cfg, sizeof(cfg))) {
            qDebug() << "[登录] 设备名称:" << cfg.deviceName;
            qDebug() << "[登录] 设备型号:" << cfg.deviceModel;
            qDebug() << "[登录] 设备序列号:" << cfg.serialNumber;
            qDebug() << "[登录] 设备版本:" << cfg.deviceVersion;
        } else {
            qDebug() << "[登录] 获取设备信息失败";
        }
        
        // 关闭相机自带的 OSD 时间（在主线程中执行）
        qDebug() << "[登录] 尝试关闭相机自带的OSD时间";
        locker.unlock();
        disableCameraOSDTime();
        
        // 打印当前视频配置
        qDebug() << "[登录] 打印当前视频配置";
        printVideoConfig(MAIN);  // 打印主码流配置
        printVideoConfig(EXTRA1); // 打印子码流配置
    } else {
        // 登录失败
        qDebug() << "[登录] 发射错误信号";
        emit errorOccurred(result.errorMsg);
        
        // 确保状态回到未连接
        m_connected = false;
        m_userID = 0;
        qDebug() << "[登录] 确保状态回到未连接";
        
        // 在主线程中发射状态变化信号
        qDebug() << "[登录] 发射状态变化信号(false)";
        emit connectionStatusChanged(false);
        qDebug() << "[登录] 登录失败处理完成";
    }
}

bool DeviceManager::logout()
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_connected) {
        return true;
    }

    if (m_streaming) {
        locker.unlock();
        stopStream();
        locker.relock();
    }
    
    int ret = UNIV_DEV_Logout(m_userID);
    if (ret != SUCCESS) {
        emit errorOccurred(QString("注销失败: %1").arg(ret));
        return false;
    }
    
    m_connected = false;
    m_userID = 0;
    // 清空镜头管理器的用户ID
    m_lensManager->setUserID(0);
    emit connectionStatusChanged(false);
    qDebug() << "设备注销成功";
    return true;
}

bool DeviceManager::isConnected() const
{
    QMutexLocker locker(&m_mutex);
    return m_connected;
}

uint64_t DeviceManager::getUserID() const
{
    QMutexLocker locker(&m_mutex);
    return m_userID;
}

void DeviceManager::OnException(uint32_t event, uint64_t userID)
{
    // 先安全检查并临时获取 instance
    DeviceManager* safeInstance = nullptr;
    {
        QMutexLocker lock(&s_instanceMutex);
        safeInstance = instance;
    }
    
    if (!safeInstance) {
        return;
    }
    
    switch (event) {
    case EXCEPTION_KEEP_ALIVE:
        qDebug() << "保活失败，设备断开连接，用户ID:" << userID;
        emit safeInstance->errorOccurred("保活失败，设备断开连接");
        safeInstance->logout();
        break;
    case EXCEPTION_SESSION_CLOSE:
        qDebug() << "会话断开连接，用户ID:" << userID;
        emit safeInstance->errorOccurred("会话断开连接");
        break;
    default:
        break;
    }
}

QString DeviceManager::analyzeLoginError(int errorCode)
{
    switch (errorCode) {
    case -1:
        return "登录失败: 网络连接失败，请检查设备IP是否正确或网络是否通畅";
    case -2:
        return "登录失败: 用户名或密码错误";
    case -3:
        return "登录失败: 设备不在线或无法访问";
    case -4:
        return "登录失败: 设备已被其他用户登录";
    case -5:
        return "登录失败: 设备权限不足";
    case -6:
        return "登录失败: 设备超时无响应";
    case -7:
        return "登录失败: 设备版本不兼容";
    case -8:
        return "登录失败: 设备激活失败";
    case -9:
        return "登录失败: 设备存储空间不足";
    case -10:
        return "登录失败: 设备网络配置错误";
    default:
        return QString("登录失败: 未知错误 (错误码: %1)").arg(errorCode);
    }
}

bool DeviceManager::startStream()
{
    QMutexLocker locker(&m_mutex);

    if (!m_connected) {
        qWarning() << "[Stream] 设备未连接，无法开始预览";
        return false;
    }

    if (m_streaming) {
        qDebug() << "[Stream] 已经在预览中";
        return true;
    }

    // 启动解码线程
    if (!m_decodeThread->isRunning()) {
        m_decodeThread->start();
    }

    uint64_t playHandle = 0;
    int ret = UNIV_DEV_RealPlay(m_userID, 0, MAIN, OnStreamData, &playHandle);
    if (ret != SUCCESS) {
        qCritical() << "[Stream] 开始预览失败，错误码:" << ret;
        return false;
    }

    m_playHandle = playHandle;
    m_streaming = true;
    qDebug() << "[Stream] 开始预览成功，句柄:" << m_playHandle;
    return true;
}

bool DeviceManager::stopStream()
{
    QMutexLocker locker(&m_mutex);

    if (!m_streaming) {
        return true;
    }

    int ret = UNIV_DEV_StopRealPlay(m_playHandle);
    if (ret != SUCCESS) {
        qWarning() << "[Stream] 停止预览失败，错误码:" << ret;
    }

    m_streaming = false;
    m_playHandle = 0;
    qDebug() << "[Stream] 停止预览成功";
    return true;
}

bool DeviceManager::isStreaming() const
{
    QMutexLocker locker(&m_mutex);
    return m_streaming;
}

bool DeviceManager::isRecording() const
{
    QMutexLocker locker(&m_mutex);
    return m_isRecording;
}

void DeviceManager::OnStreamData(uint64_t /*handle*/, uint8_t dataType, void* pData, uint32_t dataSize)
{
    // 先安全检查并临时获取 instance
    DeviceManager* safeInstance = nullptr;
    {
        QMutexLocker lock(&s_instanceMutex);
        safeInstance = instance;
    }
    
    if (!safeInstance || !pData || dataSize == 0) {
        return;
    }

    if (dataType == 'I' || dataType == 'P') {
        // 深拷贝数据到QByteArray，快速退出回调
        QByteArray safeData(static_cast<const char*>(pData), dataSize);
        // 推给解码线程
        safeInstance->m_decodeThread->pushData(safeData);
        
        // 如果正在录像，也推给录像线程
        if (safeInstance->m_isRecording && safeInstance->m_recordThread) {
            safeInstance->m_recordThread->pushData(safeData);
        }
    }
}

void DeviceManager::disableCameraOSDTime()
{
    QMutexLocker locker(&m_mutex);

    if (!m_connected || m_userID == 0) {
        qWarning() << "[OSD] 设备未连接，无法关闭OSD时间";
        return;
    }

    // 获取当前OSD配置
    UNIV_DEV_OSD_PARAM osdParam;
    memset(&osdParam, 0, sizeof(osdParam));

    int ret = UNIV_DEV_GetConfig(m_userID, UNIV_CFG_OSD, &osdParam, sizeof(osdParam));
    if (ret != SUCCESS) {
        qWarning() << "[OSD] 获取OSD配置失败，错误码:" << ret;
        return;
    }

    // 关闭时间显示
    osdParam.enableTime = 0;  // 0=关闭，1=开启

    // 应用新配置
    ret = UNIV_DEV_SetConfig(m_userID, UNIV_CFG_OSD, &osdParam, sizeof(osdParam));
    if (ret != SUCCESS) {
        qWarning() << "[OSD] 设置OSD配置失败，错误码:" << ret;
    } else {
        qDebug() << "[OSD] 成功关闭相机自带的OSD时间";
    }
}

void DeviceManager::printVideoConfig(uint8_t streamType)
{
    QMutexLocker locker(&m_mutex);

    if (!m_connected || m_userID == 0) {
        qWarning() << "[VideoConfig] 设备未连接，无法获取视频配置";
        return;
    }

    QString streamName;
    switch (streamType) {
    case MAIN:
        streamName = "主码流";
        break;
    case EXTRA1:
        streamName = "子码流";
        break;
    case EXTRA2:
        streamName = "子码流2";
        break;
    default:
        streamName = "未知码流";
        break;
    }

    // 获取当前视频编码配置
    UNIV_DEV_VIDEO_ENC_PARAM videoParam;
    memset(&videoParam, 0, sizeof(videoParam));
    videoParam.stream = streamType;  // 指定要获取的码流类型

    int ret = UNIV_DEV_GetConfig(m_userID, UNIV_CFG_VIDEO_ENCODE, &videoParam, sizeof(videoParam));
    if (ret != SUCCESS) {
        qWarning() << "[VideoConfig] 获取" << streamName << "配置失败，错误码:" << ret;
        return;
    }

    qDebug() << "========================================";
    qDebug() << "[VideoConfig]" << streamName << "配置信息:";
    qDebug() << "  码流类型:" << (videoParam.streamType == 0 ? "视频流" : "复合流");
    qDebug() << "  视频编码:" << videoParam.videoEncType;
    qDebug() << "  分辨率:" << videoParam.resolution.width << "x" << videoParam.resolution.height;
    qDebug() << "  帧率(fps):" << videoParam.videoFrameRate;
    qDebug() << "  I帧间隔:" << videoParam.intervalFrameI;
    qDebug() << "  视频码率:" << videoParam.videoBitrate;
    qDebug() << "  码率类型:" << (videoParam.bitrateType == 0 ? "定码率" : "变码率");
    qDebug() << "  图像质量:" << videoParam.picQuality;
    qDebug() << "========================================";
}

bool DeviceManager::startRecord(const QString& filePath)
{
    QMutexLocker locker(&m_mutex);

    if (!m_connected || m_userID == 0) {
        emit errorOccurred("设备未连接，无法开始录像");
        return false;
    }

    if (m_isRecording) {
        qWarning() << "[Record] 已经在录像中";
        return false;
    }

    // 获取相机真实帧率
    UNIV_DEV_VIDEO_ENC_PARAM videoParam;
    memset(&videoParam, 0, sizeof(videoParam));
    videoParam.stream = MAIN;

    int ret = UNIV_DEV_GetConfig(m_userID, UNIV_CFG_VIDEO_ENCODE, &videoParam, sizeof(videoParam));
    int frameRate = 25;
    if (ret == SUCCESS) {
        frameRate = videoParam.videoFrameRate;
        qDebug() << "[Record] 获取到相机帧率:" << frameRate;
    }

    // 创建录像线程
    if (!m_recordThread) {
        m_recordThread = new RecordThread(this);
        connect(m_recordThread, &RecordThread::errorOccurred, this, &DeviceManager::onRecordError);
        connect(m_recordThread, &RecordThread::recordFinished, this, &DeviceManager::onRecordFinished);
    }

    // 初始化录像
    if (!m_recordThread->initRecord(filePath, frameRate)) {
        return false;
    }

    m_isRecording = true;
    m_recordThread->start();
    
    // 启动数据记录（同时保存云台数据
    locker.unlock();
    m_dataRecorder->startRecording(filePath);
    
    // 尝试启动PTZ自动查询
    // 注意：PTZController是在MainWindow中管理，我们假设在MainWindow中已经连接好了
    // 这里先记录PTZ角度
    emit recordStarted();
    qDebug() << "[Record] 开始录像，文件:" << filePath;
    return true;
}

void DeviceManager::stopRecord()
{
    QMutexLocker locker(&m_mutex);

    if (!m_isRecording) {
        return;
    }

    m_isRecording = false;

    if (m_recordThread) {
        m_recordThread->stopRecord();
    }
    
    // 停止数据记录
    locker.unlock();
    m_dataRecorder->stopRecording();

    qDebug() << "[Record] 停止录像";
}

void DeviceManager::onRecordError(const QString &error)
{
    qWarning() << "[Record] 录像错误:" << error;
    emit errorOccurred(error);
    
    QMutexLocker locker(&m_mutex);
    m_isRecording = false;
}

void DeviceManager::onRecordFinished(const QString &filePath)
{
    qDebug() << "[Record] 录像完成，文件:" << filePath;
    emit recordStopped(filePath);
}

void DeviceManager::onPTZAngleReceived(float pan, float tilt)
{
    // 更新数据记录器的PTZ数据
    if (m_dataRecorder->isRecording()) {
        // 假设PTZ速度暂时设为0，需要从PTZController获取实际速度
        float panSpeed = 0.0f;
        float tiltSpeed = 0.0f;
        m_dataRecorder->updatePTZData(pan, tilt, panSpeed, tiltSpeed);
    }
    
    // 转发PTZ角度信号
    emit ptzAngleReceived(pan, tilt);
}

void DeviceManager::onDataRecordStarted(const QString &filePath)
{
    qDebug() << "[DataRecorder] 数据记录已开始，文件:" << filePath;
}

void DeviceManager::onDataRecordStopped(const QString &filePath)
{
    qDebug() << "[DataRecorder] 数据记录已结束，文件:" << filePath;
}

// SDK OSD 相关接口实现
bool DeviceManager::enableOSDTime(bool enable)
{
    QMutexLocker locker(&m_mutex);

    if (!m_connected || m_userID == 0) {
        qWarning() << "[OSD] 设备未连接，无法设置时间显示";
        return false;
    }

    // 获取当前OSD配置
    UNIV_DEV_OSD_PARAM osdParam;
    memset(&osdParam, 0, sizeof(osdParam));

    int ret = UNIV_DEV_GetConfig(m_userID, UNIV_CFG_OSD, &osdParam, sizeof(osdParam));
    if (ret != SUCCESS) {
        qWarning() << "[OSD] 获取OSD配置失败，错误码:" << ret;
        return false;
    }

    // 设置时间显示开关
    osdParam.enableTime = enable ? 1 : 0;

    // 应用新配置
    ret = UNIV_DEV_SetConfig(m_userID, UNIV_CFG_OSD, &osdParam, sizeof(osdParam));
    if (ret != SUCCESS) {
        qWarning() << "[OSD] 设置OSD配置失败，错误码:" << ret;
        return false;
    }

    qDebug() << "[OSD] 成功" << (enable ? "开启" : "关闭") << "时间显示";
    return true;
}

bool DeviceManager::enableOSDWeek(bool enable)
{
    QMutexLocker locker(&m_mutex);

    if (!m_connected || m_userID == 0) {
        qWarning() << "[OSD] 设备未连接，无法设置星期显示";
        return false;
    }

    // 获取当前OSD配置
    UNIV_DEV_OSD_PARAM osdParam;
    memset(&osdParam, 0, sizeof(osdParam));

    int ret = UNIV_DEV_GetConfig(m_userID, UNIV_CFG_OSD, &osdParam, sizeof(osdParam));
    if (ret != SUCCESS) {
        qWarning() << "[OSD] 获取OSD配置失败，错误码:" << ret;
        return false;
    }

    // 设置星期显示开关
    osdParam.enableWeek = enable ? 1 : 0;

    // 应用新配置
    ret = UNIV_DEV_SetConfig(m_userID, UNIV_CFG_OSD, &osdParam, sizeof(osdParam));
    if (ret != SUCCESS) {
        qWarning() << "[OSD] 设置OSD配置失败，错误码:" << ret;
        return false;
    }

    qDebug() << "[OSD] 成功" << (enable ? "开启" : "关闭") << "星期显示";
    return true;
}

bool DeviceManager::setOSDPosition(int x, int y)
{
    QMutexLocker locker(&m_mutex);

    if (!m_connected || m_userID == 0) {
        qWarning() << "[OSD] 设备未连接，无法设置位置";
        return false;
    }

    // 获取当前OSD配置
    UNIV_DEV_OSD_PARAM osdParam;
    memset(&osdParam, 0, sizeof(osdParam));

    int ret = UNIV_DEV_GetConfig(m_userID, UNIV_CFG_OSD, &osdParam, sizeof(osdParam));
    if (ret != SUCCESS) {
        qWarning() << "[OSD] 获取OSD配置失败，错误码:" << ret;
        return false;
    }

    // 设置时间显示位置
    osdParam.timePosition.X = x;
    osdParam.timePosition.Y = y;

    // 应用新配置
    ret = UNIV_DEV_SetConfig(m_userID, UNIV_CFG_OSD, &osdParam, sizeof(osdParam));
    if (ret != SUCCESS) {
        qWarning() << "[OSD] 设置OSD配置失败，错误码:" << ret;
        return false;
    }

    qDebug() << "[OSD] 成功设置时间显示位置: (" << x << "," << y << ")";
    return true;
}

bool DeviceManager::setOSDFontSize(uint8_t size)
{
    QMutexLocker locker(&m_mutex);

    if (!m_connected || m_userID == 0) {
        qWarning() << "[OSD] 设备未连接，无法设置字体大小";
        return false;
    }

    if (size > 3) {
        qWarning() << "[OSD] 字体大小无效，范围: 0-3";
        return false;
    }

    // 获取当前OSD配置
    UNIV_DEV_OSD_PARAM osdParam;
    memset(&osdParam, 0, sizeof(osdParam));

    int ret = UNIV_DEV_GetConfig(m_userID, UNIV_CFG_OSD, &osdParam, sizeof(osdParam));
    if (ret != SUCCESS) {
        qWarning() << "[OSD] 获取OSD配置失败，错误码:" << ret;
        return false;
    }

    // 设置字体大小
    osdParam.fontSize = size;

    // 应用新配置
    ret = UNIV_DEV_SetConfig(m_userID, UNIV_CFG_OSD, &osdParam, sizeof(osdParam));
    if (ret != SUCCESS) {
        qWarning() << "[OSD] 设置OSD配置失败，错误码:" << ret;
        return false;
    }

    QString sizeStr;
    switch (size) {
    case 0: sizeStr = "16x16"; break;
    case 1: sizeStr = "32x32"; break;
    case 2: sizeStr = "48x48"; break;
    case 3: sizeStr = "64x64"; break;
    }

    qDebug() << "[OSD] 成功设置字体大小:" << sizeStr;
    return true;
}


