#include "objecttracker.h"
#include "fftutils.h"
#include "logging_categories.h"
#include <cmath>
#include <algorithm>
#include <QDebug>
#include <QElapsedTimer>

// ========================
// 构造函数 / 析构函数
// ========================
ObjectTracker::ObjectTracker(QObject *parent)
    : QObject(parent)
    , m_initialized(false)
    , m_posX(0), m_posY(0)
    , m_targetW(0), m_targetH(0)
    , m_currentScale(1.0f)
    , m_modelW(0), m_modelH(0)
    , m_inOcclusion(false)
    , m_lostCount(0)
    , m_psrBaseline(0.0f)
    , m_baselineLocked(false)
    , m_padding(1.5f)           // 从 2.0 降为 1.5（目标 2.5 倍区域），减少峰值稀释
    , m_outputSigmaFactor(1.0f / 16.0f)
    , m_lambda(1e-2f)
    , m_learningRate(0.025f)
    , m_scaleStep(1.05f)
    , m_numScales(9)
    , m_minScaleFactor(0.01f)
    , m_maxScaleFactor(50.0f)
{
    // 初始化卡尔曼滤波噪声矩阵
    // 正常模式: w_sigma=10, Q = 100*I(4)
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            m_kfQ[i][j] = (i == j) ? 100.0f : 0.0f;

    // 遮挡模式: w_sigma*3=30, Q_lost = 900*I(4)（增大不确定性，恢复后更快信任新测量）
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            m_kfQLost[i][j] = (i == j) ? 900.0f : 0.0f;

    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j)
            m_kfR[i][j] = (i == j) ? 100.0f : 0.0f;  // v_sigma^2 = 10^2

    // 尺度估计参数
    m_scaleEnabled = true;
    m_scaleModelW = 0;
    m_scaleModelH = 0;
}

ObjectTracker::~ObjectTracker()
{
}

// ========================
// 重置
// ========================
void ObjectTracker::reset()
{
    m_initialized = false;
    m_inOcclusion = false;
    m_lostCount = 0;
    m_psrBaseline = 0.0f;
    m_baselineLocked = false;
    m_psrHistory.clear();
    m_hfNum.clear();
    m_hfDen.clear();
    m_hfNumBackup.clear();
    m_hfDenBackup.clear();
    // 尺度估计重置
    m_scaleEnabled = true;
    m_scaleModelW = 0;
    m_scaleModelH = 0;
    m_scaleFactors.clear();
    m_scaleWindow.clear();
    m_sfNum.clear();
    m_sfDen.clear();
    m_sfNumBackup.clear();
    m_sfDenBackup.clear();
    m_ysf.clear();
}

// ========================
// QImage转灰度矩阵
// ========================
std::vector<std::vector<float>> ObjectTracker::imageToGray(const QImage& img)
{
    if (img.isNull() || img.width() <= 0 || img.height() <= 0) {
        qWarning() << "[imageToGray] 空图像或无效尺寸";
        return {};
    }

    QImage gray = img.convertToFormat(QImage::Format_Grayscale8);
    if (gray.isNull()) {
        qWarning() << "[imageToGray] 转换为灰度图失败";
        return {};
    }

    int h = gray.height();
    int w = gray.width();
    if (h <= 0 || w <= 0) {
        qWarning() << "[imageToGray] 转换后尺寸无效:" << w << "x" << h;
        return {};
    }

    std::vector<std::vector<float>> result(h, std::vector<float>(w));
    for (int y = 0; y < h; ++y) {
        const uchar* line = gray.scanLine(y);
        if (!line) {
            qWarning() << "[imageToGray] scanLine 为空，行 y=" << y;
            break;
        }
        for (int x = 0; x < w; ++x) {
            result[y][x] = static_cast<float>(line[x]);
        }
    }
    return result;
}

// ========================
// 生成高斯标签
// ========================
std::vector<std::vector<std::complex<float>>> ObjectTracker::createGaussianLabel(
    int w, int h, float sigma)
{
    std::vector<std::vector<std::complex<float>>> label(h, std::vector<std::complex<float>>(w));
    float sigmaSq = sigma * sigma;
    for (int r = 0; r < h; ++r) {
        float ry = static_cast<float>(r - h / 2);
        for (int c = 0; c < w; ++c) {
            float cx = static_cast<float>(c - w / 2);
            float val = std::exp(-0.5f * (ry * ry + cx * cx) / sigmaSq);
            label[r][c] = std::complex<float>(val, 0.0f);
        }
    }
    // FFT2
    fft2d(label, false);
    return label;
}

// ========================
// 生成余弦窗
// ========================
std::vector<std::vector<float>> ObjectTracker::createCosWindow(int w, int h)
{
    std::vector<float> hannH = hannWindow(h);
    std::vector<float> hannW = hannWindow(w);
    std::vector<std::vector<float>> window(h, std::vector<float>(w));
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c)
            window[r][c] = hannH[r] * hannW[c];
    return window;
}

// ========================
// 初始化跟踪器
// ========================
bool ObjectTracker::init(const QImage& frame, const QRectF& targetRect)
{
    reset();
    qCDebug(trackerLog) << "[Tracker] init 开始，目标框=" << targetRect;

    if (frame.isNull() || targetRect.width() <= 0 || targetRect.height() <= 0) {
        qWarning() << "[ObjectTracker] 初始化失败: 无效图像或目标框";
        return false;
    }

    int frameW = frame.width();
    int frameH = frame.height();
    if (frameW <= 0 || frameH <= 0) {
        qWarning() << "[ObjectTracker] 初始化失败: 图像尺寸无效";
        return false;
    }

    // 将目标框裁剪到图像内部
    qreal left = std::max<qreal>(0, targetRect.left());
    qreal top = std::max<qreal>(0, targetRect.top());
    qreal right = std::min<qreal>(static_cast<qreal>(frameW), targetRect.right());
    qreal bottom = std::min<qreal>(static_cast<qreal>(frameH), targetRect.bottom());
    qreal clippedW = right - left;
    qreal clippedH = bottom - top;
    if (clippedW < 4 || clippedH < 4) {
        qWarning() << "[ObjectTracker] 初始化失败: 目标框裁剪后过小" << clippedW << "x" << clippedH;
        return false;
    }

    m_posX = (left + right) * 0.5f;
    m_posY = (top + bottom) * 0.5f;
    m_targetW = clippedW;
    m_targetH = clippedH;
    m_currentScale = 1.0f;

    int rawModelW = static_cast<int>(m_targetW * (1.0f + m_padding));
    int rawModelH = static_cast<int>(m_targetH * (1.0f + m_padding));
    rawModelW = std::clamp(rawModelW, 4, frameW);
    rawModelH = std::clamp(rawModelH, 4, frameH);

    // 直接使用原始尺寸，不强制 2 的幂次（与参考实现一致）
    // nextPow2 会过度撑大模型区域，导致响应峰值稀释、计算量爆炸
    m_modelW = rawModelW;
    m_modelH = rawModelH;
    qCDebug(trackerLog) << "[Tracker] m_model=" << m_modelW << "x" << m_modelH
             << " pos=(" << m_posX << "," << m_posY << ")"
             << " target=" << m_targetW << "x" << m_targetH;

    float sigma = std::sqrt(m_targetW * m_targetH) * m_outputSigmaFactor;
    m_yf = createGaussianLabel(m_modelW, m_modelH, sigma);

    m_cosWindow = createCosWindow(m_modelW, m_modelH);
    qCDebug(trackerLog) << "[Tracker] 高斯标签与余弦窗生成完毕";

    try {
        std::vector<std::vector<float>> image = imageToGray(frame);
        qCDebug(trackerLog) << "[Tracker] imageToGray 完成，尺寸=" << image.size()
                 << (image.empty() ? 0 : static_cast<int>(image[0].size()));

        std::vector<std::vector<float>> sample = getTranslationSample(image, m_posX, m_posY, m_currentScale);
        qCDebug(trackerLog) << "[Tracker] getTranslationSample 完成，sample=" << sample.size()
                 << (sample.empty() ? 0 : static_cast<int>(sample[0].size()));

        auto sampleComplex = realToComplex(sample);
        fft2d(sampleComplex, false);
        auto sampleConj = conjugate(sampleComplex);
        m_hfNum = elementMultiply(m_yf, sampleConj);

        m_hfDen.resize(m_modelH, std::vector<float>(m_modelW));
        for (int r = 0; r < m_modelH; ++r)
            for (int c = 0; c < m_modelW; ++c)
                m_hfDen[r][c] = std::norm(sampleComplex[r][c]);

        m_hfNumBackup = m_hfNum;
        m_hfDenBackup = m_hfDen;
        qCDebug(trackerLog) << "[Tracker] 平移滤波器训练完毕";

        // KF 初始化
        m_kf.x[0] = m_posX;
        m_kf.x[1] = m_posY;
        m_kf.x[2] = 0.0f;
        m_kf.x[3] = 0.0f;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                m_kf.P[i][j] = (i == j) ? 8.0f : 0.0f;

        // scale 边界
        float minScale = std::ceil(std::log(std::max(5.0f / m_modelW, 5.0f / m_modelH)) / std::log(m_scaleStep));
        m_minScaleFactor = std::pow(m_scaleStep, minScale);
        int imgH = frame.height();
        int imgW = frame.width();
        float maxScale = std::floor(std::log(std::min(static_cast<float>(imgW) / m_targetW, static_cast<float>(imgH) / m_targetH)) / std::log(m_scaleStep));
        m_maxScaleFactor = std::pow(m_scaleStep, maxScale);
        qCDebug(trackerLog) << "[Tracker] scale bounds: min=" << m_minScaleFactor
                 << " max=" << m_maxScaleFactor;

        if (m_scaleEnabled) {
            qCDebug(trackerLog) << "[Tracker] scale 初始化，nScales=" << m_numScales;
            m_scaleFactors.resize(m_numScales);
            int centerIdx = (m_numScales + 1) / 2;
            for (int s = 0; s < m_numScales; ++s) {
                m_scaleFactors[s] = std::pow(m_scaleStep, static_cast<float>(centerIdx - 1 - s));
            }

            m_scaleWindow = hannWindow(m_numScales);
            if (m_numScales % 2 == 0) {
                auto hw2 = hannWindow(m_numScales + 1);
                m_scaleWindow.assign(hw2.begin() + 1, hw2.end());
            }

            float scaleModelMaxArea = 512.0f;
            float scaleModelFactor = 1.0f;
            if (m_targetW * m_targetH > scaleModelMaxArea) {
                scaleModelFactor = std::sqrt(scaleModelMaxArea / (m_targetW * m_targetH));
            }
            m_scaleModelW = std::max(4, static_cast<int>(std::floor(m_targetW * scaleModelFactor)));
            m_scaleModelH = std::max(4, static_cast<int>(std::floor(m_targetH * scaleModelFactor)));
            qCDebug(trackerLog) << "[Tracker] scaleModel=" << m_scaleModelW << "x" << m_scaleModelH;

            float scaleSigma = static_cast<float>(m_numScales) / std::sqrt(static_cast<float>(m_numScales));
            std::vector<float> ys(m_numScales, 0.0f);
            for (int s = 0; s < m_numScales; ++s) {
                int ss = s - (m_numScales - 1) / 2;
                ys[s] = std::exp(-0.5f * static_cast<float>(ss * ss) / (scaleSigma * scaleSigma));
            }

            m_ysf.resize(m_numScales);
            for (int i = 0; i < m_numScales; ++i)
                m_ysf[i] = std::complex<float>(ys[i], 0.0f);
            fft1d(m_ysf, false);
            qCDebug(trackerLog) << "[Tracker] scale 标签 FFT 完毕";

            auto scaleSample = getScaleSample(image, m_posX, m_posY);
            qCDebug(trackerLog) << "[Tracker] scaleSample 完成，rows="
                     << scaleSample.size()
                     << (scaleSample.empty() ? 0 : static_cast<int>(scaleSample[0].size()));
            if (!scaleSample.empty()) {
                initScaleFilter(scaleSample);
                qCDebug(trackerLog) << "[Tracker] initScaleFilter 完成";
            } else {
                qWarning() << "[ObjectTracker] 尺度样本为空，禁用尺度估计";
                m_scaleEnabled = false;
            }
        }
    } catch (const std::exception &e) {
        qCritical() << "[Tracker] 初始化阶段异常:" << e.what();
        return false;
    } catch (...) {
        qCritical() << "[Tracker] 初始化阶段未知异常";
        return false;
    }

    m_initialized = true;
    qCDebug(trackerLog) << "[ObjectTracker] 初始化成功, 模型尺寸:" << m_modelW << "x" << m_modelH
             << "目标:(" << m_posX << "," << m_posY << ") " << m_targetW << "x" << m_targetH;
    return true;
}

// ========================
// 提取平移样本
// ========================
std::vector<std::vector<float>> ObjectTracker::getTranslationSample(
    const std::vector<std::vector<float>>& image,
    float cx, float cy, float scale)
{
    // NaN/Inf 防护：防止卡尔曼滤波发散后传入非法尺度
    if (std::isnan(scale) || std::isinf(scale) || scale < 0.01f) {
        qWarning() << "[getTranslationSample] 非法 scale=" << scale
                     << "，回退为 1.0，pos=(" << cx << "," << cy << ")";
        scale = 1.0f;
    }

    int patchW = static_cast<int>(static_cast<float>(m_modelW) * scale);
    int patchH = static_cast<int>(static_cast<float>(m_modelH) * scale);
    if (patchW < 2) patchW = 2;
    if (patchH < 2) patchH = 2;
    // 防超大patch：限制到最大图像尺寸的一半
    int imgH = static_cast<int>(image.size());
    int imgW = imgH > 0 ? static_cast<int>(image[0].size()) : 0;
    if (imgW > 0 && patchW > imgW) patchW = imgW;
    if (imgH > 0 && patchH > imgH) patchH = imgH;

    if (imgW > 0) cx = std::max(0.0f, std::min(static_cast<float>(imgW - 1), cx));
    if (imgH > 0) cy = std::max(0.0f, std::min(static_cast<float>(imgH - 1), cy));

    auto patch = extractGrayPatch(image, cx, cy, patchW, patchH);
    patch = resizePatch(patch, m_modelH, m_modelW);

    // 特征归一化（零均值、单位方差）—— 与参考实现一致
    // 不归一化会导致相关滤波器响应平坦、峰值不突出
    float sum = 0.0f;
    int count = 0;
    for (int r = 0; r < m_modelH; ++r) {
        for (int c = 0; c < m_modelW; ++c) {
            sum += patch[r][c];
            count++;
        }
    }
    float mean = (count > 0) ? sum / count : 0.0f;
    float sqSum = 0.0f;
    for (int r = 0; r < m_modelH; ++r)
        for (int c = 0; c < m_modelW; ++c) {
            float d = patch[r][c] - mean;
            sqSum += d * d;
        }
    float stddev = (count > 0) ? std::sqrt(sqSum / count) : 1.0f;
    if (stddev < 1e-6f) stddev = 1e-6f;

    // 应用余弦窗（先归一化再加窗，与参考实现顺序一致）
    for (int r = 0; r < m_modelH; ++r)
        for (int c = 0; c < m_modelW; ++c)
            patch[r][c] = ((patch[r][c] - mean) / stddev) * m_cosWindow[r][c];

    return patch;
}

// ========================
// 计算响应图
// ========================
std::vector<std::vector<float>> ObjectTracker::computeResponse(
    const std::vector<std::vector<float>>& sample)
{
    auto sampleComplex = realToComplex(sample);
    fft2d(sampleComplex, false);

    // response = ifft2( sum(hf_num .* xtf, 3) ./ (hf_den + lambda) )
    auto num = elementMultiply(m_hfNum, sampleComplex);
    auto responseComplex = elementDivide(num, realToComplex(m_hfDen), m_lambda);
    fft2d(responseComplex, true);

    return complexToReal(responseComplex);
}

// ========================
// 查找最大响应位置
// ========================
float ObjectTracker::findMaxResponse(const std::vector<std::vector<float>>& response, int& row, int& col)
{
    float maxVal = -1e10f;
    int h = static_cast<int>(response.size());
    int w = h > 0 ? static_cast<int>(response[0].size()) : 0;
    // 默认回退到中心位置
    row = h / 2;
    col = w / 2;
    bool foundValid = false;
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            float v = response[r][c];
            if (!std::isnan(v) && !std::isinf(v) && v > maxVal) {
                maxVal = v;
                row = r;
                col = c;
                foundValid = true;
            }
        }
    }
    if (!foundValid) {
        qWarning() << "[findMaxResponse] 响应图中无有效数据，回退到中心位置";
        maxVal = 0.0f;
    }
    return maxVal;
}

// ========================
// 计算PSR
// ========================
float ObjectTracker::computePSR(const std::vector<std::vector<float>>& response)
{
    int h = static_cast<int>(response.size());
    int w = h > 0 ? static_cast<int>(response[0].size()) : 0;

    int maxR, maxC;
    float maxVal = findMaxResponse(response, maxR, maxC);

    // 抑制峰值区域（半径5）
    const int radius = 5;
    float sum = 0.0f;
    float sumSq = 0.0f;
    int count = 0;

    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            if (std::abs(r - maxR) > radius || std::abs(c - maxC) > radius) {
                sum += response[r][c];
                sumSq += response[r][c] * response[r][c];
                count++;
            }
        }
    }

    if (count == 0) return 0.0f;

    float mean = sum / count;
    float variance = sumSq / count - mean * mean;
    float stddev = std::sqrt(std::max(0.0f, variance));

    if (stddev < 1e-6f) stddev = 1e-6f;

    return (maxVal - mean) / stddev;
}

// ========================
// 卡尔曼滤波预测
// ========================
void ObjectTracker::kfPredict()
{
    // NaN/溢出检测：如果状态或协方差已经发散，重置为有效位置
    for (int i = 0; i < 4; ++i) {
        if (std::isnan(m_kf.x[i]) || std::isinf(m_kf.x[i]) || std::abs(m_kf.x[i]) > 1e6f) {
            m_kf.x[i] = 0.0f;
            if (i == 0) m_kf.x[i] = m_posX;
            if (i == 1) m_kf.x[i] = m_posY;
        }
    }
    // P 矩阵保护：如果协方差矩阵有 NaN/Inf/超大数据，重置为初始值
    bool pDirty = false;
    for (int i = 0; i < 4 && !pDirty; ++i) {
        for (int j = 0; j < 4 && !pDirty; ++j) {
            if (std::isnan(m_kf.P[i][j]) || std::isinf(m_kf.P[i][j]) || std::abs(m_kf.P[i][j]) > 1e8f) {
                pDirty = true;
            }
        }
    }
    if (pDirty) {
        qWarning() << "[kfPredict] P矩阵异常，已重置为初始值";
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                m_kf.P[i][j] = (i == j) ? 8.0f : 0.0f;
    }

    // 状态转移矩阵 A = [1 0 T 0; 0 1 0 T; 0 0 1 0; 0 0 0 1], T=1
    float xPred[4];
    xPred[0] = m_kf.x[0] + m_kf.x[2];
    xPred[1] = m_kf.x[1] + m_kf.x[3];
    xPred[2] = m_kf.x[2];
    xPred[3] = m_kf.x[3];

    // P_pred = A*P*A' + Q  (完整公式，A=[1 0 T 0; 0 1 0 T; 0 0 1 0; 0 0 0 1], T=1)
    // 直接展开 A*P*A'，利用A的特殊结构：
    // A*P 的第0行 = P[0] + P[2]，第1行 = P[1] + P[3]，第2行 = P[2]，第3行 = P[3]
    // 再乘 A'（=A^T），得到完整的协方差传播
    const auto& P = m_kf.P;
    float PPred[4][4] = {
        { P[0][0] + 2*P[0][2] + P[2][2],   P[0][1] + P[0][3] + P[1][2] + P[2][3],   P[0][2] + P[2][2],             P[0][3] + P[2][3] },
        { P[1][0] + P[3][0] + P[1][2] + P[3][2],   P[1][1] + 2*P[1][3] + P[3][3],         P[1][2] + P[3][2],             P[1][3] + P[3][3] },
        { P[2][0] + P[2][2],                     P[2][1] + P[2][3],                       P[2][2],                         P[2][3] },
        { P[3][0] + P[3][2],                     P[3][1] + P[3][3],                       P[3][2],                         P[3][3] }
    };
    // PPred = A*P*A' + Q (使用当前Q矩阵，遮挡时Q更大)
    // 根据遮挡状态选择Q：遮挡时使用更大的过程噪声，允许位置不确定性更快增长
    const float (*activeQ)[4] = m_inOcclusion ? m_kfQLost : m_kfQ;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            PPred[i][j] += activeQ[i][j];

    for (int i = 0; i < 4; ++i)
        m_kf.x[i] = xPred[i];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            m_kf.P[i][j] = PPred[i][j];
}

// ========================
// 卡尔曼滤波更新
// ========================
void ObjectTracker::kfUpdate(float measX, float measY)
{
    // 测量值有效性检查：NaN或过大的测量值会导致状态发散
    if (std::isnan(measX) || std::isinf(measX)) measX = m_kf.x[0];
    if (std::isnan(measY) || std::isinf(measY)) measY = m_kf.x[1];

    // H = [1 0 0 0; 0 1 0 0]
    // S = H*P*H' + R
    float S[2][2];
    S[0][0] = m_kf.P[0][0] + m_kfR[0][0];
    S[0][1] = m_kf.P[0][1] + m_kfR[0][1];
    S[1][0] = m_kf.P[1][0] + m_kfR[1][0];
    S[1][1] = m_kf.P[1][1] + m_kfR[1][1];

    // 防止S元素溢出（当P变得非常大时）
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j)
            if (std::isnan(S[i][j]) || std::isinf(S[i][j]) || std::abs(S[i][j]) > 1e10f)
                S[i][j] = (i == j) ? m_kfR[i][j] : 0.0f;

    // det(S) 修复P1#5: 分母添加epsilon=1e-10f防护除零
    float detS = S[0][0] * S[1][1] - S[0][1] * S[1][0];
    static constexpr float epsilon = 1e-10f;
    if (std::abs(detS) < epsilon) detS = epsilon;

    // K = P*H'*inv(S)
    float K[4][2];
    K[0][0] = (m_kf.P[0][0] * S[1][1] - m_kf.P[0][1] * S[1][0]) / detS;
    K[0][1] = (-m_kf.P[0][0] * S[0][1] + m_kf.P[0][1] * S[0][0]) / detS;
    K[1][0] = (m_kf.P[1][0] * S[1][1] - m_kf.P[1][1] * S[1][0]) / detS;
    K[1][1] = (-m_kf.P[1][0] * S[0][1] + m_kf.P[1][1] * S[0][0]) / detS;
    K[2][0] = (m_kf.P[2][0] * S[1][1] - m_kf.P[2][1] * S[1][0]) / detS;
    K[2][1] = (-m_kf.P[2][0] * S[0][1] + m_kf.P[2][1] * S[0][0]) / detS;
    K[3][0] = (m_kf.P[3][0] * S[1][1] - m_kf.P[3][1] * S[1][0]) / detS;
    K[3][1] = (-m_kf.P[3][0] * S[0][1] + m_kf.P[3][1] * S[0][0]) / detS;

    // 更新状态
    float y[2] = { measX - m_kf.x[0], measY - m_kf.x[1] };
    for (int i = 0; i < 4; ++i)
        m_kf.x[i] += K[i][0] * y[0] + K[i][1] * y[1];

    // 状态溢出保护：如果任何状态值非法，重置为安全值
    for (int i = 0; i < 4; ++i) {
        if (std::isnan(m_kf.x[i]) || std::isinf(m_kf.x[i]) || std::abs(m_kf.x[i]) > 1e6f) {
            m_kf.x[i] = 0.0f;
            if (i == 0) m_kf.x[i] = m_posX;
            if (i == 1) m_kf.x[i] = m_posY;
        }
    }

    // 更新协方差 P = (I - K*H)*P
    float I_KH[4][4];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            I_KH[i][j] = (i == j) ? 1.0f : 0.0f;
    I_KH[0][0] -= K[0][0];
    I_KH[0][1] -= K[0][1];
    I_KH[1][0] -= K[1][0];
    I_KH[1][1] -= K[1][1];
    I_KH[2][0] -= K[2][0];
    I_KH[2][1] -= K[2][1];
    I_KH[3][0] -= K[3][0];
    I_KH[3][1] -= K[3][1];

    float PNew[4][4];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            PNew[i][j] = 0.0f;
            for (int k = 0; k < 4; ++k)
                PNew[i][j] += I_KH[i][k] * m_kf.P[k][j];
        }

    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            m_kf.P[i][j] = PNew[i][j];

    // 修复P1#4: 协方差矩阵对称化 P = (P + P')/2，防止数值误差导致非对称
    for (int i = 0; i < 4; ++i) {
        for (int j = i + 1; j < 4; ++j) {
            float avg = (m_kf.P[i][j] + m_kf.P[j][i]) * 0.5f;
            m_kf.P[i][j] = avg;
            m_kf.P[j][i] = avg;
        }
    }

    // P矩阵溢出保护：如果协方差变得非常大，重置为初始值
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            if (std::isnan(m_kf.P[i][j]) || std::isinf(m_kf.P[i][j])
                || std::abs(m_kf.P[i][j]) > 1e10f) {
                m_kf.P[i][j] = (i == j) ? 8.0f : 0.0f;
            }
        }
    }
}

// ========================
// 更新滤波器（支持自适应学习率）
// ========================
void ObjectTracker::updateFilter(const std::vector<std::vector<float>>& sample,
                                  bool adaptAppearance)
{
    auto sampleComplex = realToComplex(sample);
    fft2d(sampleComplex, false);
    auto sampleConj = conjugate(sampleComplex);

    // 新模型
    auto newNum = elementMultiply(m_yf, sampleConj);
    std::vector<std::vector<float>> newDen(m_modelH, std::vector<float>(m_modelW));
    for (int r = 0; r < m_modelH; ++r)
        for (int c = 0; c < m_modelW; ++c)
            newDen[r][c] = std::norm(sampleComplex[r][c]);

    // 自适应学习率：PSR下降过渡态时加速外观适应（与MATLAB一致）
    float lr = adaptAppearance
        ? std::min(m_learningRate * 3.0f, 0.15f)  // 加速适应，上限0.15
        : m_learningRate;

    // 滑动平均更新
    for (int r = 0; r < m_modelH; ++r) {
        for (int c = 0; c < m_modelW; ++c) {
            m_hfNum[r][c] = (1.0f - lr) * m_hfNum[r][c] + lr * newNum[r][c];
            m_hfDen[r][c] = (1.0f - lr) * m_hfDen[r][c] + lr * newDen[r][c];
        }
    }
}

// ========================
// 遮挡重检测
// ========================
bool ObjectTracker::reDetect(const std::vector<std::vector<float>>& image,
                                float, float)
{
    if (!m_baselineLocked) return false;

    float kfX = m_kf.x[0];
    float kfY = m_kf.x[1];
    float vx = m_kf.x[2];
    float vy = m_kf.x[3];
    float speed = std::sqrt(vx * vx + vy * vy);
    int searchRadius = std::max(static_cast<int>(std::max(m_targetW, m_targetH) * 2.0f),
                                 static_cast<int>(speed * 4.0f));

    const int offsets[9][2] = {
        {-searchRadius, -searchRadius}, {-searchRadius, 0}, {-searchRadius, searchRadius},
        {0, -searchRadius}, {0, 0}, {0, searchRadius},
        {searchRadius, -searchRadius}, {searchRadius, 0}, {searchRadius, searchRadius}
    };

    float bestPsr = 0.0f;
    int bestRow = m_modelH / 2;
    int bestCol = m_modelW / 2;
    float bestPosX = kfX;
    float bestPosY = kfY;

    // 保存当前滤波器，切换到备份滤波器进行重检测
    auto savedHfNum = m_hfNum;
    auto savedHfDen = m_hfDen;
    m_hfNum = m_hfNumBackup;
    m_hfDen = m_hfDenBackup;

    for (int i = 0; i < 9; ++i) {
        float sx = kfX + offsets[i][1];
        float sy = kfY + offsets[i][0];

        auto sample = getTranslationSample(image, sx, sy, m_currentScale);
        auto response = computeResponse(sample);
        int row, col;
        findMaxResponse(response, row, col);  // 返回的maxVal由PSR函数内部使用，此处只取行列
        float psr = computePSR(response);

        if (psr > bestPsr) {
            bestPsr = psr;
            bestRow = row;
            bestCol = col;
            bestPosX = sx;
            bestPosY = sy;
        }
    }

    // 恢复当前滤波器
    m_hfNum = savedHfNum;
    m_hfDen = savedHfDen;

    float recoverThr = 0.8f * m_psrBaseline;
    if (bestPsr > recoverThr) {
        float dx = (-m_modelW / 2.0f + bestCol) * m_currentScale;
        float dy = (-m_modelH / 2.0f + bestRow) * m_currentScale;
        float newX = bestPosX + dx;
        float newY = bestPosY + dy;

        // 验证与KF预测的距离
        float dist = std::sqrt((newX - kfX) * (newX - kfX) + (newY - kfY) * (newY - kfY));
        float maxDist = std::max(m_targetW, m_targetH) * 4.0f;

        if (dist < maxDist) {
            m_posX = newX;
            m_posY = newY;

            // 融合恢复模型（备份60% + 当前40%）
            for (int r = 0; r < m_modelH; ++r) {
                for (int c = 0; c < m_modelW; ++c) {
                    m_hfNum[r][c] = 0.6f * m_hfNumBackup[r][c] + 0.4f * m_hfNum[r][c];
                    m_hfDen[r][c] = 0.6f * m_hfDenBackup[r][c] + 0.4f * m_hfDen[r][c];
                }
            }

            // KF更新（标准卡尔曼：仅位置测量，速度隐式估计）
            kfUpdate(m_posX, m_posY);
            return true;
        }
    }
    return false;
}

// 跨线程帧处理槽函数（在 worker thread 中执行）
void ObjectTracker::processFrameSlot(const QImage& frame)
{
    try {
        TrackResult result = update(frame);
        emit trackingDone(result);
    } catch (const std::exception &e) {
        qCritical() << "[Tracker] processFrameSlot 异常:" << e.what();
        TrackResult err;
        err.valid = false;
        emit trackingDone(err);
    } catch (...) {
        qCritical() << "[Tracker] processFrameSlot 未知异常";
        TrackResult err;
        err.valid = false;
        emit trackingDone(err);
    }
}

TrackResult ObjectTracker::update(const QImage& frame)
{
    QElapsedTimer timer;
    timer.start();

    TrackResult result;
    if (!m_initialized) {
        qWarning() << "[Tracker] update: 未初始化，直接返回";
        return result;
    }

    qCDebug(trackerLog) << "[Tracker] update 开始，frame=" << frame.width() << "x" << frame.height()
             << " format=" << static_cast<int>(frame.format())
             << " model=" << m_modelW << "x" << m_modelH
             << " pos=(" << m_posX << "," << m_posY << ") scale=" << m_currentScale;

    std::vector<std::vector<float>> image = imageToGray(frame);
    if (image.empty()) {
        qWarning() << "[Tracker] 图像灰度转换失败，跳过当前帧";
        return result;
    }
    qCDebug(trackerLog) << "[Tracker] 图像灰度转换完成，尺寸="
             << image.size() << "x" << (image.empty() ? 0 : image[0].size());

    // 保存当前位置作为上一帧位置（用于KF速度计算和重检测）
    float prevPosX = m_posX;
    float prevPosY = m_posY;

    // === KF预测 ===
    kfPredict();
    float kfPredX = m_kf.x[0];
    float kfPredY = m_kf.x[1];
    qCDebug(trackerLog) << "[Tracker] KF预测=(" << kfPredX << "," << kfPredY << ")";

    // === 提取检测样本（以KF预测位置为中心，加快运动目标鲁棒性）===
    auto sample = getTranslationSample(image, kfPredX, kfPredY, m_currentScale);
    if (sample.empty() || sample[0].empty()) {
        qWarning() << "[Tracker] 平移采样为空，跳过当前帧";
        return result;
    }
    qCDebug(trackerLog) << "[Tracker] 采样完成，sample=" << sample.size() << "x" << sample[0].size()
             << "开始响应计算";
    auto response = computeResponse(sample);
    if (response.empty() || response[0].empty()) {
        qWarning() << "[Tracker] 响应图为空，跳过当前帧";
        return result;
    }
    qCDebug(trackerLog) << "[Tracker] 响应计算完成，response 尺寸=" << response.size()
             << (response.empty() ? 0 : response[0].size());

    int maxRow = 0, maxCol = 0;
    findMaxResponse(response, maxRow, maxCol);
    float psr = computePSR(response);
    qCDebug(trackerLog) << "[Tracker] maxResponseIdx=(" << maxRow << "," << maxCol << ") psr=" << psr;

    // NaN/Inf 防护：如果检测位置发散，回退到 KF 预测位置
    float detX = kfPredX + (-m_modelW / 2.0f + maxCol) * m_currentScale;
    float detY = kfPredY + (-m_modelH / 2.0f + maxRow) * m_currentScale;
    if (std::isnan(detX) || std::isinf(detX)) detX = m_posX;
    if (std::isnan(detY) || std::isinf(detY)) detY = m_posY;
    if (std::isnan(m_currentScale) || std::isinf(m_currentScale) || m_currentScale < 0.01f) {
        qWarning() << "[Tracker] m_currentScale 非法=" << m_currentScale << "，重置为 1.0";
        m_currentScale = 1.0f;
    }

    // === 尺度估计（FHOG特征 + 多尺度滤波器） ===
    if (m_scaleEnabled) {
        auto scaleSample = getScaleSample(image, detX, detY);
        if (!scaleSample.empty()) {
            int bestScaleIdx = computeScaleResponse(scaleSample);
            // 更新尺度因子（限制范围）
            if (bestScaleIdx >= 0 && bestScaleIdx < static_cast<int>(m_scaleFactors.size())) {
                float sf = m_scaleFactors[bestScaleIdx];
                if (std::isnan(sf) || std::isinf(sf)) sf = 1.0f;
                float newScale = m_currentScale * sf;
                if (newScale < m_minScaleFactor) newScale = m_minScaleFactor;
                if (newScale > m_maxScaleFactor) newScale = m_maxScaleFactor;
                m_currentScale = newScale;
            }
        }
    }

    result.confidence = psr;
    result.occluded = m_inOcclusion;
    result.recovered = false;

    // === 决策逻辑（与MATLAB dsst_kf.m 一致的三态机）===
    bool adaptAppearance = false;  // 自适应外观学习率标志

    if (!m_inOcclusion) {
        bool lowPsr = m_baselineLocked && psr < 0.75f * m_psrBaseline;

        if (lowPsr) {
            m_lostCount++;
        } else {
            m_lostCount = 0;
        }

        if (m_lostCount >= 3) {
            // 连续低PSR -> 确认遮挡，切换到纯KF预测模式
            m_inOcclusion = true;
            m_lostCount = 1;
            // 使用KF预测位置（与MATLAB一致：遮挡时 Xk=Xk_pred, P=P_pred）
            m_posX = kfPredX;
            m_posY = kfPredY;
            result.occluded = true;
            emit trackingLost();
        } else if (m_lostCount > 0) {
            // PSR下降过渡态（未确认遮挡）
            // MATLAB: 使用检测位置但保持KF为预测态，防止污染
            m_posX = detX;
            m_posY = detY;
            adaptAppearance = true;  // 标记需要加速外观适应
        } else {
            // 正常跟踪
            m_posX = detX;
            m_posY = detY;
            kfUpdate(m_posX, m_posY);

            // 收集PSR基线（baseline锁定后限制历史长度，防止内存无限增长）
            if (!m_baselineLocked) {
                m_psrHistory.push_back(psr);
                if (static_cast<int>(m_psrHistory.size()) >= 30) {
                    float sum = 0.0f;
                    for (float v : m_psrHistory) sum += v;
                    m_psrBaseline = sum / m_psrHistory.size();
                    m_baselineLocked = true;
                    m_psrHistory.clear();  // 锁定后释放，不再需要
                }
            }
        }
    } else {
        // 遮挡中（使用KF预测位置）
        if (m_lostCount >= 10) {
            // 尝试重检测（传入上一帧位置用于正确的KF速度计算）
            if (reDetect(image, prevPosX, prevPosY)) {
                m_inOcclusion = false;
                m_lostCount = 0;
                result.recovered = true;
                result.occluded = false;
                emit trackingRecovered();
                // reDetect() 已混合平移滤波器 (0.6*备份+0.4*当前)，此处仅恢复尺度滤波器
                if (!m_sfNumBackup.empty()) {
                    m_sfNum = m_sfNumBackup;
                    m_sfDen = m_sfDenBackup;
                }
            } else {
                m_posX = kfPredX;
                m_posY = kfPredY;
                m_lostCount++;
            }
        } else {
            m_posX = kfPredX;
            m_posY = kfPredY;
            m_lostCount++;
        }
    }

    // === 模板更新（仅在非遮挡时）===
    if (!m_inOcclusion) {
        auto updateSample = getTranslationSample(image, m_posX, m_posY, m_currentScale);
        updateFilter(updateSample, adaptAppearance);

        // 尺度滤波器更新
        if (m_scaleEnabled && m_lostCount == 0) {
            auto scaleUpdateSample = getScaleSample(image, m_posX, m_posY);
            if (!scaleUpdateSample.empty()) {
                updateScaleFilter(scaleUpdateSample);
                // 定期备份尺度滤波器
                if (m_baselineLocked) {
                    m_sfNumBackup = m_sfNum;
                    m_sfDenBackup = m_sfDen;
                }
            }
        }

        // 定期备份模型
        if (m_lostCount == 0 && m_baselineLocked) {
            m_hfNumBackup = m_hfNum;
            m_hfDenBackup = m_hfDen;
        }
    }

    // === 构建结果 ===
    float currentW = m_targetW * m_currentScale;
    float currentH = m_targetH * m_currentScale;
    result.bbox = QRectF(m_posX - currentW / 2.0f, m_posY - currentH / 2.0f,
                         currentW, currentH);
    result.valid = true;
    result.fps = 1000.0f / std::max(1LL, timer.elapsed());

    // 提取KF内部状态供伺服控制器使用（前馈+PID）
    result.kfX  = m_kf.x[0];  // KF滤波后的平滑位置X
    result.kfY  = m_kf.x[1];  // KF滤波后的平滑位置Y
    result.kfVx = m_kf.x[2];  // KF估计的目标速度X（像素/帧）
    result.kfVy = m_kf.x[3];  // KF估计的目标速度Y（像素/帧）

    return result;
}

// ========================
// 获取当前目标框
// ========================
QRectF ObjectTracker::getCurrentBBox() const
{
    float currentW = m_targetW * m_currentScale;
    float currentH = m_targetH * m_currentScale;
    return QRectF(m_posX - currentW / 2.0f, m_posY - currentH / 2.0f,
                  currentW, currentH);
}

// ========================
// 尺度估计方法实现（像素展平方式，与参考实现一致）
// ========================

// 提取尺度样本：对 nScales 个尺度分别提取图像块 → 缩放到 scale_model_sz → 灰度归一化 → 展平
// 返回 [scaleModelW*scaleModelH × nScales] 矩阵
std::vector<std::vector<float>> ObjectTracker::getScaleSample(
    const std::vector<std::vector<float>>& image,
    float cx, float cy)
{
    int imgH = static_cast<int>(image.size());
    int imgW = imgH > 0 ? static_cast<int>(image[0].size()) : 0;
    if (imgH < m_scaleModelH + 1 || imgW < m_scaleModelW + 1) return {};

    int nPixels = m_scaleModelW * m_scaleModelH;
    std::vector<std::vector<float>> result(nPixels, std::vector<float>(m_numScales, 0.0f));

    cx = std::max(0.0f, std::min(static_cast<float>(imgW - 1), cx));
    cy = std::max(0.0f, std::min(static_cast<float>(imgH - 1), cy));

    for (int s = 0; s < m_numScales; ++s) {
        int patchW = static_cast<int>(std::floor(m_targetW * m_scaleFactors[s]));
        int patchH = static_cast<int>(std::floor(m_targetH * m_scaleFactors[s]));
        if (patchW < 4) patchW = 4;
        if (patchH < 4) patchH = 4;

        auto patch = extractGrayPatch(image, cx, cy, patchW, patchH);
        patch = resizePatch(patch, m_scaleModelH, m_scaleModelW);

        // 灰度归一化（与参考实现一致）
        float sum = 0.0f;
        int count = 0;
        for (int r = 0; r < m_scaleModelH; ++r)
            for (int c = 0; c < m_scaleModelW; ++c) {
                sum += patch[r][c];
                count++;
            }
        float mean = (count > 0) ? sum / count : 0.0f;
        float sqSum = 0.0f;
        for (int r = 0; r < m_scaleModelH; ++r)
            for (int c = 0; c < m_scaleModelW; ++c) {
                float d = patch[r][c] - mean;
                sqSum += d * d;
            }
        float stddev = (count > 0) ? std::sqrt(sqSum / count) : 1.0f;
        if (stddev < 1e-6f) stddev = 1e-6f;

        // 展平为一维向量并应用尺度窗
        float windowVal = (s < static_cast<int>(m_scaleWindow.size())) ? m_scaleWindow[s] : 1.0f;
        for (int r = 0; r < m_scaleModelH; ++r)
            for (int c = 0; c < m_scaleModelW; ++c) {
                int idx = r * m_scaleModelW + c;
                if (idx < nPixels)
                    result[idx][s] = ((patch[r][c] - mean) / stddev) * windowVal;
            }
    }

    return result;
}

// 初始化尺度滤波器（首帧调用）
void ObjectTracker::initScaleFilter(const std::vector<std::vector<float>>& scaleSample)
{
    if (scaleSample.empty() || scaleSample[0].empty()) return;

    int nPixels = static_cast<int>(scaleSample.size());
    int nScalesLocal = static_cast<int>(scaleSample[0].size());

    // sf_num: [nPixels][nScales] 频域分子
    // sf_den: [nScales] 频域能量分母（跨像素求和）
    m_sfNum.resize(nPixels);
    m_sfDen.assign(nScalesLocal, 0.0f);

    for (int p = 0; p < nPixels; ++p) {
        std::vector<std::complex<float>> xsfFft(nScalesLocal);
        for (int s = 0; s < nScalesLocal; ++s)
            xsfFft[s] = std::complex<float>(scaleSample[p][s], 0.0f);
        fft1d(xsfFft, false);

        m_sfNum[p].resize(nScalesLocal);
        for (int s = 0; s < nScalesLocal; ++s) {
            m_sfNum[p][s] = xsfFft[s] * std::conj(m_ysf[s]);
            m_sfDen[s] += std::norm(xsfFft[s]);
        }
    }

    m_sfNumBackup = m_sfNum;
    m_sfDenBackup = m_sfDen;

    qCDebug(trackerLog) << "[ObjectTracker] 尺度滤波器初始化完成, 像素数:" << nPixels
             << "尺度:" << nScalesLocal
             << "模型尺寸:" << m_scaleModelW << "x" << m_scaleModelH;
}

// 计算尺度响应，返回最佳尺度索引
int ObjectTracker::computeScaleResponse(const std::vector<std::vector<float>>& scaleSample)
{
    int centerIdx = (m_numScales - 1) / 2;
    if (scaleSample.empty() || m_sfNum.empty() || m_sfDen.empty())
        return centerIdx;

    int nPixels = static_cast<int>(scaleSample.size());
    int nScalesLocal = static_cast<int>(scaleSample[0].size());
    if (nScalesLocal == 0)
        return centerIdx;

    // 对每个像素行做 FFT，累加分子
    std::vector<std::complex<float>> sumNum(nScalesLocal, std::complex<float>(0, 0));
    for (int p = 0; p < nPixels; ++p) {
        if (static_cast<int>(scaleSample[p].size()) != nScalesLocal) continue;
        std::vector<std::complex<float>> xsf(nScalesLocal);
        for (int s = 0; s < nScalesLocal; ++s) {
            float sv = scaleSample[p][s];
            if (std::isnan(sv) || std::isinf(sv)) sv = 0.0f;
            xsf[s] = std::complex<float>(sv, 0.0f);
        }
        fft1d(xsf, false);

        for (int s = 0; s < nScalesLocal; ++s) {
            sumNum[s] += m_sfNum[p][s] * std::conj(xsf[s]);
        }
    }

    // 除以分母
    std::vector<std::complex<float>> sumResponse(nScalesLocal, std::complex<float>(0, 0));
    for (int s = 0; s < nScalesLocal; ++s) {
        float den = m_sfDen[s] + m_lambda;
        if (den < 1e-6f) den = 1e-6f;
        if (std::isnan(sumNum[s].real()) || std::isinf(sumNum[s].real())) {
            sumResponse[s] = std::complex<float>(0.0f, 0.0f);
        } else {
            sumResponse[s] = sumNum[s] / std::complex<float>(den, 0.0f);
        }
    }

    // IFFT
    fft1d(sumResponse, true);

    // 找最大响应索引
    int bestIdx = centerIdx;
    float maxVal = -1e10f;
    bool foundValid = false;
    for (int s = 0; s < nScalesLocal; ++s) {
        float rv = sumResponse[s].real();
        if (!std::isnan(rv) && !std::isinf(rv) && rv > maxVal) {
            maxVal = rv;
            bestIdx = s;
            foundValid = true;
        }
    }
    if (!foundValid) bestIdx = centerIdx;
    if (bestIdx < 0) bestIdx = 0;
    if (bestIdx >= static_cast<int>(m_scaleFactors.size()))
        bestIdx = static_cast<int>(m_scaleFactors.size()) - 1;

    return bestIdx;
}

// 更新尺度滤波器
void ObjectTracker::updateScaleFilter(const std::vector<std::vector<float>>& scaleSample)
{
    if (scaleSample.empty() || m_sfNum.empty() || m_sfDen.empty()) return;

    int nPixels = static_cast<int>(scaleSample.size());
    int nScalesLocal = static_cast<int>(scaleSample[0].size());

    std::vector<float> newDen(nScalesLocal, 0.0f);
    std::vector<std::vector<std::complex<float>>> newNum(nPixels);

    for (int p = 0; p < nPixels; ++p) {
        std::vector<std::complex<float>> newXsf(nScalesLocal);
        for (int s = 0; s < nScalesLocal; ++s)
            newXsf[s] = std::complex<float>(scaleSample[p][s], 0.0f);
        fft1d(newXsf, false);

        newNum[p].resize(nScalesLocal);
        for (int s = 0; s < nScalesLocal; ++s) {
            newNum[p][s] = m_ysf[s] * std::conj(newXsf[s]);
            newDen[s] += std::norm(newXsf[s]);
        }
    }

    // 滑动平均更新
    for (int p = 0; p < nPixels; ++p) {
        for (int s = 0; s < nScalesLocal; ++s) {
            m_sfNum[p][s] = (1.0f - m_learningRate) * m_sfNum[p][s] + m_learningRate * newNum[p][s];
        }
    }
    for (int s = 0; s < nScalesLocal; ++s) {
        m_sfDen[s] = (1.0f - m_learningRate) * m_sfDen[s] + m_learningRate * newDen[s];
    }
}
