#ifndef IDETECTOR_H
#define IDETECTOR_H

#include <QString>
#include <QRectF>
#include <vector>
#include <string>
#include <opencv2/core.hpp>

// 单个目标检测框
struct DetectionBox {
    QRectF rect;           // 目标边界框（图像绝对像素坐标：x, y, w, h）
    float confidence;      // 检测置信度 (0.0f ~ 1.0f)
    int classId;           // 目标类别索引
    std::string className; // 目标类别标签名称

    DetectionBox() : rect(0, 0, 0, 0), confidence(0.0f), classId(-1) {}
    DetectionBox(const QRectF& r, float conf, int cls, const std::string& name = "")
        : rect(r), confidence(conf), classId(cls), className(name) {}
};

// 检测器配置
struct DetectorConfig {
    int inputWidth = 640;                  // 模型输入宽（严格静态尺寸）
    int inputHeight = 640;                 // 模型输入高（严格静态尺寸）
    float confThreshold = 0.25f;           // 置信度筛选阈值
    float nmsThreshold = 0.45f;            // NMS 抑制阈值
    bool useGpu = true;                    // 是否优先启用 DirectML GPU 硬件加速
    int deviceId = 0;                      // GPU 设备索引
    std::vector<std::string> classNames;   // 类别映射表 (默认为 COCO 80 类)
};

// 抽象检测器接口（纯虚基类，完全解耦 Windows ONNXRuntime 与未来 RK3588 NPU）
class IDetector {
public:
    virtual ~IDetector() = default;

    virtual bool loadModel(const QString& modelPath, const DetectorConfig& config = DetectorConfig()) = 0;
    virtual bool isLoaded() const = 0;
    virtual std::vector<DetectionBox> detect(const cv::Mat& frame) = 0;
    virtual float getInferenceTimeMs() const = 0;
    virtual cv::Size getInputSize() const = 0;
};

#endif // IDETECTOR_H
