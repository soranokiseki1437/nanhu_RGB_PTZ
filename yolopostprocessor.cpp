#include "yolopostprocessor.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

static const std::vector<std::string> s_cocoNames = {
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat", "traffic light",
    "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat", "dog", "horse", "sheep", "cow",
    "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
    "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove", "skateboard", "surfboard",
    "tennis racket", "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple",
    "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch",
    "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse", "remote", "keyboard", "cell phone",
    "microwave", "oven", "toaster", "sink", "refrigerator", "book", "clock", "vase", "scissors", "teddy bear",
    "hair drier", "toothbrush"
};

const std::vector<std::string>& YoloPostProcessor::getCocoNames()
{
    return s_cocoNames;
}

cv::Mat YoloPostProcessor::letterbox(
    const cv::Mat& src,
    int netWidth, int netHeight,
    float& scale, float& padW, float& padH,
    const cv::Scalar& padColor)
{
    if (src.empty() || netWidth <= 0 || netHeight <= 0) {
        scale = 1.0f;
        padW = 0.0f;
        padH = 0.0f;
        return cv::Mat();
    }

    float scaleX = static_cast<float>(netWidth) / static_cast<float>(src.cols);
    float scaleY = static_cast<float>(netHeight) / static_cast<float>(src.rows);
    scale = std::min(scaleX, scaleY);

    int newW = static_cast<int>(std::round(src.cols * scale));
    int newH = static_cast<int>(std::round(src.rows * scale));

    padW = (netWidth - newW) * 0.5f;
    padH = (netHeight - newH) * 0.5f;

    cv::Mat resized;
    cv::resize(src, resized, cv::Size(newW, newH), 0, 0, cv::INTER_LINEAR);

    int top = static_cast<int>(std::round(padH - 0.1f));
    int bottom = netHeight - newH - top;
    int left = static_cast<int>(std::round(padW - 0.1f));
    int right = netWidth - newW - left;

    cv::Mat letterboxed;
    cv::copyMakeBorder(resized, letterboxed, top, bottom, left, right,
                       cv::BORDER_CONSTANT, padColor);

    return letterboxed;
}

std::vector<DetectionBox> YoloPostProcessor::process(
    const float* outputTensor,
    int tensorChannels,
    int tensorAnchors,
    int srcWidth, int srcHeight,
    float scale, float padW, float padH,
    float confThresh, float nmsThresh,
    const std::vector<std::string>& classNames)
{
    std::vector<DetectionBox> candidates;
    if (!outputTensor || tensorChannels < 5 || tensorAnchors <= 0 || scale <= 0.0f) {
        return candidates;
    }

    int numClasses = tensorChannels - 4;
    const std::vector<std::string>& labels = classNames.empty() ? s_cocoNames : classNames;

    candidates.reserve(128);

    // 标准 YOLOv8/v11 静态张量布局: [1, 4 + numClasses, anchors]
    // 通道 0: cx, 通道 1: cy, 通道 2: w, 通道 3: h, 通道 4~83: class scores
    for (int a = 0; a < tensorAnchors; ++a) {
        // 查找该锚点下的最高置信度类别
        float maxScore = -1.0f;
        int maxClassId = -1;

        for (int c = 0; c < numClasses; ++c) {
            float score = outputTensor[(4 + c) * tensorAnchors + a];
            if (score > maxScore) {
                maxScore = score;
                maxClassId = c;
            }
        }

        if (maxScore < confThresh) {
            continue;
        }

        // 读取 640x640 空间下的中心点与宽高
        float cx = outputTensor[0 * tensorAnchors + a];
        float cy = outputTensor[1 * tensorAnchors + a];
        float w  = outputTensor[2 * tensorAnchors + a];
        float h  = outputTensor[3 * tensorAnchors + a];

        // 逆映射回原图像绝对像素坐标
        float origCx = (cx - padW) / scale;
        float origCy = (cy - padH) / scale;
        float origW  = w / scale;
        float origH  = h / scale;

        float x = origCx - origW * 0.5f;
        float y = origCy - origH * 0.5f;

        // 边界裁剪 Clamp
        x = std::max(0.0f, std::min(x, static_cast<float>(srcWidth) - 1.0f));
        y = std::max(0.0f, std::min(y, static_cast<float>(srcHeight) - 1.0f));
        float right  = std::min(static_cast<float>(srcWidth), x + origW);
        float bottom = std::min(static_cast<float>(srcHeight), y + origH);

        float finalW = right - x;
        float finalH = bottom - y;

        if (finalW < 2.0f || finalH < 2.0f) {
            continue;
        }

        std::string labelName = (maxClassId >= 0 && maxClassId < static_cast<int>(labels.size()))
                                ? labels[maxClassId] : std::to_string(maxClassId);

        candidates.emplace_back(QRectF(x, y, finalW, finalH), maxScore, maxClassId, labelName);
    }

    std::vector<DetectionBox> finalBoxes;
    nonMaximumSuppression(candidates, nmsThresh, finalBoxes);
    return finalBoxes;
}

void YoloPostProcessor::nonMaximumSuppression(
    std::vector<DetectionBox>& candidates,
    float nmsThresh,
    std::vector<DetectionBox>& results)
{
    results.clear();
    if (candidates.empty()) {
        return;
    }

    // 按置信度降序排序
    std::sort(candidates.begin(), candidates.end(),
              [](const DetectionBox& a, const DetectionBox& b) {
                  return a.confidence > b.confidence;
              });

    std::vector<bool> suppressed(candidates.size(), false);

    for (size_t i = 0; i < candidates.size(); ++i) {
        if (suppressed[i]) {
            continue;
        }

        results.push_back(candidates[i]);

        for (size_t j = i + 1; j < candidates.size(); ++j) {
            if (suppressed[j]) {
                continue;
            }

            // 同类别间执行 IoU 抑制（class-specific NMS）
            if (candidates[i].classId == candidates[j].classId) {
                float iou = computeIoU(candidates[i].rect, candidates[j].rect);
                if (iou > nmsThresh) {
                    suppressed[j] = true;
                }
            }
        }
    }
}

float YoloPostProcessor::computeIoU(const QRectF& a, const QRectF& b)
{
    QRectF intersection = a.intersected(b);
    if (intersection.isEmpty()) {
        return 0.0f;
    }

    float interArea = static_cast<float>(intersection.width() * intersection.height());
    float unionArea = static_cast<float>(a.width() * a.height() + b.width() * b.height()) - interArea;

    if (unionArea <= 0.0f) {
        return 0.0f;
    }
    return interArea / unionArea;
}
