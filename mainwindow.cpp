#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QSerialPortInfo>
#include <QMessageBox>
#include <QDateTime>
#include <QDebug>
#include <QTimer>
#include <QFileDialog>
#include <QDir>
#include <QMouseEvent>
#include <QPainter>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_ptzController(nullptr)
    , m_ptzThread(nullptr)
    , m_connTimer(new QTimer(this))
    , m_isConnecting(false)
    , m_ptzConnected(false)
    , m_deviceManager(new DeviceManager(this))
    , m_captureManager(new CaptureManager(this))
    , m_imageProcessor(new ImageProcessor(this))
    , m_configManager(new ConfigManager(this))
    , m_rgbConnected(false)
    , m_timedCaptureEnabled(false)
    , m_recordTimer(new QTimer(this))
    , m_recordSeconds(0)
    , m_trackingController(new TrackingController(this))
    , m_isSelectingTarget(false)
    , m_trackBoxVisible(false)
    , m_trackBoxOccluded(false)
{
    ui->setupUi(this);
    initUI();
    
    // 设置管理器之间的关联
    m_captureManager->setDeviceManager(m_deviceManager);
    m_captureManager->setImageProcessor(m_imageProcessor);
    
    // 连接信号槽
    connect(m_deviceManager, &DeviceManager::connectionStatusChanged, this, &MainWindow::onConnectionStatusChanged);
    connect(m_deviceManager, &DeviceManager::errorOccurred, this, &MainWindow::onErrorOccurred);
    connect(m_deviceManager, &DeviceManager::frameReceived, this, &MainWindow::onFrameReceived);
    connect(m_deviceManager, &DeviceManager::streamStarted, this, [this](bool ok){
        if (!ok) ui->statusbar->showMessage("开启视频预览失败", 3000);
    });
    connect(m_deviceManager, &DeviceManager::recordStarted, this, &MainWindow::onRecordStarted);
    connect(m_deviceManager, &DeviceManager::recordStopped, this, &MainWindow::onRecordStopped);
    connect(m_captureManager, &CaptureManager::captureFinished, this, &MainWindow::onCaptureFinished);
    connect(m_captureManager, &CaptureManager::errorOccurred, this, &MainWindow::onErrorOccurred);
    connect(m_imageProcessor, &ImageProcessor::errorOccurred, this, &MainWindow::onErrorOccurred);

    // P8：退出时保存配置（ConfigManager 析构不再写盘，QApplication 收尾阶段 QSettings 不可靠）
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this](){
        m_configManager->saveConfig();
    });

    // 连接目标跟踪信号槽
    connect(m_trackingController, &TrackingController::stateChanged, this, &MainWindow::onTrackStateChanged);
    connect(m_trackingController, &TrackingController::trackingResult, this, &MainWindow::onTrackResult);
    connect(m_trackingController, &TrackingController::drawTrackingBox, this, &MainWindow::onTrackBoxDraw);
    connect(m_trackingController, &TrackingController::statusMessage, this, &MainWindow::onTrackStatusMessage);
    connect(m_trackingController, &TrackingController::ptzControlDelta, this, &MainWindow::onPtzControlDelta);
    
    // PTZ 连接超时定时器
    m_connTimer->setSingleShot(true);
    connect(m_connTimer, &QTimer::timeout, this, &MainWindow::onConnectionTimeout);
    
    // 录像定时器
    m_recordTimer->setInterval(1000);
    connect(m_recordTimer, &QTimer::timeout, this, &MainWindow::onRecordTimerTimeout);
    
    // 初始化 PTZ 方向盘控制
    ui->left_up->setAutoRepeat(false);
    ui->up->setAutoRepeat(false);
    ui->right_up->setAutoRepeat(false);
    ui->left->setAutoRepeat(false);
    ui->right->setAutoRepeat(false);
    ui->left_down->setAutoRepeat(false);
    ui->down->setAutoRepeat(false);
    ui->right_down->setAutoRepeat(false);
    
    // 上
    connect(ui->up, &QPushButton::pressed, this, [this]() {
        emit ptzMoveDirection(0x08, 0, static_cast<uint8_t>(ui->spinSpeed->value() * 63));
    });
    connect(ui->up, &QPushButton::released, this, [this]() { emit ptzStop(); });
    
    // 下
    connect(ui->down, &QPushButton::pressed, this, [this]() {
        emit ptzMoveDirection(0x10, 0, static_cast<uint8_t>(ui->spinSpeed->value() * 63));
    });
    connect(ui->down, &QPushButton::released, this, [this]() { emit ptzStop(); });
    
    // 左
    connect(ui->left, &QPushButton::pressed, this, [this]() {
        emit ptzMoveDirection(0x04, static_cast<uint8_t>(ui->spinSpeed->value() * 63), 0);
    });
    connect(ui->left, &QPushButton::released, this, [this]() { emit ptzStop(); });
    
    // 右
    connect(ui->right, &QPushButton::pressed, this, [this]() {
        emit ptzMoveDirection(0x02, static_cast<uint8_t>(ui->spinSpeed->value() * 63), 0);
    });
    connect(ui->right, &QPushButton::released, this, [this]() { emit ptzStop(); });
    
    // 左上
    connect(ui->left_up, &QPushButton::pressed, this, [this]() {
        uint8_t spd = static_cast<uint8_t>(ui->spinSpeed->value() * 63);
        emit ptzMoveDirection(0x0C, spd, spd);
    });
    connect(ui->left_up, &QPushButton::released, this, [this]() { emit ptzStop(); });
    
    // 右上
    connect(ui->right_up, &QPushButton::pressed, this, [this]() {
        uint8_t spd = static_cast<uint8_t>(ui->spinSpeed->value() * 63);
        emit ptzMoveDirection(0x0A, spd, spd);
    });
    connect(ui->right_up, &QPushButton::released, this, [this]() { emit ptzStop(); });
    
    // 左下
    connect(ui->left_down, &QPushButton::pressed, this, [this]() {
        uint8_t spd = static_cast<uint8_t>(ui->spinSpeed->value() * 63);
        emit ptzMoveDirection(0x14, spd, spd);
    });
    connect(ui->left_down, &QPushButton::released, this, [this]() { emit ptzStop(); });
    
    // 右下
    connect(ui->right_down, &QPushButton::pressed, this, [this]() {
        uint8_t spd = static_cast<uint8_t>(ui->spinSpeed->value() * 63);
        emit ptzMoveDirection(0x12, spd, spd);
    });
    connect(ui->right_down, &QPushButton::released, this, [this]() { emit ptzStop(); });
    
    // === 镜头控制按钮绑定 ===
    // 【关键】镜头控制按钮禁用 autoRepeat！
    // 因为只需要一次 pressed(start) + 一次 released(stop)，不需要重复发送！
    if (ui->btnZoomIn) ui->btnZoomIn->setAutoRepeat(false);
    if (ui->btnZoomOut) ui->btnZoomOut->setAutoRepeat(false);
    if (ui->btnFocusNear) ui->btnFocusNear->setAutoRepeat(false);
    if (ui->btnFocusFar) ui->btnFocusFar->setAutoRepeat(false);
    if (ui->btnIrisOpen) ui->btnIrisOpen->setAutoRepeat(false);
    if (ui->btnIrisClose) ui->btnIrisClose->setAutoRepeat(false);
    
    // 变倍+
    if (ui->btnZoomIn) {
        connect(ui->btnZoomIn, &QPushButton::pressed, this, [this]() {
            uint8_t speed = static_cast<uint8_t>(ui->spinLensSpeed->value());
            m_deviceManager->getLensManager()->zoomIn(speed, false);
        });
        connect(ui->btnZoomIn, &QPushButton::released, this, [this]() {
            uint8_t speed = static_cast<uint8_t>(ui->spinLensSpeed->value());
            m_deviceManager->getLensManager()->zoomIn(speed, true);
        });
    }
    
    // 变倍-
    if (ui->btnZoomOut) {
        connect(ui->btnZoomOut, &QPushButton::pressed, this, [this]() {
            uint8_t speed = static_cast<uint8_t>(ui->spinLensSpeed->value());
            m_deviceManager->getLensManager()->zoomOut(speed, false);
        });
        connect(ui->btnZoomOut, &QPushButton::released, this, [this]() {
            uint8_t speed = static_cast<uint8_t>(ui->spinLensSpeed->value());
            m_deviceManager->getLensManager()->zoomOut(speed, true);
        });
    }
    
    // 聚焦近
    if (ui->btnFocusNear) {
        connect(ui->btnFocusNear, &QPushButton::pressed, this, [this]() {
            uint8_t speed = static_cast<uint8_t>(ui->spinLensSpeed->value());
            m_deviceManager->getLensManager()->focusNear(speed, false);
        });
        connect(ui->btnFocusNear, &QPushButton::released, this, [this]() {
            uint8_t speed = static_cast<uint8_t>(ui->spinLensSpeed->value());
            m_deviceManager->getLensManager()->focusNear(speed, true);
        });
    }
    
    // 聚焦远
    if (ui->btnFocusFar) {
        connect(ui->btnFocusFar, &QPushButton::pressed, this, [this]() {
            uint8_t speed = static_cast<uint8_t>(ui->spinLensSpeed->value());
            m_deviceManager->getLensManager()->focusFar(speed, false);
        });
        connect(ui->btnFocusFar, &QPushButton::released, this, [this]() {
            uint8_t speed = static_cast<uint8_t>(ui->spinLensSpeed->value());
            m_deviceManager->getLensManager()->focusFar(speed, true);
        });
    }
    
    // 光圈大
    if (ui->btnIrisOpen) {
        connect(ui->btnIrisOpen, &QPushButton::pressed, this, [this]() {
            uint8_t speed = static_cast<uint8_t>(ui->spinLensSpeed->value());
            m_deviceManager->getLensManager()->irisOpen(speed, false);
        });
        connect(ui->btnIrisOpen, &QPushButton::released, this, [this]() {
            uint8_t speed = static_cast<uint8_t>(ui->spinLensSpeed->value());
            m_deviceManager->getLensManager()->irisOpen(speed, true);
        });
    }
    
    // 光圈小
    if (ui->btnIrisClose) {
        connect(ui->btnIrisClose, &QPushButton::pressed, this, [this]() {
            uint8_t speed = static_cast<uint8_t>(ui->spinLensSpeed->value());
            m_deviceManager->getLensManager()->irisClose(speed, false);
        });
        connect(ui->btnIrisClose, &QPushButton::released, this, [this]() {
            uint8_t speed = static_cast<uint8_t>(ui->spinLensSpeed->value());
            m_deviceManager->getLensManager()->irisClose(speed, true);
        });
    }
    
    // 聚焦模式切换
    if (ui->checkBoxManualFocus) {
        connect(ui->checkBoxManualFocus, &QCheckBox::clicked, this, &MainWindow::on_checkBoxManualFocus_clicked);
        connect(m_deviceManager->getLensManager(), &LensManager::focusModeChanged, this, &MainWindow::onFocusModeChanged);
    }
    
    // 光圈模式切换
    if (ui->checkBoxManualIris) {
        connect(ui->checkBoxManualIris, &QCheckBox::clicked, this, &MainWindow::on_checkBoxManualIris_clicked);
        connect(m_deviceManager->getLensManager(), &LensManager::irisModeChanged, this, &MainWindow::onIrisModeChanged);
    }
}

MainWindow::~MainWindow()
{
    if (m_ptzThread && m_ptzController) {
        // 在 PTZ 线程中关闭串口，避免跨线程析构
        QMetaObject::invokeMethod(m_ptzController, "prepareForDestruction",
                                  Qt::BlockingQueuedConnection);
    }
    if (m_ptzThread) {
        disconnect(m_ptzThread, &QThread::finished, m_ptzController, &QObject::deleteLater);
        m_ptzThread->quit();
        m_ptzThread->wait();
    }
    delete m_ptzController;
    m_ptzController = nullptr;
    delete m_ptzThread;
    m_ptzThread = nullptr;
    delete ui;
}

void MainWindow::initUI()
{
    // 初始化 PTZ 模块
    initPTZModule();
    
    // 初始化 RGB 模块
    initRGBModule();

    // 初始化目标跟踪模块
    initTrackingModule();

    // 更新 UI 状态
    updatePTZUIState();
    updateRGBUIState();
    updateTrackingUIState();
}

void MainWindow::initPTZModule()
{
    // 刷新串口列表
    on_btnRefresh_clicked();
    
    // 只禁用控制按钮，不禁用连接控件
    ui->btnMove->setEnabled(false);
    ui->btnStop->setEnabled(false);
    ui->btnQuery->setEnabled(false);
    ui->btnReset->setEnabled(false);
    ui->spinPan->setEnabled(false);
    ui->spinTilt->setEnabled(false);
    ui->spinSpeed->setEnabled(false);
    ui->groupBox_2->setEnabled(false);
}

void MainWindow::initRGBModule()
{
    // 加载配置
    ui->lineEditIP->setText(m_configManager->getDeviceIP());
    ui->lineEditUsername->setText(m_configManager->getDeviceUsername());
    ui->lineEditPassword->setText(m_configManager->getDevicePassword());
    ui->spinBoxInterval->setValue(m_configManager->getCaptureInterval());
    ui->spinBoxQuality->setValue(m_configManager->getCaptureQuality());
    ui->lineEditSavePath->setText(m_configManager->getSavePath());

    // 设置保存路径
    m_captureManager->setSavePath(m_configManager->getSavePath());

    // 初始化镜头控制按钮
    if (ui->btnZoomIn) ui->btnZoomIn->setEnabled(false);
    if (ui->btnZoomOut) ui->btnZoomOut->setEnabled(false);
    if (ui->btnFocusNear) ui->btnFocusNear->setEnabled(false);
    if (ui->btnFocusFar) ui->btnFocusFar->setEnabled(false);
    if (ui->btnIrisOpen) ui->btnIrisOpen->setEnabled(false);
    if (ui->btnIrisClose) ui->btnIrisClose->setEnabled(false);
    if (ui->spinLensSpeed) ui->spinLensSpeed->setEnabled(false);
}

void MainWindow::initTrackingModule()
{
    // UI 控件已在 mainwindow.ui 中添加，直接使用 ui-> 访问
    ui->btnTrackStart->setEnabled(false);
    ui->btnTrackStop->setEnabled(false);
    ui->spinTrackDeadZone->setValue(20);
    ui->spinTrackMaxSpeed->setValue(20);
    ui->spinTrackKp->setValue(50);      // 0.50 * 100
    ui->spinTrackKi->setValue(2);       // 0.02 * 100
    ui->spinTrackKd->setValue(30);      // 0.30 * 100
    ui->spinTrackKff->setValue(15);     // 0.15 * 100
    ui->spinTrackPredict->setValue(2);

    // 连接跟踪按钮
    connect(ui->btnTrackSelect, &QPushButton::clicked, this, &MainWindow::on_btnTrackSelect_clicked);
    connect(ui->btnTrackStart, &QPushButton::clicked, this, &MainWindow::on_btnTrackStart_clicked);
    connect(ui->btnTrackStop, &QPushButton::clicked, this, &MainWindow::on_btnTrackStop_clicked);

    // 跟踪源模式选择（可见光/红外），下次框选生效
    connect(ui->cmbTrackSource, &QComboBox::currentIndexChanged, this, [this](int index){
        m_trackingController->setInfraredMode(index == 1);
    });
}

void MainWindow::updateTrackingUIState()
{
    bool canTrack = m_rgbConnected;
    TrackingController::TrackingState state = m_trackingController->state();

    ui->btnTrackSelect->setEnabled(canTrack && (state == TrackingController::Idle));
    ui->btnTrackStart->setEnabled(canTrack && (state == TrackingController::Selecting));
    ui->btnTrackStop->setEnabled(state == TrackingController::Initializing ||
                                 state == TrackingController::Tracking ||
                                 state == TrackingController::Lost ||
                                 state == TrackingController::Paused);

    // 更新状态标签
    QString statusText;
    switch (state) {
    case TrackingController::Idle: statusText = "空闲"; break;
    case TrackingController::Selecting: statusText = "等待框选..."; break;
    case TrackingController::Initializing: statusText = "初始化中..."; break;
    case TrackingController::Tracking: statusText = "跟踪中"; break;
    case TrackingController::Lost: statusText = "目标丢失"; break;
    case TrackingController::Paused: statusText = "已暂停"; break;
    }

    if (!canTrack) statusText = "相机未连接";
    ui->labelTrackStatus->setText(statusText);
}

void MainWindow::updatePTZUIState()
{
    if (m_ptzConnected) {
        ui->btnConnect->setText("断开");
        // 启用控制按钮
        ui->btnMove->setEnabled(true);
        ui->btnStop->setEnabled(true);
        ui->btnQuery->setEnabled(true);
        ui->btnReset->setEnabled(true);
        ui->spinPan->setEnabled(true);
        ui->spinTilt->setEnabled(true);
        ui->spinSpeed->setEnabled(true);
        ui->groupBox_2->setEnabled(true);
    } else {
        ui->btnConnect->setText("打开串口");
        // 禁用控制按钮
        ui->btnMove->setEnabled(false);
        ui->btnStop->setEnabled(false);
        ui->btnQuery->setEnabled(false);
        ui->btnReset->setEnabled(false);
        ui->spinPan->setEnabled(false);
        ui->spinTilt->setEnabled(false);
        ui->spinSpeed->setEnabled(false);
        ui->groupBox_2->setEnabled(false);
    }
}

void MainWindow::updateRGBUIState()
{
    if (m_rgbConnected) {
        ui->rgbBtnConnect->setText("断开");
        ui->rgbLabelStatus->setText("状态: 已连接");
        ui->rgbLabelStatus->setStyleSheet("color: green;");
        
        // 启用抓图控制
        ui->rgbBtnCaptureOnce->setEnabled(true);
        ui->rgbCheckBoxTimedCapture->setEnabled(true);
        ui->spinBoxInterval->setEnabled(true);
        ui->spinBoxQuality->setEnabled(true);
        // 启用录像按钮
        ui->rgbBtnRecord->setEnabled(true);
        ui->lineEditVideoSavePath->setEnabled(true);
        ui->rgbBtnSelectVideoPath->setEnabled(true);
        
        // 启用镜头控制按钮（变焦始终启用）
        if (ui->btnZoomIn) ui->btnZoomIn->setEnabled(true);
        if (ui->btnZoomOut) ui->btnZoomOut->setEnabled(true);
        if (ui->spinLensSpeed) ui->spinLensSpeed->setEnabled(true);
        if (ui->checkBoxManualFocus) ui->checkBoxManualFocus->setEnabled(true);
        if (ui->checkBoxManualIris) ui->checkBoxManualIris->setEnabled(true);
        
        // 聚焦按钮和光圈按钮的状态由相应的信号控制，这里不需要设置
        // 因为登录成功后会自动触发 getFocusModeFromDevice() 和 getIrisModeFromDevice()，然后触发信号更新
    } else {
        ui->rgbBtnConnect->setText("连接");
        ui->rgbLabelStatus->setText("状态: 未连接");
        ui->rgbLabelStatus->setStyleSheet("color: red;");
        
        // 禁用抓图控制
        ui->rgbBtnCaptureOnce->setEnabled(false);
        ui->rgbCheckBoxTimedCapture->setEnabled(false);
        ui->rgbCheckBoxTimedCapture->setChecked(false);
        m_timedCaptureEnabled = false;
        m_captureManager->stopTimedCapture();
        ui->spinBoxInterval->setEnabled(false);
        ui->spinBoxQuality->setEnabled(false);
        // 禁用录像按钮
        ui->rgbBtnRecord->setEnabled(false);
        ui->rgbBtnRecord->setChecked(false);
        ui->rgbBtnRecord->setText("开始录像");
        ui->rgbLabelRecordStatus->setText("就绪");
        ui->rgbLabelRecordTime->setText("00:00:00");
        ui->lineEditVideoSavePath->setEnabled(false);
        ui->rgbBtnSelectVideoPath->setEnabled(false);
        
        // 禁用所有镜头控制按钮
        if (ui->btnZoomIn) ui->btnZoomIn->setEnabled(false);
        if (ui->btnZoomOut) ui->btnZoomOut->setEnabled(false);
        if (ui->btnFocusNear) ui->btnFocusNear->setEnabled(false);
        if (ui->btnFocusFar) ui->btnFocusFar->setEnabled(false);
        if (ui->btnIrisOpen) ui->btnIrisOpen->setEnabled(false);
        if (ui->btnIrisClose) ui->btnIrisClose->setEnabled(false);
        if (ui->spinLensSpeed) ui->spinLensSpeed->setEnabled(false);
        if (ui->checkBoxManualFocus) ui->checkBoxManualFocus->setEnabled(false);
    }
}

void MainWindow::displayImage(const QImage &image)
{
    if (image.isNull()) {
        return;
    }

    // 保存最后一帧（用于框选初始化）
    m_lastFrame = image.copy();

    // 显示图片（C8：缩放结果缓存到 m_scaledPixmap，跟踪框重绘时复用）
    QPixmap pixmap = QPixmap::fromImage(image);
    QSize labelSize = ui->rgbLabelImage->size();
    m_scaledPixmap = pixmap.scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_scaledFrameSize = image.size();

    ui->rgbLabelImage->setPixmap(drawTrackBoxOnPixmap(m_scaledPixmap));
}

// C8：在缩放 pixmap 上叠画跟踪框（QPixmap 写时复制，只有实际有框时才 detach 一次像素拷贝）
QPixmap MainWindow::drawTrackBoxOnPixmap(const QPixmap &base) const
{
    QPixmap pm = base;
    if (m_trackBoxVisible && !m_trackBox.isEmpty() && !pm.isNull() && m_scaledFrameSize.isValid()) {
        QPainter painter(&pm);
        painter.setRenderHint(QPainter::Antialiasing);

        // 计算缩放比例
        const float scaleX = static_cast<float>(pm.width()) / m_scaledFrameSize.width();
        const float scaleY = static_cast<float>(pm.height()) / m_scaledFrameSize.height();

        const QRectF scaledBox(
            m_trackBox.x() * scaleX,
            m_trackBox.y() * scaleY,
            m_trackBox.width() * scaleX,
            m_trackBox.height() * scaleY
        );

        if (m_trackBoxOccluded) {
            painter.setPen(QPen(Qt::red, 2, Qt::DashLine));
        } else {
            painter.setPen(QPen(Qt::green, 2, Qt::SolidLine));
        }
        painter.drawRect(scaledBox);

        // 绘制中心十字
        const QPointF center = scaledBox.center();
        painter.drawLine(QPointF(center.x() - 8, center.y()), QPointF(center.x() + 8, center.y()));
        painter.drawLine(QPointF(center.x(), center.y() - 8), QPointF(center.x(), center.y() + 8));

        painter.end();
    }
    return pm;
}

void MainWindow::addToHistory(const QImage & /*image*/, const QString &filePath)
{
    // 获取文件名
    QString fileName = QFileInfo(filePath).fileName();
    
    // 创建列表项
    QListWidgetItem *item = new QListWidgetItem(fileName);
    ui->rgbListWidgetHistory->insertItem(0, item);
    
    // 限制历史记录数量
    if (ui->rgbListWidgetHistory->count() > 10) {
        delete ui->rgbListWidgetHistory->takeItem(ui->rgbListWidgetHistory->count() - 1);
    }
}

// PTZ 控制相关槽
void MainWindow::on_btnRefresh_clicked()
{
    // 记录当前下拉框中选中的串口号
    QString currentSelection = ui->comboPort->currentText();
    
    // 清空现有的列表
    ui->comboPort->clear();
    
    // 重新扫描底层可用的物理/虚拟串口
    const auto infos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : infos) {
        ui->comboPort->addItem(info.portName());
    }
    
    // 界面交互逻辑处理
    if (ui->comboPort->count() == 0) {
        // 如果物理层确实没有检测到任何串口
        ui->comboPort->addItem("无串口");
        ui->btnConnect->setEnabled(false);
    } else {
        // 如果有串口，启用连接按钮
        ui->btnConnect->setEnabled(true);
        
        // 优先策略：1) 恢复用户之前的选中 2) 若存在COM9则默认选中COM9 3) 否则使用第一个串口
        int index = ui->comboPort->findText(currentSelection);
        if (index == -1) {
            index = ui->comboPort->findText("COM9");
        }
        if (index != -1) {
            ui->comboPort->setCurrentIndex(index);
        } else {
            ui->comboPort->setCurrentIndex(0);
        }
    }
}

void MainWindow::on_btnConnect_clicked()
{
    // 如果当前已经存在实例，说明是"断开"操作
    if (m_ptzController) {
        if (m_ptzThread) {
            // 在 PTZ 线程中关闭串口，避免跨线程析构
            QMetaObject::invokeMethod(m_ptzController, "prepareForDestruction",
                                      Qt::BlockingQueuedConnection);
            // 断掉 finished->deleteLater 防止 quit+wait 后自动删除
            disconnect(m_ptzThread, &QThread::finished, m_ptzController, &QObject::deleteLater);
            m_ptzThread->quit();
            m_ptzThread->wait();
        }
        delete m_ptzController;
        m_ptzController = nullptr;
        delete m_ptzThread;
        m_ptzThread = nullptr;
        m_ptzConnected = false;
        updatePTZUIState();
        return;
    }

    // 尝试连接前的校验
    QString portName = ui->comboPort->currentText();
    if (portName.isEmpty() || portName == "无串口") {
        QMessageBox::warning(this, "警告", "请先选择一个有效的串口！");
        return;
    }
    
    int baudRate = ui->comboBaud->currentText().toInt();
    uint8_t address = 1;
    
    // 创建线程和控制器 (不指定parent)
    m_ptzThread = new QThread();
    m_ptzController = new PTZController(portName, baudRate, address, nullptr);

    // 绑定线程信号
    connect(m_ptzThread, &QThread::finished, m_ptzController, &QObject::deleteLater);
    // 【关键修复】：线程启动后调用 init()
    connect(m_ptzThread, &QThread::started, m_ptzController, &PTZController::init);

    // 绑定控制器信号
    connect(m_ptzController, &PTZController::angleReceived, this, &MainWindow::handleAngleReceived);
    connect(this, &MainWindow::ptzMoveTo, m_ptzController, &PTZController::moveTo);
    connect(this, &MainWindow::ptzStop, m_ptzController, &PTZController::stop);
    connect(this, &MainWindow::ptzQueryAngle, m_ptzController, &PTZController::queryAngle);
    connect(this, &MainWindow::ptzMoveDirection, m_ptzController, &PTZController::moveDirection);
    connect(this, &MainWindow::ptzStartAutoQuery, m_ptzController, &PTZController::startAutoQuery);
    connect(this, &MainWindow::ptzStopAutoQuery, m_ptzController, &PTZController::stopAutoQuery);
    
    // 连接 PTZController 到 DeviceManager（数据记录）
    connect(m_ptzController, &PTZController::angleReceived, m_deviceManager, &DeviceManager::onPTZAngleReceived);

    // Hex 数据监控仪
    connect(m_ptzController, &PTZController::rawDataInout, this, [this](bool isTx, QByteArray data){
        QString hexStr = data.toHex(' ').toUpper();
        if(isTx) {
            ui->textLog->append(QString("<font color='#0055FF'><b>[TX]</b> %1</font>").arg(hexStr));
        } else {
            ui->textLog->append(QString("<font color='#008000'><b>[RX]</b> %1</font>").arg(hexStr));
        }
    });

    // 移动到线程，启动线程
    m_ptzController->moveToThread(m_ptzThread);
    m_ptzThread->start();

    m_isConnecting = true;
    ui->btnConnect->setEnabled(false);
    ui->btnConnect->setText("握手中...");

    // 【关键修复】：延时发送查询指令，确保 init() 执行完毕
    QTimer::singleShot(100, this, [this]() {
        // 发送一条查询指令，呼叫云台
        emit ptzQueryAngle();
    });

    // 启动 1000毫秒 (1秒) 的超时倒计时
    m_connTimer->start(1000);
}

void MainWindow::on_btnMove_clicked()
{
    if (!m_ptzController) return;
    
    // 获取数值
    float pan = static_cast<float>(ui->spinPan->value());
    float tilt = static_cast<float>(ui->spinTilt->value());
    float speed = static_cast<float>(ui->spinSpeed->value());
    
    // 通过信号调用，避免跨线程直接调用
    emit ptzMoveTo(pan, tilt, speed);
}

void MainWindow::on_btnStop_clicked()
{
    if (!m_ptzController) return;
    emit ptzStop();
}

void MainWindow::on_btnQuery_clicked()
{
    if (!m_ptzController) return;
    emit ptzQueryAngle();
}

void MainWindow::on_btnReset_clicked()
{
    if (!m_ptzController) return;
    
    qDebug() << "[复位] 云台回到初始位置 (0, 0)";
    ui->textLog->append("云台复位 -> 回到初始位置 (0°, 0°)");
    
    // 从界面速度控件读取速度（与方向按钮保持一致），不再使用固定 0.5
    float speed = static_cast<float>(ui->spinSpeed->value());
    qDebug() << "[复位] 使用界面速度:" << speed;
    emit ptzMoveTo(0.0f, 0.0f, speed);
}

void MainWindow::handleAngleReceived(float pan, float tilt, float panSpeed, float tiltSpeed)
{
    // 如果是处于握手状态收到了数据，说明真的是云台！
    if (m_isConnecting) {
        m_isConnecting = false;
        m_connTimer->stop();

        m_ptzConnected = true;
        ui->btnConnect->setEnabled(true);
        updatePTZUIState();
    }

    // C1：速度已随信号传入（PTZ 线程内取值），不再跨线程调 getter

    // 更新 DataRecorder 的 PTZ 数据（包含速度）
    if (m_deviceManager && m_deviceManager->getDataRecorder()) {
        m_deviceManager->getDataRecorder()->updatePTZData(pan, tilt, panSpeed, tiltSpeed);
    }
    
    // 更新日志
    ui->textLog->append(QString("云台反馈 -> 水平: %1°  垂直: %2°  水平速度: %3  垂直速度: %4")
                        .arg(pan).arg(tilt).arg(panSpeed).arg(tiltSpeed));
}

void MainWindow::onConnectionTimeout()
{
    // 前置校验：如果已经连接成功，直接返回，防止误杀线程
    if (m_ptzConnected) return;
    
    m_isConnecting = false;
    
    // 销毁无用连接
    if (m_ptzController) {
        if (m_ptzThread) {
            // 在 PTZ 线程中关闭串口，避免跨线程析构
            QMetaObject::invokeMethod(m_ptzController, "prepareForDestruction",
                                      Qt::BlockingQueuedConnection);
            disconnect(m_ptzThread, &QThread::finished, m_ptzController, &QObject::deleteLater);
            m_ptzThread->quit();
            m_ptzThread->wait();
        }
        delete m_ptzController;
        m_ptzController = nullptr;
        delete m_ptzThread;
        m_ptzThread = nullptr;
    }
    
    // 恢复 UI 状态
    ui->btnConnect->setEnabled(true);
    ui->btnConnect->setText("打开串口");
    ui->groupControl->setEnabled(false);
    
    QMessageBox::warning(this, "连接超时", "串口打开成功，但未收到云台响应！\n\n请检查：\n1. 是否选错了串口（比如选了虚拟串口）？\n2. 云台是否已通电且接线正常？\n3. 波特率和地址是否匹配？");
}

// RGB 图像捕获相关槽
void MainWindow::on_rgbBtnConnect_clicked()
{
    if (m_rgbConnected) {
        // 断开连接
        m_deviceManager->logout();
    } else {
        // 连接设备
        QString ip = ui->lineEditIP->text();
        QString username = ui->lineEditUsername->text();
        QString password = ui->lineEditPassword->text();
        
        // 保存配置
        m_configManager->setDeviceIP(ip);
        m_configManager->setDeviceUsername(username);
        m_configManager->setDevicePassword(password);
        m_configManager->saveConfig();
        
        // 尝试连接
        m_deviceManager->login(ip, username, password);
    }
}

void MainWindow::on_rgbBtnCaptureOnce_clicked()
{
    if (!m_rgbConnected) {
        return;
    }
    
    // 保存配置
    m_configManager->setCaptureQuality(ui->spinBoxQuality->value());
    m_configManager->saveConfig();
    
    // 执行单次抓图
    m_captureManager->captureOnce(ui->spinBoxQuality->value(), 1920, 1080);
}

void MainWindow::on_rgbCheckBoxTimedCapture_stateChanged(int arg1)
{
    if (!m_rgbConnected) {
        ui->rgbCheckBoxTimedCapture->setChecked(false);
        return;
    }
    
    m_timedCaptureEnabled = (arg1 == Qt::Checked);
    
    if (m_timedCaptureEnabled) {
        // 保存配置
        m_configManager->setCaptureInterval(ui->spinBoxInterval->value());
        m_configManager->setCaptureQuality(ui->spinBoxQuality->value());
        m_configManager->saveConfig();
        
        // 开始定时抓图
        m_captureManager->startTimedCapture(ui->spinBoxInterval->value() * 1000, ui->spinBoxQuality->value(), 1920, 1080);
    } else {
        // 停止定时抓图
        m_captureManager->stopTimedCapture();
    }
}

void MainWindow::on_rgbBtnSelectSavePath_clicked()
{
    QString path = QFileDialog::getExistingDirectory(this, "选择保存路径", m_configManager->getSavePath());
    if (!path.isEmpty()) {
        ui->lineEditSavePath->setText(path);
        m_configManager->setSavePath(path);
        m_configManager->saveConfig();
        m_captureManager->setSavePath(path);
    }
}

void MainWindow::on_rgbBtnSelectVideoPath_clicked()
{
    QString path = QFileDialog::getExistingDirectory(this, "选择录像保存路径", ui->lineEditVideoSavePath->text());
    if (!path.isEmpty()) {
        ui->lineEditVideoSavePath->setText(path);
    }
}

void MainWindow::on_rgbBtnRecord_clicked()
{
    if (ui->rgbBtnRecord->isChecked()) {
        // 开始录像
        QString saveDir = ui->lineEditVideoSavePath->text();
        
        // 检查并创建目录
        QDir dir(saveDir);
        if (!dir.exists()) {
            if (!dir.mkpath(".")) {
                QMessageBox::warning(this, "错误", "无法创建保存目录！");
                ui->rgbBtnRecord->setChecked(false);
                return;
            }
        }
        
        // 生成文件名
        QDateTime currentTime = QDateTime::currentDateTime();
        QString fileName = QString("record_%1.mp4").arg(currentTime.toString("yyyyMMdd_hhmmss"));
        QString fullPath = dir.absoluteFilePath(fileName);
        
        // 开始录像
        if (m_deviceManager->startRecord(fullPath)) {
            // 录像开始成功，UI更新会在 onRecordStarted 中处理
        } else {
            QMessageBox::warning(this, "错误", "开始录像失败！");
            ui->rgbBtnRecord->setChecked(false);
        }
    } else {
        // 停止录像
        m_deviceManager->stopRecord();
        // 录像停止，UI更新会在 onRecordStopped 中处理
    }
}

void MainWindow::onRecordTimerTimeout()
{
    m_recordSeconds++;
    ui->rgbLabelRecordTime->setText(formatTime(m_recordSeconds));
}

void MainWindow::onRecordStarted()
{
    m_recordSeconds = 0;
    ui->rgbLabelRecordTime->setText("00:00:00");
    ui->rgbBtnRecord->setText("停止录像");
    ui->rgbLabelRecordStatus->setText("录像中...");
    ui->lineEditVideoSavePath->setEnabled(false);
    ui->rgbBtnSelectVideoPath->setEnabled(false);
    m_recordTimer->start();
    qDebug() << "[MainWindow] 录像已开始";
    
    // 启动 PTZ 自动查询 (10Hz)
    if (m_ptzController && m_ptzConnected) {
        emit ptzStartAutoQuery(100);
        qDebug() << "[MainWindow] PTZ 自动查询已启动 (10Hz)";
    }
}

void MainWindow::onRecordStopped(const QString &filePath)
{
    m_recordTimer->stop();
    m_recordSeconds = 0;
    ui->rgbLabelRecordTime->setText("00:00:00");
    ui->rgbBtnRecord->setChecked(false);
    ui->rgbBtnRecord->setText("开始录像");
    ui->rgbLabelRecordStatus->setText("就绪");
    ui->lineEditVideoSavePath->setEnabled(true);
    ui->rgbBtnSelectVideoPath->setEnabled(true);
    qDebug() << "[MainWindow] 录像已完成，文件:" << filePath;
    
    // 停止 PTZ 自动查询
    if (m_ptzController) {
        emit ptzStopAutoQuery();
        qDebug() << "[MainWindow] PTZ 自动查询已停止";
    }
    
    // 显示数据记录文件信息
    QString csvPath = QFileInfo(filePath).absolutePath() + "/" + QFileInfo(filePath).completeBaseName() + ".csv";
    if (QFile::exists(csvPath)) {
        ui->statusbar->showMessage(QString("录像已保存: %1，数据已保存: %2").arg(filePath, csvPath), 5000);
    } else {
        ui->statusbar->showMessage(QString("录像已保存: %1").arg(filePath), 5000);
    }
}

QString MainWindow::formatTime(int seconds)
{
    int hours = seconds / 3600;
    int minutes = (seconds % 3600) / 60;
    int secs = seconds % 60;
    return QString("%1:%2:%3")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0'));
}

void MainWindow::onConnectionStatusChanged(bool connected)
{
    m_rgbConnected = connected;
    updateRGBUIState();
    updateTrackingUIState();

    if (connected) {
        qDebug() << "[MainWindow] 设备连接成功，开始视频预览";
        m_deviceManager->requestStartStream();  // C7：RealPlay 移出 UI 线程
    } else {
        qDebug() << "[MainWindow] 设备断开连接，停止视频预览";
        // 断开时停止跟踪
        if (m_trackingController->isTracking()) {
            m_trackingController->stopTracking();
        }
    }
}

void MainWindow::onCaptureFinished(const QImage &image, const QString &filePath)
{
    // 抓图图片不显示在预览区，只添加到历史记录
    // SDK的OSD会自动添加时间戳，直接保存原始图片
    
    // 添加到历史记录
    addToHistory(image, filePath);
    
    // 更新状态
    ui->statusbar->showMessage(QString("抓图成功: %1").arg(QFileInfo(filePath).fileName()), 3000);
}

void MainWindow::onErrorOccurred(const QString &error)
{
    // 显示错误信息
    ui->rgbLabelError->setText(QString("错误: %1").arg(error));
    ui->statusbar->showMessage(error, 5000);
    qDebug() << "错误:" << error;
}

void MainWindow::onFrameReceived(const QImage &frame)
{
    if (frame.isNull()) {
        return;
    }

    // 显示图片（SDK的OSD已自动添加时间戳）
    displayImage(frame);

    // 如果正在跟踪，传递帧给跟踪控制器
    if (m_trackingController->isTracking()) {
        m_trackingController->processFrame(frame);
    }
}

void MainWindow::onFocusModeChanged(bool isManualFocus)
{
    if (ui->checkBoxManualFocus) {
        // 使用 blockSignals 避免死循环
        const bool blocked = ui->checkBoxManualFocus->blockSignals(true);
        ui->checkBoxManualFocus->setChecked(isManualFocus);
        ui->checkBoxManualFocus->blockSignals(blocked);
        
        ui->textLog->append(QString("[镜头控制] 聚焦模式已切换为: %1")
                           .arg(isManualFocus ? "手动对焦" : "自动对焦"));
        
        // 【确保】变焦按钮始终启用
        if (ui->btnZoomIn) ui->btnZoomIn->setEnabled(true);
        if (ui->btnZoomOut) ui->btnZoomOut->setEnabled(true);
        
        // 【UI 联动】自动对焦模式下禁用手动对焦按钮
        if (ui->btnFocusNear) {
            ui->btnFocusNear->setEnabled(isManualFocus);
        }
        if (ui->btnFocusFar) {
            ui->btnFocusFar->setEnabled(isManualFocus);
        }
    }
}

void MainWindow::on_checkBoxManualFocus_clicked(bool checked)
{
    if (m_deviceManager && m_deviceManager->getLensManager()) {
        m_deviceManager->getLensManager()->setFocusMode(checked);
    }
}

void MainWindow::onIrisModeChanged(bool isManualIris)
{
    if (ui->checkBoxManualIris) {
        // 使用 blockSignals 避免死循环
        const bool blocked = ui->checkBoxManualIris->blockSignals(true);
        ui->checkBoxManualIris->setChecked(isManualIris);
        ui->checkBoxManualIris->blockSignals(blocked);
        
        ui->textLog->append(QString("[镜头控制] 光圈模式已切换为: %1")
                           .arg(isManualIris ? "手动/光圈优先" : "自动"));
        
        // 【UI 联动】自动光圈模式下禁用手动光圈按钮
        if (ui->btnIrisOpen) {
            ui->btnIrisOpen->setEnabled(isManualIris);
        }
        if (ui->btnIrisClose) {
            ui->btnIrisClose->setEnabled(isManualIris);
        }
    }
}

void MainWindow::on_checkBoxManualIris_clicked(bool checked)
{
    if (m_deviceManager && m_deviceManager->getLensManager()) {
        m_deviceManager->getLensManager()->setIrisMode(checked);
    }
}

// ========================
// 目标跟踪相关槽函数
// ========================

void MainWindow::on_btnTrackSelect_clicked()
{
    m_trackingController->startSelection();
    m_isSelectingTarget = true;
    ui->statusbar->showMessage("请在视频画面上按住鼠标左键框选目标", 5000);
}

void MainWindow::on_btnTrackStart_clicked()
{
    if (!m_isSelectingTarget || m_selectBox.isEmpty()) {
        ui->statusbar->showMessage("请先框选目标", 3000);
        return;
    }

    // 空帧防护
    if (m_lastFrame.isNull()) {
        ui->statusbar->showMessage("尚未收到视频帧", 3000);
        m_isSelectingTarget = false;
        m_selectBox = QRectF();
        return;
    }

    qDebug() << "[TrackStart] === begin ===";
    qDebug() << "[TrackStart] m_selectBox:" << m_selectBox;
    qDebug() << "[TrackStart] frame size:" << m_lastFrame.width() << "x" << m_lastFrame.height();

    // 从 UI 控件读取参数
    m_trackingController->setDeadZonePixels(ui->spinTrackDeadZone->value());
    m_trackingController->setMaxSpeed(ui->spinTrackMaxSpeed->value());
    m_trackingController->setProportionalGain(ui->spinTrackKp->value() / 100.0f);
    m_trackingController->setIntegralGain(ui->spinTrackKi->value() / 100.0f);
    m_trackingController->setDerivativeGain(ui->spinTrackKd->value() / 100.0f);
    m_trackingController->setFeedforwardGain(ui->spinTrackKff->value() / 100.0f);
    m_trackingController->setPredictHorizon(ui->spinTrackPredict->value());

    qDebug() << "[TrackStart] params set, will setTarget...";

    // 设置帧尺寸
    m_trackingController->setFrameSize(m_lastFrame.width(), m_lastFrame.height());

    // 启动跟踪（setTarget 内部会创建子线程并返回；此处加 try/catch 防止直接崩溃）
    try {
        m_trackingController->setTarget(m_lastFrame, m_selectBox);
    } catch (const std::exception &e) {
        qCritical() << "[TrackStart] setTarget 抛出异常:" << e.what();
        ui->statusbar->showMessage(QString("setTarget 异常: %1").arg(e.what()), 5000);
        return;
    } catch (...) {
        qCritical() << "[TrackStart] setTarget 抛出未知异常";
        ui->statusbar->showMessage("setTarget 异常（未知类型）", 5000);
        return;
    }

    qDebug() << "[TrackStart] setTarget 完成，state=" << m_trackingController->state();
    m_isSelectingTarget = false;
    m_selectBox = QRectF();
}

void MainWindow::on_btnTrackStop_clicked()
{
    m_trackingController->stopTracking();
    m_isSelectingTarget = false;
    m_selectBox = QRectF();
    m_trackBoxVisible = false;
    updateTrackingUIState();
}

void MainWindow::onTrackStateChanged(TrackingController::TrackingState state)
{
    updateTrackingUIState();

    QString msg;
    switch (state) {
    case TrackingController::Idle: msg = "跟踪已停止"; break;
    case TrackingController::Selecting: msg = "请框选目标"; break;
    case TrackingController::Initializing: msg = "跟踪器初始化中"; break;
    case TrackingController::Tracking: msg = "跟踪运行中"; break;
    case TrackingController::Lost: msg = "目标丢失，尝试恢复"; break;
    case TrackingController::Paused: msg = "跟踪已暂停"; break;
    }
    ui->statusbar->showMessage(msg, 3000);
}

void MainWindow::onTrackResult(const TrackResult& result)
{
    // 更新数据记录器
    if (m_deviceManager && m_deviceManager->getDataRecorder() &&
        m_deviceManager->getDataRecorder()->isRecording()) {
        float offsetX = result.bbox.center().x() - m_lastFrame.width() / 2.0f;
        float offsetY = result.bbox.center().y() - m_lastFrame.height() / 2.0f;
        m_deviceManager->getDataRecorder()->updateDetectionData(
            result.bbox.center().x(), result.bbox.center().y(),
            offsetX, offsetY, result.confidence);
    }
}

void MainWindow::onTrackBoxDraw(const QRectF& bbox, bool occluded)
{
    m_trackBox = bbox;
    m_trackBoxVisible = !bbox.isEmpty();
    m_trackBoxOccluded = occluded;

    // C8：直接复用缓存的缩放 pixmap 叠画跟踪框，省去 fromImage+scaled 全量重算
    if (!m_scaledPixmap.isNull()) {
        ui->rgbLabelImage->setPixmap(drawTrackBoxOnPixmap(m_scaledPixmap));
    }
}

void MainWindow::onTrackStatusMessage(const QString& msg)
{
    ui->statusbar->showMessage(msg, 3000);
    ui->textLog->append(QString("[跟踪] %1").arg(msg));
}

void MainWindow::onPtzControlDelta(int deltaX, int deltaY, int speedX, int speedY)
{
    if (!m_ptzConnected || !m_ptzController) {
        return;
    }

    // 速度输出转PTZ方向指令（负反馈闭环）
    // deltaX > 0: 目标在右侧，云台右转 (cmd2 = 0x02)
    // deltaX < 0: 目标在左侧，云台左转 (cmd2 = 0x04)
    // deltaY > 0: 目标在下方，云台下转 (cmd2 = 0x10)
    // deltaY < 0: 目标在上方，云台上转 (cmd2 = 0x08)

    uint8_t cmd2 = 0x00;
    uint8_t hSpeed = 0;
    uint8_t vSpeed = 0;

    // R-03: 根据速度输出的符号（而非脱靶量）决定方向
    if (speedX > 0) {
        cmd2 |= 0x02;  // 右
        hSpeed = static_cast<uint8_t>(speedX);
    } else if (speedX < 0) {
        cmd2 |= 0x04;  // 左
        hSpeed = static_cast<uint8_t>(-speedX);
    }

    if (speedY > 0) {
        cmd2 |= 0x10;  // 下
        vSpeed = static_cast<uint8_t>(speedY);
    } else if (speedY < 0) {
        cmd2 |= 0x08;  // 上
        vSpeed = static_cast<uint8_t>(-speedY);
    }

    if (cmd2 != 0x00) {
        emit ptzMoveDirection(cmd2, hSpeed, vSpeed);
    } else {
        emit ptzStop();
    }
}

// ========================
// 鼠标事件（框选目标）
// ========================

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (!m_isSelectingTarget) {
        QMainWindow::mousePressEvent(event);
        return;
    }

    // 检查点击是否在图像标签内
    QPoint labelPos = ui->rgbLabelImage->mapFrom(this, event->pos());
    QRect labelRect = ui->rgbLabelImage->rect();
    if (!labelRect.contains(labelPos)) {
        QMainWindow::mousePressEvent(event);
        return;
    }

    m_selectStart = labelPos;
    m_selectBox = QRectF();
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_isSelectingTarget) {
        QMainWindow::mouseMoveEvent(event);
        return;
    }

    QPoint labelPos = ui->rgbLabelImage->mapFrom(this, event->pos());
    QRect labelRect = ui->rgbLabelImage->rect();
    if (!labelRect.contains(labelPos)) {
        QMainWindow::mouseMoveEvent(event);
        return;
    }

    // 计算框选区域（在标签坐标系）
    int x = std::min(m_selectStart.x(), labelPos.x());
    int y = std::min(m_selectStart.y(), labelPos.y());
    int w = std::abs(labelPos.x() - m_selectStart.x());
    int h = std::abs(labelPos.y() - m_selectStart.y());

    if (w < 5 || h < 5) return;

    // 转换到原始图像坐标（考虑 KeepAspectRatio 留白）
    if (!m_lastFrame.isNull()) {
        QSize labelSize = ui->rgbLabelImage->size();
        float frameW = static_cast<float>(m_lastFrame.width());
        float frameH = static_cast<float>(m_lastFrame.height());

        // 计算实际显示的图像在 label 中的区域（KeepAspectRatio 下可能有黑边）
        float displayScale = std::min(labelSize.width() / frameW, labelSize.height() / frameH);
        float displayW = frameW * displayScale;
        float displayH = frameH * displayScale;
        float offsetX = (labelSize.width() - displayW) / 2.0f;
        float offsetY = (labelSize.height() - displayH) / 2.0f;

        // 标签坐标 → 原始图像坐标
        float imgX = (x - offsetX) * frameW / displayW;
        float imgY = (y - offsetY) * frameH / displayH;
        float imgW = w * frameW / displayW;
        float imgH = h * frameH / displayH;

        m_selectBox = QRectF(imgX, imgY, imgW, imgH);

        // 实时显示框选（在缩放的画布上绘制，坐标需偏移）
        // R-09: 复用已缓存的 m_scaledPixmap，避免每次拖动都缩放 1080P 图像
        QPixmap overlay = m_scaledPixmap.copy();
        QPainter painter(&overlay);
        painter.setPen(QPen(Qt::yellow, 2, Qt::DashLine));
        painter.drawRect(QRectF(x - offsetX, y - offsetY, w, h));
        painter.end();
        ui->rgbLabelImage->setPixmap(overlay);
    }
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_isSelectingTarget) {
        QMainWindow::mouseReleaseEvent(event);
        return;
    }

    if (m_selectBox.width() < 10 || m_selectBox.height() < 10) {
        ui->statusbar->showMessage("框选区域太小，请重新框选", 3000);
        m_selectBox = QRectF();
        return;
    }

    ui->statusbar->showMessage(QString("已框选目标: %1x%2, 点击[开始跟踪]启动")
                               .arg(static_cast<int>(m_selectBox.width()))
                               .arg(static_cast<int>(m_selectBox.height())), 3000);
    updateTrackingUIState();
}
