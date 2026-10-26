#ifndef AMFTEVALUATOR_H
#define AMFTEVALUATOR_H

#include <opencv2/core.hpp>
#include <deque>
#include <vector>

/**
 * @brief AMFT 评分结构体（基于海军工程大学顶会论文三维特征融合）
 */
struct AMFTScore {
    float sConf = 0.0f;       // 维度 1: 跟踪置信度得分 [0.0, 1.0] (归一化 PSR)
    float sMot = 0.0f;        // 维度 2: 运动连续性得分 [0.0, 1.0] (结合位置重合度 IoU 与位移突变率偏差 D_t)
    float sBg = 0.0f;         // 维度 3: 背景抗干扰与可区分度得分 [0.0, 1.0] (目标与周围背景上下文分布差异)
    float rTotal = 0.0f;      // 综合可靠度 R_t = 0.45*sConf + 0.35*sMot + 0.20*sBg
    float dynamicTau = 0.65f; // 滑动窗口自适应动态阈值 tau_t = tau_0 - alpha * sigma_t
    bool isReliable = true;   // 判定结果: R_t >= tau_t
};

/**
 * @brief 自适应多特征融合 (AMFT) 评估器
 * 
 * 理论基础：海军工程大学 & 空军预警学院 2025 IEEE 顶会论文
 * 《Adaptive Multi-Feature Fusion-Based UAV Detection and Tracking Collaboration Framework Trigger Mechanism》
 *
 * 核心机制：
 * 1. 三维特征评估：跟踪置信度 S_conf + 运动平滑度 S_mot + 背景可区分度 S_bg；
 * 2. 滑动窗口动态阈值：维护 K=10 帧历史得分，根据其标准差动态调整判定阈值 tau_t；
 * 3. 稳态抑制瞬时抖动，突发遮挡/失锁快速响应。
 */
class AMFTEvaluator {
public:
    explicit AMFTEvaluator(int windowSize = 10, float tau0 = 0.65f, float alpha = 0.5f);
    ~AMFTEvaluator() = default;

    // 重置评估器状态
    void reset();

    // 初始化初始目标框
    void init(const cv::Rect2f& initBox);

    // 评估当前帧的跟踪可靠度
    AMFTScore evaluate(float psr, bool detValid, const cv::Rect2f& currentBox, const cv::Mat& image);

    // 参数调整
    void setWindowSize(int k) { m_windowSize = (k > 1 ? k : 1); }
    void setTau0(float tau0) { m_tau0 = tau0; }
    void setAlpha(float alpha) { m_alpha = alpha; }
    void setWeights(float wConf, float wMot, float wBg);

    // 获取当前动态阈值
    float currentTau() const { return m_lastTau; }

private:
    float computeSConf(float psr, bool detValid);
    float computeSMot(const cv::Rect2f& currentBox);
    float computeSBg(const cv::Rect2f& currentBox, const cv::Mat& image);
    float computeIoU(const cv::Rect2f& b1, const cv::Rect2f& b2) const;

private:
    int m_windowSize = 10;
    float m_tau0 = 0.65f;
    float m_alpha = 0.5f;

    // 特征加权系数（总和为 1.0）
    float m_wConf = 0.45f;
    float m_wMot = 0.35f;
    float m_wBg = 0.20f;

    // 历史状态缓存
    bool m_hasPrevBox = false;
    cv::Rect2f m_prevBox;
    std::deque<cv::Point2f> m_velHistory; // 最近 3 帧位移向量
    std::deque<float> m_rHistory;         // 最近 K 帧综合得分 R_t
    float m_lastTau = 0.65f;
};

#endif // AMFTEVALUATOR_H
