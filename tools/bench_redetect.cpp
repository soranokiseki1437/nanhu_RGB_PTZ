#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#include "../dsstcore.h"
#include "../onnxdetector.h"
#include "../onnxsottracker.h"
#include "../yolopostprocessor.h"

namespace fs = std::filesystem;

static float computeIoU(const cv::Rect2f& a, const cv::Rect2f& b)
{
    float x1 = std::max(a.x, b.x);
    float y1 = std::max(a.y, b.y);
    float x2 = std::min(a.x + a.width, b.x + b.width);
    float y2 = std::min(a.y + a.height, b.y + b.height);
    float iw = std::max(0.0f, x2 - x1);
    float ih = std::max(0.0f, y2 - y1);
    float ia = iw * ih;
    float ua = std::max(0.0f, a.width) * std::max(0.0f, a.height)
             + std::max(0.0f, b.width) * std::max(0.0f, b.height) - ia;
    return (ua > 0.0f) ? (ia / ua) : 0.0f;
}

static float computeCLE(const cv::Rect2f& a, const cv::Rect2f& b)
{
    float cx1 = a.x + a.width * 0.5f;
    float cy1 = a.y + a.height * 0.5f;
    float cx2 = b.x + b.width * 0.5f;
    float cy2 = b.y + b.height * 0.5f;
    float dx = cx1 - cx2;
    float dy = cy1 - cy2;
    return std::sqrt(dx * dx + dy * dy);
}

static float computePSR(const cv::Mat& response)
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

// 1:1 exact copy of ObjectTracker::TargetAnchor & extractPatchAppearance / verifyRecoveryCandidate
struct TargetAnchor {
    bool valid = false;
    cv::Rect2f initBbox;
    cv::Mat origPatchGray;
    cv::Mat histHS;
    cv::Mat histHS_fullBox; // full box HS hist matching Route A benchmark
    cv::Mat histV;
    cv::Mat patchNorm;
    bool hasTexture = false;
    bool isColor = true;
    float meanSat = 0.0f;
};

static bool extractPatchAppearance(const cv::Mat& image, const cv::Rect2f& box,
                                   cv::Mat& outHistHS, cv::Mat& outHistV, cv::Mat& outPatchNorm,
                                   bool& outHasTexture, bool& outIsColor, float& outMeanSat)
{
    if (image.empty() || box.width < 4.0f || box.height < 4.0f) return false;
    const cv::Rect imgBounds(0, 0, image.cols, image.rows);
    cv::Rect roi(static_cast<int>(std::round(box.x)),
                 static_cast<int>(std::round(box.y)),
                 static_cast<int>(std::round(box.width)),
                 static_cast<int>(std::round(box.height)));
    roi &= imgBounds;
    if (roi.width < 4 || roi.height < 4) return false;

    cv::Rect histRoi = roi;
    if (roi.width >= 12 && roi.height >= 12) {
        const int insetX = std::max(1, roi.width / 10);
        const int insetY = std::max(1, roi.height / 10);
        cv::Rect inner(roi.x + insetX, roi.y + insetY,
                       roi.width - 2 * insetX, roi.height - 2 * insetY);
        if (inner.width >= 4 && inner.height >= 4) histRoi = inner;
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

    cv::Mat grayRoi;
    if (image.channels() >= 3) cv::cvtColor(image(roi), grayRoi, cv::COLOR_BGR2GRAY);
    else grayRoi = image(roi);

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

static float bhattSim(const cv::Mat& h1, const cv::Mat& h2)
{
    if (h1.empty() || h2.empty()) return 0.0f;
    const double d = cv::compareHist(h1, h2, cv::HISTCMP_BHATTACHARYYA);
    if (std::isnan(d) || std::isinf(d)) return 0.0f;
    return std::clamp(static_cast<float>(1.0 - d * d), 0.0f, 1.0f);
}

enum class KernelVerifyMode {
    NONE = 0,
    DSST = 1,
    LIGHTTRACK = 2
};

static bool verifyRecoveryCandidate(
    const cv::Mat& image,
    const cv::Rect2f& candBox,
    const TargetAnchor& anchor,
    KernelVerifyMode kmode,
    const DsstCore& dsst,
    OnnxSotTracker* sot,
    float& outMatchScore)
{
    cv::Mat candHS, candV, candPatch;
    bool candTex = false, candColor = true;
    float candSat = 0.0f;
    if (!extractPatchAppearance(image, candBox, candHS, candV, candPatch, candTex, candColor, candSat)) {
        return false;
    }

    const float bcV = bhattSim(anchor.histV, candV);
    float histSim = bcV;
    if (anchor.isColor && candColor) {
        const float bcHS = bhattSim(anchor.histHS, candHS);
        const float satWeight = std::clamp((anchor.meanSat - 15.0f) / 45.0f, 0.25f, 0.65f);
        histSim = satWeight * bcHS + (1.0f - satWeight) * bcV;
    }
    const float zncc = (anchor.hasTexture && candTex)
        ? std::clamp(static_cast<float>(anchor.patchNorm.dot(candPatch)), -1.0f, 1.0f)
        : 0.5f;

    // isTotalLost == true thresholds (matching ObjectTracker::verifyRecoveryCandidate)
    if (histSim < 0.45f) return false;

    if (kmode == KernelVerifyMode::NONE) {
        outMatchScore = 0.65f * histSim + 0.35f * std::clamp(zncc, 0.0f, 1.0f);
        return true;
    }

    float kernelScore = 0.0f;
    const cv::Point2f candCenter(candBox.x + candBox.width * 0.5f,
                                 candBox.y + candBox.height * 0.5f);

    if (kmode == KernelVerifyMode::LIGHTTRACK) {
        if (sot && sot->isInitialized()) {
            const SotResult probe = sot->peekBox(image, candBox, true);
            if (probe.valid) {
                const float dist = static_cast<float>(cv::norm(probe.center - candCenter));
                const float maxShift = std::max(candBox.width, candBox.height) * 0.75f + 12.0f;
                if (dist <= maxShift) kernelScore = probe.score;
            }
        }
        if (kernelScore < 0.68f) return false;
        if (zncc < 0.20f && histSim < 0.62f) return false;
        outMatchScore = 0.45f * histSim + 0.35f * kernelScore
                      + 0.20f * std::clamp(zncc, 0.0f, 1.0f);
        return true;
    } else {
        const float baseArea = std::max(16.0f, static_cast<float>(dsst.targetSize().area()));
        const float candArea = std::max(16.0f, candBox.width * candBox.height);
        const float candScale = std::sqrt(candArea / baseArea);
        cv::Mat resp;
        const cv::Rect r = dsst.detectOnlyBackup(image, candCenter, candScale, &resp);
        if (r.width > 0 && !resp.empty()) {
            const cv::Point2f rCenter(r.x + r.width * 0.5f, r.y + r.height * 0.5f);
            const float dist = static_cast<float>(cv::norm(rCenter - candCenter));
            const float maxShift = std::max(candBox.width, candBox.height) * 0.85f + 12.0f;
            if (dist <= maxShift) kernelScore = computePSR(resp);
        }
        if (kernelScore < 6.0f) return false;
        if (zncc < 0.20f && histSim < 0.60f && kernelScore < 9.0f) return false;
        const float psrNorm = std::clamp((kernelScore - 4.0f) / 16.0f, 0.0f, 1.0f);
        outMatchScore = 0.45f * histSim + 0.35f * psrNorm
                      + 0.20f * std::clamp(zncc, 0.0f, 1.0f);
        return true;
    }
}

struct RedetectResult {
    bool accepted = false;
    cv::Rect2f box;
    float score = 0.0f;
    float timeMs = 0.0f;
};

// Select candidate from precomputed YOLO detections using exact tryDetectAndRecover rules (isTotalLost=true)
static RedetectResult filterYoloDetections(
    const std::vector<DetectionBox>& detections,
    const cv::Mat& image,
    const TargetAnchor& anchor,
    KernelVerifyMode kmode,
    const DsstCore& dsst,
    OnnxSotTracker* sot,
    bool applyAnchorVerify,
    float detMs)
{
    RedetectResult res;
    if (detections.empty()) {
        res.timeMs = detMs;
        return res;
    }

    auto t0 = std::chrono::steady_clock::now();
    const float currentW = anchor.initBbox.width;
    const float currentH = anchor.initBbox.height;
    const float currentAspect = (currentH > 1.0f) ? (currentW / currentH) : 1.0f;
    const float origArea = std::max(16.0f, currentW * currentH);
    const float minConfThr = 0.35f; // isTotalLost=true

    float bestScore = 0.0f;
    cv::Rect2f bestBox;
    bool found = false;

    for (const auto& det : detections) {
        if (det.confidence < minConfThr) continue;

        const float dw = static_cast<float>(det.rect.width());
        const float dh = static_cast<float>(det.rect.height());
        const float detAspect = (dh > 1.0f) ? (dw / dh) : 1.0f;
        const float aspectDiff = std::abs(detAspect - currentAspect) / std::max(currentAspect, 0.1f);
        if (aspectDiff > 1.5f) continue;

        const float detArea = dw * dh;
        const float areaRatio = detArea / origArea;
        if (areaRatio < 0.15f || areaRatio > 6.00f) continue;

        const cv::Rect2f candBox(static_cast<float>(det.rect.x()),
                                 static_cast<float>(det.rect.y()),
                                 dw, dh);
        const float aspectWeight = std::max(0.0f, 1.0f - aspectDiff * 0.5f);

        float candidateScore = 0.0f;
        if (applyAnchorVerify) {
            float anchorMatchScore = 0.0f;
            if (!verifyRecoveryCandidate(image, candBox, anchor, kmode, dsst, sot, anchorMatchScore)) {
                continue;
            }
            candidateScore = 0.55f * anchorMatchScore + 0.30f * det.confidence + 0.15f * aspectWeight;
        } else {
            candidateScore = 0.85f * det.confidence + 0.15f * aspectWeight;
        }

        if (candidateScore > bestScore) {
            bestScore = candidateScore;
            bestBox = candBox;
            found = true;
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    float filterMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
    res.timeMs = detMs + filterMs;

    const float scoreThreshold = applyAnchorVerify ? 0.40f : 0.30f;
    if (found && bestScore >= scoreThreshold) {
        res.accepted = true;
        res.box = bestBox;
        res.score = bestScore;
    }
    return res;
}

// Route A: Pyramid Multi-Scale ZNCC + Color / Full Anchor Verification
static RedetectResult runRouteA(
    const cv::Mat& image,
    const cv::Mat& grayFull,
    const TargetAnchor& anchor,
    bool useFullAnchorVerify,
    const DsstCore& dsst,
    OnnxSotTracker* sot)
{
    RedetectResult res;
    auto t0 = std::chrono::steady_clock::now();

    if (anchor.origPatchGray.empty() || anchor.initBbox.width < 4.0f || anchor.initBbox.height < 4.0f) {
        return res;
    }

    cv::Scalar meanVal, stdVal;
    cv::meanStdDev(anchor.origPatchGray, meanVal, stdVal);
    if (stdVal[0] < 5.0) {
        res.timeMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
        return res;
    }

    const float gw = anchor.initBbox.width;
    const float gh = anchor.initBbox.height;
    const float minDim = std::min(gw, gh);
    const float s = (minDim >= 32.0f) ? 0.25f : 0.50f;

    cv::Mat graySmall;
    cv::resize(grayFull, graySmall, cv::Size(), s, s, cv::INTER_AREA);

    float bestScore = -1.0f;
    cv::Point bestLoc(0, 0);
    float bestScale = 1.0f;
    int bestWt = 0, bestHt = 0;

    const float scales[3] = {0.85f, 1.00f, 1.15f};
    for (float sc : scales) {
        int wt = std::max(8, std::min(graySmall.cols - 2, static_cast<int>(std::round(gw * s * sc))));
        int ht = std::max(8, std::min(graySmall.rows - 2, static_cast<int>(std::round(gh * s * sc))));
        if (wt >= graySmall.cols || ht >= graySmall.rows) continue;

        cv::Mat tplSc;
        cv::resize(anchor.origPatchGray, tplSc, cv::Size(wt, ht), 0, 0, cv::INTER_AREA);

        cv::Mat resMap;
        cv::matchTemplate(graySmall, tplSc, resMap, cv::TM_CCOEFF_NORMED);
        double maxV = -1.0;
        cv::Point maxL;
        cv::minMaxLoc(resMap, nullptr, &maxV, nullptr, &maxL);
        if (static_cast<float>(maxV) > bestScore) {
            bestScore = static_cast<float>(maxV);
            bestLoc = maxL;
            bestScale = sc;
            bestWt = wt;
            bestHt = ht;
        }
    }

    if (bestScore < 0.50f) {
        res.score = bestScore;
        res.timeMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
        return res;
    }

    const float cx = (bestLoc.x + bestWt * 0.5f) / s;
    const float cy = (bestLoc.y + bestHt * 0.5f) / s;
    const float wRecov = gw * bestScale;
    const float hRecov = gh * bestScale;
    cv::Rect2f candBox(cx - wRecov * 0.5f, cy - hRecov * 0.5f, wRecov, hRecov);

    if (!useFullAnchorVerify) {
        // Exactly match Python RouteAPyramidZNCC (full box 16x8 HS color check >= 0.45)
        cv::Rect clipBox(
            std::max(0, std::min(image.cols - 1, static_cast<int>(std::round(candBox.x)))),
            std::max(0, std::min(image.rows - 1, static_cast<int>(std::round(candBox.y)))),
            0, 0);
        int bx2 = std::max(clipBox.x + 1, std::min(image.cols, static_cast<int>(std::round(candBox.x + candBox.width))));
        int by2 = std::max(clipBox.y + 1, std::min(image.rows, static_cast<int>(std::round(candBox.y + candBox.height))));
        clipBox.width = bx2 - clipBox.x;
        clipBox.height = by2 - clipBox.y;

        cv::Mat candHsv;
        cv::cvtColor(image(clipBox), candHsv, cv::COLOR_BGR2HSV);
        int hsSizes[] = {16, 8};
        float hRange[] = {0.0f, 180.0f};
        float sRange[] = {0.0f, 256.0f};
        const float* hsRanges[] = {hRange, sRange};
        const int hsCh[] = {0, 1};
        cv::Mat candHist;
        cv::calcHist(&candHsv, 1, hsCh, cv::Mat(), candHist, 2, hsSizes, hsRanges, true, false);
        cv::normalize(candHist, candHist, 1.0, 0.0, cv::NORM_L1);
        float csim = bhattSim(anchor.histHS_fullBox, candHist);

        res.timeMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (csim >= 0.45f) {
            res.accepted = true;
            res.box = candBox;
            res.score = 0.5f * bestScore + 0.5f * csim;
        }
    } else {
        float anchorScore = 0.0f;
        bool ok = verifyRecoveryCandidate(image, candBox, anchor, KernelVerifyMode::DSST, dsst, sot, anchorScore);
        res.timeMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (ok) {
            res.accepted = true;
            res.box = candBox;
            res.score = anchorScore;
        }
    }
    return res;
}

struct MethodStats {
    std::string name;
    int validFrames = 0;
    int acceptedValid = 0;
    int absentFrames = 0;
    int absentFP = 0;
    std::vector<float> ious;
    std::vector<float> cles;
    std::vector<float> latencies;

    void record(const RedetectResult& r, bool isValid, const cv::Rect2f& gt) {
        latencies.push_back(r.timeMs);
        if (isValid) {
            validFrames++;
            if (r.accepted) {
                acceptedValid++;
                ious.push_back(computeIoU(r.box, gt));
                cles.push_back(computeCLE(r.box, gt));
            } else {
                ious.push_back(0.0f);
                cles.push_back(9999.0f);
            }
        } else {
            absentFrames++;
            if (r.accepted) absentFP++;
        }
    }
};

struct SummaryMetrics {
    float auc = 0.0f;
    float dp20 = 0.0f;
    float meanIoU = 0.0f;
    float sr50 = 0.0f;
    float meanCleHit = 9999.0f;
    float missRate = 1.0f;
    float hijackOnValidRate = 0.0f; // Accepted valid frame but IoU == 0 (wrong target)
    float absentFpRate = 0.0f;
    float avgMs = 0.0f;
};

static SummaryMetrics computeSummary(const MethodStats& ms)
{
    SummaryMetrics s;
    if (ms.validFrames == 0) return s;

    // AUC over 21 thresholds 0.00..1.00 step 0.05
    double aucSum = 0.0;
    for (int k = 0; k <= 20; ++k) {
        float th = k * 0.05f;
        int cnt = 0;
        for (float v : ms.ious) if (v >= th) cnt++;
        aucSum += static_cast<double>(cnt) / ms.validFrames;
    }
    s.auc = static_cast<float>(aucSum / 21.0);

    int cntDp20 = 0, cntSr50 = 0, cntZeroIou = 0, cntHijack = 0, hitCount = 0;
    double iouSum = 0.0, cleHitSum = 0.0;
    for (size_t i = 0; i < ms.ious.size(); ++i) {
        float v = ms.ious[i];
        float c = ms.cles[i];
        iouSum += v;
        if (c <= 20.0f) cntDp20++;
        if (v >= 0.50f) cntSr50++;
        if (v == 0.0f) cntZeroIou++;
        if (c < 9000.0f) {
            cleHitSum += c;
            hitCount++;
            if (v == 0.0f) cntHijack++;
        }
    }
    s.dp20 = static_cast<float>(cntDp20) / ms.validFrames;
    s.meanIoU = static_cast<float>(iouSum / ms.validFrames);
    s.sr50 = static_cast<float>(cntSr50) / ms.validFrames;
    s.meanCleHit = (hitCount > 0) ? static_cast<float>(cleHitSum / hitCount) : 9999.0f;
    s.missRate = static_cast<float>(cntZeroIou) / ms.validFrames;
    s.hijackOnValidRate = static_cast<float>(cntHijack) / ms.validFrames;
    s.absentFpRate = (ms.absentFrames > 0) ? (static_cast<float>(ms.absentFP) / ms.absentFrames) : 0.0f;

    if (!ms.latencies.empty()) {
        double tSum = std::accumulate(ms.latencies.begin(), ms.latencies.end(), 0.0);
        s.avgMs = static_cast<float>(tSum / ms.latencies.size());
    }
    return s;
}

static std::vector<cv::Rect2f> loadGT(const std::string& gtPath)
{
    std::vector<cv::Rect2f> gts;
    std::ifstream file(gtPath);
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::replace(line.begin(), line.end(), ',', ' ');
        std::replace(line.begin(), line.end(), '\t', ' ');
        std::istringstream ss(line);
        float x, y, w, h;
        if (ss >> x >> y >> w >> h) {
            gts.emplace_back(x, y, w, h);
        }
    }
    return gts;
}

struct SeqSpec {
    std::string name;
    int step;
    std::string desc;
};

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QLoggingCategory::setFilterRules("*.debug=false\n*.info=false");

    const std::vector<SeqSpec> sequences = {
        {"video01", 5,  "Daylight medium UAV, smooth flight"},
        {"video03", 2,  "Distant small UAV (42x40)"},
        {"video04", 3,  "UAV with tree/building clutter"},
        {"video05", 4,  "UAV maneuvering and turning"},
        {"video07", 10, "Long sequence with absent frames"},
        {"video11", 8,  "Medium UAV attitude change"},
        {"video16", 10, "Small UAV, low contrast"}
    };

    // Load AI detectors (DirectML RTX 5070)
    DetectorConfig dcfg;
    dcfg.useGpu = true;
    dcfg.confThreshold = 0.25f;
    dcfg.nmsThreshold = 0.45f;

    auto detUav11 = std::make_shared<OnnxDetector>();
    if (!detUav11->loadModel("models/yolo11n_uav_ft.onnx", dcfg)) {
        std::cerr << "Failed to load models/yolo11n_uav_ft.onnx\n";
        return 1;
    }
    auto detUav8 = std::make_shared<OnnxDetector>();
    if (!detUav8->loadModel("models/yolov8n_uav_ft.onnx", dcfg)) {
        std::cerr << "Failed to load models/yolov8n_uav_ft.onnx\n";
        return 1;
    }
    auto detCoco8 = std::make_shared<OnnxDetector>();
    if (!detCoco8->loadModel("models/yolov8n.onnx", dcfg)) {
        std::cerr << "Failed to load models/yolov8n.onnx\n";
        return 1;
    }

    // Load LightTrack engine (DirectML RTX 5070)
    SotTrackerConfig scfg;
    scfg.templateModel = "models/lighttrack_template.onnx";
    scfg.searchModelSmall = "models/lighttrack_search_256.onnx";
    scfg.searchModelBig = "models/lighttrack_search_288.onnx";
    scfg.useGpu = true;
    auto sot = std::make_shared<OnnxSotTracker>(scfg);
    if (!sot->ensureLoaded()) {
        std::cerr << "Failed to load LightTrack ONNX models\n";
        return 1;
    }

    // Warmup detectors on first image
    {
        cv::Mat warmImg = cv::imread("datasets/Anti-UAV/tracking/Anti-UAV-Tracking-V0/video01/00001.jpg");
        if (!warmImg.empty()) {
            for (int i = 0; i < 3; ++i) {
                detUav11->detect(warmImg);
                detUav8->detect(warmImg);
                detCoco8->detect(warmImg);
            }
        }
    }

    const std::vector<std::string> methodOrder = {
        "YOLO11n_UAV_Raw",
        "YOLO11n_UAV_Exist_DSST",
        "YOLO11n_UAV_Exist_LT",
        "YOLOv8n_UAV_Exist_DSST",
        "YOLOv8n_COCO_Raw",
        "YOLOv8n_COCO_Exist_DSST",
        "YOLOv8n_COCO_Exist_LT",
        "RouteA_ZNCC_Color",
        "RouteA_ZNCC_ExistVerify_DSST",
        "Hybrid_YOLO11n_UAV_DSST_RouteA",
        "Hybrid_YOLO11n_UAV_LT_RouteA",
        "Hybrid_YOLOv8n_COCO_DSST_RouteA"
    };

    std::map<std::string, MethodStats> overall;
    for (const auto& m : methodOrder) overall[m].name = m;

    struct PerSeqRow {
        std::string seq;
        std::string desc;
        int nValid = 0;
        int nAbsent = 0;
        std::map<std::string, SummaryMetrics> metrics;
    };
    std::vector<PerSeqRow> perSeqRows;

    std::cout << "============================================================================================================\n";
    std::cout << "EXISTING YOLO RE-DETECTION VS ROUTE A BENCHMARK (C++ Release / DirectML GPU)\n";
    std::cout << "Dataset: Anti-UAV 1080P (7 sequences, 925 valid + 64 absent frames, Frame-0 Anchor, Zero Prior)\n";
    std::cout << "============================================================================================================\n";

    for (const auto& sq : sequences) {
        std::string gtPath = "datasets/Anti-UAV/tracking/Anti-UAV-Tracking-V0GT/" + sq.name + "_gt.txt";
        std::string imgDir = "datasets/Anti-UAV/tracking/Anti-UAV-Tracking-V0/" + sq.name;
        auto gts = loadGT(gtPath);

        std::vector<std::string> imgFiles;
        for (const auto& entry : fs::directory_iterator(imgDir)) {
            auto ext = entry.path().extension().string();
            if (ext == ".jpg" || ext == ".png") {
                imgFiles.push_back(entry.path().string());
            }
        }
        std::sort(imgFiles.begin(), imgFiles.end());

        size_t nFrames = std::min(imgFiles.size(), gts.size());
        if (nFrames < 2) continue;

        cv::Mat img0 = cv::imread(imgFiles[0]);
        cv::Rect2f gt0 = gts[0];
        cv::Rect roi0(static_cast<int>(std::round(gt0.x)), static_cast<int>(std::round(gt0.y)),
                      static_cast<int>(std::round(gt0.width)), static_cast<int>(std::round(gt0.height)));
        roi0 &= cv::Rect(0, 0, img0.cols, img0.rows);

        // Initialize Anchor, DSST backup, LightTrack anchor
        TargetAnchor anchor;
        anchor.initBbox = cv::Rect2f(static_cast<float>(roi0.x), static_cast<float>(roi0.y),
                                     static_cast<float>(roi0.width), static_cast<float>(roi0.height));
        cv::cvtColor(img0(roi0), anchor.origPatchGray, cv::COLOR_BGR2GRAY);
        {
            cv::Mat hsvFull;
            cv::cvtColor(img0(roi0), hsvFull, cv::COLOR_BGR2HSV);
            int hsSizes[] = {16, 8};
            float hRange[] = {0.0f, 180.0f};
            float sRange[] = {0.0f, 256.0f};
            const float* hsRanges[] = {hRange, sRange};
            const int hsCh[] = {0, 1};
            cv::calcHist(&hsvFull, 1, hsCh, cv::Mat(), anchor.histHS_fullBox, 2, hsSizes, hsRanges, true, false);
            cv::normalize(anchor.histHS_fullBox, anchor.histHS_fullBox, 1.0, 0.0, cv::NORM_L1);
        }
        anchor.valid = extractPatchAppearance(img0, anchor.initBbox,
                                              anchor.histHS, anchor.histV, anchor.patchNorm,
                                              anchor.hasTexture, anchor.isColor, anchor.meanSat);

        DsstCore dsst;
        dsst.init(img0, roi0);
        sot->init(img0, anchor.initBbox);

        std::map<std::string, MethodStats> seqStats;
        for (const auto& m : methodOrder) seqStats[m].name = m;

        int seqValid = 0, seqAbsent = 0;

        for (size_t fIdx = 1; fIdx < nFrames; fIdx += sq.step) {
            cv::Rect2f gt = gts[fIdx];
            bool isValid = (gt.width > 0.0f && gt.height > 0.0f);
            cv::Mat img = cv::imread(imgFiles[fIdx]);
            if (img.empty()) continue;

            if (isValid) seqValid++;
            else seqAbsent++;

            cv::Mat grayFull;
            cv::cvtColor(img, grayFull, cv::COLOR_BGR2GRAY);

            // Run detectors once per frame and time end-to-end
            auto td0 = std::chrono::steady_clock::now();
            auto detsU11 = detUav11->detect(img);
            float msU11 = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - td0).count();

            auto td1 = std::chrono::steady_clock::now();
            auto detsU8 = detUav8->detect(img);
            float msU8 = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - td1).count();

            auto td2 = std::chrono::steady_clock::now();
            auto detsC8 = detCoco8->detect(img);
            float msC8 = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - td2).count();

            auto r_u11_raw  = filterYoloDetections(detsU11, img, anchor, KernelVerifyMode::NONE, dsst, sot.get(), false, msU11);
            auto r_u11_dsst = filterYoloDetections(detsU11, img, anchor, KernelVerifyMode::DSST, dsst, sot.get(), true, msU11);
            auto r_u11_lt   = filterYoloDetections(detsU11, img, anchor, KernelVerifyMode::LIGHTTRACK, dsst, sot.get(), true, msU11);

            auto r_u8_dsst  = filterYoloDetections(detsU8,  img, anchor, KernelVerifyMode::DSST, dsst, sot.get(), true, msU8);

            auto r_c8_raw   = filterYoloDetections(detsC8,  img, anchor, KernelVerifyMode::NONE, dsst, sot.get(), false, msC8);
            auto r_c8_dsst  = filterYoloDetections(detsC8,  img, anchor, KernelVerifyMode::DSST, dsst, sot.get(), true, msC8);
            auto r_c8_lt    = filterYoloDetections(detsC8,  img, anchor, KernelVerifyMode::LIGHTTRACK, dsst, sot.get(), true, msC8);

            auto r_ra_col   = runRouteA(img, grayFull, anchor, false, dsst, sot.get());
            auto r_ra_dsst  = runRouteA(img, grayFull, anchor, true,  dsst, sot.get());

            auto makeHybrid = [](const RedetectResult& primary, const RedetectResult& fallback) {
                if (primary.accepted) return primary;
                RedetectResult h = fallback;
                h.timeMs = primary.timeMs + fallback.timeMs;
                return h;
            };
            auto r_hyb_u11_dsst = makeHybrid(r_u11_dsst, r_ra_col);
            auto r_hyb_u11_lt   = makeHybrid(r_u11_lt,   r_ra_col);
            auto r_hyb_c8_dsst  = makeHybrid(r_c8_dsst,  r_ra_col);

            auto rec = [&](const std::string& name, const RedetectResult& rr) {
                seqStats[name].record(rr, isValid, gt);
                overall[name].record(rr, isValid, gt);
            };
            rec("YOLO11n_UAV_Raw", r_u11_raw);
            rec("YOLO11n_UAV_Exist_DSST", r_u11_dsst);
            rec("YOLO11n_UAV_Exist_LT", r_u11_lt);
            rec("YOLOv8n_UAV_Exist_DSST", r_u8_dsst);
            rec("YOLOv8n_COCO_Raw", r_c8_raw);
            rec("YOLOv8n_COCO_Exist_DSST", r_c8_dsst);
            rec("YOLOv8n_COCO_Exist_LT", r_c8_lt);
            rec("RouteA_ZNCC_Color", r_ra_col);
            rec("RouteA_ZNCC_ExistVerify_DSST", r_ra_dsst);
            rec("Hybrid_YOLO11n_UAV_DSST_RouteA", r_hyb_u11_dsst);
            rec("Hybrid_YOLO11n_UAV_LT_RouteA", r_hyb_u11_lt);
            rec("Hybrid_YOLOv8n_COCO_DSST_RouteA", r_hyb_c8_dsst);
        }

        PerSeqRow row;
        row.seq = sq.name;
        row.desc = sq.desc;
        row.nValid = seqValid;
        row.nAbsent = seqAbsent;
        for (const auto& m : methodOrder) {
            row.metrics[m] = computeSummary(seqStats[m]);
        }
        perSeqRows.push_back(row);

        const auto& mUavRaw = row.metrics["YOLO11n_UAV_Raw"];
        const auto& mUavDsst = row.metrics["YOLO11n_UAV_Exist_DSST"];
        const auto& mUavLt = row.metrics["YOLO11n_UAV_Exist_LT"];
        const auto& mCoco = row.metrics["YOLOv8n_COCO_Exist_DSST"];
        const auto& mRA = row.metrics["RouteA_ZNCC_Color"];

        std::cout << "[" << std::left << std::setw(7) << sq.name << " V=" << std::setw(3) << seqValid << "] "
                  << "UAV_Raw AUC=" << std::fixed << std::setprecision(3) << mUavRaw.auc
                  << " | UAV_DSST AUC=" << mUavDsst.auc
                  << " | UAV_LT AUC=" << mUavLt.auc
                  << " | COCO_DSST AUC=" << mCoco.auc
                  << " | RouteA AUC=" << mRA.auc << "\n";
    }

    std::cout << "\n==================================================================================================================================================\n";
    std::cout << std::left << std::setw(32) << "Method"
              << std::right << std::setw(9) << "AUC(%)"
              << std::setw(9) << "DP20(%)"
              << std::setw(9) << "SR50(%)"
              << std::setw(9) << "mIoU"
              << std::setw(10) << "HitCLE"
              << std::setw(10) << "Miss(%)"
              << std::setw(11) << "Hijack(%)"
              << std::setw(13) << "AbsentFP"
              << std::setw(10) << "Time(ms)" << "\n";
    std::cout << "--------------------------------------------------------------------------------------------------------------------------------------------------\n";

    std::map<std::string, SummaryMetrics> overallSummaries;
    for (const auto& m : methodOrder) {
        const auto& st = overall[m];
        SummaryMetrics s = computeSummary(st);
        overallSummaries[m] = s;
        std::ostringstream fpStr;
        fpStr << st.absentFP << "/" << st.absentFrames << "(" << std::fixed << std::setprecision(1) << s.absentFpRate * 100.0f << "%)";

        std::cout << std::left << std::setw(32) << m
                  << std::right << std::fixed
                  << std::setw(9) << std::setprecision(2) << s.auc * 100.0f
                  << std::setw(9) << std::setprecision(2) << s.dp20 * 100.0f
                  << std::setw(9) << std::setprecision(2) << s.sr50 * 100.0f
                  << std::setw(9) << std::setprecision(4) << s.meanIoU
                  << std::setw(10) << std::setprecision(1) << s.meanCleHit
                  << std::setw(10) << std::setprecision(2) << s.missRate * 100.0f
                  << std::setw(11) << std::setprecision(2) << s.hijackOnValidRate * 100.0f
                  << std::setw(13) << fpStr.str()
                  << std::setw(10) << std::setprecision(2) << s.avgMs << "\n";
    }
    std::cout << "==================================================================================================================================================\n";

    // Write JSON output for full analysis
    std::ofstream jf("scripts/benchmark_yolo_vs_route_a.json");
    if (jf.is_open()) {
        auto writeMetricsJson = [&](const SummaryMetrics& s, int absFp, int absTot) {
            jf << "{"
               << "\"auc\": " << s.auc << ", "
               << "\"dp20\": " << s.dp20 << ", "
               << "\"sr50\": " << s.sr50 << ", "
               << "\"mean_iou\": " << s.meanIoU << ", "
               << "\"mean_cle_hit\": " << s.meanCleHit << ", "
               << "\"miss_rate\": " << s.missRate << ", "
               << "\"hijack_on_valid_rate\": " << s.hijackOnValidRate << ", "
               << "\"absent_fp\": " << absFp << ", "
               << "\"absent_total\": " << absTot << ", "
               << "\"absent_fp_rate\": " << s.absentFpRate << ", "
               << "\"avg_ms\": " << s.avgMs
               << "}";
        };
        jf << "{\n  \"overall\": {\n";
        for (size_t i = 0; i < methodOrder.size(); ++i) {
            const auto& m = methodOrder[i];
            jf << "    \"" << m << "\": ";
            writeMetricsJson(overallSummaries[m], overall[m].absentFP, overall[m].absentFrames);
            jf << (i + 1 < methodOrder.size() ? ",\n" : "\n");
        }
        jf << "  },\n  \"per_sequence\": [\n";
        for (size_t si = 0; si < perSeqRows.size(); ++si) {
            const auto& r = perSeqRows[si];
            jf << "    {\n      \"seq\": \"" << r.seq << "\", \"desc\": \"" << r.desc
               << "\", \"n_valid\": " << r.nValid << ", \"n_absent\": " << r.nAbsent << ",\n      \"methods\": {\n";
            for (size_t mi = 0; mi < methodOrder.size(); ++mi) {
                const auto& m = methodOrder[mi];
                jf << "        \"" << m << "\": ";
                writeMetricsJson(r.metrics.at(m), 0, r.nAbsent);
                jf << (mi + 1 < methodOrder.size() ? ",\n" : "\n");
            }
            jf << "      }\n    }" << (si + 1 < perSeqRows.size() ? ",\n" : "\n");
        }
        jf << "  ]\n}\n";
        jf.close();
        std::cout << "Saved JSON results to scripts/benchmark_yolo_vs_route_a.json\n";
    }
    return 0;
}
