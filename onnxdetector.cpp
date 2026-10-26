#include "onnxdetector.h"
#include "yolopostprocessor.h"
#include <QDebug>
#include <QFileInfo>
#include <chrono>
#include <opencv2/imgproc.hpp>
#include <string.h>

#include "onnxruntime_c_api.h"

// MinGW GCC SAL 兼容性宏补齐
#ifndef _Maybenull_
#define _Maybenull_
#endif
#ifndef _Maybenull_string_
#define _Maybenull_string_
#endif

#include "dml_provider_factory.h"


OnnxDetector::OnnxDetector()
{
}

OnnxDetector::~OnnxDetector()
{
    std::lock_guard<std::mutex> lock(m_detectMutex);
    release();
}

void OnnxDetector::release()
{
    if (m_inputName) {
        free(m_inputName);
        m_inputName = nullptr;
    }
    if (m_outputName) {
        free(m_outputName);
        m_outputName = nullptr;
    }
    if (m_memoryInfo && m_api) {
        m_api->ReleaseMemoryInfo(m_memoryInfo);
        m_memoryInfo = nullptr;
    }
    if (m_session && m_api) {
        m_api->ReleaseSession(m_session);
        m_session = nullptr;
    }
    if (m_sessionOptions && m_api) {
        m_api->ReleaseSessionOptions(m_sessionOptions);
        m_sessionOptions = nullptr;
    }
    if (m_env && m_api) {
        m_api->ReleaseEnv(m_env);
        m_env = nullptr;
    }

    m_isLoaded = false;
    m_usingGpu = false;
    m_api = nullptr;
}

bool OnnxDetector::loadModel(const QString& modelPath, const DetectorConfig& config)
{
    std::lock_guard<std::mutex> lock(m_detectMutex);
    QFileInfo checkFile(modelPath);
    if (!checkFile.exists() || !checkFile.isFile()) {
        qWarning().noquote() << "OnnxDetector: Model file not found:" << modelPath;
        return false;
    }

    return initializeSession(modelPath, config);
}

bool OnnxDetector::initializeSession(const QString& modelPath, const DetectorConfig& config)
{
    release();
    m_config = config;

    m_api = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    if (!m_api) {
        qWarning() << "OnnxDetector: Failed to get OrtApi for version" << ORT_API_VERSION;
        return false;
    }

    OrtStatus* status = m_api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "OnnxDetector", &m_env);
    if (status != nullptr) {
        qWarning() << "OnnxDetector: CreateEnv failed:" << m_api->GetErrorMessage(status);
        m_api->ReleaseStatus(status);
        return false;
    }

    status = m_api->CreateSessionOptions(&m_sessionOptions);
    if (status != nullptr) {
        qWarning() << "OnnxDetector: CreateSessionOptions failed:" << m_api->GetErrorMessage(status);
        m_api->ReleaseStatus(status);
        return false;
    }

    m_api->SetIntraOpNumThreads(m_sessionOptions, 2);
    m_api->SetSessionGraphOptimizationLevel(m_sessionOptions, ORT_ENABLE_ALL);

    // 优先尝试 DirectML (GPU 加速，NVIDIA RTX 5070)
    m_usingGpu = false;
    if (config.useGpu) {
        OrtStatus* dmlStatus = OrtSessionOptionsAppendExecutionProvider_DML(m_sessionOptions, config.deviceId);
        if (dmlStatus == nullptr) {
            m_usingGpu = true;
            qInfo() << "OnnxDetector: DirectML GPU acceleration successfully enabled on adapter" << config.deviceId;
        } else {
            qWarning() << "OnnxDetector: DirectML init failed (" << m_api->GetErrorMessage(dmlStatus)
                       << "), automatically falling back to CPU execution.";
            m_api->ReleaseStatus(dmlStatus);
        }
    }

    // Windows 下使用 wchar_t 路径
    std::wstring wModelPath = modelPath.toStdWString();
    status = m_api->CreateSession(m_env, wModelPath.c_str(), m_sessionOptions, &m_session);
    if (status != nullptr) {
        qWarning() << "OnnxDetector: CreateSession failed for model:" << modelPath
                   << "Error:" << m_api->GetErrorMessage(status);
        m_api->ReleaseStatus(status);
        return false;
    }

    OrtAllocator* allocator = nullptr;
    status = m_api->GetAllocatorWithDefaultOptions(&allocator);
    if (status != nullptr || !allocator) {
        qWarning() << "OnnxDetector: Failed to get default allocator.";
        if (status) m_api->ReleaseStatus(status);
        return false;
    }

    // 获取输入节点名称与尺寸
    char* rawInName = nullptr;
    status = m_api->SessionGetInputName(m_session, 0, allocator, &rawInName);
    if (status == nullptr && rawInName) {
        m_inputName = _strdup(rawInName);
        allocator->Free(allocator, rawInName);
    } else {
        if (status) m_api->ReleaseStatus(status);
        qWarning() << "OnnxDetector: Failed to query input name.";
        return false;
    }

    // 获取输出节点名称与尺寸
    char* rawOutName = nullptr;
    status = m_api->SessionGetOutputName(m_session, 0, allocator, &rawOutName);
    if (status == nullptr && rawOutName) {
        m_outputName = _strdup(rawOutName);
        allocator->Free(allocator, rawOutName);
    } else {
        if (status) m_api->ReleaseStatus(status);
        qWarning() << "OnnxDetector: Failed to query output name.";
        return false;
    }

    // 查询输入维度 (标准 YOLO 布局: [1, 3, 640, 640])
    OrtTypeInfo* inTypeInfo = nullptr;
    if (m_api->SessionGetInputTypeInfo(m_session, 0, &inTypeInfo) == nullptr && inTypeInfo) {
        const OrtTensorTypeAndShapeInfo* shapeInfo = nullptr;
        m_api->CastTypeInfoToTensorInfo(inTypeInfo, &shapeInfo);
        size_t dimCount = 0;
        m_api->GetDimensionsCount(shapeInfo, &dimCount);
        if (dimCount >= 4) {
            std::vector<int64_t> dims(dimCount);
            m_api->GetDimensions(shapeInfo, dims.data(), dimCount);
            if (dims[2] > 0) m_inputHeight = static_cast<int>(dims[2]);
            if (dims[3] > 0) m_inputWidth = static_cast<int>(dims[3]);
        }
        m_api->ReleaseTypeInfo(inTypeInfo);
    }

    // 查询输出维度 (例如 YOLOv8: [1, 84, 8400])
    OrtTypeInfo* outTypeInfo = nullptr;
    if (m_api->SessionGetOutputTypeInfo(m_session, 0, &outTypeInfo) == nullptr && outTypeInfo) {
        const OrtTensorTypeAndShapeInfo* outShapeInfo = nullptr;
        m_api->CastTypeInfoToTensorInfo(outTypeInfo, &outShapeInfo);
        size_t outDimCount = 0;
        m_api->GetDimensionsCount(outShapeInfo, &outDimCount);
        if (outDimCount >= 3) {
            std::vector<int64_t> outDims(outDimCount);
            m_api->GetDimensions(outShapeInfo, outDims.data(), outDimCount);
            if (outDims[1] > 0) m_tensorChannels = static_cast<int>(outDims[1]);
            if (outDims[2] > 0) m_tensorAnchors = static_cast<int>(outDims[2]);
        }
        m_api->ReleaseTypeInfo(outTypeInfo);
    }

    // 创建 CPU 内存信息描述符
    status = m_api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &m_memoryInfo);
    if (status != nullptr) {
        qWarning() << "OnnxDetector: CreateCpuMemoryInfo failed:" << m_api->GetErrorMessage(status);
        m_api->ReleaseStatus(status);
        return false;
    }

    // 预分配连续内存
    m_inputTensorValues.resize(3 * m_inputWidth * m_inputHeight);
    m_isLoaded = true;

    qInfo().noquote() << QString("OnnxDetector: Model loaded successfully! Input: [%1] (%2x%3), Output: [%4] (%5x%6), Backend: %7")
        .arg(m_inputName).arg(m_inputWidth).arg(m_inputHeight)
        .arg(m_outputName).arg(m_tensorChannels).arg(m_tensorAnchors)
        .arg(m_usingGpu ? "DirectML (GPU)" : "CPU");

    return true;
}

bool OnnxDetector::isLoaded() const
{
    std::lock_guard<std::mutex> lock(m_detectMutex);
    return m_isLoaded && m_session != nullptr;
}

std::vector<DetectionBox> OnnxDetector::detect(const cv::Mat& frame)
{
    std::lock_guard<std::mutex> lock(m_detectMutex);

    if (!m_isLoaded || frame.empty() || !m_session) {
        return {};
    }

    float scale = 1.0f;
    float padW = 0.0f;
    float padH = 0.0f;

    // 1. 前处理：Letterbox 保持宽高比居中缩放
    cv::Mat letterboxed = YoloPostProcessor::letterbox(
        frame, m_inputWidth, m_inputHeight, scale, padW, padH);

    // 2. 颜色空间转换：BGR -> RGB
    cv::Mat rgb;
    cv::cvtColor(letterboxed, rgb, cv::COLOR_BGR2RGB);

    // 3. 数据重排：HWC -> CHW，归一化 [0.0, 1.0]
    int totalPixels = m_inputWidth * m_inputHeight;
    float* ptrR = m_inputTensorValues.data();
    float* ptrG = ptrR + totalPixels;
    float* ptrB = ptrG + totalPixels;

    for (int r = 0; r < m_inputHeight; ++r) {
        const uchar* row = rgb.ptr<uchar>(r);
        for (int c = 0; c < m_inputWidth; ++c) {
            int idx = r * m_inputWidth + c;
            ptrR[idx] = row[c * 3 + 0] / 255.0f;
            ptrG[idx] = row[c * 3 + 1] / 255.0f;
            ptrB[idx] = row[c * 3 + 2] / 255.0f;
        }
    }

    // 4. 构建输入 OrtValue
    int64_t inputDims[4] = {1, 3, m_inputHeight, m_inputWidth};
    OrtValue* inputTensor = nullptr;
    OrtStatus* status = m_api->CreateTensorWithDataAsOrtValue(
        m_memoryInfo,
        m_inputTensorValues.data(),
        m_inputTensorValues.size() * sizeof(float),
        inputDims, 4,
        ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
        &inputTensor);

    if (status != nullptr) {
        qWarning() << "OnnxDetector: CreateTensorWithDataAsOrtValue failed:" << m_api->GetErrorMessage(status);
        m_api->ReleaseStatus(status);
        return {};
    }

    // 5. 执行推理并高精度计时
    const char* inputNames[] = {m_inputName};
    const char* outputNames[] = {m_outputName};
    OrtValue* outputTensor = nullptr;

    auto t0 = std::chrono::high_resolution_clock::now();
    status = m_api->Run(m_session, nullptr, inputNames, &inputTensor, 1, outputNames, 1, &outputTensor);
    auto t1 = std::chrono::high_resolution_clock::now();
    m_lastInferenceTimeMs = std::chrono::duration<float, std::milli>(t1 - t0).count();

    if (status != nullptr) {
        qWarning() << "OnnxDetector: OrtRun failed:" << m_api->GetErrorMessage(status);
        m_api->ReleaseStatus(status);
        m_api->ReleaseValue(inputTensor);
        return {};
    }

    // 6. 获取输出张量指针并执行纯 C++ NMS 与坐标映射
    float* outputData = nullptr;
    status = m_api->GetTensorMutableData(outputTensor, reinterpret_cast<void**>(&outputData));
    std::vector<DetectionBox> detections;
    if (status == nullptr && outputData) {
        detections = YoloPostProcessor::process(
            outputData,
            m_tensorChannels,
            m_tensorAnchors,
            frame.cols,
            frame.rows,
            scale,
            padW,
            padH,
            m_config.confThreshold,
            m_config.nmsThreshold,
            m_config.classNames);
    } else {
        if (status) m_api->ReleaseStatus(status);
    }

    // 7. 释放单次推理输入输出张量
    m_api->ReleaseValue(inputTensor);
    m_api->ReleaseValue(outputTensor);

    return detections;
}

float OnnxDetector::getInferenceTimeMs() const
{
    return m_lastInferenceTimeMs;
}

cv::Size OnnxDetector::getInputSize() const
{
    return cv::Size(m_inputWidth, m_inputHeight);
}
