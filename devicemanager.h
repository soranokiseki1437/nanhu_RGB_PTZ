#ifndef DEVICEMANAGER_H
#define DEVICEMANAGER_H

#include <QObject>
#include <QString>
#include <QMutex>
#include <QFutureWatcher>
#include <atomic>
#include "sdk/sdk.h"
#include "DecodeThread.h"
#include "RecordThread.h"
#include "lensmanager.h"
#include "datarecorder.h"

// 登录结果结构体
struct LoginResult {
    bool success;
    uint64_t userID;
    QString errorMsg;
};
Q_DECLARE_METATYPE(LoginResult)

class DeviceManager : public QObject
{
    Q_OBJECT

public:
    explicit DeviceManager(QObject *parent = nullptr);
    ~DeviceManager();

public slots:
    void login(const QString &ip, const QString &username, const QString &password);
    bool logout();
    bool startStream();
    bool stopStream();
    // C7：异步开启预览（UNIV_DEV_RealPlay 阻塞调用在 QtConcurrent 线程执行，完成后发 streamStarted）
    void requestStartStream();
    bool startRecord(const QString& filePath);
    void stopRecord();
    
    // SDK OSD 相关接口
    bool enableOSDTime(bool enable);  // 启用/禁用时间显示
    bool enableOSDWeek(bool enable);  // 启用/禁用星期显示
    bool setOSDPosition(int x, int y); // 设置时间显示位置
    bool setOSDFontSize(uint8_t size); // 设置字体大小 (0-16*16, 1-32*32, 2-48*48, 3-64*64)

public:
    bool isConnected() const;
    uint64_t getUserID() const;
    bool isStreaming() const;
    bool isRecording() const;
    static DeviceManager *instance;
    
    // 获取镜头管理器
    LensManager* getLensManager();
    
    // 获取数据记录器
    DataRecorder* getDataRecorder();

signals:
    void connectionStatusChanged(bool connected);
    void errorOccurred(const QString &error);
    void frameReceived(const QImage &frame);
    void streamStarted(bool ok);   // C7：requestStartStream 完成通知
    void recordStarted();
    void recordStopped(const QString &filePath);
    
    // PTZ数据信号（转发）
    void ptzAngleReceived(float pan, float tilt);

private:
    bool m_sdkInitialized;
    bool m_connected;
    uint64_t m_userID;
    QString m_ip;
    QString m_username;
    QString m_password;
    mutable QMutex m_mutex;
    uint64_t m_playHandle;
    bool m_streaming;
    DecodeThread* m_decodeThread;
    RecordThread* m_recordThread;
    std::atomic<bool> m_isRecording{false};  // C4：SDK 流回调线程会读，须原子
    QFutureWatcher<LoginResult>* m_loginWatcher;
    LensManager* m_lensManager;
    DataRecorder* m_dataRecorder;  // 数据记录器
    
    // 静态回调保护标志
    static QMutex s_instanceMutex;
    static std::atomic<int> s_activeCallbacks;

    bool initSDK();
    void cleanupSDK();
    QString analyzeLoginError(int errorCode);

public slots:
    void onPTZAngleReceived(float pan, float tilt, float panSpeed, float tiltSpeed);  // PTZ角度接收槽

private slots:
    void onFrameDecoded(const QImage &frame);
    void onLoginFinished();
    void onRecordError(const QString &error);
    void onRecordFinished(const QString &filePath);
    void onDataRecordStarted(const QString &filePath);
    void onDataRecordStopped(const QString &filePath);

private:
    static void UNIV_CALLBACK OnException(uint32_t event, uint64_t userID);
    static void UNIV_CALLBACK OnStreamData(uint64_t handle, uint8_t dataType, void* pData, uint32_t dataSize);
    void disableCameraOSDTime();
    void printVideoConfig(uint8_t streamType);
    static LoginResult loginInBackground(QString ip, QString username, QString password);
};

#endif // DEVICEMANAGER_H
