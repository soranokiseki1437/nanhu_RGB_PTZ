#ifndef YOLOPOSTPROCESSOR_H
#define YOLOPOSTPROCESSOR_H

#include "IDetector.h"
#include <vector>
#include <string>
#include <opencv2/core.hpp>

class YoloPostProcessor {
public:
    // 获取 COCO 80 类别默认名称列表
    static const std::vector<std::string>& getCocoNames();

    // 图像前处理：保持宽高比 Letterbox 缩放，空余部分填充 114
    static cv::Mat letterbox(
        const cv::Mat& src,
        int netWidth, int netHeight,
        float& scale, float& padW, float& padH,
        const cv::Scalar& padColor = cv::Scalar(114, 114, 114));

    // 后处理核心：解析模型原始输出张量 (1 x channels x anchors)，执行坐标还原与纯 C++ NMS
    // 适配 YOLOv8 / YOLO11 标准静态结构 [1, 4 + num_classes, 8400]
    static std::vector<DetectionBox> process(
        const float* outputTensor,
        int tensorChannels,      // 例如 84 (4 坐标 + 80 类别得分)
        int tensorAnchors,       // 例如 8400 (640x640 静态尺寸下的总锚点数)
        int srcWidth, int srcHeight,
        float scale, float padW, float padH,
        float confThresh, float nmsThresh,
        const std::vector<std::string>& classNames);

    // 纯 C++ NMS 实现（计算空间 IoU 并按置信度抑制）
    static void nonMaximumSuppression(
        std::vector<DetectionBox>& candidates,
        float nmsThresh,
        std::vector<DetectionBox>& results);

    // 计算两矩形 IoU (Intersection over Union)
    static float computeIoU(const QRectF& a, const QRectF& b);
};

#endif // YOLOPOSTPROCESSOR_H
