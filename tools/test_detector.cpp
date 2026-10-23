#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <iostream>
#include <iomanip>
#include <chrono>

#include "../onnxdetector.h"
#include "../yolopostprocessor.h"

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    std::cout << "================================================================" << std::endl;
    std::cout << "      RGB_PTZ_Integrated 目标检测引擎性能实测与指标验收工具      " << std::endl;
    std::cout << "================================================================" << std::endl;

    QString modelPath = "models/yolov8n.onnx";
    if (argc > 1) {
        modelPath = QString::fromLocal8Bit(argv[1]);
    }

    std::cout << ">> 待测模型: " << modelPath.toLocal8Bit().constData() << std::endl;
    if (!QFileInfo::exists(modelPath)) {
        std::cerr << "[错误] 模型文件不存在: " << modelPath.toLocal8Bit().constData() << std::endl;
        return 1;
    }

    // 准备测试图像（从本地对拍数据集 01 序列中读取真实第一帧）
    cv::Mat testFrame;
    std::string imgPath = "testdata/sequences/01/imgs/0000.bmp";
    testFrame = cv::imread(imgPath);
    if (!testFrame.empty()) {
        std::cout << ">> 读取对拍实测图像成功: " << imgPath
                  << " (尺寸: " << testFrame.cols << "x" << testFrame.rows << ")" << std::endl;
    }


    if (testFrame.empty()) {
        std::cout << ">> 生成合成测试图像 (1920x1080)..." << std::endl;
        testFrame = cv::Mat(1080, 1920, CV_8UC3, cv::Scalar(100, 120, 140));
        // 绘制几个矩形模拟目标
        cv::rectangle(testFrame, cv::Rect(400, 300, 120, 260), cv::Scalar(50, 50, 200), -1);
        cv::rectangle(testFrame, cv::Rect(900, 500, 300, 180), cv::Scalar(20, 180, 20), -1);
    }

    // =========================================================================
    // 测试 1：DirectML GPU 硬件加速 (NVIDIA RTX 5070)
    // =========================================================================
    std::cout << "\n----------------------------------------------------------------" << std::endl;
    std::cout << " [1] 测试 DirectML GPU 硬件加速模式 (NVIDIA RTX 5070)" << std::endl;
    std::cout << "----------------------------------------------------------------" << std::endl;

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

    std::cout << ">> 模型加载成功! 当前后端: " << (gpuDetector.isUsingGpu() ? "DirectML (GPU 加速)" : "CPU 回退") << std::endl;

    // 热身 10 轮
    std::cout << ">> 正在进行 GPU 推理热身 (10 轮)..." << std::endl;
    for (int i = 0; i < 10; ++i) {
        gpuDetector.detect(testFrame);
    }

    // 连续基准测试 100 轮
    const int benchmarkIters = 100;
    std::cout << ">> 正在执行基准评测 (" << benchmarkIters << " 轮连续推理)..." << std::endl;

    float totalGpuTime = 0.0f;
    std::vector<DetectionBox> lastDetections;

    auto tStart = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < benchmarkIters; ++i) {
        lastDetections = gpuDetector.detect(testFrame);
        totalGpuTime += gpuDetector.getInferenceTimeMs();
    }
    auto tEnd = std::chrono::high_resolution_clock::now();
    float totalWallTime = std::chrono::duration<float, std::milli>(tEnd - tStart).count();

    float avgGpuInferenceMs = totalGpuTime / benchmarkIters;
    float avgGpuTotalMs = totalWallTime / benchmarkIters;
    float gpuFps = 1000.0f / avgGpuTotalMs;

    std::cout << ">> [GPU 结果] 单帧纯推理平均耗时 : " << std::fixed << std::setprecision(2) << avgGpuInferenceMs << " ms" << std::endl;
    std::cout << ">> [GPU 结果] 单帧总耗时(含前后处理): " << std::fixed << std::setprecision(2) << avgGpuTotalMs << " ms" << std::endl;
    std::cout << ">> [GPU 结果] 测得帧率 (FPS)         : " << std::fixed << std::setprecision(1) << gpuFps << " FPS" << std::endl;
    std::cout << ">> [GPU 结果] 检测到目标数量       : " << lastDetections.size() << " 个" << std::endl;

    for (size_t i = 0; i < lastDetections.size() && i < 5; ++i) {
        const auto& d = lastDetections[i];
        std::cout << "   - 目标 " << (i + 1) << ": [" << d.className << "] "
                  << "置信度=" << std::fixed << std::setprecision(3) << d.confidence
                  << " 坐标=[x=" << d.rect.x() << ", y=" << d.rect.y()
                  << ", w=" << d.rect.width() << ", h=" << d.rect.height() << "]" << std::endl;
    }

    // =========================================================================
    // 测试 2：CPU 多线程基准测试（自动降级验证）
    // =========================================================================
    std::cout << "\n----------------------------------------------------------------" << std::endl;
    std::cout << " [2] 测试 CPU 多线程降级兜底模式" << std::endl;
    std::cout << "----------------------------------------------------------------" << std::endl;

    DetectorConfig cpuConfig;
    cpuConfig.useGpu = false;
    cpuConfig.confThreshold = 0.25f;
    cpuConfig.nmsThreshold = 0.45f;

    OnnxDetector cpuDetector;
    if (!cpuDetector.loadModel(modelPath, cpuConfig)) {
        std::cerr << "[错误] CPU 检测器模型加载失败！" << std::endl;
        return 1;
    }

    // 热身 3 轮
    for (int i = 0; i < 3; ++i) {
        cpuDetector.detect(testFrame);
    }

    const int cpuIters = 20;
    float totalCpuTime = 0.0f;
    auto tCpuStart = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < cpuIters; ++i) {
        cpuDetector.detect(testFrame);
        totalCpuTime += cpuDetector.getInferenceTimeMs();
    }
    auto tCpuEnd = std::chrono::high_resolution_clock::now();
    float totalCpuWallTime = std::chrono::duration<float, std::milli>(tCpuEnd - tCpuStart).count();

    float avgCpuInferenceMs = totalCpuTime / cpuIters;
    float avgCpuTotalMs = totalCpuWallTime / cpuIters;
    float cpuFps = 1000.0f / avgCpuTotalMs;

    std::cout << ">> [CPU 结果] 单帧纯推理平均耗时 : " << std::fixed << std::setprecision(2) << avgCpuInferenceMs << " ms" << std::endl;
    std::cout << ">> [CPU 结果] 单帧总耗时(含前后处理): " << std::fixed << std::setprecision(2) << avgCpuTotalMs << " ms" << std::endl;
    std::cout << ">> [CPU 结果] 测得帧率 (FPS)         : " << std::fixed << std::setprecision(1) << cpuFps << " FPS" << std::endl;

    // =========================================================================
    // 总结与验收判定
    // =========================================================================
    std::cout << "\n================================================================" << std::endl;
    std::cout << "                     验收判定与加速比总结                        " << std::endl;
    std::cout << "================================================================" << std::endl;
    float speedup = (avgCpuInferenceMs > 0.0f && avgGpuInferenceMs > 0.0f) ? (avgCpuInferenceMs / avgGpuInferenceMs) : 1.0f;
    std::cout << ">> RTX 5070 DirectML 相比 CPU 加速比: " << std::fixed << std::setprecision(2) << speedup << "x" << std::endl;

    bool passedBenchmark = (avgGpuInferenceMs <= 15.0f);
    std::cout << ">> 方案 01 阶段一验收指标 (GPU 推理 <= 15ms): "
              << (passedBenchmark ? "[通过 PASS]" : "[未达标 FAIL]") << std::endl;
    std::cout << "================================================================" << std::endl;

    return passedBenchmark ? 0 : 2;
}
