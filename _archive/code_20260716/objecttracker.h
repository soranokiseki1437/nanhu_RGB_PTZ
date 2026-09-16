#ifndef OBJECTTRACKER_H
#define OBJECTTRACKER_H

#include <QObject>
#include <QRect>
#include <QImage>
#include <vector>
#include <complex>
#include "fftutils.h"

// 跟踪结果结构体
struct TrackResult {
    bool valid;           // 跟踪是否有效
    QRectF bbox;          // 目标框 (中心x, 中心y, 宽, 高)
    float confidence;     // 置信度 (PSR值)
    bool occluded;        // 是否被遮挡
    bool recovered;       // 是否刚从遮挡恢复
    float fps;            // 处理帧率

    // KF内部状态（供伺服控制器使用）
    float kfX;            // KF滤波后的目标中心X（像素，平滑后）
    float kfY;            // KF滤波后的目标中心Y（像素，平滑后）
    float kfVx;           // KF估计的目标速度X（像素/帧）
    float kfVy;           // KF估计的目标速度Y（像素/帧）

    TrackResult()
        : valid(false), confidence(0.0f), occluded(false), recovered(false), fps(0.0f),
          kfX(0.0f), kfY(0.0f), kfVx(0.0f), kfVy(0.0f) {}
};
Q_DECLARE_METATYPE(TrackResult)

// DSST + KF 目标跟踪器
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

    // 获取当前目标位置
    QRectF getCurrentBBox() const;

    // 是否正在跟踪
    bool isTracking() const { return m_initialized; }

    // 设置参数
    void setPadding(float padding) { m_padding = padding; }
    void setLearningRate(float lr) { m_learningRate = lr; }
    void setLambda(float lambda) { m_lambda = lambda; }

    // 调试辅助
    QString modelSize() const { return QString("%1x%2").arg(m_modelW).arg(m_modelH); }
    QString scaleModelSize() const { return QString("%1x%2").arg(m_scaleModelW).arg(m_scaleModelH); }

signals:
    void trackingLost();
    void trackingRecovered();
    void trackingDone(const TrackResult& result);  // 跨线程通知跟踪结果

public slots:
    // 跨线程帧处理入口（在 worker thread 中调用）
    void processFrameSlot(const QImage& frame);

private:
    // === 核心DSST相关 ===
    bool m_initialized;

    // 目标状态
    float m_posX, m_posY;           // 目标中心位置
    float m_targetW, m_targetH;     // 基础目标尺寸
    float m_currentScale;           // 当前尺度因子

    // 模型尺寸（含padding）
    int m_modelW, m_modelH;

    // 滤波器模型
    std::vector<std::vector<std::complex<float>>> m_hfNum;  // 平移滤波器分子
    std::vector<std::vector<float>> m_hfDen;                // 平移滤波器分母
    std::vector<std::vector<std::complex<float>>> m_hfNumBackup;
    std::vector<std::vector<float>> m_hfDenBackup;

    // 余弦窗
    std::vector<std::vector<float>> m_cosWindow;

    // 高斯标签
    std::vector<std::vector<std::complex<float>>> m_yf;

    // === 卡尔曼滤波相关 ===
    struct KFState {
        float x[4];     // [px, py, vx, vy]
        float P[4][4];  // 协方差
    } m_kf;

    float m_kfQ[4][4];  // 过程噪声（正常模式）
    float m_kfQLost[4][4]; // 过程噪声（遮挡模式，更大不确定性）
    float m_kfR[2][2];  // 观测噪声

    // === 遮挡检测相关 ===
    bool m_inOcclusion;
    int m_lostCount;
    float m_psrBaseline;
    bool m_baselineLocked;
    std::vector<float> m_psrHistory;

    // === 尺度估计（FHOG特征 + 33尺度滤波器） ===
    bool m_scaleEnabled;                    // 是否启用尺度估计
    int m_scaleModelW, m_scaleModelH;       // 尺度模型尺寸（基于目标大小缩放）
    std::vector<float> m_scaleFactors;      // 33个尺度因子 [1.02^16, 1.02^15, ..., 1.02^-16]
    std::vector<float> m_scaleWindow;       // 尺度Hann窗 (33维)
    std::vector<std::vector<std::complex<float>>> m_sfNum;   // 尺度滤波器分子 (通道×尺度，频域)
    std::vector<std::vector<float>> m_sfDen;    // 尺度滤波器分母 (31通道 × 33尺度)
    std::vector<std::vector<std::complex<float>>> m_sfNumBackup;  // 备份（遮挡时使用）
    std::vector<std::vector<float>> m_sfDenBackup;
    std::vector<std::complex<float>> m_ysf;     // 尺度高斯标签的FFT (33维)

    FHOGConfig m_fhogConfig;               // FHOG配置

    // === 参数 ===
    float m_padding;
    float m_outputSigmaFactor;
    float m_lambda;
    float m_learningRate;
    float m_scaleStep;
    int m_numScales;
    float m_minScaleFactor;
    float m_maxScaleFactor;

    // === 内部方法 ===
    // QImage转灰度矩阵
    std::vector<std::vector<float>> imageToGray(const QImage& img);

    // 提取平移样本
    std::vector<std::vector<float>> getTranslationSample(
        const std::vector<std::vector<float>>& image,
        float cx, float cy, float scale);

    // 计算响应图
    std::vector<std::vector<float>> computeResponse(
        const std::vector<std::vector<float>>& sample);

    // 计算PSR
    float computePSR(const std::vector<std::vector<float>>& response);

    // 更新滤波器（adaptAppearance=true时使用加速学习率）
    void updateFilter(const std::vector<std::vector<float>>& sample,
                      bool adaptAppearance = false);

    // 卡尔曼滤波预测
    void kfPredict();

    // 卡尔曼滤波更新（仅使用位置测量，速度由KF内部通过状态转移隐式估计
    void kfUpdate(float measX, float measY);

    // 生成高斯标签
    std::vector<std::vector<std::complex<float>>> createGaussianLabel(
        int w, int h, float sigma);

    // 生成余弦窗
    std::vector<std::vector<float>> createCosWindow(int w, int h);

    // 矩阵工具
    float findMaxResponse(const std::vector<std::vector<float>>& response, int& row, int& col);

    // 遮挡重检测（传入上一帧位置用于KF速度计算）
    bool reDetect(const std::vector<std::vector<float>>& image,
                  float prevPosX = 0.0f, float prevPosY = 0.0f);

    // === 尺度估计内部方法 ===
    // 提取尺度样本（33个尺度的FHOG特征矩阵）
    std::vector<std::vector<float>> getScaleSample(
        const std::vector<std::vector<float>>& image,
        float cx, float cy);

    // 初始化尺度滤波器（首帧调用）
    void initScaleFilter(const std::vector<std::vector<float>>& scaleSample);

    // 计算尺度响应，返回最佳尺度索引
    int computeScaleResponse(const std::vector<std::vector<float>>& scaleSample);

    // 更新尺度滤波器
    void updateScaleFilter(const std::vector<std::vector<float>>& scaleSample);
};

#endif // OBJECTTRACKER_H
