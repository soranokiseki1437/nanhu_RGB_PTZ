#include "lensmanager.h"
#include <QDebug>

// ============================================
// LensWorker 工作者类实现
// ============================================

LensWorker::LensWorker(QObject *parent)
    : QObject(parent)
    , m_userID(0)
{
}

LensWorker::~LensWorker()
{
}

void LensWorker::setUserID(uint64_t userID)
{
    m_userID = userID;
}

void LensWorker::executeCommand(UNIV_PTZ_COMMAND command, uint8_t speed, uint8_t stop)
{
    qDebug() << "[LensWorker] 执行命令:" 
             << "Command:" << command 
             << "Speed:" << static_cast<int>(speed) 
             << "Stop:" << static_cast<int>(stop);
    
    if (m_userID != 0) {
        int32_t result = UNIV_DEV_PTZControl(m_userID, command, 0, speed, stop);
        if (result != SUCCESS) {
            qWarning() << "[LensWorker] SDK PTZControl 失败!" 
                       << " Command:" << command
                       << " Speed:" << static_cast<int>(speed)
                       << " Stop:" << static_cast<int>(stop)
                       << " ErrorCode:" << result;
        } else {
            qDebug() << "[LensWorker] SDK PTZControl 成功";
        }
    } else {
        qWarning() << "[LensWorker] 无法执行命令 - userID 为 0";
    }
}

void LensWorker::executeSetFocusMode(bool isManual, uint64_t userID)
{
    qDebug() << "[LensWorker] executeSetFocusMode 开始:" << (isManual ? "手动" : "自动");
    
    if (userID == 0) {
        qWarning() << "[LensWorker] userID 为 0，无法设置聚焦模式";
        emit focusModeSetResult(false, isManual);
        return;
    }
    
    // 准备 SDK 调用参数
    UNIV_DEV_CAMERA_PARAM cameraParam;
    memset(&cameraParam, 0, sizeof(cameraParam));
    cameraParam.scene = -1; // 获取当前生效场景（仅用于读取）
    
    // 先读取完整配置
    int32_t getResult = UNIV_DEV_GetConfig(userID, UNIV_CFG_CAMERA, &cameraParam, sizeof(cameraParam));
    if (getResult != SUCCESS) {
        qWarning() << "[LensWorker] 获取相机配置失败，错误码:" << getResult;
        emit focusModeSetResult(false, isManual);
        return;
    }
    
    // 修改聚焦模式
    uint8_t newMode = isManual ? 1 : 0; // 1 = ManualFocus, 0 = AutoFocus
    cameraParam.fullParam.extra.focus.mode = newMode;
    
    // 【关键修复】重置 scene 为合法的默认值，不能写回 -1！
    cameraParam.scene = 0;
    
    // 写回设备
    int32_t setResult = UNIV_DEV_SetConfig(userID, UNIV_CFG_CAMERA, &cameraParam, sizeof(cameraParam));
    if (setResult != SUCCESS) {
        qWarning() << "[LensWorker] 设置聚焦模式失败，错误码:" << setResult;
        emit focusModeSetResult(false, isManual);
        return;
    }
    
    qDebug() << "[LensWorker] 聚焦模式设置成功:" << (isManual ? "手动" : "自动");
    emit focusModeSetResult(true, isManual);
}

void LensWorker::executeSetIrisMode(bool isManual, uint64_t userID)
{
    qDebug() << "[LensWorker] executeSetIrisMode 开始:" << (isManual ? "手动/光圈优先" : "自动");
    
    if (userID == 0) {
        qWarning() << "[LensWorker] userID 为 0，无法设置光圈模式";
        emit irisModeSetResult(false, isManual);
        return;
    }
    
    // 准备 SDK 调用参数
    UNIV_DEV_CAMERA_PARAM cameraParam;
    memset(&cameraParam, 0, sizeof(cameraParam));
    cameraParam.scene = -1; // 获取当前生效场景
    
    // 先读取完整配置
    int32_t getResult = UNIV_DEV_GetConfig(userID, UNIV_CFG_CAMERA, &cameraParam, sizeof(cameraParam));
    if (getResult != SUCCESS) {
        qWarning() << "[LensWorker] 获取相机配置失败，错误码:" << getResult;
        emit irisModeSetResult(false, isManual);
        return;
    }
    
    // 修改曝光模式
    // mode: 0=自动, 1=手动, 2=光圈优先, 3=快门优先
    // 如果是手动模式，我们用 2=光圈优先（用户调光圈，相机自动调快门和增益）
    // 如果是自动模式，我们用 0=自动
    cameraParam.fullParam.basic.exposure.mode = isManual ? 2 : 0;
    
    // 重置 scene 为合法值
    cameraParam.scene = 0;
    
    // 写回设备
    int32_t setResult = UNIV_DEV_SetConfig(userID, UNIV_CFG_CAMERA, &cameraParam, sizeof(cameraParam));
    if (setResult != SUCCESS) {
        qWarning() << "[LensWorker] 设置光圈模式失败，错误码:" << setResult;
        emit irisModeSetResult(false, isManual);
        return;
    }
    
    qDebug() << "[LensWorker] 光圈模式设置成功:" << (isManual ? "手动/光圈优先" : "自动");
    emit irisModeSetResult(true, isManual);
}

void LensWorker::executeGetFocusMode(uint64_t userID)
{
    qDebug() << "[LensWorker] executeGetFocusMode 开始";
    
    if (userID == 0) {
        qWarning() << "[LensWorker] userID 为 0，无法读取聚焦模式";
        emit focusModeGetResult(false, false);
        return;
    }
    
    UNIV_DEV_CAMERA_PARAM cameraParam;
    memset(&cameraParam, 0, sizeof(cameraParam));
    cameraParam.scene = -1; // 获取当前生效场景
    
    int32_t result = UNIV_DEV_GetConfig(userID, UNIV_CFG_CAMERA, &cameraParam, sizeof(cameraParam));
    if (result != SUCCESS) {
        qWarning() << "[LensWorker] 从设备读取聚焦模式失败，错误码:" << result;
        emit focusModeGetResult(false, false);
        return;
    }
    
    uint8_t mode = cameraParam.fullParam.extra.focus.mode;
    bool isManual = (mode == 1); // 1 = ManualFocus
    
    qDebug() << "[LensWorker] 从设备读取聚焦模式成功:" 
             << (mode == 0 ? "自动" : (mode == 1 ? "手动" : "半自动"));
    
    emit focusModeGetResult(true, isManual);
}

void LensWorker::executeGetIrisMode(uint64_t userID)
{
    qDebug() << "[LensWorker] executeGetIrisMode 开始";
    
    if (userID == 0) {
        qWarning() << "[LensWorker] userID 为 0，无法读取光圈模式";
        emit irisModeGetResult(false, false);
        return;
    }
    
    UNIV_DEV_CAMERA_PARAM cameraParam;
    memset(&cameraParam, 0, sizeof(cameraParam));
    cameraParam.scene = -1; // 获取当前生效场景
    
    int32_t result = UNIV_DEV_GetConfig(userID, UNIV_CFG_CAMERA, &cameraParam, sizeof(cameraParam));
    if (result != SUCCESS) {
        qWarning() << "[LensWorker] 从设备读取光圈模式失败，错误码:" << result;
        emit irisModeGetResult(false, false);
        return;
    }
    
    uint8_t exposureMode = cameraParam.fullParam.basic.exposure.mode;
    bool isManual = (exposureMode == 2); // 现在只认光圈优先模式
    
    qDebug() << "[LensWorker] 从设备读取光圈模式成功:" 
             << (isManual ? "手动/光圈优先" : "自动") 
             << "(原始mode:" << static_cast<int>(exposureMode) << ")";
    
    emit irisModeGetResult(true, isManual);
}

// ============================================
// LensManager 管理类实现
// ============================================

LensManager::LensManager(QObject *parent)
    : QObject(parent)
    , m_userID(0)
    , m_ready(false)
    , m_workerThread(new QThread(this))
    , m_worker(new LensWorker())
    , m_focusMode(AutoFocus)
    , m_isManualIris(false)
{
    // 启动工作线程
    m_worker->moveToThread(m_workerThread);
    m_workerThread->start();

    // 连接信号槽 - 使用 QueuedConnection 确保线程安全
    connect(this, &LensManager::userIDChanged,
            m_worker, &LensWorker::setUserID,
            Qt::QueuedConnection);

    connect(this, &LensManager::commandQueued,
            m_worker, &LensWorker::executeCommand,
            Qt::QueuedConnection);
            
    connect(this, &LensManager::setFocusModeRequested,
            m_worker, &LensWorker::executeSetFocusMode,
            Qt::QueuedConnection);
            
    connect(this, &LensManager::setIrisModeRequested,
            m_worker, &LensWorker::executeSetIrisMode,
            Qt::QueuedConnection);
            
    connect(this, &LensManager::getFocusModeRequested,
            m_worker, &LensWorker::executeGetFocusMode,
            Qt::QueuedConnection);
            
    connect(this, &LensManager::getIrisModeRequested,
            m_worker, &LensWorker::executeGetIrisMode,
            Qt::QueuedConnection);
            
    connect(m_worker, &LensWorker::focusModeSetResult,
            this, &LensManager::onFocusModeSetResult,
            Qt::QueuedConnection);
            
    connect(m_worker, &LensWorker::irisModeSetResult,
            this, &LensManager::onIrisModeSetResult,
            Qt::QueuedConnection);
            
    connect(m_worker, &LensWorker::focusModeGetResult,
            this, &LensManager::onFocusModeGetResult,
            Qt::QueuedConnection);
            
    connect(m_worker, &LensWorker::irisModeGetResult,
            this, &LensManager::onIrisModeGetResult,
            Qt::QueuedConnection);

    // P6：模式切换等待超时定时器（onePushFocus 用）
    m_modeSwitchTimer = new QTimer(this);
    m_modeSwitchTimer->setSingleShot(true);
    connect(m_modeSwitchTimer, &QTimer::timeout,
            this, &LensManager::onModeSwitchTimeout);
}

LensManager::~LensManager()
{
    // 停止工作线程
    if (m_workerThread && m_workerThread->isRunning()) {
        m_workerThread->quit();
        m_workerThread->wait();
    }
    
    // 清理资源
    delete m_worker;
    m_worker = nullptr;
}

void LensManager::setUserID(uint64_t userID)
{
    QMutexLocker locker(&m_mutex);
    m_userID = userID;
    m_ready = (userID != 0);
    
    // 同步设置工作者的 userID（通过信号槽跨线程传递）
    if (m_worker) {
        emit userIDChanged(userID);
    }
    
    // 如果登录成功，从设备读取当前聚焦模式和光圈模式
    if (m_ready) {
        uint64_t userIDCopy = userID;
        locker.unlock();
        emit getFocusModeRequested(userIDCopy);
        emit getIrisModeRequested(userIDCopy);
    }
}

bool LensManager::isReady() const
{
    QMutexLocker locker(&m_mutex);
    return m_ready;
}

LensManager::FocusMode LensManager::getFocusMode() const
{
    QMutexLocker locker(&m_mutex);
    return m_focusMode;
}

void LensManager::setFocusMode(bool isManual)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_ready) {
        emit errorOccurred("镜头管理未就绪，无法设置聚焦模式");
        return;
    }
    
    FocusMode newMode = isManual ? ManualFocus : AutoFocus;
    
    // 如果和当前模式一样，直接返回
    if (newMode == m_focusMode) {
        return;
    }
    
    uint64_t userID = m_userID;
    locker.unlock();
    
    qDebug() << "[LensManager] 发送异步请求设置聚焦模式:" << (isManual ? "手动" : "自动");
    emit setFocusModeRequested(isManual, userID);
}

void LensManager::setIrisMode(bool isManual)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_ready) {
        emit errorOccurred("镜头管理未就绪，无法设置光圈模式");
        return;
    }
    
    uint64_t userID = m_userID;
    locker.unlock();
    
    qDebug() << "[LensManager] 发送异步请求设置光圈模式:" << (isManual ? "手动/光圈优先" : "自动");
    emit setIrisModeRequested(isManual, userID);
}

void LensManager::onFocusModeSetResult(bool success, bool isManual)
{
    if (success) {
        QMutexLocker locker(&m_mutex);
        m_focusMode = isManual ? ManualFocus : AutoFocus;
        locker.unlock();
        emit focusModeChanged(isManual);
        qDebug() << "[LensManager] 聚焦模式更新成功:" << (isManual ? "手动" : "自动");

        // P6：onePushFocus 等待模式切换成功后再下发一键聚焦命令
        if (m_waitingModeSwitchFor == static_cast<int>(FOCUS_ONEPUSH)) {
            m_waitingModeSwitchFor = -1;
            m_modeSwitchTimer->stop();
            emit commandQueued(FOCUS_ONEPUSH, 50, 0);
            qDebug() << "[LensManager] 模式切换完成，下发一键聚焦";
        }
    } else {
        // P6：切换失败同样终止挂起状态（错误已提示，无需等超时）
        if (m_waitingModeSwitchFor == static_cast<int>(FOCUS_ONEPUSH)) {
            m_waitingModeSwitchFor = -1;
            m_modeSwitchTimer->stop();
        }
        emit errorOccurred("设置聚焦模式失败");
        qWarning() << "[LensManager] 聚焦模式设置失败";
    }
}

void LensManager::onModeSwitchTimeout()
{
    // P6：等待模式切换结果超时，放弃挂起的一键聚焦命令
    if (m_waitingModeSwitchFor == static_cast<int>(FOCUS_ONEPUSH)) {
        m_waitingModeSwitchFor = -1;
        emit errorOccurred("模式切换超时，one-push 未执行");
        qWarning() << "[LensManager] onePushFocus 模式切换等待超时";
    }
}

void LensManager::onIrisModeSetResult(bool success, bool isManual)
{
    if (success) {
        QMutexLocker locker(&m_mutex);
        m_isManualIris = isManual;
        locker.unlock();
        emit irisModeChanged(isManual);
        qDebug() << "[LensManager] 光圈模式更新成功:" << (isManual ? "手动/光圈优先" : "自动");
    } else {
        emit errorOccurred("设置光圈模式失败");
        qWarning() << "[LensManager] 光圈模式设置失败";
    }
}

void LensManager::onFocusModeGetResult(bool success, bool isManual)
{
    if (success) {
        QMutexLocker locker(&m_mutex);
        m_focusMode = isManual ? ManualFocus : AutoFocus;
        locker.unlock();
        emit focusModeChanged(isManual);
    } else {
        emit errorOccurred("读取聚焦模式失败");
    }
}

void LensManager::onIrisModeGetResult(bool success, bool isManual)
{
    if (success) {
        QMutexLocker locker(&m_mutex);
        m_isManualIris = isManual;
        locker.unlock();
        emit irisModeChanged(isManual);
    } else {
        emit errorOccurred("读取光圈模式失败");
    }
}

void LensManager::zoomIn(uint8_t speed, bool stop)
{
    // 确保 speed 在有效范围 [1,100] 内
    if (speed < 1) speed = 1;
    if (speed > 100) speed = 100;
    
    emit commandQueued(ZOOM_IN, speed, stop ? 1 : 0);
}

void LensManager::zoomOut(uint8_t speed, bool stop)
{
    if (speed < 1) speed = 1;
    if (speed > 100) speed = 100;
    
    emit commandQueued(ZOOM_OUT, speed, stop ? 1 : 0);
}

void LensManager::focusNear(uint8_t speed, bool stop)
{
    if (speed < 1) speed = 1;
    if (speed > 100) speed = 100;
    
    emit commandQueued(FOCUS_NEAR, speed, stop ? 1 : 0);
}

void LensManager::focusFar(uint8_t speed, bool stop)
{
    if (speed < 1) speed = 1;
    if (speed > 100) speed = 100;
    
    emit commandQueued(FOCUS_FAR, speed, stop ? 1 : 0);
}

void LensManager::irisOpen(uint8_t speed, bool stop)
{
    if (speed < 1) speed = 1;
    if (speed > 100) speed = 100;
    
    emit commandQueued(IRIS_OPEN, speed, stop ? 1 : 0);
}

void LensManager::irisClose(uint8_t speed, bool stop)
{
    if (speed < 1) speed = 1;
    if (speed > 100) speed = 100;
    
    emit commandQueued(IRIS_CLOSE, speed, stop ? 1 : 0);
}

void LensManager::onePushFocus()
{
    QMutexLocker locker(&m_mutex);
    if (!m_ready) {
        emit errorOccurred("镜头管理未就绪");
        return;
    }
    
    // 检查当前模式，如果是手动模式，先自动切换到自动模式
    bool needsModeSwitch = (m_focusMode == ManualFocus);
    
    locker.unlock();
    
    if (needsModeSwitch) {
        // P6：先切自动模式，等 onFocusModeSetResult 成功后再下发一键聚焦，
        // 避免模式切换与聚焦命令在设备端时序竞争
        qDebug() << "[LensManager] 一键聚焦前自动切换到自动模式（等待切换成功）";
        m_waitingModeSwitchFor = static_cast<int>(FOCUS_ONEPUSH);
        m_modeSwitchTimer->start(1000);
        setFocusMode(false);
        return;
    }

    // 执行一键聚焦使用默认速度 50，stop 为 0
    emit commandQueued(FOCUS_ONEPUSH, 50, 0);
}
