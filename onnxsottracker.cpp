#include "onnxsottracker.h"
#include <QDebug>
#include <QFileInfo>
#include <chrono>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <opencv2/imgproc.hpp>

#include "onnxruntime_c_api.h"

// MinGW GCC SAL 兼容性宏补齐
#ifndef _Maybenull_
#define _Maybenull_
#endif
#ifndef _Maybenull_string_
#define _Maybenull_string_
#endif

#include "dml_provider_factory.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// 符号常量
static const int STRIDE = 16;
static const int EXEMPLAR_SIZE = 127;
static const int TEMPLATE_FEAT_ELEMS = 1 * 96 * 8 * 8; // 6144

// 银行家舍入（round half to even，对应 Python 3 内建 round）
static inline int bankersRound(double val)
{
    return static_cast<int>(std::nearbyint(val));
}

// 四舍五入、半数远离零（python2round，正数时等同 std::round）
static inline int pyRound2(double val)
{
    return static_cast<int>(std::round(val));
}

// 尺度计算辅助函数
static inline double szf(double a, double b)
{
    double pad = (a + b) * 0.5;
    return std::sqrt((a + pad) * (b + pad));
}

// 比例变动辅助函数
static inline double change(double q)
{
    if (q <= 0.0) return 1e6;
    return std::max(q, 1.0 / q);
}

OnnxSotTracker::OnnxSotTracker(const SotTrackerConfig& cfg)
    : m_config(cfg)
{
}

OnnxSotTracker::~OnnxSotTracker()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    releaseAll();
}

void OnnxSotTracker::releaseAll()
{
    if (m_tmplInName) { free(m_tmplInName); m_tmplInName = nullptr; }
    if (m_tmplOutName) { free(m_tmplOutName); m_tmplOutName = nullptr; }

    auto freeNet = [this](SearchNet& net) {
        if (net.inName0) { free(net.inName0); net.inName0 = nullptr; }
        if (net.inName1) { free(net.inName1); net.inName1 = nullptr; }
        if (net.outName0) { free(net.outName0); net.outName0 = nullptr; }
        if (net.outName1) { free(net.outName1); net.outName1 = nullptr; }
        if (net.session && m_api) {
            m_api->ReleaseSession(net.session);
            net.session = nullptr;
        }
    };
    freeNet(m_small);
    freeNet(m_big);
    m_active = nullptr;

    if (m_tmplSession && m_api) {
        m_api->ReleaseSession(m_tmplSession);
        m_tmplSession = nullptr;
    }
    if (m_memoryInfo && m_api) {
        m_api->ReleaseMemoryInfo(m_memoryInfo);
        m_memoryInfo = nullptr;
    }
    if (m_sessionOptions && m_api) {
        m_api->ReleaseSessionOptions(m_sessionOptions);
        m_sessionOptions = nullptr;
    }
    if (m_env && m_api) {
        m_api->ReleaseEnv(m_env);
        m_env = nullptr;
    }

    m_api = nullptr;
    m_loaded = false;
    m_usingGpu = false;
    m_initialized = false;
}

bool OnnxSotTracker::isLoaded() const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_loaded;
}

bool OnnxSotTracker::ensureLoaded()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_loaded) return true;
    return loadModel(m_config);
}

bool OnnxSotTracker::initializeTemplateSession(const QString& modelPath)
{
    std::wstring wPath = modelPath.toStdWString();
    OrtStatus* status = m_api->CreateSession(m_env, wPath.c_str(), m_sessionOptions, &m_tmplSession);
    if (status != nullptr) {
        qWarning() << "OnnxSotTracker: CreateSession failed for template:" << modelPath
                   << m_api->GetErrorMessage(status);
        m_api->ReleaseStatus(status);
        return false;
    }

    OrtAllocator* allocator = nullptr;
    status = m_api->GetAllocatorWithDefaultOptions(&allocator);
    if (status != nullptr || !allocator) {
        if (status) m_api->ReleaseStatus(status);
        return false;
    }

    // 查询输入输出名称
    char* inName = nullptr;
    status = m_api->SessionGetInputName(m_tmplSession, 0, allocator, &inName);
    if (status != nullptr) { m_api->ReleaseStatus(status); return false; }
    m_tmplInName = _strdup(inName);
    m_api->AllocatorFree(allocator, inName);

    char* outName = nullptr;
    status = m_api->SessionGetOutputName(m_tmplSession, 0, allocator, &outName);
    if (status != nullptr) { m_api->ReleaseStatus(status); return false; }
    m_tmplOutName = _strdup(outName);
    m_api->AllocatorFree(allocator, outName);

    // 预热 3 次
    std::vector<float> dummyInput(3 * 127 * 127, 0.0f);
    int64_t inDims[4] = {1, 3, 127, 127};
    OrtValue* inVal = nullptr;
    status = m_api->CreateTensorWithDataAsOrtValue(
        m_memoryInfo, dummyInput.data(), dummyInput.size() * sizeof(float),
        inDims, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &inVal);
    if (status == nullptr && inVal) {
        const char* inNames[] = {m_tmplInName};
        const char* outNames[] = {m_tmplOutName};
        for (int i = 0; i < 3; ++i) {
            OrtValue* outVal = nullptr;
            OrtStatus* runStat = m_api->Run(m_tmplSession, nullptr, inNames, &inVal, 1, outNames, 1, &outVal);
            if (runStat == nullptr && outVal) {
                m_api->ReleaseValue(outVal);
            } else if (runStat) {
                m_api->ReleaseStatus(runStat);
            }
        }
        m_api->ReleaseValue(inVal);
    } else if (status) {
        m_api->ReleaseStatus(status);
    }

    return true;
}

bool OnnxSotTracker::initializeSearchSession(SearchNet& net, const QString& modelPath, int expectedInSize, int expectedScoreSize)
{
    net.inSize = expectedInSize;
    net.scoreSize = expectedScoreSize;

    std::wstring wPath = modelPath.toStdWString();
    OrtStatus* status = m_api->CreateSession(m_env, wPath.c_str(), m_sessionOptions, &net.session);
    if (status != nullptr) {
        qWarning() << "OnnxSotTracker: CreateSession failed for search:" << modelPath
                   << m_api->GetErrorMessage(status);
        m_api->ReleaseStatus(status);
        return false;
    }

    OrtAllocator* allocator = nullptr;
    status = m_api->GetAllocatorWithDefaultOptions(&allocator);
    if (status != nullptr || !allocator) {
        if (status) m_api->ReleaseStatus(status);
        return false;
    }

    // 输入 0 (search_img), 输入 1 (template_feat)
    char* inName0 = nullptr;
    status = m_api->SessionGetInputName(net.session, 0, allocator, &inName0);
    if (status != nullptr) { m_api->ReleaseStatus(status); return false; }
    net.inName0 = _strdup(inName0);
    m_api->AllocatorFree(allocator, inName0);

    char* inName1 = nullptr;
    status = m_api->SessionGetInputName(net.session, 1, allocator, &inName1);
    if (status != nullptr) { m_api->ReleaseStatus(status); return false; }
    net.inName1 = _strdup(inName1);
    m_api->AllocatorFree(allocator, inName1);

    // 输出 0 (cls_logit), 输出 1 (reg_dist)
    char* outName0 = nullptr;
    status = m_api->SessionGetOutputName(net.session, 0, allocator, &outName0);
    if (status != nullptr) { m_api->ReleaseStatus(status); return false; }
    net.outName0 = _strdup(outName0);
    m_api->AllocatorFree(allocator, outName0);

    char* outName1 = nullptr;
    status = m_api->SessionGetOutputName(net.session, 1, allocator, &outName1);
    if (status != nullptr) { m_api->ReleaseStatus(status); return false; }
    net.outName1 = _strdup(outName1);
    m_api->AllocatorFree(allocator, outName1);

    // 预计算 gridX, gridY 与 hanning 余弦窗
    int S = net.scoreSize;
    int I = net.inSize;
    net.gridX.resize(S * S);
    net.gridY.resize(S * S);
    net.window.resize(S * S);

    int sz_x = S / 2;
    int sz_y = S / 2;
    std::vector<float> hanning1d(S);
    for (int i = 0; i < S; ++i) {
        hanning1d[i] = 0.5f - 0.5f * std::cos(static_cast<float>(2.0 * M_PI * i / (S - 1)));
    }

    for (int r = 0; r < S; ++r) {
        float y_val = static_cast<float>(r - sz_y);
        for (int c = 0; c < S; ++c) {
            float x_val = static_cast<float>(c - sz_x);
            int idx = r * S + c;
            net.gridX[idx] = x_val * STRIDE + static_cast<float>(I / 2);
            net.gridY[idx] = y_val * STRIDE + static_cast<float>(I / 2);
            net.window[idx] = hanning1d[r] * hanning1d[c];
        }
    }

    // 预热 3 次
    std::vector<float> dummySearch(3 * I * I, 0.0f);
    std::vector<float> dummyFeat(TEMPLATE_FEAT_ELEMS, 0.0f);
    int64_t in0Dims[4] = {1, 3, I, I};
    int64_t in1Dims[4] = {1, 96, 8, 8};

    OrtValue* inVal0 = nullptr;
    OrtValue* inVal1 = nullptr;
    m_api->CreateTensorWithDataAsOrtValue(m_memoryInfo, dummySearch.data(), dummySearch.size() * sizeof(float),
                                         in0Dims, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &inVal0);
    m_api->CreateTensorWithDataAsOrtValue(m_memoryInfo, dummyFeat.data(), dummyFeat.size() * sizeof(float),
                                         in1Dims, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &inVal1);

    if (inVal0 && inVal1) {
        const char* inNames[] = {net.inName0, net.inName1};
        const char* outNames[] = {net.outName0, net.outName1};
        OrtValue* inVals[] = {inVal0, inVal1};
        for (int i = 0; i < 3; ++i) {
            OrtValue* outVals[2] = {nullptr, nullptr};
            OrtStatus* runStat = m_api->Run(net.session, nullptr, inNames, inVals, 2, outNames, 2, outVals);
            if (runStat == nullptr) {
                if (outVals[0]) m_api->ReleaseValue(outVals[0]);
                if (outVals[1]) m_api->ReleaseValue(outVals[1]);
            } else {
                m_api->ReleaseStatus(runStat);
            }
        }
    }
    if (inVal0) m_api->ReleaseValue(inVal0);
    if (inVal1) m_api->ReleaseValue(inVal1);

    return true;
}

bool OnnxSotTracker::loadModel(const SotTrackerConfig& cfg)
{
    releaseAll();
    m_config = cfg;

    m_api = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    if (!m_api) {
        qWarning() << "OnnxSotTracker: Failed to get OrtApi for version" << ORT_API_VERSION;
        return false;
    }

    OrtStatus* status = m_api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "OnnxSotTracker", &m_env);
    if (status != nullptr) {
        qWarning() << "OnnxSotTracker: CreateEnv failed:" << m_api->GetErrorMessage(status);
        m_api->ReleaseStatus(status);
        return false;
    }

    status = m_api->CreateSessionOptions(&m_sessionOptions);
    if (status != nullptr) {
        qWarning() << "OnnxSotTracker: CreateSessionOptions failed:" << m_api->GetErrorMessage(status);
        m_api->ReleaseStatus(status);
        return false;
    }

    m_api->SetIntraOpNumThreads(m_sessionOptions, 1);
    m_api->SetSessionGraphOptimizationLevel(m_sessionOptions, ORT_ENABLE_ALL);
    m_api->SetSessionExecutionMode(m_sessionOptions, ORT_SEQUENTIAL);
    m_api->DisableMemPattern(m_sessionOptions); // DirectML EP 必须项

    if (cfg.strictGpu) {
        m_api->AddSessionConfigEntry(m_sessionOptions, "session.disable_cpu_ep_fallback", "1");
    }

    m_usingGpu = false;
    if (cfg.useGpu) {
        OrtStatus* dmlStatus = OrtSessionOptionsAppendExecutionProvider_DML(m_sessionOptions, cfg.deviceId);
        if (dmlStatus == nullptr) {
            m_usingGpu = true;
            qInfo() << "OnnxSotTracker: DirectML GPU enabled on device" << cfg.deviceId;
        } else {
            qWarning() << "OnnxSotTracker: DirectML failed (" << m_api->GetErrorMessage(dmlStatus)
                       << "), fallback to CPU.";
            m_api->ReleaseStatus(dmlStatus);
        }
    }

    status = m_api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &m_memoryInfo);
    if (status != nullptr) {
        qWarning() << "OnnxSotTracker: CreateCpuMemoryInfo failed:" << m_api->GetErrorMessage(status);
        m_api->ReleaseStatus(status);
        return false;
    }

    // 加载模板模型
    if (!initializeTemplateSession(cfg.templateModel)) {
        releaseAll();
        return false;
    }

    // 加载 256 搜索模型 (输出 16x16)
    if (!initializeSearchSession(m_small, cfg.searchModelSmall, 256, 16)) {
        releaseAll();
        return false;
    }

    // 加载 288 搜索模型 (输出 18x18)
    if (!initializeSearchSession(m_big, cfg.searchModelBig, 288, 18)) {
        releaseAll();
        return false;
    }

    m_templateFeat.resize(TEMPLATE_FEAT_ELEMS, 0.0f);
    m_anchorTemplateFeat.resize(TEMPLATE_FEAT_ELEMS, 0.0f);
    m_loaded = true;
    qInfo().noquote() << QString("OnnxSotTracker: Loaded successfully! Backend: %1")
                             .arg(m_usingGpu ? "DirectML (GPU)" : "CPU");
    return true;
}

cv::Mat OnnxSotTracker::cropPatch(const cv::Mat& rgb, cv::Point2d pos, int sz, int modelSz,
                                  const cv::Scalar& avgChans, int& outCropSz, int& outCropXmin, int& outCropYmin)
{
    outCropSz = sz;
    double c = (sz + 1.0) / 2.0;
    int xmin = bankersRound(pos.x - c);
    int ymin = bankersRound(pos.y - c);
    int xmax = xmin + sz - 1;
    int ymax = ymin + sz - 1;

    outCropXmin = xmin;
    outCropYmin = ymin;

    int W = rgb.cols;
    int H = rgb.rows;

    cv::Mat patch(sz, sz, CV_8UC3, avgChans);

    int src_x1 = std::max(0, xmin);
    int src_y1 = std::max(0, ymin);
    int src_x2 = std::min(W - 1, xmax);
    int src_y2 = std::min(H - 1, ymax);

    if (src_x1 <= src_x2 && src_y1 <= src_y2) {
        int dst_x1 = src_x1 - xmin;
        int dst_y1 = src_y1 - ymin;
        int copy_w = src_x2 - src_x1 + 1;
        int copy_h = src_y2 - src_y1 + 1;

        cv::Rect srcRect(src_x1, src_y1, copy_w, copy_h);
        cv::Rect dstRect(dst_x1, dst_y1, copy_w, copy_h);
        rgb(srcRect).copyTo(patch(dstRect));
    }

    if (sz != modelSz) {
        cv::Mat resized;
        cv::resize(patch, resized, cv::Size(modelSz, modelSz), 0, 0, cv::INTER_LINEAR);
        return resized;
    }
    return patch;
}

void OnnxSotTracker::toTensorNCHW(const cv::Mat& patch, float* outBuffer)
{
    int H = patch.rows;
    int W = patch.cols;
    int planeSize = H * W;
    float* rPlane = outBuffer;
    float* gPlane = outBuffer + planeSize;
    float* bPlane = outBuffer + planeSize * 2;

    for (int y = 0; y < H; ++y) {
        const uchar* ptr = patch.ptr<uchar>(y);
        for (int x = 0; x < W; ++x) {
            int idx = y * W + x;
            rPlane[idx] = static_cast<float>(ptr[0]) / 255.0f;
            gPlane[idx] = static_cast<float>(ptr[1]) / 255.0f;
            bPlane[idx] = static_cast<float>(ptr[2]) / 255.0f;
            ptr += 3;
        }
    }
}

bool OnnxSotTracker::init(const cv::Mat& bgr, const cv::Rect2f& bbox)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!ensureLoaded() || bgr.empty() || bgr.type() != CV_8UC3) {
        return false;
    }
    if (bbox.width < 4.0f || bbox.height < 4.0f) {
        return false;
    }

    m_imW = bgr.cols;
    m_imH = bgr.rows;

    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);

    // 计算全局通道均值并向下取整（截断到 uint8）
    cv::Scalar meanVal = cv::mean(rgb);
    m_avgChans = cv::Scalar(
        std::floor(meanVal[0]),
        std::floor(meanVal[1]),
        std::floor(meanVal[2])
    );

    double cx = bbox.x + bbox.width / 2.0;
    double cy = bbox.y + bbox.height / 2.0;
    m_pos = cv::Point2d(cx, cy);
    m_sz = cv::Size2d(bbox.width, bbox.height);

    // 根据首帧面积比选定激活搜索分支（跟踪期间固定）
    double areaRatio = (bbox.width * bbox.height) / (static_cast<double>(m_imW) * m_imH);
    if (areaRatio < m_config.bigSzAreaRatio) {
        m_active = &m_big;   // 288
    } else {
        m_active = &m_small; // 256
    }

    // 模板尺寸计算
    double wc_z = m_sz.width + m_config.contextAmount * (m_sz.width + m_sz.height);
    double hc_z = m_sz.height + m_config.contextAmount * (m_sz.width + m_sz.height);
    double s_z = std::sqrt(wc_z * hc_z);
    int s_z_round = bankersRound(s_z);

    int dummySz = 0, dummyXmin = 0, dummyYmin = 0;
    cv::Mat tmplPatch = cropPatch(rgb, m_pos, s_z_round, EXEMPLAR_SIZE, m_avgChans, dummySz, dummyXmin, dummyYmin);

    std::vector<float> tmplTensor(3 * EXEMPLAR_SIZE * EXEMPLAR_SIZE);
    toTensorNCHW(tmplPatch, tmplTensor.data());

    int64_t inDims[4] = {1, 3, EXEMPLAR_SIZE, EXEMPLAR_SIZE};
    OrtValue* inVal = nullptr;
    OrtStatus* status = m_api->CreateTensorWithDataAsOrtValue(
        m_memoryInfo, tmplTensor.data(), tmplTensor.size() * sizeof(float),
        inDims, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &inVal);
    if (status != nullptr) {
        qWarning() << "OnnxSotTracker: Template tensor creation failed:" << m_api->GetErrorMessage(status);
        m_api->ReleaseStatus(status);
        return false;
    }

    const char* inNames[] = {m_tmplInName};
    const char* outNames[] = {m_tmplOutName};
    OrtValue* outVal = nullptr;

    status = m_api->Run(m_tmplSession, nullptr, inNames, &inVal, 1, outNames, 1, &outVal);
    m_api->ReleaseValue(inVal);

    if (status != nullptr) {
        qWarning() << "OnnxSotTracker: Template forward failed:" << m_api->GetErrorMessage(status);
        m_api->ReleaseStatus(status);
        return false;
    }

    float* featData = nullptr;
    status = m_api->GetTensorMutableData(outVal, reinterpret_cast<void**>(&featData));
    if (status != nullptr || !featData) {
        if (status) m_api->ReleaseStatus(status);
        m_api->ReleaseValue(outVal);
        return false;
    }

    std::memcpy(m_templateFeat.data(), featData, TEMPLATE_FEAT_ELEMS * sizeof(float));
    std::memcpy(m_anchorTemplateFeat.data(), featData, TEMPLATE_FEAT_ELEMS * sizeof(float));
    m_api->ReleaseValue(outVal);

    m_initialized = true;
    return true;
}

SotResult OnnxSotTracker::forwardSearch(const cv::Mat& bgr, cv::Point2d pos, cv::Size2d sz, bool updateState,
                                        const std::vector<float>* overrideTemplateFeat)
{
    SotResult res;
    res.valid = false;

    const std::vector<float>& activeFeat = (overrideTemplateFeat && overrideTemplateFeat->size() == TEMPLATE_FEAT_ELEMS)
                                               ? *overrideTemplateFeat
                                               : m_templateFeat;
    if (!m_initialized || !m_active || bgr.empty() || bgr.type() != CV_8UC3 ||
        sz.width < 2.0 || sz.height < 2.0 || activeFeat.size() != TEMPLATE_FEAT_ELEMS) {
        return res;
    }

    auto t_start_total = std::chrono::steady_clock::now();

    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);

    // 计算搜索裁切边长
    double wc_z = sz.width + m_config.contextAmount * (sz.width + sz.height);
    double hc_z = sz.height + m_config.contextAmount * (sz.width + sz.height);
    double s_z = std::sqrt(wc_z * hc_z);
    double scale_z = static_cast<double>(EXEMPLAR_SIZE) / s_z;

    int instanceSize = m_active->inSize;
    int scoreSize = m_active->scoreSize;
    double d_search = (instanceSize - EXEMPLAR_SIZE) / 2.0;
    double pad = d_search / scale_z;
    double s_x = s_z + 2.0 * pad;
    int s_x_round = pyRound2(s_x);

    int cropSz = 0, cropXmin = 0, cropYmin = 0;
    cv::Mat searchPatch = cropPatch(rgb, pos, s_x_round, instanceSize, m_avgChans, cropSz, cropXmin, cropYmin);

    std::vector<float> searchTensor(3 * instanceSize * instanceSize);
    toTensorNCHW(searchPatch, searchTensor.data());

    int64_t in0Dims[4] = {1, 3, instanceSize, instanceSize};
    int64_t in1Dims[4] = {1, 96, 8, 8};

    OrtValue* inVal0 = nullptr;
    OrtValue* inVal1 = nullptr;
    m_api->CreateTensorWithDataAsOrtValue(
        m_memoryInfo, searchTensor.data(), searchTensor.size() * sizeof(float),
        in0Dims, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &inVal0);
    m_api->CreateTensorWithDataAsOrtValue(
        m_memoryInfo, const_cast<float*>(activeFeat.data()), activeFeat.size() * sizeof(float),
        in1Dims, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &inVal1);

    if (!inVal0 || !inVal1) {
        if (inVal0) m_api->ReleaseValue(inVal0);
        if (inVal1) m_api->ReleaseValue(inVal1);
        return res;
    }

    const char* inNames[] = {m_active->inName0, m_active->inName1};
    const char* outNames[] = {m_active->outName0, m_active->outName1};
    OrtValue* inVals[] = {inVal0, inVal1};
    OrtValue* outVals[2] = {nullptr, nullptr};

    // T1: 纯模型前向计时
    auto t_start_run = std::chrono::steady_clock::now();
    OrtStatus* status = m_api->Run(m_active->session, nullptr, inNames, inVals, 2, outNames, 2, outVals);
    auto t_end_run = std::chrono::steady_clock::now();
    m_lastModelMs = std::chrono::duration<float, std::milli>(t_end_run - t_start_run).count();

    m_api->ReleaseValue(inVal0);
    m_api->ReleaseValue(inVal1);

    if (status != nullptr) {
        qWarning() << "OnnxSotTracker: Search forward failed:" << m_api->GetErrorMessage(status);
        m_api->ReleaseStatus(status);
        return res;
    }

    float* clsLogit = nullptr;
    float* regDist = nullptr;
    m_api->GetTensorMutableData(outVals[0], reinterpret_cast<void**>(&clsLogit));
    m_api->GetTensorMutableData(outVals[1], reinterpret_cast<void**>(&regDist));

    if (!clsLogit || !regDist) {
        m_api->ReleaseValue(outVals[0]);
        m_api->ReleaseValue(outVals[1]);
        return res;
    }

    // 后处理解码（逐行对齐 Lighttrack.update）
    int S = scoreSize;
    int S2 = S * S;
    double tw = sz.width * scale_z;
    double th = sz.height * scale_z;
    double sz_wh_target = szf(tw, th);
    double target_ratio = tw / th;

    double maxPscore = -1e30;
    int opt_r = 0, opt_c = 0;
    double opt_penalty = 1.0;
    double opt_cls = 0.0;
    double opt_x1 = 0.0, opt_y1 = 0.0, opt_x2 = 0.0, opt_y2 = 0.0;
    float globalClsMax = -1e30f;

    for (int r = 0; r < S; ++r) {
        for (int c = 0; c < S; ++c) {
            int idx = r * S + c;
            float logit = clsLogit[idx];
            float clsVal = 1.0f / (1.0f + std::exp(-logit));
            if (clsVal > globalClsMax) {
                globalClsMax = clsVal;
            }

            float gx = m_active->gridX[idx];
            float gy = m_active->gridY[idx];

            float l = regDist[0 * S2 + idx];
            float t = regDist[1 * S2 + idx];
            float r_dist = regDist[2 * S2 + idx];
            float b_dist = regDist[3 * S2 + idx];

            float x1 = gx - l;
            float y1 = gy - t;
            float x2 = gx + r_dist;
            float y2 = gy + b_dist;

            double pw = x2 - x1;
            double ph = y2 - y1;

            double s_c = change(szf(pw, ph) / sz_wh_target);
            double r_c = change(target_ratio / (pw / ph));

            double penalty = std::exp(-(r_c * s_c - 1.0) * m_config.penaltyK);
            double pscore = penalty * clsVal;
            pscore = pscore * (1.0 - m_config.windowInfluence) + m_active->window[idx] * m_config.windowInfluence;

            // 行优先寻找首个最大值；若为 NaN 则按 numpy 逻辑处理
            if (std::isnan(pscore)) {
                if (!std::isnan(maxPscore)) {
                    maxPscore = pscore;
                    opt_r = r; opt_c = c;
                    opt_penalty = penalty; opt_cls = clsVal;
                    opt_x1 = x1; opt_y1 = y1; opt_x2 = x2; opt_y2 = y2;
                }
            } else if (pscore > maxPscore || std::isnan(maxPscore)) {
                maxPscore = pscore;
                opt_r = r; opt_c = c;
                opt_penalty = penalty; opt_cls = clsVal;
                opt_x1 = x1; opt_y1 = y1; opt_x2 = x2; opt_y2 = y2;
            }
        }
    }

    m_api->ReleaseValue(outVals[0]);
    m_api->ReleaseValue(outVals[1]);

    // 计算解码位置与尺寸
    double pred_xs = (opt_x1 + opt_x2) / 2.0;
    double pred_ys = (opt_y1 + opt_y2) / 2.0;
    double pred_w = opt_x2 - opt_x1;
    double pred_h = opt_y2 - opt_y1;

    double diff_xs = (pred_xs - (instanceSize / 2)) / scale_z;
    double diff_ys = (pred_ys - (instanceSize / 2)) / scale_z;
    pred_w /= scale_z;
    pred_h /= scale_z;

    double lr_val = opt_penalty * opt_cls * m_config.lr;

    double res_xs = pos.x + diff_xs;
    double res_ys = pos.y + diff_ys;
    double res_w = pred_w * lr_val + (1.0 - lr_val) * sz.width;
    double res_h = pred_h * lr_val + (1.0 - lr_val) * sz.height;

    cv::Point2d newPos(res_xs, res_ys);
    cv::Size2d newSz(sz.width * (1.0 - lr_val) + lr_val * res_w,
                     sz.height * (1.0 - lr_val) + lr_val * res_h);

    // 检查是否非有限值
    if (!std::isfinite(newPos.x) || !std::isfinite(newPos.y) ||
        !std::isfinite(newSz.width) || !std::isfinite(newSz.height)) {
        res.valid = false;
        return res;
    }

    // 边界钳制
    newPos.x = std::max(0.0, std::min(static_cast<double>(m_imW), newPos.x));
    newPos.y = std::max(0.0, std::min(static_cast<double>(m_imH), newPos.y));
    newSz.width = std::max(10.0, std::min(static_cast<double>(m_imW), newSz.width));
    newSz.height = std::max(10.0, std::min(static_cast<double>(m_imH), newSz.height));

    if (updateState) {
        m_pos = newPos;
        m_sz = newSz;
    }

    auto t_end_total = std::chrono::steady_clock::now();
    m_lastTotalMs = std::chrono::duration<float, std::milli>(t_end_total - t_start_total).count();

    res.valid = true;
    res.center = cv::Point2f(static_cast<float>(newPos.x), static_cast<float>(newPos.y));
    res.size = cv::Size2f(static_cast<float>(newSz.width), static_cast<float>(newSz.height));
    res.bbox = cv::Rect2f(
        std::max(0.0f, static_cast<float>(newPos.x - newSz.width / 2.0)),
        std::max(0.0f, static_cast<float>(newPos.y - newSz.height / 2.0)),
        static_cast<float>(newSz.width),
        static_cast<float>(newSz.height)
    );
    res.score = static_cast<float>(opt_cls);

    res.dbg.instanceSize = instanceSize;
    res.dbg.cropSz = cropSz;
    res.dbg.cropXmin = cropXmin;
    res.dbg.cropYmin = cropYmin;
    res.dbg.argRow = opt_r;
    res.dbg.argCol = opt_c;
    res.dbg.clsMax = globalClsMax;

    return res;
}

SotResult OnnxSotTracker::track(const cv::Mat& bgr)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return forwardSearch(bgr, m_pos, m_sz, true);
}

SotResult OnnxSotTracker::peek(const cv::Mat& bgr, cv::Point2f center)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return forwardSearch(bgr, cv::Point2d(center.x, center.y), m_sz, false);
}

SotResult OnnxSotTracker::peekBox(const cv::Mat& bgr, const cv::Rect2f& bbox, bool useAnchorTemplate)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    cv::Point2d center(bbox.x + bbox.width * 0.5, bbox.y + bbox.height * 0.5);
    cv::Size2d sz(std::max(4.0f, bbox.width), std::max(4.0f, bbox.height));
    const std::vector<float>* featPtr = useAnchorTemplate ? &m_anchorTemplateFeat : &m_templateFeat;
    return forwardSearch(bgr, center, sz, false, featPtr);
}

void OnnxSotTracker::setState(cv::Point2f center, cv::Size2f size)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_pos.x = std::max(0.0, std::min(static_cast<double>(m_imW), static_cast<double>(center.x)));
    m_pos.y = std::max(0.0, std::min(static_cast<double>(m_imH), static_cast<double>(center.y)));
    m_sz.width = std::max(10.0, std::min(static_cast<double>(m_imW), static_cast<double>(size.width)));
    m_sz.height = std::max(10.0, std::min(static_cast<double>(m_imH), static_cast<double>(size.height)));
}

