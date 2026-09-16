#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QThread>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QTimer>
#include "ptzcontroller.h"
#include "devicemanager.h"
#include "capturemanager.h"
#include "imageprocessor.h"
#include "configmanager.h"
#include "trackingcontroller.h"
#include "objecttracker.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // 报错
    void onErrorOccurred(const QString &error);

    // PTZ 控制相关槽
    void on_btnRefresh_clicked();
    void on_btnConnect_clicked();
    void on_btnMove_clicked();
    void on_btnStop_clicked();
    void on_btnQuery_clicked();
    void on_btnReset_clicked();
    void handleAngleReceived(float pan, float tilt);
    void onConnectionTimeout();
    
    // RGB 图像捕获相关槽
    void on_rgbBtnConnect_clicked();
    void on_rgbBtnCaptureOnce_clicked();
    void on_rgbCheckBoxTimedCapture_stateChanged(int arg1);
    void on_rgbBtnSelectSavePath_clicked();
    void on_rgbBtnSelectVideoPath_clicked();
    void on_rgbBtnRecord_clicked();
    void onConnectionStatusChanged(bool connected);
    void onCaptureFinished(const QImage &image, const QString &filePath);
    void onFrameReceived(const QImage &frame);
    void onRecordTimerTimeout();
    void onRecordStarted();
    void onRecordStopped(const QString &filePath);
    
    // 聚焦模式相关槽
    void onFocusModeChanged(bool isManualFocus);
    void on_checkBoxManualFocus_clicked(bool checked);
    // 光圈模式相关槽
    void onIrisModeChanged(bool isManualIris);
    void on_checkBoxManualIris_clicked(bool checked);

    // 目标跟踪相关槽
    void on_btnTrackSelect_clicked();
    void on_btnTrackStart_clicked();
    void on_btnTrackStop_clicked();
    void onTrackStateChanged(TrackingController::TrackingState state);
    void onTrackResult(const TrackResult& result);
    void onTrackBoxDraw(const QRectF& bbox, bool occluded);
    void onTrackStatusMessage(const QString& msg);
    void onPtzControlDelta(int deltaX, int deltaY, int speedX, int speedY);

signals:
    // 跨线程调用PTZ控制器的信号
    void ptzMoveTo(float pan, float tilt, float speed);
    void ptzStop();
    void ptzQueryAngle();
    void ptzMoveDirection(uint8_t cmd2, uint8_t hSpeed, uint8_t vSpeed);

private:
    // 初始化 UI
    void initUI();
    // 初始化 PTZ 控制模块
    void initPTZModule();
    // 初始化 RGB 图像捕获模块
    void initRGBModule();
    // 初始化目标跟踪模块
    void initTrackingModule();
    // 更新 PTZ UI 状态
    void updatePTZUIState();
    // 更新 RGB UI 状态
    void updateRGBUIState();
    // 更新跟踪 UI 状态
    void updateTrackingUIState();
    // 显示 RGB 图像
    void displayImage(const QImage &image);
    // 添加到 RGB 历史记录
    void addToHistory(const QImage &image, const QString &filePath);
    // 格式化时间显示
    QString formatTime(int seconds);

private:
    Ui::MainWindow *ui;
    
    // PTZ 相关
    PTZController *m_ptzController;
    QThread *m_ptzThread; // PTZ 线程
    QTimer *m_connTimer;
    bool m_isConnecting;
    bool m_ptzConnected;
    
    // RGB 相关
    DeviceManager *m_deviceManager;
    CaptureManager *m_captureManager;
    ImageProcessor *m_imageProcessor;
    ConfigManager *m_configManager;
    bool m_rgbConnected;
    bool m_timedCaptureEnabled;
    
    // 录像相关
    QTimer *m_recordTimer;
    int m_recordSeconds;

    // 目标跟踪相关
    TrackingController *m_trackingController;
    bool m_isSelectingTarget;       // 是否处于框选模式
    QPoint m_selectStart;           // 框选起点
    QRectF m_selectBox;             // 用户框选区域
    QRectF m_trackBox;              // 当前跟踪框
    bool m_trackBoxVisible;         // 是否显示跟踪框
    bool m_trackBoxOccluded;        // 跟踪框是否遮挡状态
    QImage m_lastFrame;             // 最后一帧（用于框选时初始化）

protected:
    // 鼠标事件（用于框选目标）
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
};

#endif // MAINWINDOW_H
