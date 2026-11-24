#include "objecttracker.h"
#include "imagematconvert.h"
#include "logging_categories.h"
#include "onnxdetector.h"
#include <cmath>
#include <QElapsedTimer>
#include <QFile>
#include <QCoreApplication>
#include <QDir>
#include <algorithm>

// ========================
// 构造函数 / 析构函数
// ========================
ObjectTracker::ObjectTracker(QObject *parent)
    : QObject(parent)
    , m_kfQ(cv::Matx44f::eye() * 100.0f)
    , m_kfQLost(cv::Matx44f::eye() * 900.0f)
    , m_kfR(cv::Matx22f::eye() * 100.0f)
    , m_evaluator(10, 0.65f, 0.5f)
{
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
    m_trackingState = AMFTTrackingState::TRACK_NORMAL;
    m_lostCount = 0;
    m_frameCount = 0;
    m_needScaleCalibration = false;
    m_zoomCalibRetries = 0;
    m_reacquireConsecutiveHits = 0;
    m_candidateBox = DetectionBox();
    m_anchor = TargetAnchor();
    m_evaluator.reset();
    m_lastScore = AMFTScore();
}

QString ObjectTracker::modelSize() const
{
    if (m_kernel == TrackingKernel::KERNEL_LIGHTTRACK) {
        return m_sot ? m_sot->name() : QStringLiteral("LightTrack-Uninitialized");
    }
    const cv::Size ms = m_core.modelSize();
    return QString("%1x%2").arg(ms.width).arg(ms.height);
}

// ========================
// 跟踪内核重置与前向分派
// ========================
bool ObjectTracker::reinitKernel(const cv::Mat& bgr, const cv::Rect& roi)
{
    if (m_kernel == TrackingKernel::KERNEL_LIGHTTRACK) {
        if (!m_sot) {
            qWarning() << "[ObjectTracker] reinitKernel: LightTrack 引擎未注入";
            return false;
        }
        if (!m_sot->ensureLoaded()) {
            qWarning() << "[ObjectTracker] reinitKernel: LightTrack 模型加载失败";
            return false;
        }
        const cv::Rect2f bbox(static_cast<float>(roi.x), static_cast<float>(roi.y),
                              static_cast<float>(roi.width), static_cast<float>(roi.height));
        return m_sot->init(bgr, bbox);
    } else {
        return m_core.init(bgr, roi);
    }
}

ObjectTracker::KernelOutput ObjectTracker::runKernel(const cv::Mat& bgr)
{
    KernelOutput ko;
    if (m_kernel == TrackingKernel::KERNEL_LIGHTTRACK) {
        if (!m_sot || !m_sot->isInitialized()) {
            return ko;
        }
        SotResult r = m_sot->track(bgr);
        ko.valid = r.valid;
        ko.center = r.center;
        ko.size = r.size;
        ko.rawScore = r.score;
        // 方案 9.4: 使 computeSConf 的 (psr - 5) / 20 映射输出恰为 rawScore
        ko.psrForAmft = 5.0f + 20.0f * ko.rawScore;
        return ko;
    } else {
        cv::Mat response;
        const cv::Rect bbox = m_core.update(bgr, &response);
        ko.valid = (bbox.width > 0 && bbox.height > 0);
        if (ko.valid) {
            ko.center = m_core.position();
            ko.size = cv::Size2f(m_targetW * m_core.currentScale(), m_targetH * m_core.currentScale());
            ko.rawScore = computePSR(response);
        } else {
            ko.rawScore = 0.0f;
        }
        ko.psrForAmft = ko.rawScore;
        return ko;
    }
}

// ========================
// 变焦联动：触发尺度纠偏校准
// ========================
void ObjectTracker::triggerScaleCalibration()
{
    m_needScaleCalibration = true;
    m_zoomCalibRetries = 5;
    qCDebug(trackerLog) << "[Tracker] 变焦尺度纠偏已标记，将连续进行至多 5 帧 AI 尺度重校准";
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

    const int frameW = frame.width();
    const int frameH = frame.height();
    if (frameW <= 0 || frameH <= 0) {
        qWarning() << "[ObjectTracker] 初始化失败: 图像尺寸无效";
        return false;
    }

    // 将目标框裁剪到图像内部
    const qreal left = std::max<qreal>(0, targetRect.left());
    const qreal top = std::max<qreal>(0, targetRect.top());
    const qreal right = std::min<qreal>(static_cast<qreal>(frameW), targetRect.right());
    const qreal bottom = std::min<qreal>(static_cast<qreal>(frameH), targetRect.bottom());
    const qreal clippedW = right - left;
    const qreal clippedH = bottom - top;
    if (clippedW < 4 || clippedH < 4) {
        qWarning() << "[ObjectTracker] 初始化失败: 目标框裁剪后过小" << clippedW << "x" << clippedH;
        return false;
    }

    m_posX = static_cast<float>((left + right) * 0.5);
    m_posY = static_cast<float>((top + bottom) * 0.5);
    m_targetW = static_cast<float>(clippedW);
    m_targetH = static_cast<float>(clippedH);
    m_currentScale = 1.0f;

    cv::Mat image = QImageToMat(frame);
    if (image.empty()) {
        qWarning() << "[ObjectTracker] 初始化失败: QImage 转 cv::Mat 失败";
        return false;
    }

    const cv::Rect roi(static_cast<int>(left), static_cast<int>(top),
                       static_cast<int>(clippedW), static_cast<int>(clippedH));
    if (!reinitKernel(image, roi)) {
        qWarning() << "[ObjectTracker] 内核初始化失败";
        return false;
    }

    // KF 初始化
    m_kf.x = cv::Vec4f(m_posX, m_posY, 0.0f, 0.0f);
    m_kf.P = cv::Matx44f::eye() * 8.0f;

    // AMFT 评估器初始化
    m_evaluator.init(cv::Rect2f(m_posX - m_targetW * 0.5f,
                                m_posY - m_targetH * 0.5f,
                                m_targetW, m_targetH));

    // 固化初始目标不可变外观锚点（全程禁止被重捕流程覆写）
    m_anchor = TargetAnchor();
    m_anchor.initBbox = cv::Rect2f(static_cast<float>(left), static_cast<float>(top),
                                   static_cast<float>(clippedW), static_cast<float>(clippedH));
    m_anchor.valid = extractPatchAppearance(image, m_anchor.initBbox,
                                            m_anchor.histHS, m_anchor.histV, m_anchor.patchNorm,
                                            m_anchor.hasTexture, m_anchor.isColor, m_anchor.meanSat);

    m_initialized = true;
    m_trackingState = AMFTTrackingState::TRACK_NORMAL;
    qCDebug(trackerLog) << "[ObjectTracker] 初始化成功, 模型尺寸:" << modelSize()
             << "目标:(" << m_posX << "," << m_posY << ") " << m_targetW << "x" << m_targetH;
    return true;
}

// ========================
// 计算PSR（内核响应图版本，半径5圆形峰值抑制）
// ========================
float ObjectTracker::computePSR(const cv::Mat& response)
{
    if (response.empty()) return 0.0f;

    cv::Point maxLoc;
    double maxVal = 0.0;
    cv::minMaxLoc(response, nullptr, &maxVal, nullptr, &maxLoc);

    cv::Mat mask = cv::Mat::ones(response.size(), CV_8U);
    cv::circle(mask, maxLoc, 5, cv::Scalar(0), -1);

    cv::Scalar mean, stddev;
    cv::meanStdDev(response, mean, stddev, mask);
    const double sd = std::max(stddev[0], 1e-6);
    return static_cast<float>((maxVal - mean[0]) / sd);
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
            if (std::isnan(m_kf.P(i, j)) || std::isinf(m_kf.P(i, j)) || std::abs(m_kf.P(i, j)) > 1e8f) {
                pDirty = true;
            }
        }
    }
    if (pDirty) {
        qWarning() << "[kfPredict] P矩阵异常，已重置为初始值";
        m_kf.P = cv::Matx44f::eye() * 8.0f;
    }

    // 状态转移矩阵 A = [1 0 T 0; 0 1 0 T; 0 0 1 0; 0 0 0 1], T=1
    const cv::Matx44f A(1.0f, 0.0f, 1.0f, 0.0f,
                        0.0f, 1.0f, 0.0f, 1.0f,
                        0.0f, 0.0f, 1.0f, 0.0f,
                        0.0f, 0.0f, 0.0f, 1.0f);

    m_kf.x = A * m_kf.x;
    const bool inLoss = (m_trackingState == AMFTTrackingState::TRACK_OCCLUDED ||
                         m_trackingState == AMFTTrackingState::TRACK_LOST ||
                         m_inOcclusion);
    if (inLoss) {
        // 遮挡/丢失期间对推算速度施加阻尼，防止长期失锁时预测坐标飞出画面
        const float damp = (m_trackingState == AMFTTrackingState::TRACK_LOST) ? 0.0f : 0.85f;
        m_kf.x[2] *= damp;
        m_kf.x[3] *= damp;
    }
    const cv::Matx44f& activeQ = inLoss ? m_kfQLost : m_kfQ;
    m_kf.P = A * m_kf.P * A.t() + activeQ;
}

// ========================
// 卡尔曼滤波更新
// ========================
void ObjectTracker::kfUpdate(float measX, float measY)
{
    // 测量值有效性检查：NaN或过大的测量值会导致状态发散
    if (std::isnan(measX) || std::isinf(measX)) measX = m_kf.x[0];
    if (std::isnan(measY) || std::isinf(measY)) measY = m_kf.x[1];

    // H = [1 0 0 0; 0 1 0 0]，S = H*P*H' + R
    const cv::Matx<float, 2, 4> H(1.0f, 0.0f, 0.0f, 0.0f,
                                  0.0f, 1.0f, 0.0f, 0.0f);
    cv::Matx22f S = H * m_kf.P * H.t() + m_kfR;

    // 防止S元素溢出（当P变得非常大时）
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 2; ++j) {
            if (std::isnan(S(i, j)) || std::isinf(S(i, j)) || std::abs(S(i, j)) > 1e10f) {
                S(i, j) = (i == j) ? m_kfR(i, j) : 0.0f;
            }
        }
    }

    // 分母epsilon防护除零
    float detS = S(0, 0) * S(1, 1) - S(0, 1) * S(1, 0);
    static constexpr float epsilon = 1e-10f;
    if (std::abs(detS) < epsilon) detS = (detS < 0) ? -epsilon : epsilon;

    // K = P*H'*inv(S)
    const cv::Matx22f Sinv(S(1, 1) / detS, -S(0, 1) / detS,
                           -S(1, 0) / detS, S(0, 0) / detS);
    const cv::Matx<float, 4, 2> K = m_kf.P * H.t() * Sinv;

    // 更新状态: x = x + K * (z - H * x)
    const cv::Vec2f z(measX, measY);
    m_kf.x += K * (z - H * m_kf.x);

    // 状态溢出保护：如果任何状态值非法，重置为安全值
    for (int i = 0; i < 4; ++i) {
        if (std::isnan(m_kf.x[i]) || std::isinf(m_kf.x[i]) || std::abs(m_kf.x[i]) > 1e6f) {
            m_kf.x[i] = 0.0f;
            if (i == 0) m_kf.x[i] = m_posX;
            if (i == 1) m_kf.x[i] = m_posY;
        }
    }

    // 更新协方差 P = (I - K*H)*P
    const cv::Matx44f I = cv::Matx44f::eye();
    m_kf.P = (I - K * H) * m_kf.P;

    // 协方差矩阵对称化 P = (P + P')/2，防止数值误差导致非对称
    m_kf.P = (m_kf.P + m_kf.P.t()) * 0.5f;

    // P矩阵溢出保护：如果协方差变得非常大，重置为初始值
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            if (std::isnan(m_kf.P(i, j)) || std::isinf(m_kf.P(i, j)) || std::abs(m_kf.P(i, j)) > 1e10f) {
                m_kf.P(i, j) = (i == j) ? 8.0f : 0.0f;
            }
        }
    }
}

// ========================
// 初始目标外观特征提取与锚点校验（防止丢失后被异物永久劫持）
// ========================
bool ObjectTracker::extractPatchAppearance(const cv::Mat& image, const cv::Rect2f& box,
                                           cv::Mat& outHistHS, cv::Mat& outHistV, cv::Mat& outPatchNorm,
                                           bool& outHasTexture, bool& outIsColor, float& outMeanSat) const
{
    if (image.empty() || box.width < 4.0f || box.height < 4.0f) {
        return false;
    }

    const cv::Rect imgBounds(0, 0, image.cols, image.rows);
    cv::Rect roi(static_cast<int>(std::round(box.x)),
                 static_cast<int>(std::round(box.y)),
                 static_cast<int>(std::round(box.width)),
                 static_cast<int>(std::round(box.height)));
    roi &= imgBounds;
    if (roi.width < 4 || roi.height < 4) {
        return false;
    }

    // 直方图采用适度内缩ROI，降低框边缘背景像素渗入干扰
    cv::Rect histRoi = roi;
    if (roi.width >= 12 && roi.height >= 12) {
        const int insetX = std::max(1, roi.width / 10);
        const int insetY = std::max(1, roi.height / 10);
        cv::Rect inner(roi.x + insetX, roi.y + insetY,
                       roi.width - 2 * insetX, roi.height - 2 * insetY);
        if (inner.width >= 4 && inner.height >= 4) {
            histRoi = inner;
        }
    }

    const cv::Mat histPatch = image(histRoi);
    if (image.channels() >= 3) {
        outIsColor = true;
        cv::Mat hsv;
        cv::cvtColor(histPatch, hsv, cv::COLOR_BGR2HSV);
        outMeanSat = static_cast<float>(cv::mean(hsv)[1]);

        int hsSizes[] = {16, 8};
        float hRange[] = {0.0f, 180.0f};
        float sRange[] = {0.0f, 256.0f};
        const float* hsRanges[] = {hRange, sRange};
        const int hsCh[] = {0, 1};
        cv::calcHist(&hsv, 1, hsCh, cv::Mat(), outHistHS, 2, hsSizes, hsRanges, true, false);
        cv::normalize(outHistHS, outHistHS, 1.0, 0.0, cv::NORM_L1);

        int vSize = 16;
        float vRange[] = {0.0f, 256.0f};
        const float* vRanges[] = {vRange};
        const int vCh[] = {2};
        cv::calcHist(&hsv, 1, vCh, cv::Mat(), outHistV, 1, &vSize, vRanges, true, false);
        cv::normalize(outHistV, outHistV, 1.0, 0.0, cv::NORM_L1);
    } else {
        outIsColor = false;
        outMeanSat = 0.0f;
        outHistHS.release();

        int gSize = 16;
        float gRange[] = {0.0f, 256.0f};
        const float* gRanges[] = {gRange};
        const int gCh[] = {0};
        cv::calcHist(&histPatch, 1, gCh, cv::Mat(), outHistV, 1, &gSize, gRanges, true, false);
        cv::normalize(outHistV, outHistV, 1.0, 0.0, cv::NORM_L1);
    }

    // 24x24 零均值单位范数灰度模板（用于 ZNCC 结构纹理比对）
    cv::Mat grayRoi;
    if (image.channels() >= 3) {
        cv::cvtColor(image(roi), grayRoi, cv::COLOR_BGR2GRAY);
    } else {
        grayRoi = image(roi);
    }
    cv::Mat resized;
    const int interp = (roi.width > 24 || roi.height > 24) ? cv::INTER_AREA : cv::INTER_LINEAR;
    cv::resize(grayRoi, resized, cv::Size(24, 24), 0, 0, interp);
    resized.convertTo(outPatchNorm, CV_32F);
    outPatchNorm -= cv::mean(outPatchNorm)[0];
    const double normVal = cv::norm(outPatchNorm, cv::NORM_L2);
    if (normVal > 1e-3) {
        outPatchNorm /= static_cast<float>(normVal);
        outHasTexture = true;
    } else {
        outPatchNorm = cv::Mat::zeros(24, 24, CV_32F);
        outHasTexture = false;
    }
    return true;
}

ObjectTracker::AnchorMatchScore ObjectTracker::evaluateAnchorSimilarity(
    const cv::Mat& image, const cv::Rect2f& candBox, bool runKernelCheck) const
{
    AnchorMatchScore res;
    if (!m_anchor.valid) {
        res.histSim = 1.0f;
        res.zncc = 1.0f;
        return res;
    }

    cv::Mat candHS, candV, candPatch;
    bool candTex = false, candColor = true;
    float candSat = 0.0f;
    if (!extractPatchAppearance(image, candBox, candHS, candV, candPatch, candTex, candColor, candSat)) {
        return res;
    }

    auto bhattSim = [](const cv::Mat& h1, const cv::Mat& h2) -> float {
        if (h1.empty() || h2.empty()) return 0.0f;
        const double d = cv::compareHist(h1, h2, cv::HISTCMP_BHATTACHARYYA);
        if (std::isnan(d) || std::isinf(d)) return 0.0f;
        return std::clamp(static_cast<float>(1.0 - d * d), 0.0f, 1.0f);
    };

    const float bcV = bhattSim(m_anchor.histV, candV);
    if (m_anchor.isColor && candColor) {
        const float bcHS = bhattSim(m_anchor.histHS, candHS);
        // 低饱和度（黑白灰目标）自动提高亮度权重，避免色相噪声误伤
        const float satWeight = std::clamp((m_anchor.meanSat - 15.0f) / 45.0f, 0.25f, 0.65f);
        res.histSim = satWeight * bcHS + (1.0f - satWeight) * bcV;
    } else {
        res.histSim = bcV;
    }

    if (m_anchor.hasTexture && candTex) {
        res.zncc = std::clamp(static_cast<float>(m_anchor.patchNorm.dot(candPatch)), -1.0f, 1.0f);
    } else {
        res.zncc = 0.5f;
    }

    if (runKernelCheck) {
        const cv::Point2f candCenter(candBox.x + candBox.width * 0.5f,
                                     candBox.y + candBox.height * 0.5f);
        if (m_kernel == TrackingKernel::KERNEL_LIGHTTRACK) {
            if (m_sot && m_sot->isInitialized()) {
                const SotResult probe = m_sot->peekBox(image, candBox, true);
                if (probe.valid) {
                    const float dist = static_cast<float>(cv::norm(probe.center - candCenter));
                    const float maxShift = std::max(candBox.width, candBox.height) * 0.75f + 12.0f;
                    if (dist <= maxShift) {
                        res.kernelScore = probe.score;
                    }
                }
            }
        } else {
            const float baseArea = std::max(16.0f, static_cast<float>(m_core.targetSize().area()));
            const float candArea = std::max(16.0f, candBox.width * candBox.height);
            const float candScale = std::sqrt(candArea / baseArea);
            cv::Mat resp;
            const cv::Rect r = m_core.detectOnlyBackup(image, candCenter, candScale, &resp);
            if (r.width > 0 && !resp.empty()) {
                const cv::Point2f rCenter(r.x + r.width * 0.5f, r.y + r.height * 0.5f);
                const float dist = static_cast<float>(cv::norm(rCenter - candCenter));
                const float maxShift = std::max(candBox.width, candBox.height) * 0.85f + 12.0f;
                if (dist <= maxShift) {
                    res.kernelScore = const_cast<ObjectTracker*>(this)->computePSR(resp);
                }
            }
        }
    }

    return res;
}

bool ObjectTracker::verifyRecoveryCandidate(const cv::Mat& image, const cv::Rect2f& candBox,
                                            bool isTotalLost, float& outMatchScore) const
{
    const AnchorMatchScore ms = evaluateAnchorSimilarity(image, candBox, true);

    const float minHistSim = isTotalLost ? 0.45f : 0.40f;
    if (ms.histSim < minHistSim) {
        return false;
    }

    if (m_kernel == TrackingKernel::KERNEL_LIGHTTRACK) {
        const float minSotAnchor = isTotalLost ? 0.68f : 0.60f;
        if (ms.kernelScore < minSotAnchor) {
            return false;
        }
        if (ms.zncc < 0.20f && ms.histSim < 0.62f) {
            return false;
        }
        outMatchScore = 0.45f * ms.histSim + 0.35f * ms.kernelScore
                      + 0.20f * std::clamp(ms.zncc, 0.0f, 1.0f);
    } else {
        const float minBackupPsr = isTotalLost ? 6.0f : 5.0f;
        if (ms.kernelScore < minBackupPsr) {
            return false;
        }
        if (ms.zncc < 0.20f && ms.histSim < 0.60f && ms.kernelScore < 9.0f) {
            return false;
        }
        const float psrNorm = std::clamp((ms.kernelScore - 4.0f) / 16.0f, 0.0f, 1.0f);
        outMatchScore = 0.45f * ms.histSim + 0.35f * psrNorm
                      + 0.20f * std::clamp(ms.zncc, 0.0f, 1.0f);
    }

    return true;
}

bool ObjectTracker::recoverWithAnchorState(const cv::Mat& image, const cv::Rect& newRoi)
{
    Q_UNUSED(image);
    const float cx = newRoi.x + newRoi.width * 0.5f;
    const float cy = newRoi.y + newRoi.height * 0.5f;

    if (m_kernel == TrackingKernel::KERNEL_LIGHTTRACK) {
        if (!m_sot || !m_sot->isInitialized()) {
            return false;
        }
        // 仅重设位置与尺寸，保留原始目标模板特征 m_templateFeat / m_anchorTemplateFeat
        m_sot->setState(cv::Point2f(cx, cy),
                        cv::Size2f(static_cast<float>(newRoi.width), static_cast<float>(newRoi.height)));
        m_posX = cx;
        m_posY = cy;
        m_targetW = static_cast<float>(newRoi.width);
        m_targetH = static_cast<float>(newRoi.height);
        m_currentScale = 1.0f;
        return true;
    }

    // DSST：从 init 备份高比重恢复滤波器，禁止销毁初始模板备份
    const float baseArea = std::max(16.0f, static_cast<float>(m_core.targetSize().area()));
    const float newArea = std::max(16.0f, static_cast<float>(newRoi.width * newRoi.height));
    const float candScale = std::sqrt(newArea / baseArea);
    m_core.blendBackup(0.85f);
    m_core.setPosition(cv::Point2f(cx, cy));
    m_core.setScale(candScale);
    m_core.setLearningFrozen(false);

    m_posX = cx;
    m_posY = cy;
    m_targetW = static_cast<float>(m_core.targetSize().width);
    m_targetH = static_cast<float>(m_core.targetSize().height);
    m_currentScale = m_core.currentScale();
    return true;
}

// ========================
// 事件驱动全局检测重捕
// ========================
bool ObjectTracker::tryDetectAndRecover(const cv::Mat& image)
{
    // 如果未外部设置检测器，则尝试懒加载默认模型 (优先 UAV 专属微调模型)
    if (!m_detector || !m_detector->isLoaded()) {
        const QStringList candidatePaths = {
            "models/yolo11n_uav_ft.onnx",
            "models/yolov8n_uav_ft.onnx",
            "models/yolov8n.onnx",
            QCoreApplication::applicationDirPath() + "/models/yolo11n_uav_ft.onnx",
            QCoreApplication::applicationDirPath() + "/models/yolov8n_uav_ft.onnx",
            QCoreApplication::applicationDirPath() + "/models/yolov8n.onnx",
            QDir::currentPath() + "/models/yolo11n_uav_ft.onnx",
            QDir::currentPath() + "/models/yolov8n.onnx"
        };
        QString foundPath;
        for (const auto& path : candidatePaths) {
            if (QFile::exists(path)) {
                foundPath = path;
                break;
            }
        }
        if (!foundPath.isEmpty()) {
            auto onnxDet = std::make_shared<OnnxDetector>();
            DetectorConfig cfg;
            cfg.useGpu = true;
            if (onnxDet->loadModel(foundPath, cfg)) {
                m_detector = onnxDet;
                qCDebug(trackerLog) << "[Tracker] 自动载入默认检测器成功:" << foundPath;
            }
        }
    }

    if (!m_detector || !m_detector->isLoaded()) {
        return false;
    }

    // 单次唤醒 YOLO 全图检测 (DirectML 仅耗时 3.06ms)
    std::vector<DetectionBox> detections = m_detector->detect(image);
    if (detections.empty()) {
        m_reacquireConsecutiveHits = 0;
        return false;
    }

    const float kfX = m_kf.x[0];
    const float kfY = m_kf.x[1];
    const float vx = m_kf.x[2];
    const float vy = m_kf.x[3];
    const float speed = std::sqrt(vx * vx + vy * vy);

    const float currentW = m_targetW * m_currentScale;
    const float currentH = m_targetH * m_currentScale;
    const float currentAspect = (currentH > 1.0f) ? (currentW / currentH) : 1.0f;
    const float origArea = std::max(16.0f, currentW * currentH);
    const float frameDiag = std::sqrt(static_cast<float>(image.cols * image.cols + image.rows * image.rows));

    const bool isTotalLost = (m_trackingState == AMFTTrackingState::TRACK_LOST) || (m_lostCount >= 15);

    // 第一级搜索半径：随失锁时间与速度线性扩张；第二级搜索半径：全图对角线
    const float maxRadius = isTotalLost ? frameDiag : 
        std::min(frameDiag * 0.40f, std::max(currentW * 3.0f, speed * m_lostCount * 1.5f + 80.0f));

    // 全图检索提高置信度门限以抑制杂波虚警
    const float minConfThr = isTotalLost ? 0.35f : 0.25f;

    float bestCandidateScore = 0.0f;
    DetectionBox bestBox;
    bool found = false;

    for (size_t i = 0; i < detections.size(); ++i) {
        const auto& det = detections[i];
        if (det.confidence < minConfThr) continue;

        const float cx = static_cast<float>(det.rect.x() + det.rect.width() * 0.5f);
        const float cy = static_cast<float>(det.rect.y() + det.rect.height() * 0.5f);

        const float dist = std::sqrt((cx - kfX) * (cx - kfX) + (cy - kfY) * (cy - kfY));

        // 第一级距离拦截；第二级全图允许
        if (!isTotalLost && dist > maxRadius) continue;

        // 几何长宽比校验：抑制地面长条杂波（如路面白线、屋檐反光等极端高长宽比目标）
        const float detAspect = (det.rect.height() > 1.0f) ? 
            (static_cast<float>(det.rect.width()) / det.rect.height()) : 1.0f;
        const float aspectDiff = std::abs(detAspect - currentAspect) / std::max(currentAspect, 0.1f);
        if (aspectDiff > 1.5f) continue;

        // 面积与几何比率校验（全图丢失态约束至 0.15x ~ 6.0x，防止极端大小异物劫持）
        const float detArea = static_cast<float>(det.rect.width() * det.rect.height());
        const float areaRatio = detArea / origArea;
        const float minAreaRatio = isTotalLost ? 0.15f : 0.20f;
        const float maxAreaRatio = isTotalLost ? 6.00f : 5.00f;
        if (areaRatio < minAreaRatio || areaRatio > maxAreaRatio) continue;

        // 核心防劫持门禁：候选框必须通过初始目标锚点外观 + 内核特征一致性校验
        const cv::Rect2f candBox(static_cast<float>(det.rect.x()),
                                 static_cast<float>(det.rect.y()),
                                 static_cast<float>(det.rect.width()),
                                 static_cast<float>(det.rect.height()));
        float anchorMatchScore = 0.0f;
        if (!verifyRecoveryCandidate(image, candBox, isTotalLost, anchorMatchScore)) {
            qCDebug(trackerLog) << "[ReDetect] 候选#" << i << QString::fromStdString(det.className)
                                << "未通过原始锚点一致性校验，已拦截";
            continue;
        }

        float candidateScore = 0.0f;
        if (!isTotalLost) {
            const float distNorm = dist / maxRadius;
            const float distPenalty = std::max(0.0f, 1.0f - distNorm * distNorm);
            const float areaPenalty = std::min(areaRatio, 1.0f / areaRatio);
            candidateScore = 0.50f * anchorMatchScore + 0.50f * (det.confidence * distPenalty * areaPenalty);
        } else {
            const float aspectWeight = std::max(0.0f, 1.0f - aspectDiff * 0.5f);
            candidateScore = 0.55f * anchorMatchScore + 0.30f * det.confidence + 0.15f * aspectWeight;
        }

        qCDebug(trackerLog) << "[ReDetect] 候选#" << i << QString::fromStdString(det.className)
                            << "pos=(" << cx << "," << cy << ") conf=" << det.confidence
                            << "anchor=" << anchorMatchScore
                            << "dist=" << dist << "maxR=" << maxRadius
                            << "score=" << candidateScore;

        if (candidateScore > bestCandidateScore) {
            bestCandidateScore = candidateScore;
            bestBox = det;
            found = true;
        }
    }

    const float scoreThreshold = isTotalLost ? 0.40f : 0.30f;
    if (!found || bestCandidateScore < scoreThreshold) {
        m_reacquireConsecutiveHits = 0;
        qCDebug(trackerLog) << "[ReDetect] 未找到合格候选目标 (最高分=" << bestCandidateScore
                            << ", 门限=" << scoreThreshold << ")";
        return false;
    }

    // 全图检索增加时序双帧空间一致性确认（防止单帧杂波或跳变误劫持）
    if (isTotalLost) {
        if (m_reacquireConsecutiveHits == 0) {
            m_reacquireConsecutiveHits = 1;
            m_candidateBox = bestBox;
            qCDebug(trackerLog) << "[ReDetect] 全图首帧捕获锚点候选，等待下一帧空间连续确认...";
            return false;
        }
        const float bx = static_cast<float>(bestBox.rect.x() + bestBox.rect.width() * 0.5f);
        const float by = static_cast<float>(bestBox.rect.y() + bestBox.rect.height() * 0.5f);
        const float px = static_cast<float>(m_candidateBox.rect.x() + m_candidateBox.rect.width() * 0.5f);
        const float py = static_cast<float>(m_candidateBox.rect.y() + m_candidateBox.rect.height() * 0.5f);
        const float stepDist = std::sqrt((bx - px) * (bx - px) + (by - py) * (by - py));
        const float maxConsistDist = std::max({
            static_cast<float>(bestBox.rect.width()),
            static_cast<float>(bestBox.rect.height()),
            static_cast<float>(m_candidateBox.rect.width()),
            static_cast<float>(m_candidateBox.rect.height())
        }) * 2.5f + 150.0f;
        if (stepDist > maxConsistDist) {
            m_candidateBox = bestBox;
            m_reacquireConsecutiveHits = 1;
            qCDebug(trackerLog) << "[ReDetect] 双帧候选空间跳变过大(" << stepDist << "px)，重新计数确认";
            return false;
        }
    }
    m_reacquireConsecutiveHits = 0;

    qCDebug(trackerLog) << "[ReDetect] 最佳匹配锁定成功: pos=("
                        << (bestBox.rect.x() + bestBox.rect.width() * 0.5f) << ","
                        << (bestBox.rect.y() + bestBox.rect.height() * 0.5f)
                        << ") score=" << bestCandidateScore;

    cv::Rect newRoi(static_cast<int>(std::round(bestBox.rect.x())),
                    static_cast<int>(std::round(bestBox.rect.y())),
                    static_cast<int>(std::round(bestBox.rect.width())),
                    static_cast<int>(std::round(bestBox.rect.height())));
    newRoi &= cv::Rect(0, 0, image.cols, image.rows);

    if (newRoi.width < 4 || newRoi.height < 4) {
        return false;
    }

    // 使用初始锚点恢复跟踪状态，严禁覆写原始目标模板
    if (!recoverWithAnchorState(image, newRoi)) {
        return false;
    }

    // 机电伺服平稳软启动机制：
    // 若从 TRACK_LOST 恢复，强制清零卡尔曼速度并重置协方差，杜绝超大速度跳变
    if (isTotalLost) {
        m_kf.x = cv::Vec4f(m_posX, m_posY, 0.0f, 0.0f);
        m_kf.P = cv::Matx44f::eye() * 8.0f;
    } else {
        kfUpdate(m_posX, m_posY);
    }

    const float evalW = m_targetW * m_currentScale;
    const float evalH = m_targetH * m_currentScale;
    m_evaluator.init(cv::Rect2f(m_posX - evalW * 0.5f,
                                m_posY - evalH * 0.5f,
                                evalW, evalH));

    return true;
}

// ========================
// 变焦联动：执行定向尺度校准与模板自适应重置
// ========================
bool ObjectTracker::calibrateScaleOnZoom(const cv::Mat& image)
{
    // 如果未外部设置检测器，则尝试懒加载
    if (!m_detector || !m_detector->isLoaded()) {
        tryDetectAndRecover(image);
    }
    if (!m_detector || !m_detector->isLoaded()) {
        return false;
    }

    const float kfX = m_kf.x[0];
    const float kfY = m_kf.x[1];

    std::vector<DetectionBox> detections = m_detector->detect(image);
    if (detections.empty()) {
        return false;
    }

    float bestScore = 0.0f;
    DetectionBox bestBox;
    bool matched = false;

    // 变焦工况下，允许目标尺寸大幅度缩放 (0.1x ~ 10.0x)，重点考量空间近邻与长宽比一致性
    const float currentAspect = (m_targetH > 1.0f) ? (m_targetW / m_targetH) : 1.0f;
    const float zoomSearchRadius = std::max(image.cols, image.rows) * 0.45f;

    for (const auto& det : detections) {
        if (det.confidence < 0.25f) continue;

        const float cx = det.rect.x() + det.rect.width() * 0.5f;
        const float cy = det.rect.y() + det.rect.height() * 0.5f;
        const float dist = std::sqrt((cx - kfX) * (cx - kfX) + (cy - kfY) * (cy - kfY));
        if (dist > zoomSearchRadius) continue;

        const float detAspect = (det.rect.height() > 1.0f) ? 
            (static_cast<float>(det.rect.width()) / det.rect.height()) : 1.0f;
        const float aspectDiff = std::abs(detAspect - currentAspect) / std::max(currentAspect, 0.1f);
        if (aspectDiff > 1.2f) continue; // 排除长宽比畸变过大目标

        const float spatialWeight = 1.0f - (dist / zoomSearchRadius);
        const float aspectWeight = std::max(0.1f, 1.0f - aspectDiff * 0.6f);
        const cv::Rect2f candBox(static_cast<float>(det.rect.x()),
                                 static_cast<float>(det.rect.y()),
                                 static_cast<float>(det.rect.width()),
                                 static_cast<float>(det.rect.height()));
        const AnchorMatchScore am = evaluateAnchorSimilarity(image, candBox, false);
        if (m_anchor.valid && am.histSim < 0.35f) {
            continue;
        }
        const float score = det.confidence * spatialWeight * aspectWeight * std::max(0.4f, am.histSim);

        if (score > bestScore) {
            bestScore = score;
            bestBox = det;
            matched = true;
        }
    }

    if (!matched || bestScore < 0.20f) {
        return false;
    }

    const cv::Rect newRoi(static_cast<int>(std::round(bestBox.rect.x())),
                          static_cast<int>(std::round(bestBox.rect.y())),
                          static_cast<int>(std::round(bestBox.rect.width())),
                          static_cast<int>(std::round(bestBox.rect.height())));
    const cv::Rect clampedRoi = newRoi & cv::Rect(0, 0, image.cols, image.rows);

    if (clampedRoi.width < 6 || clampedRoi.height < 6) {
        return false;
    }

    if (!reinitKernel(image, clampedRoi)) {
        return false;
    }

    if (m_kernel == TrackingKernel::KERNEL_DSST) {
        m_core.setLearningFrozen(false);
    }

    m_posX = clampedRoi.x + clampedRoi.width * 0.5f;
    m_posY = clampedRoi.y + clampedRoi.height * 0.5f;
    m_targetW = static_cast<float>(clampedRoi.width);
    m_targetH = static_cast<float>(clampedRoi.height);
    m_currentScale = 1.0f;

    kfUpdate(m_posX, m_posY);
    m_evaluator.init(cv::Rect2f(clampedRoi.x, clampedRoi.y, clampedRoi.width, clampedRoi.height));

    qCDebug(trackerLog) << "[Tracker] 变焦尺度重校准成功: 新尺寸=" << m_targetW << "x" << m_targetH
                        << "中心=(" << m_posX << "," << m_posY << ") score=" << bestScore;
    return true;
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

// C9：异步初始化槽（在 worker thread 中执行）
void ObjectTracker::initSlot(const QImage& frame, const QRectF& targetRect)
{
    bool ok = false;
    try {
        ok = init(frame, targetRect);
    } catch (const std::exception &e) {
        qCritical() << "[Tracker] initSlot 异常:" << e.what();
        ok = false;
    } catch (...) {
        qCritical() << "[Tracker] initSlot 未知异常";
        ok = false;
    }
    emit initDone(ok, ok ? modelSize() : QString());
}

// ========================
// 纯局部跟踪模式遮挡重检测 (DSST 邻域采样 / LightTrack peek)
// ========================
bool ObjectTracker::reDetect(const cv::Mat& image)
{
    if (m_kernel == TrackingKernel::KERNEL_LIGHTTRACK) {
        if (!m_sot || !m_sot->isInitialized()) return false;
        const float kfX = m_kf.x[0];
        const float kfY = m_kf.x[1];
        SotResult r = m_sot->peek(image, cv::Point2f(kfX, kfY));
        const float recoverThr = 0.8f * m_psrBaseline;
        if (!r.valid || r.score <= recoverThr) return false;

        const float dist = std::sqrt((r.center.x - kfX) * (r.center.x - kfX)
                                   + (r.center.y - kfY) * (r.center.y - kfY));
        const float maxDist = 2.0f * std::max(m_targetW, m_targetH);
        if (dist > maxDist) return false;

        m_sot->setState(r.center, r.size);
        m_posX = r.center.x;
        m_posY = r.center.y;
        m_targetW = r.size.width;
        m_targetH = r.size.height;
        m_currentScale = 1.0f;
        kfUpdate(m_posX, m_posY);
        return true;
    }

    const cv::Size modelSz = m_core.modelSize();
    const float scale = m_core.currentScale();

    const float kfX = m_kf.x[0];
    const float kfY = m_kf.x[1];

    // 9 个候选点：KF 预测中心 + 8 个方向偏移
    const float stepX = modelSz.width * scale * 0.5f;
    const float stepY = modelSz.height * scale * 0.5f;

    const std::vector<cv::Point2f> candidates = {
        cv::Point2f(kfX, kfY),
        cv::Point2f(kfX - stepX, kfY),
        cv::Point2f(kfX + stepX, kfY),
        cv::Point2f(kfX, kfY - stepY),
        cv::Point2f(kfX, kfY + stepY),
        cv::Point2f(kfX - stepX, kfY - stepY),
        cv::Point2f(kfX + stepX, kfY - stepY),
        cv::Point2f(kfX - stepX, kfY + stepY),
        cv::Point2f(kfX + stepX, kfY + stepY)
    };

    float bestPsr = 0.0f;
    float bestPosX = kfX;
    float bestPosY = kfY;

    for (const auto& center : candidates) {
        if (center.x < 0 || center.x >= image.cols ||
            center.y < 0 || center.y >= image.rows) continue;

        cv::Mat resp;
        const cv::Rect r = m_core.detectOnly(image, center, &resp);
        if (r.width <= 0 || resp.empty()) continue;

        const float psr = computePSR(resp);
        if (psr > bestPsr) {
            cv::Point maxLoc;
            cv::minMaxLoc(resp, nullptr, nullptr, nullptr, &maxLoc);
            bestPsr = psr;
            bestPosX = center.x + scale * (-modelSz.width / 2.0f + maxLoc.x);
            bestPosY = center.y + scale * (-modelSz.height / 2.0f + maxLoc.y);
        }
    }

    const float recoverThr = 0.8f * m_psrBaseline;
    if (bestPsr <= recoverThr) return false;

    // 验证与 KF 预测的距离（防止远距离虚警）
    const float dist = std::sqrt((bestPosX - kfX) * (bestPosX - kfX)
                               + (bestPosY - kfY) * (bestPosY - kfY));
    const float maxDist = std::max(modelSz.width, modelSz.height) * scale * 2.0f;
    if (dist > maxDist) return false;

    // 成功重检测：更新位置并恢复
    m_posX = bestPosX;
    m_posY = bestPosY;
    m_core.setLearningFrozen(false);
    kfUpdate(m_posX, m_posY);
    return true;
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

    cv::Mat image = QImageToMat(frame);
    if (image.empty()) {
        qWarning() << "[Tracker] 图像转换失败，跳过当前帧";
        return result;
    }

    ++m_frameCount;
    const bool logThisFrame = (m_frameCount % 50 == 1);

    // === 1. KF 预测 ===
    kfPredict();
    const float kfPredX = m_kf.x[0];
    const float kfPredY = m_kf.x[1];

    // 检查变焦尺度纠偏（支持步进与终态触发，具备 5 帧重试容错）
    if (m_needScaleCalibration || m_zoomCalibRetries > 0) {
        if (calibrateScaleOnZoom(image)) {
            m_needScaleCalibration = false;
            m_zoomCalibRetries = 0;
            if (m_trackingState != AMFTTrackingState::TRACK_NORMAL) {
                m_trackingState = AMFTTrackingState::TRACK_NORMAL;
                m_inOcclusion = false;
                m_lostCount = 0;
                result.recovered = true;
                result.occluded = false;
                emit trackingRecovered();
                emit trackingStateChanged(m_trackingState);
            }
        } else {
            if (m_zoomCalibRetries > 0) m_zoomCalibRetries--;
            if (m_zoomCalibRetries == 0) m_needScaleCalibration = false;
        }
    }

    // === 2. 纯局部跟踪分支 (m_redetectionEnabled == false) ===
    if (!m_redetectionEnabled) {
        result.occluded = m_inOcclusion;
        result.recovered = false;

        if (m_inOcclusion) {
            // 遮挡期间：冻结学习，纯 KF 航位推算，并尝试九宫格相关滤波局部重捕
            if (m_lostCount >= 10) {
                if (reDetect(image)) {
                    m_inOcclusion = false;
                    m_trackingState = AMFTTrackingState::TRACK_NORMAL;
                    m_lostCount = 0;
                    result.recovered = true;
                    result.occluded = false;
                    emit trackingRecovered();
                    emit trackingStateChanged(m_trackingState);
                    qCDebug(trackerLog) << "[Tracker-SOT] 九宫格原模板重检测成功，恢复跟踪 pos=("
                                        << m_posX << "," << m_posY << ")";
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
        } else {
            // 正常跟踪主循环：分派内核检测 + 尺度估计 + 模板更新
            const KernelOutput ko = runKernel(image);
            const bool detValid = ko.valid;
            const float psr = ko.rawScore;
            result.confidence = psr;
            result.psr = psr;

            float detX = m_posX;
            float detY = m_posY;
            if (detValid) {
                detX = (std::isnan(ko.center.x) || std::isinf(ko.center.x)) ? m_posX : ko.center.x;
                detY = (std::isnan(ko.center.y) || std::isinf(ko.center.y)) ? m_posY : ko.center.y;
                if (m_kernel == TrackingKernel::KERNEL_LIGHTTRACK) {
                    m_targetW = ko.size.width;
                    m_targetH = ko.size.height;
                    m_currentScale = 1.0f;
                } else {
                    m_currentScale = m_core.currentScale();
                }
            }

            // 依据 PSR 基线平稳判断（杜绝因 AMFT 短期敏感波动而频繁判丢）
            const bool lowPsr = !detValid || (m_baselineLocked && psr < 0.75f * m_psrBaseline);

            if (lowPsr) {
                m_lostCount++;
            } else {
                m_lostCount = 0;
            }

            if (m_lostCount >= 5) {
                // 连续 5 帧低 PSR 确认进入遮挡/脱靶，冻结学习，转入纯 KF 航位推算
                m_inOcclusion = true;
                m_trackingState = AMFTTrackingState::TRACK_OCCLUDED;
                m_lostCount = 1;
                m_posX = kfPredX;
                m_posY = kfPredY;
                result.occluded = true;
                if (m_kernel == TrackingKernel::KERNEL_DSST) {
                    m_core.setLearningFrozen(true);
                }
                emit trackingLost();
                emit trackingStateChanged(m_trackingState);
                qCDebug(trackerLog) << "[Tracker-SOT] 连续 5 帧 PSR 低于基线，进入遮挡保持态";
            } else if (m_lostCount > 0) {
                // PSR 下降瞬态：使用当前检测位置但不更新 KF 协方差
                if (detValid) {
                    m_posX = detX;
                    m_posY = detY;
                } else {
                    m_posX = kfPredX;
                    m_posY = kfPredY;
                }
            } else {
                // 正常稳定跟踪：更新卡尔曼滤波
                m_posX = detX;
                m_posY = detY;
                kfUpdate(m_posX, m_posY);

                // 收集前 30 帧平稳 PSR 基线
                if (!m_baselineLocked) {
                    m_psrHistory.push_back(psr);
                    if (static_cast<int>(m_psrHistory.size()) >= 30) {
                        float sum = 0.0f;
                        for (float v : m_psrHistory) sum += v;
                        m_psrBaseline = sum / m_psrHistory.size();
                        m_baselineLocked = true;
                        m_psrHistory.clear();
                        qCDebug(trackerLog) << "[Tracker-SOT] PSR 基线锁定完成: " << m_psrBaseline;
                    }
                }
            }

            if (logThisFrame) {
                qCDebug(trackerLog) << "[Tracker-SOT] frame=" << m_frameCount
                                    << " psr=" << psr << " valid=" << detValid
                                    << " pos=(" << m_posX << "," << m_posY << ")"
                                    << " scale=" << m_currentScale
                                    << " baseline=" << m_psrBaseline
                                    << (m_baselineLocked ? "(locked)" : "");
            }
        }

        // 构建 SOT 跟踪输出框并返回
        const float currentW = m_targetW * m_currentScale;
        const float currentH = m_targetH * m_currentScale;
        result.bbox = QRectF(m_posX - currentW / 2.0f, m_posY - currentH / 2.0f, currentW, currentH);
        result.valid = true;
        result.fps = 1000.0f / std::max(1LL, timer.elapsed());
        result.state = m_trackingState;

        result.kfX  = m_kf.x[0];
        result.kfY  = m_kf.x[1];
        result.kfVx = m_kf.x[2];
        result.kfVy = m_kf.x[3];

        result.procMs = timer.nsecsElapsed() / 1e6f;

        return result;
    }

    // === 2. 状态机分流 (仅在 m_redetectionEnabled 时生效) ===
    if (m_trackingState == AMFTTrackingState::TRACK_OCCLUDED || m_trackingState == AMFTTrackingState::TRACK_LOST) {
        // --- 遮挡或丢失状态 ---
        if (m_kernel == TrackingKernel::KERNEL_DSST) {
            m_core.setLearningFrozen(true);
        }
        m_lostCount++;

        // 在 OCCLUDED 态每帧尝试单次重捕（至多持续 15 帧）
        // 在 LOST 态降频每 10 帧尝试一次 (3 FPS 待机检索，降低计算负载)
        bool shouldScan = (m_trackingState == AMFTTrackingState::TRACK_OCCLUDED)
                       || (m_lostCount % 10 == 0);

        bool recovered = false;
        if (shouldScan) {
            recovered = tryDetectAndRecover(image);
        }

        if (recovered) {
            // 命中！进入重捕恢复态
            m_trackingState = AMFTTrackingState::TRACK_REACQUIRED;
            m_lostCount = 0;
            result.recovered = true;
            result.occluded = false;
            emit trackingRecovered();
            emit trackingStateChanged(m_trackingState);
            qCDebug(trackerLog) << "[Tracker] AMFT 协同唤醒: 锚点核验重捕成功! 目标位置=("
                                << m_posX << "," << m_posY << ")";
        } else {
            // 未重捕，保持钳制后的 KF 预测位置并维持遮挡/丢失态
            m_posX = std::clamp(kfPredX, 0.0f, static_cast<float>(image.cols));
            m_posY = std::clamp(kfPredY, 0.0f, static_cast<float>(image.rows));
            result.occluded = true;
            result.recovered = false;

            if (m_lostCount >= 15 && m_trackingState != AMFTTrackingState::TRACK_LOST) {
                m_trackingState = AMFTTrackingState::TRACK_LOST;
                m_kf.x[2] = 0.0f;
                m_kf.x[3] = 0.0f;
                emit trackingLost();
                emit trackingStateChanged(m_trackingState);
                qCDebug(trackerLog) << "[Tracker] 连续 15 帧重捕未果，转入 TRACK_LOST 态势";
            }
        }
    } else {
        // --- 正常局部跟踪态 (TRACK_NORMAL) 或刚重捕态 (TRACK_REACQUIRED) ---
        // 如果前一帧是 TRACK_REACQUIRED，本帧回归 TRACK_NORMAL
        if (m_trackingState == AMFTTrackingState::TRACK_REACQUIRED) {
            m_trackingState = AMFTTrackingState::TRACK_NORMAL;
            emit trackingStateChanged(m_trackingState);
        }

        // 局部跟踪主循环：分派内核检测 + 尺度估计 + 模板更新
        const KernelOutput ko = runKernel(image);
        const bool detValid = ko.valid;
        const float psr = ko.rawScore;
        result.psr = psr;

        float detX = m_posX;
        float detY = m_posY;
        if (detValid) {
            detX = (std::isnan(ko.center.x) || std::isinf(ko.center.x)) ? m_posX : ko.center.x;
            detY = (std::isnan(ko.center.y) || std::isinf(ko.center.y)) ? m_posY : ko.center.y;
            if (m_kernel == TrackingKernel::KERNEL_LIGHTTRACK) {
                m_targetW = ko.size.width;
                m_targetH = ko.size.height;
                m_currentScale = 1.0f;
            } else {
                m_currentScale = m_core.currentScale();
            }
        }

        // 构建当前目标框供 AMFT 评估与初始锚点一致性监测
        const float currentW = m_targetW * m_currentScale;
        const float currentH = m_targetH * m_currentScale;
        cv::Rect2f evalBox(detX - currentW * 0.5f, detY - currentH * 0.5f, currentW, currentH);

        // === AMFT 三维特征评估 + 内核置信底线 + 初始外观锚点守护 ===
        m_lastScore = m_evaluator.evaluate(ko.psrForAmft, detValid, evalBox, image);
        const AnchorMatchScore anchorCheck = evaluateAnchorSimilarity(image, evalBox, false);
        const bool lowKernelConf = !detValid ||
            ((m_kernel == TrackingKernel::KERNEL_LIGHTTRACK) ? (psr < 0.70f) : (psr < 7.0f));
        const bool anchorMismatch = m_anchor.valid &&
            (anchorCheck.histSim < 0.32f || (anchorCheck.histSim < 0.45f && anchorCheck.zncc < 0.10f));
        const bool frameReliable = m_lastScore.isReliable && !lowKernelConf && !anchorMismatch;

        result.amftScore = m_lastScore.rTotal;
        result.amftTau = m_lastScore.dynamicTau;
        result.confidence = m_lastScore.rTotal;

        if (frameReliable) {
            // --- 跟踪稳健 ---
            if (m_kernel == TrackingKernel::KERNEL_DSST) {
                m_core.setLearningFrozen(false);
            }
            m_posX = detX;
            m_posY = detY;
            kfUpdate(m_posX, m_posY);
            m_lostCount = 0;
            result.occluded = false;
        } else {
            // --- 可靠度异常或外观漂移：首帧即刻冻结 DSST 学习，防异物污染模板 ---
            if (m_kernel == TrackingKernel::KERNEL_DSST) {
                m_core.setLearningFrozen(true);
            }
            m_lostCount++;
            qCDebug(trackerLog) << "[Tracker] 异常预警: R_t=" << m_lastScore.rTotal
                                << " tau_t=" << m_lastScore.dynamicTau
                                << " psr/cls=" << psr
                                << " histSim=" << anchorCheck.histSim
                                << " zncc=" << anchorCheck.zncc;

            if (m_lostCount >= 4) {
                // 确认进入遮挡/失锁态 (连续 4 帧异常，约 130ms，防瞬态误判)
                m_trackingState = AMFTTrackingState::TRACK_OCCLUDED;
                emit trackingLost();
                emit trackingStateChanged(m_trackingState);

                // 立即触发单次检测重捕（需通过原始锚点校验）
                if (tryDetectAndRecover(image)) {
                    m_trackingState = AMFTTrackingState::TRACK_REACQUIRED;
                    m_lostCount = 0;
                    result.recovered = true;
                    result.occluded = false;
                    emit trackingRecovered();
                    emit trackingStateChanged(m_trackingState);
                } else {
                    m_posX = std::clamp(kfPredX, 0.0f, static_cast<float>(image.cols));
                    m_posY = std::clamp(kfPredY, 0.0f, static_cast<float>(image.rows));
                    result.occluded = true;
                }
            } else {
                // 瞬时过渡态：暂用当前检测位置但不更新 KF 协方差（若外观已显著偏离则跟随 KF 预测）
                if (detValid && !anchorMismatch && !lowKernelConf) {
                    m_posX = detX;
                    m_posY = detY;
                } else {
                    m_posX = std::clamp(kfPredX, 0.0f, static_cast<float>(image.cols));
                    m_posY = std::clamp(kfPredY, 0.0f, static_cast<float>(image.rows));
                }
                result.occluded = false;
            }
        }

        if (logThisFrame) {
            qCDebug(trackerLog) << "[Tracker] frame=" << m_frameCount
                                << "state=" << static_cast<int>(m_trackingState)
                                << "R_t=" << m_lastScore.rTotal
                                << "tau_t=" << m_lastScore.dynamicTau
                                << "psr=" << psr
                                << "pos=(" << m_posX << "," << m_posY << ")"
                                << "scale=" << m_currentScale;
        }
    }

    // === 构建输出结果 ===
    const float currentW = m_targetW * m_currentScale;
    const float currentH = m_targetH * m_currentScale;
    result.bbox = QRectF(m_posX - currentW * 0.5f, m_posY - currentH * 0.5f,
                         currentW, currentH);
    result.valid = true;
    result.state = m_trackingState;
    result.fps = 1000.0f / std::max(1LL, timer.elapsed());

    // 提取 KF 状态
    result.kfX  = m_kf.x[0];
    result.kfY  = m_kf.x[1];
    result.kfVx = m_kf.x[2];
    result.kfVy = m_kf.x[3];

    result.procMs = timer.nsecsElapsed() / 1e6f;

    return result;
}

// ========================
// 获取当前目标框
// ========================
QRectF ObjectTracker::getCurrentBBox() const
{
    const float currentW = m_targetW * m_currentScale;
    const float currentH = m_targetH * m_currentScale;
    return QRectF(m_posX - currentW * 0.5f, m_posY - currentH * 0.5f,
                  currentW, currentH);
}
