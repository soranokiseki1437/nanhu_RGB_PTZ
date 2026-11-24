#ifndef ONNXSOTTRACKER_H
#define ONNXSOTTRACKER_H

#include "ISotTracker.h"
#include <QString>
#include <vector>
#include <memory>
#include <mutex>
#include <opencv2/core.hpp>

// 前置声明 ONNX Runtime C-API 结构体
struct OrtApi;
struct OrtEnv;
struct OrtSession;
struct OrtSessionOptions;
struct OrtMemoryInfo;
struct OrtAllocator;

/**
 * @brief SOT 跟踪器配置参数
 */
struct SotTrackerConfig {
    QString templateModel     = "models/lighttrack_template.onnx";
    QString searchModelSmall  = "models/lighttrack_search_256.onnx";
    QString searchModelBig    = "models/lighttrack_search_288.onnx";
    bool  useGpu = true;
    int   deviceId = 0;
    bool  strictGpu = false;                // true：禁止 CPU 回退（session.disable_cpu_ep_fallback=1）
    float penaltyK = 0.15f;
    float windowInfluence = 0.45f;
    float lr = 0.35f;
    float contextAmount = 0.5f;
    float bigSzAreaRatio = 0.004f;
};

/**
 * @brief 基于微软官方 ONNX Runtime (C-API) 与 DirectML 加速的 LightTrack-M 轻量深度 SOT 跟踪器
 *        面向 Windows MinGW 64-bit 环境，无缝调用 NVIDIA RTX 5070 GPU 硬件加速；
 *        在 GPU 不可用时自动降级回退 CPU 多线程推理。
 */
class OnnxSotTracker : public ISotTracker {
public:
    explicit OnnxSotTracker(const SotTrackerConfig& cfg = SotTrackerConfig());
    ~OnnxSotTracker() override;

    bool loadModel(const SotTrackerConfig& cfg);   // 显式加载（离线工具或指定路径）
    bool isUsingGpu() const { return m_usingGpu; }

    // ISotTracker 接口实现
    bool ensureLoaded() override;
    bool isLoaded() const override;
    bool init(const cv::Mat& bgr, const cv::Rect2f& bbox) override;
    SotResult track(const cv::Mat& bgr) override;
    SotResult peek(const cv::Mat& bgr, cv::Point2f center) override;
    SotResult peekBox(const cv::Mat& bgr, const cv::Rect2f& bbox, bool useAnchorTemplate = true) override;
    void setState(cv::Point2f center, cv::Size2f size) override;
    bool isInitialized() const override { return m_initialized; }
    float lastModelMs() const override { return m_lastModelMs; }
    float lastTotalMs() const override { return m_lastTotalMs; }
    QString name() const override { return QStringLiteral("LightTrack-ONNX"); }

private:
    struct SearchNet {
        OrtSession* session = nullptr;
        int inSize = 0;
        int scoreSize = 0;
        char* inName0 = nullptr;
        char* inName1 = nullptr;
        char* outName0 = nullptr;
        char* outName1 = nullptr;
        std::vector<float> gridX;
        std::vector<float> gridY;
        std::vector<float> window;
    };

    void releaseAll();
    bool initializeTemplateSession(const QString& modelPath);
    bool initializeSearchSession(SearchNet& net, const QString& modelPath, int expectedInSize, int expectedScoreSize);
    SotResult forwardSearch(const cv::Mat& bgr, cv::Point2d pos, cv::Size2d sz, bool updateState,
                            const std::vector<float>* overrideTemplateFeat = nullptr);

    // 辅助前处理与几何计算
    static cv::Mat cropPatch(const cv::Mat& rgb, cv::Point2d pos, int patchSz, int modelSz,
                             const cv::Scalar& avgChans, int& outCropSz, int& outCropXmin, int& outCropYmin);
    static void toTensorNCHW(const cv::Mat& patch, float* outBuffer);

private:
    mutable std::recursive_mutex m_mutex;
    SotTrackerConfig m_config;

    const OrtApi* m_api = nullptr;
    OrtEnv* m_env = nullptr;
    OrtSessionOptions* m_sessionOptions = nullptr;
    OrtMemoryInfo* m_memoryInfo = nullptr;

    // 模板 Session
    OrtSession* m_tmplSession = nullptr;
    char* m_tmplInName = nullptr;
    char* m_tmplOutName = nullptr;

    // 搜索 Sessions (256 与 288)
    SearchNet m_small;
    SearchNet m_big;
    SearchNet* m_active = nullptr;

    // 模板特征缓存 [1, 96, 8, 8] -> 6144 float（init 时同步固化原始锚点特征）
    std::vector<float> m_templateFeat;
    std::vector<float> m_anchorTemplateFeat;

    // 内部连续跟踪状态（统一使用 double 保证累加无精度损失）
    bool m_loaded = false;
    bool m_usingGpu = false;
    bool m_initialized = false;
    cv::Point2d m_pos;
    cv::Size2d m_sz;
    cv::Scalar m_avgChans;
    int m_imW = 0;
    int m_imH = 0;

    float m_lastModelMs = 0.0f;
    float m_lastTotalMs = 0.0f;
};

#endif // ONNXSOTTRACKER_H

