#ifndef ONNXDETECTOR_H
#define ONNXDETECTOR_H

#include "IDetector.h"
#include <QString>
#include <vector>
#include <memory>
#include <mutex>

// 前置声明 ONNX Runtime C-API 结构体
struct OrtApi;
struct OrtEnv;
struct OrtSession;
struct OrtSessionOptions;
struct OrtMemoryInfo;

/**
 * @brief 基于微软官方 ONNX Runtime (C-API) 与 DirectML 的深度学习目标检测器
 *        面向 Windows MinGW 64-bit 环境，无缝调用 NVIDIA RTX 5070 GPU 硬件加速；
 *        在 GPU 不可用时自动降级回退 CPU 多线程推理。
 */
class OnnxDetector : public IDetector {
public:
    OnnxDetector();
    ~OnnxDetector() override;

    bool loadModel(const QString& modelPath, const DetectorConfig& config = DetectorConfig()) override;
    bool isLoaded() const override;
    std::vector<DetectionBox> detect(const cv::Mat& frame) override;
    float getInferenceTimeMs() const override;
    cv::Size getInputSize() const override;

    // 是否正在使用 DirectML GPU 加速
    bool isUsingGpu() const { return m_usingGpu; }

private:
    void release();
    bool initializeSession(const QString& modelPath, const DetectorConfig& config);

private:
    const OrtApi* m_api = nullptr;
    OrtEnv* m_env = nullptr;
    OrtSession* m_session = nullptr;
    OrtSessionOptions* m_sessionOptions = nullptr;
    OrtMemoryInfo* m_memoryInfo = nullptr;

    bool m_isLoaded = false;
    bool m_usingGpu = false;
    DetectorConfig m_config;
    float m_lastInferenceTimeMs = 0.0f;

    // 模型输入/输出节点信息
    char* m_inputName = nullptr;
    char* m_outputName = nullptr;
    int m_inputWidth = 640;
    int m_inputHeight = 640;
    int m_numChannels = 3;
    int m_tensorChannels = 84;
    int m_tensorAnchors = 8400;

    // 预分配的连续输入张量内存 (NCHW float)
    std::vector<float> m_inputTensorValues;
    mutable std::mutex m_detectMutex;
};

#endif // ONNXDETECTOR_H
