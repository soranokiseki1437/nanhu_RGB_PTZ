#include "objecttracker.h"
#include "imagematconvert.h"
#include "logging_categories.h"
#include "onnxdetector.h"
#include <cmath>
#include <QElapsedTimer>
#include <QFile>
#include <algorithm>

// ========================
// 构造函数 / 析构函数
// ========================
ObjectTracker::ObjectTracker(QObject *parent)
    : QObject(parent)
    , m_kfQ(cv::Matx44f::eye() * 100.0f)
    , m_kfQLost(cv::Matx44f::eye() * 900.0f)
    , m_kfR(cv::Matx22f::eye() * 100.0f)
    , m_evaluator(10, 0.65f, 0.5f)
{
}

ObjectTracker::~ObjectTracker()
{
}

// ========================
// 重置
// ========================
void ObjectTracker::reset()
{
    m_initialized = false;
    m_trackingState = AMFTTrackingState::TRACK_NORMAL;
    m_lostCount = 0;
    m_frameCount = 0;
    m_needScaleCalibration = false;
    m_evaluator.reset();
    m_lastScore = AMFTScore();
}

QString ObjectTracker::modelSize() const
{
    const cv::Size ms = m_core.modelSize();
    return QString("%1x%2").arg(ms.width).arg(ms.height);
}

// ========================
// 变焦联动：触发尺度纠偏校准
// ========================
void ObjectTracker::triggerScaleCalibration()
{
    m_needScaleCalibration = true;
    qCDebug(trackerLog) << "[Tracker] 变焦尺度纠偏已标记，下一帧将触发 AI 尺度校准";
}

// ========================
// 初始化跟踪器
// ========================
bool ObjectTracker::init(const QImage& frame, const QRectF& targetRect)
{
    reset();
    qCDebug(trackerLog) << "[Tracker] init 开始，目标框=" << targetRect;

    if (frame.isNull() || targetRect.width() <= 0 || targetRect.height() <= 0) {
        qWarning() << "[ObjectTracker] 初始化失败: 无效图像或目标框";
        return false;
    }

    const int frameW = frame.width();
    const int frameH = frame.height();
    if (frameW <= 0 || frameH <= 0) {
        qWarning() << "[ObjectTracker] 初始化失败: 图像尺寸无效";
        return false;
    }

    // 将目标框裁剪到图像内部
    const qreal left = std::max<qreal>(0, targetRect.left());
    const qreal top = std::max<qreal>(0, targetRect.top());
    const qreal right = std::min<qreal>(static_cast<qreal>(frameW), targetRect.right());
    const qreal bottom = std::min<qreal>(static_cast<qreal>(frameH), targetRect.bottom());
    const qreal clippedW = right - left;
    const qreal clippedH = bottom - top;
    if (clippedW < 4 || clippedH < 4) {
        qWarning() << "[ObjectTracker] 初始化失败: 目标框裁剪后过小" << clippedW << "x" << clippedH;
        return false;
    }

    m_posX = static_cast<float>((left + right) * 0.5);
    m_posY = static_cast<float>((top + bottom) * 0.5);
    m_targetW = static_cast<float>(clippedW);
    m_targetH = static_cast<float>(clippedH);
    m_currentScale = 1.0f;

    cv::Mat image = QImageToMat(frame);
    if (image.empty()) {
        qWarning() << "[ObjectTracker] 初始化失败: QImage 转 cv::Mat 失败";
        return false;
    }

    // 内核初始化（红外模式须在 init 前设置）
    m_core.setInfraredMode(m_infraredMode);
    const cv::Rect roi(static_cast<int>(left), static_cast<int>(top),
                       static_cast<int>(clippedW), static_cast<int>(clippedH));
    if (!m_core.init(image, roi)) {
        qWarning() << "[ObjectTracker] DsstCore 初始化失败";
        return false;
    }

    // KF 初始化
    m_kf.x = cv::Vec4f(m_posX, m_posY, 0.0f, 0.0f);
    m_kf.P = cv::Matx44f::eye() * 8.0f;

    // AMFT 评估器初始化
    m_evaluator.init(cv::Rect2f(m_posX - m_targetW * 0.5f,
                                m_posY - m_targetH * 0.5f,
                                m_targetW, m_targetH));

    m_initialized = true;
    m_trackingState = AMFTTrackingState::TRACK_NORMAL;
    qCDebug(trackerLog) << "[ObjectTracker] 初始化成功, 模型尺寸:" << modelSize()
             << "目标:(" << m_posX << "," << m_posY << ") " << m_targetW << "x" << m_targetH
             << " 红外模式:" << m_infraredMode;
    return true;
}

// ========================
// 计算PSR（内核响应图版本，半径5圆形峰值抑制）
// ========================
float ObjectTracker::computePSR(const cv::Mat& response)
{
    if (response.empty()) return 0.0f;

    cv::Point maxLoc;
    double maxVal = 0.0;
    cv::minMaxLoc(response, nullptr, &maxVal, nullptr, &maxLoc);

    cv::Mat mask = cv::Mat::ones(response.size(), CV_8U);
    cv::circle(mask, maxLoc, 5, cv::Scalar(0), -1);

    cv::Scalar mean, stddev;
    cv::meanStdDev(response, mean, stddev, mask);
    const double sd = std::max(stddev[0], 1e-6);
    return static_cast<float>((maxVal - mean[0]) / sd);
}

// ========================
// 卡尔曼滤波预测
// ========================
void ObjectTracker::kfPredict()
{
    // NaN/溢出检测：如果状态或协方差已经发散，重置为有效位置
    for (int i = 0; i < 4; ++i) {
        if (std::isnan(m_kf.x[i]) || std::isinf(m_kf.x[i]) || std::abs(m_kf.x[i]) > 1e6f) {
            m_kf.x[i] = 0.0f;
            if (i == 0) m_kf.x[i] = m_posX;
            if (i == 1) m_kf.x[i] = m_posY;
        }
    }
    // P 矩阵保护：如果协方差矩阵有 NaN/Inf/超大数据，重置为初始值
    bool pDirty = false;
    for (int i = 0; i < 4 && !pDirty; ++i) {
        for (int j = 0; j < 4 && !pDirty; ++j) {
            if (std::isnan(m_kf.P(i, j)) || std::isinf(m_kf.P(i, j)) || std::abs(m_kf.P(i, j)) > 1e8f) {
                pDirty = true;
            }
        }
    }
    if (pDirty) {
        qWarning() << "[kfPredict] P矩阵异常，已重置为初始值";
        m_kf.P = cv::Matx44f::eye() * 8.0f;
    }

    // 状态转移矩阵 A = [1 0 T 0; 0 1 0 T; 0 0 1 0; 0 0 0 1], T=1
    const cv::Matx44f A(1.0f, 0.0f, 1.0f, 0.0f,
                        0.0f, 1.0f, 0.0f, 1.0f,
                        0.0f, 0.0f, 1.0f, 0.0f,
                        0.0f, 0.0f, 0.0f, 1.0f);

    m_kf.x = A * m_kf.x;
    const bool inLoss = (m_trackingState == AMFTTrackingState::TRACK_OCCLUDED ||
                         m_trackingState == AMFTTrackingState::TRACK_LOST);
    const cv::Matx44f& activeQ = inLoss ? m_kfQLost : m_kfQ;
    m_kf.P = A * m_kf.P * A.t() + activeQ;
}

// ========================
// 卡尔曼滤波更新
// ========================
void ObjectTracker::kfUpdate(float measX, float measY)
{
    // 测量值有效性检查：NaN或过大的测量值会导致状态发散
    if (std::isnan(measX) || std::isinf(measX)) measX = m_kf.x[0];
    if (std::isnan(measY) || std::isinf(measY)) measY = m_kf.x[1];

    // H = [1 0 0 0; 0 1 0 0]，S = H*P*H' + R
    const cv::Matx<float, 2, 4> H(1.0f, 0.0f, 0.0f, 0.0f,
                                  0.0f, 1.0f, 0.0f, 0.0f);
    cv::Matx22f S = H * m_kf.P * H.t() + m_kfR;

    // 防止S元素溢出（当P变得非常大时）
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 2; ++j) {
            if (std::isnan(S(i, j)) || std::isinf(S(i, j)) || std::abs(S(i, j)) > 1e10f) {
                S(i, j) = (i == j) ? m_kfR(i, j) : 0.0f;
            }
        }
    }

    // 分母epsilon防护除零
    float detS = S(0, 0) * S(1, 1) - S(0, 1) * S(1, 0);
    static constexpr float epsilon = 1e-10f;
    if (std::abs(detS) < epsilon) detS = (detS < 0) ? -epsilon : epsilon;

    // K = P*H'*inv(S)
    const cv::Matx22f Sinv(S(1, 1) / detS, -S(0, 1) / detS,
                           -S(1, 0) / detS, S(0, 0) / detS);
    const cv::Matx<float, 4, 2> K = m_kf.P * H.t() * Sinv;

    // 更新状态: x = x + K * (z - H * x)
    const cv::Vec2f z(measX, measY);
    m_kf.x += K * (z - H * m_kf.x);

    // 状态溢出保护：如果任何状态值非法，重置为安全值
    for (int i = 0; i < 4; ++i) {
        if (std::isnan(m_kf.x[i]) || std::isinf(m_kf.x[i]) || std::abs(m_kf.x[i]) > 1e6f) {
            m_kf.x[i] = 0.0f;
            if (i == 0) m_kf.x[i] = m_posX;
            if (i == 1) m_kf.x[i] = m_posY;
        }
    }

    // 更新协方差 P = (I - K*H)*P
    const cv::Matx44f I = cv::Matx44f::eye();
    m_kf.P = (I - K * H) * m_kf.P;

    // 协方差矩阵对称化 P = (P + P')/2，防止数值误差导致非对称
    m_kf.P = (m_kf.P + m_kf.P.t()) * 0.5f;

    // P矩阵溢出保护：如果协方差变得非常大，重置为初始值
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            if (std::isnan(m_kf.P(i, j)) || std::isinf(m_kf.P(i, j)) || std::abs(m_kf.P(i, j)) > 1e10f) {
                m_kf.P(i, j) = (i == j) ? 8.0f : 0.0f;
            }
        }
    }
}

// ========================
// 单次唤醒全局检测重捕（替换旧版九宫格盲试）
// ========================
bool ObjectTracker::tryDetectAndRecover(const cv::Mat& image)
{
    // 如果未外部设置检测器，则尝试懒加载默认模型
    if (!m_detector || !m_detector->isLoaded()) {
        const QString defaultModel = "models/yolov8n.onnx";
        if (QFile::exists(defaultModel)) {
            auto onnxDet = std::make_shared<OnnxDetector>();
            DetectorConfig cfg;
            cfg.useGpu = true;
            if (onnxDet->loadModel(defaultModel, cfg)) {
                m_detector = onnxDet;
                qCDebug(trackerLog) << "[Tracker] 自动载入默认检测器成功:" << defaultModel;
            }
        }
    }

    if (!m_detector || !m_detector->isLoaded()) {
        return false;
    }

    // 单次唤醒 YOLO 全图检测 (DirectML 仅耗时 3.06ms)
    std::vector<DetectionBox> detections = m_detector->detect(image);
    if (detections.empty()) {
        return false;
    }

    const float kfX = m_kf.x[0];
    const float kfY = m_kf.x[1];
    const float vx = m_kf.x[2];
    const float vy = m_kf.x[3];
    const float speed = std::sqrt(vx * vx + vy * vy);

    const float currentW = m_targetW * m_currentScale;
    const float currentH = m_targetH * m_currentScale;
    const float targetDiag = std::sqrt(currentW * currentW + currentH * currentH);

    // 根据目标尺寸与运动速度估计合理的最大搜索半径
    float maxRadius = std::max(targetDiag * 3.5f, speed * std::min(m_lostCount, 15) * 1.5f + 60.0f);
    maxRadius = std::min(maxRadius, static_cast<float>(std::max(image.cols, image.rows)) * 0.8f);

    float bestCandidateScore = 0.0f;
    DetectionBox bestBox;
    bool found = false;

    for (const auto& det : detections) {
        if (det.confidence < 0.25f) continue;

        float cx = static_cast<float>(det.rect.x() + det.rect.width() * 0.5f);
        float cy = static_cast<float>(det.rect.y() + det.rect.height() * 0.5f);

        float dist = std::sqrt((cx - kfX) * (cx - kfX) + (cy - kfY) * (cy - kfY));
        if (dist > maxRadius) continue;

        float detArea = static_cast<float>(det.rect.width() * det.rect.height());
        float origArea = std::max(1.0f, currentW * currentH);
        float areaRatio = detArea / origArea;
        if (areaRatio < 0.15f || areaRatio > 6.0f) continue;

        float distPenalty = 1.0f - 0.5f * (dist / maxRadius);
        float areaPenalty = std::min(areaRatio, 1.0f / areaRatio);
        float candidateScore = det.confidence * distPenalty * areaPenalty;

        if (candidateScore > bestCandidateScore) {
            bestCandidateScore = candidateScore;
            bestBox = det;
            found = true;
        }
    }

    if (!found || bestCandidateScore < 0.18f) {
        return false;
    }

    // 命中目标：以全新高精检测框重置 DsstCore 模板
    cv::Rect newRoi(static_cast<int>(std::round(bestBox.rect.x())),
                    static_cast<int>(std::round(bestBox.rect.y())),
                    static_cast<int>(std::round(bestBox.rect.width())),
                    static_cast<int>(std::round(bestBox.rect.height())));
    newRoi &= cv::Rect(0, 0, image.cols, image.rows);

    if (newRoi.width < 4 || newRoi.height < 4) {
        return false;
    }

    if (!m_core.init(image, newRoi)) {
        return false;
    }

    m_core.setLearningFrozen(false);
    m_posX = newRoi.x + newRoi.width * 0.5f;
    m_posY = newRoi.y + newRoi.height * 0.5f;
    m_targetW = static_cast<float>(newRoi.width);
    m_targetH = static_cast<float>(newRoi.height);
    m_currentScale = 1.0f;

    // 更新 KF 状态与 AMFT 评估器
    kfUpdate(m_posX, m_posY);
    m_evaluator.init(cv::Rect2f(m_posX - m_targetW * 0.5f,
                                m_posY - m_targetH * 0.5f,
                                m_targetW, m_targetH));

    return true;
}

// 跨线程帧处理槽函数（在 worker thread 中执行）
void ObjectTracker::processFrameSlot(const QImage& frame)
{
    try {
        TrackResult result = update(frame);
        emit trackingDone(result);
    } catch (const std::exception &e) {
        qCritical() << "[Tracker] processFrameSlot 异常:" << e.what();
        TrackResult err;
        err.valid = false;
        emit trackingDone(err);
    } catch (...) {
        qCritical() << "[Tracker] processFrameSlot 未知异常";
        TrackResult err;
        err.valid = false;
        emit trackingDone(err);
    }
}

// C9：异步初始化槽（在 worker thread 中执行）
void ObjectTracker::initSlot(const QImage& frame, const QRectF& targetRect)
{
    bool ok = false;
    try {
        ok = init(frame, targetRect);
    } catch (const std::exception &e) {
        qCritical() << "[Tracker] initSlot 异常:" << e.what();
        ok = false;
    } catch (...) {
        qCritical() << "[Tracker] initSlot 未知异常";
        ok = false;
    }
    emit initDone(ok, ok ? modelSize() : QString());
}

TrackResult ObjectTracker::update(const QImage& frame)
{
    QElapsedTimer timer;
    timer.start();

    TrackResult result;
    if (!m_initialized) {
        qWarning() << "[Tracker] update: 未初始化，直接返回";
        return result;
    }

    cv::Mat image = QImageToMat(frame);
    if (image.empty()) {
        qWarning() << "[Tracker] 图像转换失败，跳过当前帧";
        return result;
    }

    ++m_frameCount;
    const bool logThisFrame = (m_frameCount % 50 == 1);

    // === 1. KF 预测 ===
    kfPredict();
    const float kfPredX = m_kf.x[0];
    const float kfPredY = m_kf.x[1];

    // === 2. 状态机分流 ===
    if (m_trackingState == AMFTTrackingState::TRACK_OCCLUDED || m_trackingState == AMFTTrackingState::TRACK_LOST) {
        // --- 遮挡或丢失状态 ---
        m_core.setLearningFrozen(true);
        m_lostCount++;

        // 在 OCCLUDED 态每帧尝试单次重捕（至多持续 15 帧）
        // 在 LOST 态降频每 10 帧尝试一次 (3 FPS 待机检索，彻底杜绝算力浪费)
        bool shouldScan = (m_trackingState == AMFTTrackingState::TRACK_OCCLUDED)
                       || (m_lostCount % 10 == 0);

        bool recovered = false;
        if (shouldScan) {
            recovered = tryDetectAndRecover(image);
        }

        if (recovered) {
            // 命中！进入重捕恢复态
            m_trackingState = AMFTTrackingState::TRACK_REACQUIRED;
            m_lostCount = 0;
            result.recovered = true;
            result.occluded = false;
            emit trackingRecovered();
            emit trackingStateChanged(m_trackingState);
            qCDebug(trackerLog) << "[Tracker] AMFT 协同唤醒: YOLO 重捕成功! 目标位置=("
                                << m_posX << "," << m_posY << ") 模板已强制重置";
        } else {
            // 未重捕，保持 KF 速度外推预测
            m_posX = kfPredX;
            m_posY = kfPredY;
            result.occluded = true;
            result.recovered = false;

            if (m_lostCount >= 15 && m_trackingState != AMFTTrackingState::TRACK_LOST) {
                m_trackingState = AMFTTrackingState::TRACK_LOST;
                emit trackingLost();
                emit trackingStateChanged(m_trackingState);
                qCDebug(trackerLog) << "[Tracker] 连续 15 帧重捕未果，转入 TRACK_LOST 态势";
            }
        }
    } else {
        // --- 正常局部跟踪态 (TRACK_NORMAL) 或刚重捕态 (TRACK_REACQUIRED) ---
        // 铁律执行：如果前一帧是 TRACK_REACQUIRED，本帧强制回归 TRACK_NORMAL！
        if (m_trackingState == AMFTTrackingState::TRACK_REACQUIRED) {
            m_trackingState = AMFTTrackingState::TRACK_NORMAL;
            emit trackingStateChanged(m_trackingState);
        }

        // 检查变焦尺度纠偏
        if (m_needScaleCalibration) {
            m_needScaleCalibration = false;
            tryDetectAndRecover(image);
        }

        // 局部跟踪主循环：DsstCore 高频检测 + 尺度估计 + 模板更新 (<1ms)
        cv::Mat response;
        const cv::Rect bbox = m_core.update(image, &response);
        const bool detValid = (bbox.width > 0 && bbox.height > 0);
        const float psr = detValid ? computePSR(response) : 0.0f;
        result.psr = psr;

        float detX = m_posX;
        float detY = m_posY;
        if (detValid) {
            const cv::Point2f p = m_core.position();
            detX = (std::isnan(p.x) || std::isinf(p.x)) ? m_posX : p.x;
            detY = (std::isnan(p.y) || std::isinf(p.y)) ? m_posY : p.y;
            m_currentScale = m_core.currentScale();
        }

        // 构建当前目标框供 AMFT 评估
        const float currentW = m_targetW * m_currentScale;
        const float currentH = m_targetH * m_currentScale;
        cv::Rect2f evalBox(detX - currentW * 0.5f, detY - currentH * 0.5f, currentW, currentH);

        // === AMFT 三维特征评估 + 动态滑动阈值 ===
        m_lastScore = m_evaluator.evaluate(psr, detValid, evalBox, image);
        result.amftScore = m_lastScore.rTotal;
        result.amftTau = m_lastScore.dynamicTau;
        result.confidence = m_lastScore.rTotal;

        if (m_lastScore.isReliable) {
            // --- 跟踪稳健 (R_t >= tau_t) ---
            m_posX = detX;
            m_posY = detY;
            kfUpdate(m_posX, m_posY);
            m_lostCount = 0;
            result.occluded = false;
        } else {
            // --- 可靠度异常 (R_t < tau_t) ---
            m_lostCount++;
            qCDebug(trackerLog) << "[Tracker] AMFT 警告: R_t=" << m_lastScore.rTotal
                                << " < tau_t=" << m_lastScore.dynamicTau
                                << " (conf=" << m_lastScore.sConf
                                << ", mot=" << m_lastScore.sMot
                                << ", bg=" << m_lastScore.sBg << ")";

            if (m_lostCount >= 2) {
                // 确认进入遮挡/微失锁态
                m_trackingState = AMFTTrackingState::TRACK_OCCLUDED;
                m_core.setLearningFrozen(true);
                emit trackingLost();
                emit trackingStateChanged(m_trackingState);

                // 立即触发单次检测重捕
                if (tryDetectAndRecover(image)) {
                    m_trackingState = AMFTTrackingState::TRACK_REACQUIRED;
                    m_lostCount = 0;
                    result.recovered = true;
                    result.occluded = false;
                    emit trackingRecovered();
                    emit trackingStateChanged(m_trackingState);
                } else {
                    m_posX = kfPredX;
                    m_posY = kfPredY;
                    result.occluded = true;
                }
            } else {
                // 瞬时过渡态：暂用当前检测位置但不更新 KF 协方差
                m_posX = detValid ? detX : kfPredX;
                m_posY = detValid ? detY : kfPredY;
                result.occluded = false;
            }
        }

        if (logThisFrame) {
            qCDebug(trackerLog) << "[Tracker] frame=" << m_frameCount
                                << "state=" << static_cast<int>(m_trackingState)
                                << "R_t=" << m_lastScore.rTotal
                                << "tau_t=" << m_lastScore.dynamicTau
                                << "psr=" << psr
                                << "pos=(" << m_posX << "," << m_posY << ")"
                                << "scale=" << m_currentScale;
        }
    }

    // === 构建输出结果 ===
    const float currentW = m_targetW * m_currentScale;
    const float currentH = m_targetH * m_currentScale;
    result.bbox = QRectF(m_posX - currentW * 0.5f, m_posY - currentH * 0.5f,
                         currentW, currentH);
    result.valid = true;
    result.state = m_trackingState;
    result.fps = 1000.0f / std::max(1LL, timer.elapsed());

    // 提取 KF 状态
    result.kfX  = m_kf.x[0];
    result.kfY  = m_kf.x[1];
    result.kfVx = m_kf.x[2];
    result.kfVy = m_kf.x[3];

    return result;
}

// ========================
// 获取当前目标框
// ========================
QRectF ObjectTracker::getCurrentBBox() const
{
    const float currentW = m_targetW * m_currentScale;
    const float currentH = m_targetH * m_currentScale;
    return QRectF(m_posX - currentW * 0.5f, m_posY - currentH * 0.5f,
                  currentW, currentH);
}
