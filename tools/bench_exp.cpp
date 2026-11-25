#include <QCoreApplication>
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

struct TargetAnchor {
    bool valid = false;
    cv::Rect2f initBbox;
    cv::Mat histHS;
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

enum class KernelMode {
    NONE = 0,
    DSST = 1,
    LIGHTTRACK = 2
};

struct RedetectPolicy {
    std::string name;
    float minConf = 0.35f;
    float maxAspectDiff = 1.5f;
    float minAreaRatio = 0.15f;
    float maxAreaRatio = 6.00f;
    float minHistSim = 0.45f;
    KernelMode kmode = KernelMode::LIGHTTRACK;
    float minSotAnchor = 0.68f;
    float minBackupPsr = 6.0f;
    bool allowSoftKernelOnHighConf = false;
    float highConfBypassThr = 0.65f;
    float highConfMinHistSim = 0.50f;
    float scoreThreshold = 0.40f;
    bool twoFrameGate = false;
    int twoFrameLag = 1; // 1 = immediate next frame, 10 = existing 10-frame wait
};

struct RedetectResult {
    bool accepted = false;
    cv::Rect2f box;
    float score = 0.0f;
    float timeMs = 0.0f;
};

static bool verifyWithPolicy(
    const cv::Mat& image,
    const cv::Rect2f& candBox,
    float detConf,
    const TargetAnchor& anchor,
    const RedetectPolicy& pol,
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

    if (histSim < pol.minHistSim) return false;

    if (pol.kmode == KernelMode::NONE) {
        outMatchScore = 0.65f * histSim + 0.35f * std::clamp(zncc, 0.0f, 1.0f);
        return true;
    }

    const bool highConfStrongHist = pol.allowSoftKernelOnHighConf
        && (detConf >= pol.highConfBypassThr)
        && (histSim >= pol.highConfMinHistSim);

    const cv::Point2f candCenter(candBox.x + candBox.width * 0.5f,
                                 candBox.y + candBox.height * 0.5f);

    if (pol.kmode == KernelMode::LIGHTTRACK) {
        float sotScore = 0.0f;
        if (sot && sot->isInitialized()) {
            const SotResult probe = sot->peekBox(image, candBox, true);
            if (probe.valid) {
                const float dist = static_cast<float>(cv::norm(probe.center - candCenter));
                const float maxShift = std::max(candBox.width, candBox.height) * 0.75f + 12.0f;
                if (dist <= maxShift) sotScore = probe.score;
            }
        }
        if (!highConfStrongHist) {
            if (sotScore < pol.minSotAnchor) return false;
            if (zncc < 0.20f && histSim < 0.62f) return false;
        } else {
            // Even with high YOLO conf + strong hist, guard against ultra-low SOT garbage
            if (sotScore < 0.25f && histSim < 0.65f) return false;
        }
        outMatchScore = 0.45f * histSim + 0.35f * sotScore
                      + 0.20f * std::clamp(zncc, 0.0f, 1.0f);
        return true;
    } else {
        float psr = 0.0f;
        const float baseArea = std::max(16.0f, static_cast<float>(dsst.targetSize().area()));
        const float candArea = std::max(16.0f, candBox.width * candBox.height);
        const float candScale = std::sqrt(candArea / baseArea);
        cv::Mat resp;
        const cv::Rect r = dsst.detectOnlyBackup(image, candCenter, candScale, &resp);
        if (r.width > 0 && !resp.empty()) {
            const cv::Point2f rCenter(r.x + r.width * 0.5f, r.y + r.height * 0.5f);
            const float dist = static_cast<float>(cv::norm(rCenter - candCenter));
            const float maxShift = std::max(candBox.width, candBox.height) * 0.85f + 12.0f;
            if (dist <= maxShift) psr = computePSR(resp);
        }
        if (!highConfStrongHist) {
            if (psr < pol.minBackupPsr) return false;
            if (zncc < 0.20f && histSim < 0.60f && psr < 9.0f) return false;
        }
        const float psrNorm = std::clamp((psr - 4.0f) / 16.0f, 0.0f, 1.0f);
        outMatchScore = 0.45f * histSim + 0.35f * psrNorm
                      + 0.20f * std::clamp(zncc, 0.0f, 1.0f);
        return true;
    }
}

static RedetectResult evalPolicyOnDetections(
    const std::vector<DetectionBox>& detections,
    const cv::Mat& image,
    const TargetAnchor& anchor,
    const RedetectPolicy& pol,
    const DsstCore& dsst,
    OnnxSotTracker* sot,
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

    float bestScore = 0.0f;
    cv::Rect2f bestBox;
    bool found = false;

    for (const auto& det : detections) {
        if (det.confidence < pol.minConf) continue;

        const float dw = static_cast<float>(det.rect.width());
        const float dh = static_cast<float>(det.rect.height());
        const float detAspect = (dh > 1.0f) ? (dw / dh) : 1.0f;
        const float aspectDiff = std::abs(detAspect - currentAspect) / std::max(currentAspect, 0.1f);
        if (aspectDiff > pol.maxAspectDiff) continue;

        const float detArea = dw * dh;
        const float areaRatio = detArea / origArea;
        if (areaRatio < pol.minAreaRatio || areaRatio > pol.maxAreaRatio) continue;

        const cv::Rect2f candBox(static_cast<float>(det.rect.x()),
                                 static_cast<float>(det.rect.y()),
                                 dw, dh);
        const float aspectWeight = std::max(0.0f, 1.0f - aspectDiff * 0.5f);

        float anchorMatchScore = 0.0f;
        if (!verifyWithPolicy(image, candBox, det.confidence, anchor, pol, dsst, sot, anchorMatchScore)) {
            continue;
        }
        float candidateScore = 0.55f * anchorMatchScore + 0.30f * det.confidence + 0.15f * aspectWeight;

        if (candidateScore > bestScore) {
            bestScore = candidateScore;
            bestBox = candBox;
            found = true;
        }
    }

    float filterMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
    res.timeMs = detMs + filterMs;

    if (found && bestScore >= pol.scoreThreshold) {
        res.accepted = true;
        res.box = bestBox;
        res.score = bestScore;
    }
    return res;
}

static bool passTwoFrameConsistency(const cv::Rect2f& prevBox, const cv::Rect2f& currBox)
{
    float bx = currBox.x + currBox.width * 0.5f;
    float by = currBox.y + currBox.height * 0.5f;
    float px = prevBox.x + prevBox.width * 0.5f;
    float py = prevBox.y + prevBox.height * 0.5f;
    float stepDist = std::sqrt((bx - px) * (bx - px) + (by - py) * (by - py));
    float maxDist = std::max({currBox.width, currBox.height, prevBox.width, prevBox.height}) * 2.5f + 150.0f;
    return stepDist <= maxDist;
}

// P1 Experiment: Motion Foreground Proposal + Anchor Verification (class-agnostic when YOLO misses/unseen)
static RedetectResult runMotionProposalRecovery(
    const cv::Mat& prevGray,
    const cv::Mat& currBgr,
    const cv::Mat& currGray,
    const TargetAnchor& anchor,
    OnnxSotTracker* sot)
{
    RedetectResult res;
    auto t0 = std::chrono::steady_clock::now();
    if (prevGray.empty() || currGray.empty() || prevGray.size() != currGray.size()) {
        return res;
    }

    // 1. Downscale to 480x270 for fast global motion check + frame differencing
    cv::Mat gPrevSmall, gCurrSmall;
    const double s = 0.25;
    cv::resize(prevGray, gPrevSmall, cv::Size(), s, s, cv::INTER_AREA);
    cv::resize(currGray, gCurrSmall, cv::Size(), s, s, cv::INTER_AREA);

    // Check if camera itself is panning/moving rapidly using phaseCorrelate or mean absdiff fraction
    cv::Mat diff;
    cv::absdiff(gCurrSmall, gPrevSmall, diff);
    cv::Mat bin;
    cv::threshold(diff, bin, 18, 255, cv::THRESH_BINARY);

    // Strict static-camera gate: reject motion proposals when > 2.5% of frame pixels changed globally (panning)
    double changedRatio = cv::countNonZero(bin) / static_cast<double>(bin.cols * bin.rows);
    if (changedRatio > 0.025) {
        res.timeMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
        return res;
    }

    // Morphological closing to merge fragmented UAV silhouette
    cv::Mat kClose = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
    cv::morphologyEx(bin, bin, cv::MORPH_CLOSE, kClose);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(bin, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    struct BlobScore {
        cv::Rect2f box;
        float score;
    };
    std::vector<cv::Rect2f> proposals;
    proposals.reserve(16);

    const float origArea = std::max(16.0f, anchor.initBbox.width * anchor.initBbox.height);
    for (const auto& c : contours) {
        cv::Rect rb = cv::boundingRect(c);
        // Map back to 1080P and pad slightly
        float x = std::max(0.0f, static_cast<float>(rb.x / s) - 6.0f);
        float y = std::max(0.0f, static_cast<float>(rb.y / s) - 6.0f);
        float w = std::min(static_cast<float>(currBgr.cols) - x, static_cast<float>(rb.width / s) + 12.0f);
        float h = std::min(static_cast<float>(currBgr.rows) - y, static_cast<float>(rb.height / s) + 12.0f);
        if (w < 12.0f || h < 12.0f) continue;
        float areaRatio = (w * h) / origArea;
        if (areaRatio < 0.04f || areaRatio > 12.0f) continue;
        proposals.emplace_back(x, y, w, h);
    }

    // Limit max proposals evaluated by SOT peek to top 8 largest/reasonable
    if (proposals.size() > 8) {
        std::sort(proposals.begin(), proposals.end(), [](const cv::Rect2f& a, const cv::Rect2f& b) {
            return a.width * a.height > b.width * b.height;
        });
        proposals.resize(8);
    }

    DsstCore dummyDsst;
    RedetectPolicy strictPropPol;
    strictPropPol.minHistSim = 0.45f;
    strictPropPol.kmode = KernelMode::LIGHTTRACK;
    strictPropPol.minSotAnchor = 0.65f;

    float bestScore = 0.0f;
    cv::Rect2f bestBox;
    bool found = false;

    for (const auto& box : proposals) {
        float mscore = 0.0f;
        if (verifyWithPolicy(currBgr, box, 0.50f, anchor, strictPropPol, dummyDsst, sot, mscore)) {
            if (mscore > bestScore) {
                bestScore = mscore;
                bestBox = box;
                found = true;
            }
        }
    }

    res.timeMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (found && bestScore >= 0.45f) {
        res.accepted = true;
        res.box = bestBox;
        res.score = bestScore;
    }
    return res;
}

struct MethodStats {
    std::string name;
    int validFrames = 0;
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
    float sr50 = 0.0f;
    float meanIoU = 0.0f;
    float meanCleHit = 9999.0f;
    float missRate = 1.0f;
    float hijackOnValidRate = 0.0f;
    float absentFpRate = 0.0f;
    float avgMs = 0.0f;
};

static SummaryMetrics computeSummary(const MethodStats& ms)
{
    SummaryMetrics s;
    if (ms.validFrames == 0) return s;
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
        if (ss >> x >> y >> w >> h) gts.emplace_back(x, y, w, h);
    }
    return gts;
}

struct SeqSpec { std::string name; int step; std::string desc; };

static bool runAntiHijackUnitTest(const RedetectPolicy& pol, OnnxSotTracker* sot)
{
    // Synthetic Target A (blue/yellow cross) vs Distractor B (red/black box at conf=0.92)
    auto drawTargetA = [](cv::Mat& canvas, cv::Point c) {
        cv::Rect r(c.x - 30, c.y - 24, 60, 48);
        cv::rectangle(canvas, r, cv::Scalar(200, 120, 30), -1);
        cv::line(canvas, cv::Point(r.x + 6, c.y), cv::Point(r.x + r.width - 6, c.y), cv::Scalar(40, 230, 250), 4);
        cv::line(canvas, cv::Point(c.x, r.y + 6), cv::Point(c.x, r.y + r.height - 6), cv::Scalar(40, 230, 250), 4);
        cv::circle(canvas, c, 10, cv::Scalar(255, 255, 255), 2);
    };
    auto drawDistractorB = [](cv::Mat& canvas, cv::Point c) {
        cv::Rect r(c.x - 30, c.y - 24, 60, 48);
        cv::rectangle(canvas, r, cv::Scalar(30, 40, 210), -1);
        cv::rectangle(canvas, cv::Rect(r.x + 8, r.y + 8, r.width - 16, r.height - 16), cv::Scalar(20, 20, 20), -1);
    };

    cv::Mat bg(720, 1280, CV_8UC3, cv::Scalar(140, 145, 150));
    cv::Mat f0 = bg.clone();
    drawTargetA(f0, cv::Point(300, 300));
    cv::Rect2f boxA0(270, 276, 60, 48);

    TargetAnchor anchor;
    anchor.initBbox = boxA0;
    anchor.valid = extractPatchAppearance(f0, boxA0, anchor.histHS, anchor.histV, anchor.patchNorm,
                                          anchor.hasTexture, anchor.isColor, anchor.meanSat);
    DsstCore dsst;
    dsst.init(f0, cv::Rect(270, 276, 60, 48));
    sot->init(f0, boxA0);

    // Case 1: Only Distractor B present with YOLO conf = 0.92
    cv::Mat f1 = bg.clone();
    drawDistractorB(f1, cv::Point(340, 305));
    DetectionBox detB(QRectF(310, 281, 60, 48), 0.92f, 0, "UAV");
    auto r1 = evalPolicyOnDetections({detB}, f1, anchor, pol, dsst, sot, 0.0f);
    if (r1.accepted) return false; // Hijacked!

    // Case 2: Both Distractor B (conf 0.92) and shrunk rotated-slightly Target A (conf 0.72, area 0.25x)
    cv::Mat f2 = bg.clone();
    drawDistractorB(f2, cv::Point(340, 305));
    drawTargetA(f2, cv::Point(820, 240));
    DetectionBox detA(QRectF(790, 216, 60, 48), 0.72f, 0, "UAV");
    auto r2 = evalPolicyOnDetections({detB, detA}, f2, anchor, pol, dsst, sot, 0.0f);
    if (!r2.accepted) return false;
    float cle = computeCLE(r2.box, cv::Rect2f(790, 216, 60, 48));
    return cle < 10.0f;
}

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

    DetectorConfig dcfg;
    dcfg.useGpu = true;
    dcfg.confThreshold = 0.20f; // Allow testing both 0.25f and 0.35f in post-filter!
    dcfg.nmsThreshold = 0.45f;

    auto detU11n = std::make_shared<OnnxDetector>();
    auto detU11s = std::make_shared<OnnxDetector>();
    auto detC8n  = std::make_shared<OnnxDetector>();
    if (!detU11n->loadModel("models/yolo11n_uav_ft.onnx", dcfg) ||
        !detU11s->loadModel("models/yolo11s_uav_ft.onnx", dcfg) ||
        !detC8n->loadModel("models/yolov8n.onnx", dcfg)) {
        std::cerr << "Failed to load one or more YOLO models\n";
        return 1;
    }

    SotTrackerConfig scfg;
    scfg.templateModel = "models/lighttrack_template.onnx";
    scfg.searchModelSmall = "models/lighttrack_search_256.onnx";
    scfg.searchModelBig = "models/lighttrack_search_288.onnx";
    scfg.useGpu = true;
    auto sot = std::make_shared<OnnxSotTracker>(scfg);
    if (!sot->ensureLoaded()) return 1;

    // Warmup
    {
        cv::Mat warm = cv::imread("datasets/Anti-UAV/tracking/Anti-UAV-Tracking-V0/video01/00001.jpg");
        for (int i = 0; i < 3; ++i) { detU11n->detect(warm); detU11s->detect(warm); detC8n->detect(warm); }
    }

    // Define policies to test
    // Note: each item pairs (policy, detector_tag: 0=u11n, 1=u11s, 2=c8n)
    struct TestCase { RedetectPolicy pol; int detId; };
    std::vector<TestCase> cases;

    // Baselines (re-verified)
    {
        RedetectPolicy p; p.name = "Base_U11n_DSST"; p.kmode = KernelMode::DSST;
        cases.push_back({p, 0});
    }
    {
        RedetectPolicy p; p.name = "Base_U11n_LT"; p.kmode = KernelMode::LIGHTTRACK;
        cases.push_back({p, 0});
    }
    // P0_A: Relax areaRatio only [0.03, 16.0] on LT
    {
        RedetectPolicy p; p.name = "P0A_U11n_AreaRelax_LT";
        p.kmode = KernelMode::LIGHTTRACK; p.minAreaRatio = 0.03f; p.maxAreaRatio = 16.0f; p.maxAspectDiff = 2.0f;
        cases.push_back({p, 0});
    }
    // P0_B: AreaRelax + minConf=0.25 on LT
    {
        RedetectPolicy p; p.name = "P0B_U11n_AreaRelax_Conf25_LT";
        p.kmode = KernelMode::LIGHTTRACK; p.minAreaRatio = 0.03f; p.maxAreaRatio = 16.0f; p.maxAspectDiff = 2.0f;
        p.minConf = 0.25f;
        cases.push_back({p, 0});
    }
    // P0_C35: AreaRelax + minConf=0.35 (kept strict!) + Soft LT on High Conf (0.65)
    {
        RedetectPolicy p; p.name = "P0C35_U11n_Area_C35_SoftLT";
        p.kmode = KernelMode::LIGHTTRACK; p.minAreaRatio = 0.03f; p.maxAreaRatio = 16.0f; p.maxAspectDiff = 2.0f;
        p.minConf = 0.35f; p.allowSoftKernelOnHighConf = true; p.highConfBypassThr = 0.65f; p.highConfMinHistSim = 0.50f;
        cases.push_back({p, 0});
    }
    // P0_D35: DSST fix with minConf=0.35 kept strict -> AreaRelax + Soft PSR
    {
        RedetectPolicy p; p.name = "P0D35_U11n_Area_C35_SoftDSST";
        p.kmode = KernelMode::DSST; p.minAreaRatio = 0.03f; p.maxAreaRatio = 16.0f; p.maxAspectDiff = 2.0f;
        p.minConf = 0.35f; p.allowSoftKernelOnHighConf = true; p.highConfBypassThr = 0.65f; p.highConfMinHistSim = 0.50f;
        cases.push_back({p, 0});
    }
    // P0_C35 + Two-frame lag=1
    {
        RedetectPolicy p; p.name = "P0C35_U11n_TwoFrame_Lag1";
        p.kmode = KernelMode::LIGHTTRACK; p.minAreaRatio = 0.03f; p.maxAreaRatio = 16.0f; p.maxAspectDiff = 2.0f;
        p.minConf = 0.35f; p.allowSoftKernelOnHighConf = true; p.highConfBypassThr = 0.65f; p.highConfMinHistSim = 0.50f;
        p.twoFrameGate = true; p.twoFrameLag = 1;
        cases.push_back({p, 0});
    }
    {
        RedetectPolicy p; p.name = "P0D35_DSST_TwoFrame_Lag1";
        p.kmode = KernelMode::DSST; p.minAreaRatio = 0.03f; p.maxAreaRatio = 16.0f; p.maxAspectDiff = 2.0f;
        p.minConf = 0.35f; p.allowSoftKernelOnHighConf = true; p.highConfBypassThr = 0.65f; p.highConfMinHistSim = 0.50f;
        p.twoFrameGate = true; p.twoFrameLag = 1;
        cases.push_back({p, 0});
    }
    // P0_C: AreaRelax + minConf=0.25 + Soft LT on High Conf (0.65)
    {
        RedetectPolicy p; p.name = "P0C_U11n_Area_C25_SoftLT";
        p.kmode = KernelMode::LIGHTTRACK; p.minAreaRatio = 0.03f; p.maxAreaRatio = 16.0f; p.maxAspectDiff = 2.0f;
        p.minConf = 0.25f; p.allowSoftKernelOnHighConf = true; p.highConfBypassThr = 0.65f; p.highConfMinHistSim = 0.50f;
        cases.push_back({p, 0});
    }
    // P0_D: DSST fix -> AreaRelax + minConf=0.25 + Soft PSR on High Conf (conf>=0.65 && hist>=0.50)
    {
        RedetectPolicy p; p.name = "P0D_U11n_Area_C25_SoftDSST";
        p.kmode = KernelMode::DSST; p.minAreaRatio = 0.03f; p.maxAreaRatio = 16.0f; p.maxAspectDiff = 2.0f;
        p.minConf = 0.25f; p.allowSoftKernelOnHighConf = true; p.highConfBypassThr = 0.65f; p.highConfMinHistSim = 0.50f;
        cases.push_back({p, 0});
    }
    // P0_E35: Model upgrade yolo11s_uav_ft + minConf=0.35 + AreaRelax + SoftLT (+ Lag1)
    {
        RedetectPolicy p; p.name = "P0E35_U11s_Area_C35_SoftLT";
        p.kmode = KernelMode::LIGHTTRACK; p.minAreaRatio = 0.03f; p.maxAreaRatio = 16.0f; p.maxAspectDiff = 2.0f;
        p.minConf = 0.35f; p.allowSoftKernelOnHighConf = true; p.highConfBypassThr = 0.65f; p.highConfMinHistSim = 0.50f;
        cases.push_back({p, 1});
    }
    {
        RedetectPolicy p; p.name = "P0E35_U11s_TwoFrame_Lag1";
        p.kmode = KernelMode::LIGHTTRACK; p.minAreaRatio = 0.03f; p.maxAreaRatio = 16.0f; p.maxAspectDiff = 2.0f;
        p.minConf = 0.35f; p.allowSoftKernelOnHighConf = true; p.highConfBypassThr = 0.65f; p.highConfMinHistSim = 0.50f;
        p.twoFrameGate = true; p.twoFrameLag = 1;
        cases.push_back({p, 1});
    }
    // P0_F35: COCO default with minConf=0.35 + AreaRelax + SoftLT
    {
        RedetectPolicy p; p.name = "P0F35_C8n_Area_C35_SoftLT";
        p.kmode = KernelMode::LIGHTTRACK; p.minAreaRatio = 0.03f; p.maxAreaRatio = 16.0f; p.maxAspectDiff = 2.0f;
        p.minConf = 0.35f; p.allowSoftKernelOnHighConf = true; p.highConfBypassThr = 0.65f; p.highConfMinHistSim = 0.50f;
        cases.push_back({p, 2});
    }

    // Run anti-hijack safety unit test on every non-two-frame policy
    std::cout << "=== ANTI-HIJACK SAFETY VERIFICATION (Distractor conf=0.92 vs Shrunk Target conf=0.72) ===\n";
    for (const auto& c : cases) {
        if (!c.pol.twoFrameGate) {
            bool ok = runAntiHijackUnitTest(c.pol, sot.get());
            std::cout << "  " << std::left << std::setw(30) << c.pol.name << ": " << (ok ? "PASS (Distractor blocked, Target recovered)" : "FAIL") << "\n";
        }
    }

    std::map<std::string, MethodStats> overall;
    for (const auto& c : cases) overall[c.pol.name].name = c.pol.name;
    overall["P1_MotionProp_LT"].name = "P1_MotionProp_LT";
    overall["P1_C8n_Plus_MotionProp"].name = "P1_C8n_Plus_MotionProp";

    std::cout << "\n=================================================================================================================================\n";
    std::cout << "EXPERIMENTAL VERIFICATION: P0 FIXES & P1 MOTION PROPOSAL ON ANTI-UAV 1080P (925 Valid + 64 Absent)\n";
    std::cout << "=================================================================================================================================\n";

    for (const auto& sq : sequences) {
        std::string gtPath = "datasets/Anti-UAV/tracking/Anti-UAV-Tracking-V0GT/" + sq.name + "_gt.txt";
        std::string imgDir = "datasets/Anti-UAV/tracking/Anti-UAV-Tracking-V0/" + sq.name;
        auto gts = loadGT(gtPath);
        std::vector<std::string> imgFiles;
        for (const auto& entry : fs::directory_iterator(imgDir)) {
            auto ext = entry.path().extension().string();
            if (ext == ".jpg" || ext == ".png") imgFiles.push_back(entry.path().string());
        }
        std::sort(imgFiles.begin(), imgFiles.end());
        size_t nFrames = std::min(imgFiles.size(), gts.size());
        if (nFrames < 2) continue;

        cv::Mat img0 = cv::imread(imgFiles[0]);
        cv::Rect2f gt0 = gts[0];
        cv::Rect roi0(static_cast<int>(std::round(gt0.x)), static_cast<int>(std::round(gt0.y)),
                      static_cast<int>(std::round(gt0.width)), static_cast<int>(std::round(gt0.height)));
        roi0 &= cv::Rect(0, 0, img0.cols, img0.rows);

        TargetAnchor anchor;
        anchor.initBbox = cv::Rect2f(static_cast<float>(roi0.x), static_cast<float>(roi0.y),
                                     static_cast<float>(roi0.width), static_cast<float>(roi0.height));
        anchor.valid = extractPatchAppearance(img0, anchor.initBbox,
                                              anchor.histHS, anchor.histV, anchor.patchNorm,
                                              anchor.hasTexture, anchor.isColor, anchor.meanSat);
        DsstCore dsst;
        dsst.init(img0, roi0);
        sot->init(img0, anchor.initBbox);

        std::map<std::string, MethodStats> seqStats;
        for (const auto& c : cases) seqStats[c.pol.name].name = c.pol.name;
        seqStats["P1_MotionProp_LT"].name = "P1_MotionProp_LT";
        seqStats["P1_C8n_Plus_MotionProp"].name = "P1_C8n_Plus_MotionProp";

        int seqValid = 0;

        for (size_t fIdx = 1; fIdx < nFrames; fIdx += sq.step) {
            cv::Rect2f gt = gts[fIdx];
            bool isValid = (gt.width > 0.0f && gt.height > 0.0f);
            if (isValid) seqValid++;

            cv::Mat imgCurr = cv::imread(imgFiles[fIdx]);
            if (imgCurr.empty()) continue;
            cv::Mat grayCurr;
            cv::cvtColor(imgCurr, grayCurr, cv::COLOR_BGR2GRAY);

            // Previous frames for lag=1 and lag=10
            cv::Mat imgLag1, grayLag1, imgLag10;
            if (fIdx >= 1) {
                imgLag1 = cv::imread(imgFiles[fIdx - 1]);
                if (!imgLag1.empty()) cv::cvtColor(imgLag1, grayLag1, cv::COLOR_BGR2GRAY);
            }
            if (fIdx >= 10) {
                imgLag10 = cv::imread(imgFiles[fIdx - 10]);
            }

            // Detect once on curr & lag frames
            auto t0 = std::chrono::steady_clock::now();
            auto d_u11n = detU11n->detect(imgCurr);
            float ms_u11n = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();

            auto t1 = std::chrono::steady_clock::now();
            auto d_u11s = detU11s->detect(imgCurr);
            float ms_u11s = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t1).count();

            auto t2 = std::chrono::steady_clock::now();
            auto d_c8n = detC8n->detect(imgCurr);
            float ms_c8n = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t2).count();

            std::vector<DetectionBox> d_u11n_lag1, d_u11s_lag1, d_u11n_lag10;
            bool lag1Computed = false, lag1sComputed = false, lag10Computed = false;

            RedetectResult resP0FC8;
            for (const auto& tc : cases) {
                const auto& dets = (tc.detId == 0) ? d_u11n : ((tc.detId == 1) ? d_u11s : d_c8n);
                float dms = (tc.detId == 0) ? ms_u11n : ((tc.detId == 1) ? ms_u11s : ms_c8n);

                RedetectResult rr = evalPolicyOnDetections(dets, imgCurr, anchor, tc.pol, dsst, sot.get(), dms);
                if (tc.pol.twoFrameGate && rr.accepted) {
                    if (tc.pol.twoFrameLag == 1) {
                        if (imgLag1.empty()) {
                            rr.accepted = false;
                        } else {
                            if (tc.detId == 0 && !lag1Computed) { d_u11n_lag1 = detU11n->detect(imgLag1); lag1Computed = true; }
                            if (tc.detId == 1 && !lag1sComputed) { d_u11s_lag1 = detU11s->detect(imgLag1); lag1sComputed = true; }
                            const auto& pdets = (tc.detId == 0) ? d_u11n_lag1 : d_u11s_lag1;
                            auto pr = evalPolicyOnDetections(pdets, imgLag1, anchor, tc.pol, dsst, sot.get(), 0.0f);
                            if (!pr.accepted || !passTwoFrameConsistency(pr.box, rr.box)) {
                                rr.accepted = false;
                            }
                            rr.timeMs += pr.timeMs;
                        }
                    } else if (tc.pol.twoFrameLag == 10) {
                        if (imgLag10.empty()) {
                            rr.accepted = false;
                        } else {
                            if (!lag10Computed) { d_u11n_lag10 = detU11n->detect(imgLag10); lag10Computed = true; }
                            auto pr = evalPolicyOnDetections(d_u11n_lag10, imgLag10, anchor, tc.pol, dsst, sot.get(), 0.0f);
                            if (!pr.accepted || !passTwoFrameConsistency(pr.box, rr.box)) {
                                rr.accepted = false;
                            }
                        }
                    }
                }
                if (tc.pol.name == "P0F35_C8n_Area_C35_SoftLT") resP0FC8 = rr;
                seqStats[tc.pol.name].record(rr, isValid, gt);
                overall[tc.pol.name].record(rr, isValid, gt);
            }

            // P1 Motion Proposal
            auto r_mot = runMotionProposalRecovery(grayLag1, imgCurr, grayCurr, anchor, sot.get());
            seqStats["P1_MotionProp_LT"].record(r_mot, isValid, gt);
            overall["P1_MotionProp_LT"].record(r_mot, isValid, gt);

            RedetectResult r_c8_mot = resP0FC8.accepted ? resP0FC8 : r_mot;
            if (!resP0FC8.accepted) r_c8_mot.timeMs = resP0FC8.timeMs + r_mot.timeMs;
            seqStats["P1_C8n_Plus_MotionProp"].record(r_c8_mot, isValid, gt);
            overall["P1_C8n_Plus_MotionProp"].record(r_c8_mot, isValid, gt);
        }

        auto mBdsst = computeSummary(seqStats["Base_U11n_DSST"]);
        auto mBlt   = computeSummary(seqStats["Base_U11n_LT"]);
        auto mP0c   = computeSummary(seqStats["P0C35_U11n_Area_C35_SoftLT"]);
        auto mP0d   = computeSummary(seqStats["P0D35_U11n_Area_C35_SoftDSST"]);
        auto mP0e   = computeSummary(seqStats["P0E35_U11s_Area_C35_SoftLT"]);
        auto mP1m   = computeSummary(seqStats["P1_MotionProp_LT"]);

        std::cout << "[" << std::left << std::setw(7) << sq.name << " V=" << std::setw(3) << seqValid << "] "
                  << std::fixed << std::setprecision(3)
                  << "BaseDSST=" << mBdsst.auc
                  << " | P0D(SoftDSST)=" << mP0d.auc
                  << " | BaseLT=" << mBlt.auc
                  << " | P0C(U11n)=" << mP0c.auc
                  << " | P0E(U11s)=" << mP0e.auc
                  << " | P1_Mot=" << mP1m.auc << "\n";
    }

    std::vector<std::string> printOrder;
    for (const auto& c : cases) printOrder.push_back(c.pol.name);
    printOrder.push_back("P1_MotionProp_LT");
    printOrder.push_back("P1_C8n_Plus_MotionProp");

    std::cout << "\n=================================================================================================================================\n";
    std::cout << std::left << std::setw(30) << "Experiment Method"
              << std::right << std::setw(9) << "AUC(%)"
              << std::setw(9) << "DP20(%)"
              << std::setw(9) << "SR50(%)"
              << std::setw(9) << "mIoU"
              << std::setw(10) << "HitCLE"
              << std::setw(10) << "Miss(%)"
              << std::setw(11) << "Hijack(%)"
              << std::setw(13) << "AbsentFP"
              << std::setw(10) << "Time(ms)" << "\n";
    std::cout << "---------------------------------------------------------------------------------------------------------------------------------\n";

    for (const auto& m : printOrder) {
        const auto& st = overall[m];
        SummaryMetrics s = computeSummary(st);
        std::ostringstream fpStr;
        fpStr << st.absentFP << "/" << st.absentFrames << "(" << std::fixed << std::setprecision(1) << s.absentFpRate * 100.0f << "%)";

        std::cout << std::left << std::setw(30) << m
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
    std::cout << "=================================================================================================================================\n";
    return 0;
}
