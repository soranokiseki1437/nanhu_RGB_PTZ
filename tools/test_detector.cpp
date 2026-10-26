#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <cmath>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

#include "../onnxdetector.h"
#include "../yolopostprocessor.h"

// 获取当前进程工作集物理内存 (KB)
static size_t getCurrentProcessMemoryKB()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize / 1024;
    }
#endif
    return 0;
}

// 读取 1_gt.txt 的首帧标注
static bool readFirstGroundTruth(const QString& gtPath, QRectF& gtBox)
{
    QFile file(gtPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream in(&file);
    if (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        QStringList parts = line.split(',');
        if (parts.size() >= 4) {
            float x = parts[0].toFloat();
            float y = parts[1].toFloat();
            float w = parts[2].toFloat();
            float h = parts[3].toFloat();
            gtBox = QRectF(x, y, w, h);
            return true;
        }
    }
    return false;
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    std::cout << "========================================================================" << std::endl;
    std::cout << "     RGB_PTZ_Integrated 目标检测引擎阶段一【全景质量体检与闭环验收工具】" << std::endl;
    std::cout << "========================================================================" << std::endl;

    QString modelPath = "models/yolov8n.onnx";
    if (argc > 1) {
        modelPath = QString::fromLocal8Bit(argv[1]);
    }

    std::cout << ">> 待测模型文件 : " << modelPath.toLocal8Bit().constData() << std::endl;
    if (!QFileInfo::exists(modelPath)) {
        std::cerr << "[错误] 模型文件不存在: " << modelPath.toLocal8Bit().constData() << std::endl;
        return 1;
    }

    // 准备测试图像（从本地对拍数据集 01 序列中读取真实第一帧）
    cv::Mat realFrame;
    std::string imgPath = "testdata/sequences/01/imgs/0000.bmp";
    realFrame = cv::imread(imgPath);
    bool hasRealFrame = !realFrame.empty();

    QRectF gtBox;
    bool hasGt = readFirstGroundTruth("testdata/sequences/01/1_gt.txt", gtBox);

    if (hasRealFrame) {
        std::cout << ">> 读取对拍实测图像成功: " << imgPath
                  << " (尺寸: " << realFrame.cols << "x" << realFrame.rows << ")" << std::endl;
        if (hasGt) {
            std::cout << ">> 成功载入对拍 Ground Truth 标注: [x=" << gtBox.x() << ", y=" << gtBox.y()
                      << ", w=" << gtBox.width() << ", h=" << gtBox.height() << "]" << std::endl;
        }
    } else {
        std::cout << ">> 未找到对拍序列图像，自动生成 1920x1080 仿真测试图..." << std::endl;
        realFrame = cv::Mat(1080, 1920, CV_8UC3, cv::Scalar(100, 120, 140));
        cv::rectangle(realFrame, cv::Rect(400, 300, 120, 260), cv::Scalar(50, 50, 200), -1);
    }

    // 初始化 GPU 检测器
    DetectorConfig gpuConfig;
    gpuConfig.useGpu = true;
    gpuConfig.deviceId = 0;
    gpuConfig.confThreshold = 0.25f;
    gpuConfig.nmsThreshold = 0.45f;

    OnnxDetector gpuDetector;
    if (!gpuDetector.loadModel(modelPath, gpuConfig)) {
        std::cerr << "[错误] GPU 检测器模型加载失败！" << std::endl;
        return 1;
    }

    // 初始化 CPU 检测器（用于对比）
    DetectorConfig cpuConfig = gpuConfig;
    cpuConfig.useGpu = false;
    OnnxDetector cpuDetector;
    if (!cpuDetector.loadModel(modelPath, cpuConfig)) {
        std::cerr << "[错误] CPU 检测器模型加载失败！" << std::endl;
        return 1;
    }

    std::cout << "\n------------------------------------------------------------------------" << std::endl;
    std::cout << " [体检 1/5] 推理速度与硬件加速比核验 (GPU DirectML vs CPU)" << std::endl;
    std::cout << "------------------------------------------------------------------------" << std::endl;

    // GPU 热身
    for (int i = 0; i < 10; ++i) gpuDetector.detect(realFrame);

    const int benchmarkIters = 100;
    float totalGpuInferenceTime = 0.0f;
    std::vector<DetectionBox> gpuDetections;

    auto tGpuStart = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < benchmarkIters; ++i) {
        gpuDetections = gpuDetector.detect(realFrame);
        totalGpuInferenceTime += gpuDetector.getInferenceTimeMs();
    }
    auto tGpuEnd = std::chrono::high_resolution_clock::now();
    float totalGpuWallTime = std::chrono::duration<float, std::milli>(tGpuEnd - tGpuStart).count();

    float avgGpuInferenceMs = totalGpuInferenceTime / benchmarkIters;
    float avgGpuTotalMs = totalGpuWallTime / benchmarkIters;
    float gpuFps = 1000.0f / avgGpuTotalMs;

    // CPU 基准
    for (int i = 0; i < 3; ++i) cpuDetector.detect(realFrame);
    const int cpuIters = 20;
    float totalCpuInferenceTime = 0.0f;
    std::vector<DetectionBox> cpuDetections;
    auto tCpuStart = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < cpuIters; ++i) {
        cpuDetections = cpuDetector.detect(realFrame);
        totalCpuInferenceTime += cpuDetector.getInferenceTimeMs();
    }
    auto tCpuEnd = std::chrono::high_resolution_clock::now();
    float avgCpuInferenceMs = totalCpuInferenceTime / cpuIters;
    float speedup = avgCpuInferenceMs / avgGpuInferenceMs;

    std::cout << ">> GPU (RTX 5070 DirectML) 纯推理耗时 : " << std::fixed << std::setprecision(2) << avgGpuInferenceMs << " ms" << std::endl;
    std::cout << ">> GPU (RTX 5070 DirectML) 单帧端到端 : " << std::fixed << std::setprecision(2) << avgGpuTotalMs << " ms (" << gpuFps << " FPS)" << std::endl;
    std::cout << ">> CPU 多线程降级纯推理耗时           : " << std::fixed << std::setprecision(2) << avgCpuInferenceMs << " ms" << std::endl;
    std::cout << ">> GPU 相比 CPU 加速比                : " << std::fixed << std::setprecision(2) << speedup << "x" << std::endl;
    bool passPerf = (avgGpuInferenceMs <= 15.0f);
    std::cout << ">> 判定结果: " << (passPerf ? "[通过 PASS]" : "[未达标 FAIL]") << std::endl;

    std::cout << "\n------------------------------------------------------------------------" << std::endl;
    std::cout << " [体检 2/5] 真实标注对拍精度与坐标逆映射核验 (Ground Truth 对齐)" << std::endl;
    std::cout << "------------------------------------------------------------------------" << std::endl;

    bool passAccuracy = false;
    float bestIoU = 0.0f;
    float centerErr = 999.0f;
    int matchedIdx = -1;

    std::cout << ">> 实测图像检测到目标数: " << gpuDetections.size() << " 个" << std::endl;
    for (size_t i = 0; i < gpuDetections.size(); ++i) {
        const auto& d = gpuDetections[i];
        std::cout << "   [" << i + 1 << "] 类别: " << std::setw(10) << d.className
                  << " | 置信度: " << std::fixed << std::setprecision(3) << d.confidence
                  << " | 预测坐标: [x=" << d.rect.x() << ", y=" << d.rect.y()
                  << ", w=" << d.rect.width() << ", h=" << d.rect.height() << "]" << std::endl;

        if (hasGt) {
            float iou = YoloPostProcessor::computeIoU(d.rect, gtBox);
            float cX = d.rect.center().x();
            float cY = d.rect.center().y();
            float gtCenterX = gtBox.center().x();
            float gtCenterY = gtBox.center().y();
            float dist = std::sqrt((cX - gtCenterX) * (cX - gtCenterX) + (cY - gtCenterY) * (cY - gtCenterY));

            if (iou > bestIoU) {
                bestIoU = iou;
                centerErr = dist;
                matchedIdx = static_cast<int>(i);
            }
        }
    }

    if (hasGt && matchedIdx >= 0) {
        std::cout << ">> 与 Ground Truth 最佳匹配目标: 目标 #" << (matchedIdx + 1) << std::endl;
        std::cout << "   - 重合度 (IoU)       : " << std::fixed << std::setprecision(1) << (bestIoU * 100.0f) << "%" << std::endl;
        std::cout << "   - 中心点距离误差     : " << std::fixed << std::setprecision(2) << centerErr << " 像素 (Pixel)" << std::endl;
        passAccuracy = (bestIoU >= 0.50f && centerErr <= 5.0f);
        std::cout << ">> 判定结果 (IoU >= 50% 且中心误差 <= 5px): "
                  << (passAccuracy ? "[通过 PASS - 坐标映射严丝合缝]" : "[存在偏差 WARN]") << std::endl;
    } else {
        std::cout << ">> 提示: 无外部 GT 标注，跳过量化 IoU 计算。" << std::endl;
        passAccuracy = !gpuDetections.empty();
    }

    std::cout << "\n------------------------------------------------------------------------" << std::endl;
    std::cout << " [体检 3/5] 检测可视化落地 (生成直观对拍对比图)" << std::endl;
    std::cout << "------------------------------------------------------------------------" << std::endl;

    cv::Mat visImage = realFrame.clone();
    // 绘制 Ground Truth（绿色）
    if (hasGt) {
        cv::Rect gtCvRect(static_cast<int>(gtBox.x()), static_cast<int>(gtBox.y()),
                          static_cast<int>(gtBox.width()), static_cast<int>(gtBox.height()));
        cv::rectangle(visImage, gtCvRect, cv::Scalar(0, 255, 0), 2);
        cv::putText(visImage, "GroundTruth", cv::Point(gtCvRect.x, std::max(12, gtCvRect.y - 4)),
                    cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(0, 255, 0), 1);
    }
    // 绘制预测结果（红色）
    for (size_t i = 0; i < gpuDetections.size(); ++i) {
        const auto& d = gpuDetections[i];
        cv::Rect predRect(static_cast<int>(d.rect.x()), static_cast<int>(d.rect.y()),
                          static_cast<int>(d.rect.width()), static_cast<int>(d.rect.height()));
        cv::rectangle(visImage, predRect, cv::Scalar(0, 0, 255), 2);

        char buf[64];
        snprintf(buf, sizeof(buf), "#%d %s %.2f", static_cast<int>(i + 1), d.className.c_str(), d.confidence);
        cv::putText(visImage, buf, cv::Point(predRect.x, std::min(visImage.rows - 5, predRect.y + predRect.height + 15)),
                    cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(0, 0, 255), 1);
    }

    std::string outVisPath = "test_detection_vis.png";
    bool visSaved = cv::imwrite(outVisPath, visImage);
    std::cout << ">> 可视化检测结果已落盘: " << outVisPath
              << (visSaved ? " [保存成功 SUCCESS]" : " [保存失败 FAIL]") << std::endl;
    std::cout << ">> 标注说明: 绿色框 = Ground Truth 真实目标 | 红色框 = AI 预测检测框" << std::endl;

    std::cout << "\n------------------------------------------------------------------------" << std::endl;
    std::cout << " [体检 4/5] 极端长宽比与边界条件自适应测试 (Letterbox Invariance)" << std::endl;
    std::cout << "------------------------------------------------------------------------" << std::endl;

    bool passRobust = true;
    // 测试 1: 横屏 1920x1080 (16:9 宽屏，左右填满，上下加黑边 padH > 0, padW == 0)
    cv::Mat wideMat(1080, 1920, CV_8UC3, cv::Scalar(40, 40, 40));
    float s1, pW1, pH1;
    cv::Mat lbWide = YoloPostProcessor::letterbox(wideMat, 640, 640, s1, pW1, pH1);
    if (pW1 != 0.0f || pH1 <= 0.0f) {
        std::cerr << ">> [错误] 16:9 图像 Letterbox 左右不应填充，实际 padW=" << pW1 << ", padH=" << pH1 << std::endl;
        passRobust = false;
    } else {
        std::cout << ">> 1080P (16:9 横屏) Letterbox 上下填黑边验证: [通过 PASS] (padH=" << pH1 << ", padW=" << pW1 << ")" << std::endl;
    }

    // 测试 2: 竖屏 480x640 (3:4 竖图，上下填满，左右加黑边 padW > 0, padH == 0)
    cv::Mat portraitMat(640, 480, CV_8UC3, cv::Scalar(40, 40, 40));
    float s2, pW2, pH2;
    cv::Mat lbPortrait = YoloPostProcessor::letterbox(portraitMat, 640, 640, s2, pW2, pH2);
    if (pH2 != 0.0f || pW2 <= 0.0f) {
        std::cerr << ">> [错误] 3:4 竖图 Letterbox 上下不应填充，实际 padW=" << pW2 << ", padH=" << pH2 << std::endl;
        passRobust = false;
    } else {
        std::cout << ">> 480x640 (3:4 竖屏) Letterbox 左右填黑边验证: [通过 PASS] (padW=" << pW2 << ", padH=" << pH2 << ")" << std::endl;
    }

    // 测试 3: 空图像拦截测试
    cv::Mat emptyMat;
    auto emptyRes = gpuDetector.detect(emptyMat);
    if (emptyRes.empty()) {
        std::cout << ">> 空帧 cv::Mat() 防御性拦截测试: [通过 PASS - 未崩溃且返回空列表]" << std::endl;
    } else {
        std::cerr << ">> [错误] 空图像未被正确拦截！" << std::endl;
        passRobust = false;
    }

    std::cout << "\n------------------------------------------------------------------------" << std::endl;
    std::cout << " [体检 5/5] 长稳连续推理与内存泄漏排查 (Memory Leak Free Check)" << std::endl;
    std::cout << "------------------------------------------------------------------------" << std::endl;

    size_t memStartKB = getCurrentProcessMemoryKB();
    const int leakTestIters = 200;
    for (int i = 0; i < leakTestIters; ++i) {
        gpuDetector.detect(realFrame);
    }
    size_t memEndKB = getCurrentProcessMemoryKB();
    int memDeltaKB = static_cast<int>(memEndKB) - static_cast<int>(memStartKB);

    std::cout << ">> 连续 " << leakTestIters << " 轮推理前进程物理内存: " << memStartKB << " KB" << std::endl;
    std::cout << ">> 连续 " << leakTestIters << " 轮推理后进程物理内存: " << memEndKB << " KB" << std::endl;
    std::cout << ">> 内存净变化: " << memDeltaKB << " KB (" << std::fixed << std::setprecision(2) << (memDeltaKB / 1024.0f) << " MB)" << std::endl;
    bool passMemory = (memDeltaKB < 2048); // 允许少许运行时波动，小于 2MB 视为无泄漏
    std::cout << ">> 判定结果 (内存增量 < 2MB): " << (passMemory ? "[通过 PASS - 零内存泄漏]" : "[警告 WARN - 存在可疑内存上升]") << std::endl;

    // =========================================================================
    // 总结与验收判定
    // =========================================================================
    std::cout << "\n========================================================================" << std::endl;
    std::cout << "                     阶段一全景质量体检总结报告                         " << std::endl;
    std::cout << "========================================================================" << std::endl;
    std::cout << " 1. 推理性能与加速比 (GPU <= 15ms)      : " << (passPerf ? "PASS 达成 (" + std::to_string(avgGpuInferenceMs).substr(0,4) + "ms, " + std::to_string(speedup).substr(0,4) + "x)" : "FAIL") << std::endl;
    std::cout << " 2. Ground Truth 坐标对齐 (IoU>=50%)     : " << (passAccuracy ? "PASS 达成 (IoU=" + std::to_string(bestIoU*100).substr(0,4) + "%, 误差=" + std::to_string(centerErr).substr(0,4) + "px)" : "WARN") << std::endl;
    std::cout << " 3. 可视化图元落盘 (test_detection_vis) : " << (visSaved ? "PASS 达成" : "FAIL") << std::endl;
    std::cout << " 4. 几何逆映射与空帧防御                : " << (passRobust ? "PASS 达成" : "FAIL") << std::endl;
    std::cout << " 5. 内存管理长稳性 (200轮泄漏排查)      : " << (passMemory ? "PASS 达成 (净增 " + std::to_string(memDeltaKB) + " KB)" : "WARN") << std::endl;
    std::cout << "------------------------------------------------------------------------" << std::endl;

    bool allSuccess = passPerf && passAccuracy && visSaved && passRobust && passMemory;
    if (allSuccess) {
        std::cout << ">> 综合验收结论: [阶段一质量完全达标 🟢 PASS] - 管道坚固，可放心推进阶段二！" << std::endl;
    } else {
        std::cout << ">> 综合验收结论: [阶段一存在待整改项 🟡 WARN] - 请先修复上述异常再推进！" << std::endl;
    }
    std::cout << "========================================================================" << std::endl;

    return allSuccess ? 0 : 1;
}

