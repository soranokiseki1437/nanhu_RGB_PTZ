#ifndef OBJECTTRACKER_H
#define OBJECTTRACKER_H

#include <QObject>
#include <QRect>
#include <QImage>
#include <vector>
#include <memory>
#include <opencv2/core.hpp>
#include "dsstcore.h"
#include "amftevaluator.h"
#include "IDetector.h"

// 跟踪状态机枚举（海军工程大学 AMFT 四态闭环）
enum class AMFTTrackingState {
    TRACK_NORMAL = 0,     // 正常局部跟踪态（DsstCore 独占 30 FPS，YOLO 0 FPS 休眠）
    TRACK_OCCLUDED = 1,   // 疑似遮挡态（R_t < tau_t，单次触发 YOLO 全局扫描，保持 KF 速度外推）
    TRACK_REACQUIRED = 2, // 重捕恢复态（YOLO 检出目标，强制调用 DsstCore::init() 重置模板）
    TRACK_LOST = 3        // 彻底丢失态（连续 15 帧重捕未果，云台匀速搜索或停稳，低频 3 FPS 检索）
};

// 跟踪结果结构体
struct TrackResult {
    bool valid;           // 跟踪是否有效
    QRectF bbox;          // 目标框 (中心x, 中心y, 宽, 高)
    float confidence;     // 置信度 (AMFT 综合可靠度得分 R_t)
    float psr;            // DsstCore 原始 PSR 值
    bool occluded;        // 是否被遮挡
    bool recovered;       // 是否刚从遮挡恢复
    float fps;            // 处理帧率

    // KF内部状态（供伺服控制器使用）
    float kfX;            // KF滤波后的目标中心X（像素，平滑后）
    float kfY;            // KF滤波后的目标中心Y（像素，平滑后）
    float kfVx;           // KF估计的目标速度X（像素/帧）
    float kfVy;           // KF估计的目标速度Y（像素/帧）

    // AMFT 状态信息
    float amftScore;      // 综合可靠度得分 R_t
    float amftTau;        // 动态判定阈值 tau_t
    AMFTTrackingState state; // 当前状态机状态

    TrackResult()
        : valid(false), confidence(0.0f), psr(0.0f), occluded(false), recovered(false), fps(0.0f),
          kfX(0.0f), kfY(0.0f), kfVx(0.0f), kfVy(0.0f),
          amftScore(0.0f), amftTau(0.65f), state(AMFTTrackingState::TRACK_NORMAL) {}
};
Q_DECLARE_METATYPE(TrackResult)

/**
 * @brief 基于 AMFT 自适应多特征融合的检测-跟踪协同跟踪器
 * 
 * 架构：
 * 1. 局部高频内核：DsstCore (满速 30 FPS，单帧 < 1ms)；
 * 2. 状态与可靠度评估：AMFTEvaluator (三维融合 S_conf, S_mot, S_bg + 滑动窗口动态阈值 tau_t)；
 * 3. 全局重捕引擎：IDetector (零频休眠，异常时单次事件驱动唤醒，3.06ms 极速找回)；
 * 4. 彻底剔除旧版九宫格盲试，下一帧强制回归局部跟踪。
 */
class ObjectTracker : public QObject
{
    Q_OBJECT

public:
    explicit ObjectTracker(QObject *parent = nullptr);
    ~ObjectTracker();

    // 初始化跟踪器（第一帧 + 初始目标框）
    bool init(const QImage& frame, const QRectF& targetRect);

    // 处理新一帧，返回跟踪结果
    TrackResult update(const QImage& frame);

    // 重置跟踪器
    void reset();

    // 获取当前目标框
    QRectF getCurrentBBox() const;

    // 是否正在跟踪
    bool isTracking() const { return m_initialized; }

    // 红外模式（须在 init 前设置：改变内核特征通道数）
    void setInfraredMode(bool ir) { m_infraredMode = ir; }
    bool isInfraredMode() const { return m_infraredMode; }

    // 接入/获取深度学习检测器
    void setDetector(const std::shared_ptr<IDetector>& detector) { m_detector = detector; }
    std::shared_ptr<IDetector> detector() const { return m_detector; }

    // 状态与评分获取
    AMFTTrackingState trackingState() const { return m_trackingState; }
    AMFTScore lastAMFTScore() const { return m_lastScore; }

    // 调试辅助：内核模型尺寸 "宽x高"
    QString modelSize() const;

signals:
    void trackingLost();
    void trackingRecovered();
    void trackingStateChanged(AMFTTrackingState newState);
    void trackingDone(const TrackResult& result);      // 跨线程通知跟踪结果
    void initDone(bool ok, const QString& modelInfo);  // C9：异步初始化完成通知

public slots:
    // 跨线程帧处理入口（在 worker thread 中调用）
    void processFrameSlot(const QImage& frame);

    // C9：异步初始化入口（投递到 worker 线程执行，避免 init 卡 UI 线程）
    void initSlot(const QImage& frame, const QRectF& targetRect);

    // 变焦联动：触发尺度纠偏校准
    void triggerScaleCalibration();

private:
    // === DsstCore 内核 + 模式 ===
    DsstCore m_core;
    bool m_infraredMode = false;

    // === 外壳状态 ===
    bool m_initialized = false;

    // 目标状态（每帧从内核同步）
    float m_posX = 0.0f, m_posY = 0.0f;       // 目标中心位置
    float m_targetW = 0.0f, m_targetH = 0.0f; // 基础目标尺寸（未乘尺度）
    float m_currentScale = 1.0f;              // 当前尺度因子（来自内核）

    int m_frameCount = 0;                     // 用于日志节流（每100帧输出一次）

    // === 卡尔曼滤波相关 ===
    struct KFState {
        cv::Vec4f x;       // [px, py, vx, vy]
        cv::Matx44f P;     // 协方差
    } m_kf;

    cv::Matx44f m_kfQ;     // 过程噪声（正常模式）
    cv::Matx44f m_kfQLost; // 过程噪声（遮挡模式，更大不确定性）
    cv::Matx22f m_kfR;     // 观测噪声

    // === AMFT 与状态机协同 ===
    AMFTEvaluator m_evaluator;
    std::shared_ptr<IDetector> m_detector;
    AMFTTrackingState m_trackingState = AMFTTrackingState::TRACK_NORMAL;
    AMFTScore m_lastScore;
    int m_lostCount = 0;
    bool m_needScaleCalibration = false;

    // === 内部方法 ===
    // 计算PSR（吃内核响应图，半径5圆形峰值抑制）
    float computePSR(const cv::Mat& response);

    // 卡尔曼滤波预测
    void kfPredict();

    // 卡尔曼滤波更新（仅使用位置测量，速度由KF内部通过状态转移隐式估计）
    void kfUpdate(float measX, float measY);

    // 单次唤醒全局检测重捕（替换旧版九宫格盲试）
    bool tryDetectAndRecover(const cv::Mat& image);
};

#endif // OBJECTTRACKER_H
