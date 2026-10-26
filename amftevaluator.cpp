#include "amftevaluator.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

AMFTEvaluator::AMFTEvaluator(int windowSize, float tau0, float alpha)
    : m_windowSize(windowSize > 1 ? windowSize : 1)
    , m_tau0(tau0)
    , m_alpha(alpha)
    , m_lastTau(tau0)
{
}

void AMFTEvaluator::reset()
{
    m_hasPrevBox = false;
    m_prevBox = cv::Rect2f();
    m_velHistory.clear();
    m_rHistory.clear();
    m_lastTau = m_tau0;
}

void AMFTEvaluator::init(const cv::Rect2f& initBox)
{
    reset();
    m_prevBox = initBox;
    m_hasPrevBox = true;
    m_rHistory.push_back(1.0f); // 初始时刻可靠度置满
}

void AMFTEvaluator::setWeights(float wConf, float wMot, float wBg)
{
    float sum = wConf + wMot + wBg;
    if (sum > 1e-4f) {
        m_wConf = wConf / sum;
        m_wMot  = wMot / sum;
        m_wBg   = wBg / sum;
    }
}

float AMFTEvaluator::computeIoU(const cv::Rect2f& b1, const cv::Rect2f& b2) const
{
    float x1 = std::max(b1.x, b2.x);
    float y1 = std::max(b1.y, b2.y);
    float x2 = std::min(b1.x + b1.width, b2.x + b2.width);
    float y2 = std::min(b1.y + b1.height, b2.y + b2.height);

    float interW = std::max(0.0f, x2 - x1);
    float interH = std::max(0.0f, y2 - y1);
    float interArea = interW * interH;
    float unionArea = (b1.width * b1.height) + (b2.width * b2.height) - interArea;

    if (unionArea <= 1e-5f) return 0.0f;
    return std::clamp(interArea / unionArea, 0.0f, 1.0f);
}

float AMFTEvaluator::computeSConf(float psr, bool detValid)
{
    if (!detValid || psr <= 0.0f || std::isnan(psr) || std::isinf(psr)) {
        return 0.0f;
    }
    // 经验映射：PSR 在 5.0f ~ 25.0f 之间线性归一化到 [0, 1]
    return std::clamp((psr - 5.0f) / 20.0f, 0.0f, 1.0f);
}

float AMFTEvaluator::computeSMot(const cv::Rect2f& currentBox)
{
    if (!m_hasPrevBox) {
        return 1.0f; // 首帧默认运动完美连续
    }

    float iou = computeIoU(currentBox, m_prevBox);

    cv::Point2f curCenter(currentBox.x + currentBox.width * 0.5f,
                          currentBox.y + currentBox.height * 0.5f);
    cv::Point2f prevCenter(m_prevBox.x + m_prevBox.width * 0.5f,
                           m_prevBox.y + m_prevBox.height * 0.5f);
    cv::Point2f curVel = curCenter - prevCenter;

    cv::Point2f avgVel(0.0f, 0.0f);
    if (!m_velHistory.empty()) {
        for (const auto& v : m_velHistory) {
            avgVel += v;
        }
        avgVel *= (1.0f / static_cast<float>(m_velHistory.size()));
    } else {
        avgVel = curVel;
    }

    float velDiff = static_cast<float>(cv::norm(curVel - avgVel));
    float boxDiag = std::sqrt(currentBox.width * currentBox.width + currentBox.height * currentBox.height);
    float normFactor = std::max({static_cast<float>(cv::norm(avgVel)), boxDiag * 0.25f, 5.0f});
    float dt = std::clamp(velDiff / normFactor, 0.0f, 1.0f);
    float sSmooth = 1.0f - dt;

    float sMot = 0.60f * iou + 0.40f * sSmooth;
    return std::clamp(sMot, 0.0f, 1.0f);
}

float AMFTEvaluator::computeSBg(const cv::Rect2f& currentBox, const cv::Mat& image)
{
    if (image.empty() || currentBox.width <= 2.0f || currentBox.height <= 2.0f) {
        return 0.5f;
    }

    cv::Rect imgBounds(0, 0, image.cols, image.rows);

    // 目标 ROI
    cv::Rect targetRoi(static_cast<int>(std::round(currentBox.x)),
                       static_cast<int>(std::round(currentBox.y)),
                       static_cast<int>(std::round(currentBox.width)),
                       static_cast<int>(std::round(currentBox.height)));
    targetRoi &= imgBounds;
    if (targetRoi.width <= 2 || targetRoi.height <= 2) {
        return 0.5f;
    }

    // 目标中心点
    float cx = currentBox.x + currentBox.width * 0.5f;
    float cy = currentBox.y + currentBox.height * 0.5f;

    // 背景区域（扩展 2.5 倍框）
    float bgW = currentBox.width * 2.5f;
    float bgH = currentBox.height * 2.5f;
    cv::Rect bgRoi(static_cast<int>(std::round(cx - bgW * 0.5f)),
                   static_cast<int>(std::round(cy - bgH * 0.5f)),
                   static_cast<int>(std::round(bgW)),
                   static_cast<int>(std::round(bgH)));
    bgRoi &= imgBounds;
    if (bgRoi.width <= targetRoi.width || bgRoi.height <= targetRoi.height) {
        return 0.5f;
    }

    // 目标与环形背景特征提取（自适应双模：RGB 可见光使用 HSV 2D 颜色直方图，单通道红外使用 16-bin 灰度直方图）
    cv::Mat patchTarget = image(targetRoi);
    cv::Mat patchBg = image(bgRoi);

    // 构建环形背景掩码：排除中心目标区域，提取纯净环形背景
    cv::Mat maskBg = cv::Mat::ones(bgRoi.size(), CV_8U);
    cv::Rect targetInBg(targetRoi.x - bgRoi.x, targetRoi.y - bgRoi.y, targetRoi.width, targetRoi.height);
    targetInBg &= cv::Rect(0, 0, bgRoi.width, bgRoi.height);
    if (targetInBg.area() > 0 && targetInBg.area() < bgRoi.area()) {
        maskBg(targetInBg) = 0;
    }

    cv::Mat histTarget, histBg;

    if (image.channels() >= 3) {
        // 可见光 RGB：转换至 HSV 空间，计算 H-S 2D 颜色直方图 (16 bins H, 8 bins S)
        cv::Mat hsvTarget, hsvBg;
        cv::cvtColor(patchTarget, hsvTarget, cv::COLOR_BGR2HSV);
        cv::cvtColor(patchBg, hsvBg, cv::COLOR_BGR2HSV);

        int histSize[] = { 16, 8 };
        float hRanges[] = { 0.0f, 180.0f };
        float sRanges[] = { 0.0f, 256.0f };
        const float* ranges[] = { hRanges, sRanges };
        const int channels[] = { 0, 1 };

        cv::calcHist(&hsvTarget, 1, channels, cv::Mat(), histTarget, 2, histSize, ranges, true, false);
        cv::calcHist(&hsvBg, 1, channels, maskBg, histBg, 2, histSize, ranges, true, false);
    } else {
        // 红外热成像：单通道 16-bin 灰度直方图
        int histSize = 16;
        float range[] = { 0.0f, 256.0f };
        const float* histRange = { range };
        const int channels[] = { 0 };

        cv::calcHist(&patchTarget, 1, channels, cv::Mat(), histTarget, 1, &histSize, &histRange, true, false);
        cv::calcHist(&patchBg, 1, channels, maskBg, histBg, 1, &histSize, &histRange, true, false);
    }

    cv::normalize(histTarget, histTarget, 1.0, 0.0, cv::NORM_L1);
    cv::normalize(histBg, histBg, 1.0, 0.0, cv::NORM_L1);

    // Bhattacharyya 距离: 0 为完全相同(重合/无区分度), 1 为完全不同(高区分度)
    double bhattDist = cv::compareHist(histTarget, histBg, cv::HISTCMP_BHATTACHARYYA);
    if (std::isnan(bhattDist) || std::isinf(bhattDist)) {
        return 0.5f;
    }
    return std::clamp(static_cast<float>(bhattDist), 0.0f, 1.0f);
}

AMFTScore AMFTEvaluator::evaluate(float psr, bool detValid, const cv::Rect2f& currentBox, const cv::Mat& image)
{
    AMFTScore score;
    score.sConf = computeSConf(psr, detValid);
    score.sMot  = computeSMot(currentBox);
    score.sBg   = computeSBg(currentBox, image);

    score.rTotal = m_wConf * score.sConf + m_wMot * score.sMot + m_wBg * score.sBg;
    score.rTotal = std::clamp(score.rTotal, 0.0f, 1.0f);

    // 记录综合得分历史 (滑动窗口 K)
    m_rHistory.push_back(score.rTotal);
    if (static_cast<int>(m_rHistory.size()) > m_windowSize) {
        m_rHistory.pop_front();
    }

    // 计算滑动窗口标准差 sigma_t
    float sigma = 0.0f;
    if (m_rHistory.size() >= 3) {
        float mean = 0.0f;
        for (float v : m_rHistory) {
            mean += v;
        }
        mean /= static_cast<float>(m_rHistory.size());

        float variance = 0.0f;
        for (float v : m_rHistory) {
            variance += (v - mean) * (v - mean);
        }
        variance /= static_cast<float>(m_rHistory.size());
        sigma = std::sqrt(variance);
    }

    // 动态阈值 tau_t = tau_0 - alpha * sigma_t
    float tau = m_tau0 - m_alpha * sigma;
    score.dynamicTau = std::clamp(tau, 0.35f, 0.75f);
    m_lastTau = score.dynamicTau;

    // 判定是否可靠
    score.isReliable = (score.rTotal >= score.dynamicTau);

    // 更新位移历史与上一帧框
    if (m_hasPrevBox) {
        cv::Point2f curCenter(currentBox.x + currentBox.width * 0.5f,
                              currentBox.y + currentBox.height * 0.5f);
        cv::Point2f prevCenter(m_prevBox.x + m_prevBox.width * 0.5f,
                               m_prevBox.y + m_prevBox.height * 0.5f);
        m_velHistory.push_back(curCenter - prevCenter);
        if (m_velHistory.size() > 3) {
            m_velHistory.pop_front();
        }
    }
    m_prevBox = currentBox;
    m_hasPrevBox = true;

    return score;
}
