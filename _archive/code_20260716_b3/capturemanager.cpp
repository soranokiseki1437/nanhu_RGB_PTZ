#include "capturemanager.h"
#include "devicemanager.h"
#include "imageprocessor.h"
#include "sdk/sdk.h"
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QtConcurrent>
#include <QThreadPool>

// 静态实例指针
CaptureManager *CaptureManager::instance = nullptr;
QMutex CaptureManager::s_instanceMutex;

CaptureManager::CaptureManager(QObject *parent) : QObject(parent)
    , m_deviceManager(nullptr)
    , m_imageProcessor(nullptr)
    , m_savePath("./captures")
    , m_timer(new QTimer(this))
    , m_captureInterval(1000)
    , m_quality(70)
    , m_width(0)
    , m_height(0)
    , m_processWatcher(new QFutureWatcher<ProcessResult>(this))
{
    QMutexLocker lock(&s_instanceMutex);
    instance = this;
    
    // 连接定时器信号
    connect(m_timer, &QTimer::timeout, this, &CaptureManager::onTimerCapture);
    // 跨线程排队传递数据。
    connect(this, &CaptureManager::rawSnapDataReceived, this, &CaptureManager::onRawSnapDataReceived);
    // 连接处理完成信号
    connect(m_processWatcher, &QFutureWatcher<ProcessResult>::finished, this, &CaptureManager::onImageProcessed);
}

CaptureManager::~CaptureManager()
{
    // 1. 停止所有抓图相关活动
    stopTimedCapture();
    
    // 2. 先设置 instance = nullptr，阻止新的 SDK 回调进入
    {
        QMutexLocker lock(&s_instanceMutex);
        if (instance == this) {
            instance = nullptr;
        }
    }
    
    // 3. 确保 QFutureWatcher 完成或取消正在进行的任务
    if (m_processWatcher) {
        m_processWatcher->cancel();
        m_processWatcher->waitForFinished();
    }
    
    // 4. 清理其他资源
    if (m_timer) {
        delete m_timer;
        m_timer = nullptr;
    }
}

void CaptureManager::setDeviceManager(DeviceManager *deviceManager)
{
    m_deviceManager = deviceManager;
}

void CaptureManager::setImageProcessor(ImageProcessor *imageProcessor)
{
    m_imageProcessor = imageProcessor;
    if (m_imageProcessor) {
        m_imageProcessor->setSavePath(m_savePath);
    }
}

void CaptureManager::setSavePath(const QString &savePath)
{
    m_savePath = savePath;
    if (m_imageProcessor) {
        m_imageProcessor->setSavePath(savePath);
    }
}

QString CaptureManager::getSavePath() const
{
    return m_savePath;
}

bool CaptureManager::captureOnce(uint8_t quality, uint16_t width, uint16_t height)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_deviceManager || !m_deviceManager->isConnected()) {
        emit errorOccurred("设备未连接");
        return false;
    }
    
    if (!m_imageProcessor) {
        emit errorOccurred("图像处理未初始化");
        return false;
    }
    
    // 保存当前参数
    m_quality = quality;
    m_width = width;
    m_height = height;
    
    // 执行单次抓图
    uint64_t userID = m_deviceManager->getUserID();
    int ret = UNIV_DEV_SnapOnce(userID, 0, (UNIV_RealDataCallBack)OnSnapData, quality, width, height);
    qDebug()<<"ret:"<<ret;
    if (ret != SUCCESS) {
        emit errorOccurred(QString("抓图失败: %1").arg(ret));
        return false;
    }
    
    qDebug() << "开始单次抓图";
    return true;
}

// 静态后台处理函数
ProcessResult CaptureManager::processImageInBackground(QByteArray data, QString savePath, ImageProcessor* processor)
{
    ProcessResult result;
    result.success = false;

    if (!processor) {
        result.errorMsg = "图像处理未初始化";
        return result;
    }

    // 从数据加载图片
    QImage image;
    if (!image.loadFromData(data, "JPEG")) {
        result.errorMsg = "图像数据解码失败";

        return result;
    }

    // 生成文件名
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz");
    QString fileName = QString("capture_%1.jpg").arg(timestamp);
    QString filePath = QDir(savePath).absoluteFilePath(fileName);

    // 保存图片
    if (!processor->saveImage(image, filePath)) {
        result.errorMsg = "图片保存失败";
        return result;
    }
    qDebug()<<"image:"<<image;
    result.success = true;
    result.image = image;
    result.filePath = filePath;
    return result;
}

void CaptureManager::onRawSnapDataReceived(QByteArray data, QString savePath)
{
    if (!m_imageProcessor) {
        emit errorOccurred("图像处理未初始化");
        return;
    }

    // 使用 QFutureWatcher 安全地处理异步任务
    QFuture<ProcessResult> future = QtConcurrent::run(
        processImageInBackground, data, savePath, m_imageProcessor);
    m_processWatcher->setFuture(future);
}

void CaptureManager::onImageProcessed()
{
    // 在主线程中安全地获取结果并更新 UI
    ProcessResult result = m_processWatcher->result();
    
    if (result.success) {
        emit captureFinished(result.image, result.filePath);
    } else {
        emit errorOccurred(result.errorMsg);
    }
}

bool CaptureManager::startTimedCapture(int interval, uint8_t quality, uint16_t width, uint16_t height)
{
    // 1. 使用大括号限制锁的作用域
    {
        QMutexLocker locker(&m_mutex);

        if (!m_deviceManager || !m_deviceManager->isConnected()) {
            emit errorOccurred("设备未连接");
            return false;
        }

        if (!m_imageProcessor) {
            emit errorOccurred("图像处理未初始化");
            return false;
        }

        // 保存参数
        m_captureInterval = interval;
        m_quality = quality;
        m_width = width;
        m_height = height;

        // 启动定时器
        m_timer->start(interval);
        qDebug() << "开始定时抓图，间隔:" << interval << "ms";

    } // <--- locker 在这里析构，互斥锁被释放

    // 2. 此时锁已经解开，再调用抓图就不会死锁了
    onTimerCapture();

    return true;
}

void CaptureManager::stopTimedCapture()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_timer->isActive()) {
        m_timer->stop();
        qDebug() << "停止定时抓图";
    }
}

void CaptureManager::onTimerCapture()
{
    captureOnce(m_quality, m_width, m_height);
}

// C-CDK抓图回调函数，数据搬运工，不直接在这里处理业务
void CaptureManager::OnSnapData(uint64_t /*snapHandle*/, uint8_t /*dataType*/, UNIV_DEV_SNAP_DATA* pData, uint32_t /*dataSize*/)
{
    // 先安全检查并临时获取 instance
    CaptureManager* safeInstance = nullptr;
    {
        QMutexLocker lock(&s_instanceMutex);
        safeInstance = instance;
    }
    
    if (!safeInstance || !pData || pData->dataSize == 0) return;

    // 将 C 指针里的裸数据立刻拷贝为受 Qt 管理的 QByteArray，脱离 SDK 的内存周期
    QByteArray safeData(reinterpret_cast<const char*>(pData->data), pData->dataSize);
    
    // 捕获保存路径（此时 safeInstance 仍然有效）
    QString savePath = safeInstance->m_savePath;

    // 发射信号，把数据抛给主线程
    emit safeInstance->rawSnapDataReceived(safeData, savePath);
}
