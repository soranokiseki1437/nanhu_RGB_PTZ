#ifndef LENSMANAGER_H
#define LENSMANAGER_H

#include <QObject>
#include <QString>
#include <QMutex>
#include <QThread>
#include <QTimer>
#include "sdk/sdk.h"

// 工作者类 - 在独立线程中执行 SDK 调用
class LensWorker : public QObject
{
    Q_OBJECT

public:
    explicit LensWorker(QObject *parent = nullptr);
    ~LensWorker() override;

    void setUserID(uint64_t userID);

public slots:
    void executeCommand(UNIV_PTZ_COMMAND command, uint8_t speed, uint8_t stop);
    void executeSetFocusMode(bool isManual, uint64_t userID);
    void executeSetIrisMode(bool isManual, uint64_t userID);
    void executeGetFocusMode(uint64_t userID);
    void executeGetIrisMode(uint64_t userID);

signals:
    void focusModeSetResult(bool success, bool isManual);
    void irisModeSetResult(bool success, bool isManual);
    void focusModeGetResult(bool success, bool isManual);
    void irisModeGetResult(bool success, bool isManual);

private:
    uint64_t m_userID;
};

class LensManager : public QObject
{
    Q_OBJECT

public:
    // 聚焦模式枚举
    enum FocusMode {
        AutoFocus = 0,    // 自动对焦
        ManualFocus = 1,  // 手动对焦
        SemiAutoFocus = 2 // 半自动对焦
    };
    Q_ENUM(FocusMode)

    explicit LensManager(QObject *parent = nullptr);
    ~LensManager() override;

    // 设置用户ID（在DeviceManager登录成功后调用）
    void setUserID(uint64_t userID);
    bool isReady() const;

    // 聚焦模式相关接口
    FocusMode getFocusMode() const;
    void setFocusMode(bool isManual); // true=手动, false=自动
    
    // 光圈模式相关接口
    void setIrisMode(bool isManual); // true=手动/光圈优先, false=自动

public slots:
    // 镜头控制接口
    void zoomIn(uint8_t speed, bool stop = false);
    void zoomOut(uint8_t speed, bool stop = false);
    void focusNear(uint8_t speed, bool stop = false);
    void focusFar(uint8_t speed, bool stop = false);
    void irisOpen(uint8_t speed, bool stop = false);
    void irisClose(uint8_t speed, bool stop = false);
    void onePushFocus();

private slots:
    void onFocusModeSetResult(bool success, bool isManual);
    void onIrisModeSetResult(bool success, bool isManual);
    void onFocusModeGetResult(bool success, bool isManual);
    void onIrisModeGetResult(bool success, bool isManual);
    void onModeSwitchTimeout(); // P6：模式切换等待超时

signals:
    void errorOccurred(const QString &error);
    void focusModeChanged(bool isManualFocus); // 聚焦模式变化信号
    void irisModeChanged(bool isManualIris); // 光圈模式变化信号
    void userIDChanged(uint64_t userID);
    // 内部信号 - 用于跨线程传递命令
    void commandQueued(UNIV_PTZ_COMMAND command, uint8_t speed, uint8_t stop);
    void setFocusModeRequested(bool isManual, uint64_t userID);
    void setIrisModeRequested(bool isManual, uint64_t userID);
    void getFocusModeRequested(uint64_t userID);
    void getIrisModeRequested(uint64_t userID);

private:
    uint64_t m_userID;
    bool m_ready;
    mutable QMutex m_mutex;

    QThread *m_workerThread;
    LensWorker *m_worker;

    FocusMode m_focusMode; // 本地缓存的聚焦模式
    bool m_isManualIris; // 本地缓存的光圈模式

    // P6：onePushFocus 需要先切自动模式时，挂起待执行的命令（-1 表示无挂起）
    int m_waitingModeSwitchFor = -1;
    QTimer *m_modeSwitchTimer = nullptr; // 1 秒超时保护
};

#endif // LENSMANAGER_H
