#ifndef ISOTTRACKER_H
#define ISOTTRACKER_H

#include <opencv2/core.hpp>
#include <QString>

/**
 * @brief SOT 跟踪器内部调试信息（与 Python 参考逐帧对齐用）
 */
struct SotDebugInfo {
    int instanceSize = 0;                 // 256 / 288
    int cropSz = 0, cropXmin = 0, cropYmin = 0;
    int argRow = -1, argCol = -1;         // pscore 峰值位置
    float clsMax = 0.0f;                  // sigmoid(cls) 全图最大值
};

/**
 * @brief SOT 跟踪单帧输出结果
 */
struct SotResult {
    bool valid = false;
    cv::Point2f center;                   // 原图像素，已钳制 x∈[0,W], y∈[0,H]
    cv::Size2f size;                      // 原图像素，已钳制 w∈[10,W], h∈[10,H]
    cv::Rect2f bbox;                      // 评测输出框：(max(0,cx-w/2), max(0,cy-h/2), w, h)
    float score = 0.0f;                   // sigmoid(cls) 在峰值位置的值，[0,1]
    SotDebugInfo dbg;
};

/**
 * @brief 单目标跟踪器通用抽象接口
 *        解耦算法实现（DSST, LightTrack ONNX 等）与业务状态机
 */
class ISotTracker {
public:
    virtual ~ISotTracker() = default;

    // 幂等加载；首次调用加载模型与初始化运行时
    virtual bool ensureLoaded() = 0;
    virtual bool isLoaded() const = 0;

    // 首帧或重捕初始化（输入图像为 BGR CV_8UC3 格式）
    virtual bool init(const cv::Mat& bgr, const cv::Rect2f& bbox) = 0;

    // 推进跟踪状态
    virtual SotResult track(const cv::Mat& bgr) = 0;

    // 以给定候选中心执行一次前向探针评估，不修改内部跟踪状态
    virtual SotResult peek(const cv::Mat& bgr, cv::Point2f center) = 0;

    // 重置目标位置与尺寸（保留已提取模板特征）
    virtual void setState(cv::Point2f center, cv::Size2f size) = 0;

    // 是否已完成初始化
    virtual bool isInitialized() const = 0;

    // 最近一帧模型纯前向时延 (T1, ms)
    virtual float lastModelMs() const = 0;

    // 最近一帧跟踪总时延 (T2, ms, 含前处理、前向与解码)
    virtual float lastTotalMs() const = 0;

    // 跟踪器名称标识
    virtual QString name() const = 0;
};

#endif // ISOTTRACKER_H

