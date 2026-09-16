#include "trackingcontroller.h"
#include <QDebug>
#include <QtMath>

TrackingController::TrackingController(QObject *parent)
    : QObject(parent)
    , m_tracker(nullptr)
    , m_trackerThread(nullptr)
    , m_state(Idle)
    , m_frameWidth(1920)
    , m_frameHeight(1080)
    , m_deadZonePixels(20)
    , m_maxSpeed(20)
    , m_kp(0.5f)
    , m_ki(0.02f)
    , m_kd(0.3f)
    , m_k_ff(0.15f)
    , m_T_predict(2)
    , m_integralX(0.0f)
    , m_integralY(0.0f)
    , m_prevErrorX(0.0f)
    , m_prevErrorY(0.0f)
    , m_I_max(15.0f)
    , m_pidInitialized(false)
{
}

TrackingController::~TrackingController()
{
    stopTracking();
}

void TrackingController::startSelection()
{
    if (m_state == Tracking || m_state == Selecting) {
        stopTracking();
    }
    m_state = Selecting;
    emit stateChanged(m_state);
    emit statusMessage("请在视频画面上框选要跟踪的目标");
}

void TrackingController::setTarget(const QImage& frame, const QRectF& targetRect)
{
    qDebug() << "[TrackCtrl] setTarget begin, state=" << m_state
             << " frame=" << frame.width() << "x" << frame.height()
             << " target=" << targetRect;

    if (m_state != Selecting) {
        qWarning() << "[TrackingController] 不在框选状态，无法设置目标";
        return;
    }

    // 清理旧的跟踪器（先停线程，再删对象，避免跨线程访问）
    if (m_tracker) {
        QObject::disconnect(m_tracker, nullptr, nullptr, nullptr);
    }
    if (m_trackerThread) {
        m_trackerThread->quit();
        m_trackerThread->wait();
    }
    delete m_tracker;
    m_tracker = nullptr;
    delete m_trackerThread;
    m_trackerThread = nullptr;
    qDebug() << "[TrackCtrl] 旧 tracker 清理完毕";

    // 创建独立工作线程
    m_trackerThread = new QThread(this);
    m_tracker = new ObjectTracker(nullptr);
    qDebug() << "[TrackCtrl] 新 tracker 创建完毕，开始 init()...";

    bool ok = false;
    try {
        ok = m_tracker->init(frame, targetRect);
    } catch (const std::exception &e) {
        qCritical() << "[TrackCtrl] tracker->init() 抛出异常:" << e.what();
        ok = false;
    } catch (...) {
        qCritical() << "[TrackCtrl] tracker->init() 抛出未知异常";
        ok = false;
    }

    if (!ok) {
        qWarning() << "[TrackCtrl] tracker init 失败，释放资源";
        delete m_tracker;
        m_tracker = nullptr;
        delete m_trackerThread;
        m_trackerThread = nullptr;
        emit statusMessage("跟踪器初始化失败，请重新框选");
        m_state = Idle;
        emit stateChanged(m_state);
        return;
    }
    qDebug() << "[TrackCtrl] tracker init 成功，model=" << m_tracker->modelSize()
             << " scaleModel=" << m_tracker->scaleModelSize();

    connect(m_tracker, &ObjectTracker::trackingDone, this, [this](const TrackResult& result) {
        m_lastResult = result;
        emit trackingResult(result);
        emit drawTrackingBox(result.bbox, result.occluded);

        if (result.valid && !result.occluded) {
            computePTZControl(result);
        } else {
            emit ptzControlDelta(0, 0, 0, 0);
        }
    });
    connect(m_tracker, &ObjectTracker::trackingLost, this, &TrackingController::onTrackingLost);
    connect(m_tracker, &ObjectTracker::trackingRecovered, this, &TrackingController::onTrackingRecovered);

    m_tracker->moveToThread(m_trackerThread);
    m_trackerThread->start();
    qDebug() << "[TrackCtrl] 工作线程启动完毕";

    m_state = Tracking;
    m_lastResult = TrackResult();
    m_lastResult.valid = true;
    m_lastResult.bbox = targetRect;

    resetPidState();

    emit stateChanged(m_state);
    emit statusMessage("目标跟踪已启动");
    emit drawTrackingBox(targetRect, false);
    qDebug() << "[TrackCtrl] setTarget done, state=Tracking";
}

void TrackingController::processFrame(const QImage& frame)
{
    if (m_state != Tracking || !m_tracker) {
        return;
    }

    // 修复P2#13: 空帧防护，避免传递无效帧给跟踪器
    if (frame.isNull()) {
        return;
    }

    // 更新画面尺寸（如果变化）
    if (frame.width() != m_frameWidth || frame.height() != m_frameHeight) {
        m_frameWidth = frame.width();
        m_frameHeight = frame.height();
    }

    // 跨线程异步调用：帧数据发送到 worker 线程处理
    QMetaObject::invokeMethod(m_tracker, "processFrameSlot",
                              Qt::QueuedConnection,
                              Q_ARG(QImage, frame));
}

// 重置PID积分（框选新目标或状态切换时调用）
void TrackingController::resetPidState()
{
    m_integralX = 0.0f;
    m_integralY = 0.0f;
    m_prevErrorX = 0.0f;
    m_prevErrorY = 0.0f;
    m_pidInitialized = false;
}

// ==============================
// PTZ速度控制
//     策略：KF预测前馈 + PID反馈
//
//  前馈通道（Feedforward）：
//      利用KF估计的目标运动速度，提前映射为PTZ补偿速度
//      feedforwardX = K_ff × vx
//      目的：消除匀速跟踪时的稳态脱靶量
//
//  预测模块（Prediction）：
//      将KF平滑位置向前预测 T_predict 帧
//      predictX = kfX + vx × T_predict
//      目的：补偿通信延迟和云台机械响应延迟
//
//  PID反馈通道：
//      error = 未来预测位置 - 画面中心
//      P = Kp × error
//      I = clamp(I + Ki × error, -Imax, Imax)  — 积分消除系统偏差
//      D = Kd × (error - prevError)            — 微分抑制振荡
//      PID_out = P + I + D
//
//  混合输出：
//      speed = feedforward + PID_out
//      speed = clamp(speed, -maxSpeed, maxSpeed)
//      死区过滤
// ==============================
void TrackingController::computePTZControl(const TrackResult& result)
{
    const float centerX = m_frameWidth / 2.0f;
    const float centerY = m_frameHeight / 2.0f;

    // === 1. 提取KF状态 ===
    const float kfX = result.kfX;    // KF滤波后的平滑位置
    const float kfY = result.kfY;
    const float kfVx = result.kfVx;  // KF估计的目标运动速度
    const float kfVy = result.kfVy;

    // === 2. 预测前馈：预测未来 T_predict 帧后的目标位置 ===
    const float predictX = kfX + kfVx * m_T_predict;
    const float predictY = kfY + kfVy * m_T_predict;

    // 预测脱靶量（正值=目标在中心右侧/下方，云台需向右/下转）
    const float errorX = predictX - centerX;
    const float errorY = predictY - centerY;

    // 死区：误差太小不产生动作
    const float errorXEffective = (std::abs(errorX) < m_deadZonePixels) ? 0.0f : errorX;
    const float errorYEffective = (std::abs(errorY) < m_deadZonePixels) ? 0.0f : errorY;

    if (errorXEffective == 0.0f && errorYEffective == 0.0f) {
        // 在死区内 → 清零积分，避免累积旧误差
        m_integralX = 0.0f;
        m_integralY = 0.0f;
        emit ptzControlDelta(0, 0, 0, 0);
        return;
    }

    // === 3. 速度前馈：目标运动速度直接映射为PTZ补偿速度 ===
    const float vFeedX = m_k_ff * kfVx;
    const float vFeedY = m_k_ff * kfVy;

    // === 4. PID反馈控制 ===

    // 4a. P项 — 比例响应脱靶量
    const float pX = m_kp * errorXEffective;
    const float pY = m_kp * errorYEffective;

    // 4b. I项 — 积分消除稳态误差（带防饱和clamp）
    // dt = 1 帧（跟踪器每帧一次输出）
    m_integralX += m_ki * errorXEffective;
    m_integralY += m_ki * errorYEffective;
    m_integralX = qBound(-m_I_max, m_integralX, m_I_max);
    m_integralY = qBound(-m_I_max, m_integralY, m_I_max);

    // 4c. D项 — 微分抑制振荡（首次跳过，使用上一帧误差计算变化率）
    float dX = 0.0f;
    float dY = 0.0f;
    if (m_pidInitialized) {
        dX = m_kd * (errorXEffective - m_prevErrorX);
        dY = m_kd * (errorYEffective - m_prevErrorY);
    } else {
        m_pidInitialized = true;
    }

    m_prevErrorX = errorXEffective;
    m_prevErrorY = errorYEffective;

    // PID总输出
    const float pidX = pX + m_integralX + dX;
    const float pidY = pY + m_integralY + dY;

    // === 5. 前馈 + 反馈混合 ===
    float speedXf = vFeedX + pidX;
    float speedYf = vFeedY + pidY;

    // === 6. 安全限幅 ===
    speedXf = qBound(-static_cast<float>(m_maxSpeed), speedXf, static_cast<float>(m_maxSpeed));
    speedYf = qBound(-static_cast<float>(m_maxSpeed), speedYf, static_cast<float>(m_maxSpeed));

    // === 7. 输出（带方向信息的脱靶量用于 MainWindow 决定Pelco-D命令码）===
    const int outDeltaX = qRound(errorXEffective);
    const int outDeltaY = qRound(errorYEffective);
    const int outSpeedX = qRound(std::abs(speedXf));
    const int outSpeedY = qRound(std::abs(speedYf));

    emit ptzControlDelta(outDeltaX, outDeltaY, outSpeedX, outSpeedY);
}

void TrackingController::stopTracking()
{
    // 修复P0#2: 严格顺序: disconnect → quit → wait → delete tracker → delete thread
    if (m_tracker) {
        disconnect(m_tracker, nullptr, nullptr, nullptr);  // 断开所有信号
    }
    if (m_trackerThread) {
        m_trackerThread->quit();
        m_trackerThread->wait();
    }
    if (m_tracker) {
        delete m_tracker;
        m_tracker = nullptr;
    }
    if (m_trackerThread) {
        delete m_trackerThread;
        m_trackerThread = nullptr;
    }
    m_state = Idle;
    emit stateChanged(m_state);
    emit statusMessage("跟踪已停止");
    emit drawTrackingBox(QRectF(), false);
}

void TrackingController::pauseTracking()
{
    if (m_state == Tracking) {
        m_state = Paused;
        emit stateChanged(m_state);
        emit statusMessage("跟踪已暂停");
    }
}

void TrackingController::resumeTracking()
{
    if (m_state == Paused) {
        m_state = Tracking;
        emit stateChanged(m_state);
        emit statusMessage("跟踪已恢复");
    }
}

void TrackingController::onTrackingLost()
{
    if (m_state == Tracking) {
        m_state = Lost;
        emit stateChanged(m_state);
        emit statusMessage("目标丢失，尝试恢复中...");
    }
}

void TrackingController::onTrackingRecovered()
{
    if (m_state == Lost) {
        m_state = Tracking;
        emit stateChanged(m_state);
        emit statusMessage("目标已恢复");
    }
}
