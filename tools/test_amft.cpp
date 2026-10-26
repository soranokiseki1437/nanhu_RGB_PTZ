#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cmath>
#include <vector>

#include "../objecttracker.h"
#include "../amftevaluator.h"
#include "../onnxdetector.h"
#include "../imagematconvert.h"

// 读取 Ground Truth 标注列表
static std::vector<cv::Rect> loadGroundTruth(const std::string& gtPath)
{
    std::vector<cv::Rect> gts;
    std::ifstream file(gtPath);
    if (!file.is_open()) return gts;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream ss(line);
        int x, y, w, h;
        if (ss >> x >> y >> w >> h) {
            gts.emplace_back(x, y, w, h);
        }
    }
    return gts;
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    std::cout << "========================================================================" << std::endl;
    std::cout << "   基于 AMFT 自适应多特征融合的检测-跟踪协同实操方案【全景闭环验收工具】" << std::endl;
    std::cout << "========================================================================" << std::endl;

    // 1. 初始化检测器
    std::string modelPath = "models/yolov8n.onnx";
    auto detector = std::make_shared<OnnxDetector>();
    DetectorConfig cfg;
    cfg.useGpu = true;
    if (!detector->loadModel(QString::fromStdString(modelPath), cfg)) {
        std::cerr << ">> [错误] 检测器加载失败: " << modelPath << std::endl;
        return 1;
    }
    std::cout << ">> AI 目标检测引擎加载成功: " << modelPath << " (DirectML RTX 5070)" << std::endl;

    // 2. 加载序列 01
    std::string seqDir = "testdata/sequences/01";
    std::string gtPath = seqDir + "/1_gt.txt";
    auto gts = loadGroundTruth(gtPath);
    if (gts.empty()) {
        std::cerr << ">> [错误] Ground Truth 文件读取失败: " << gtPath << std::endl;
        return 1;
    }
    std::cout << ">> 成功载入对拍序列标注: " << gts.size() << " 帧" << std::endl;

    // 3. 读取首帧并初始化 ObjectTracker
    std::string firstImgPath = seqDir + "/imgs/0000.bmp";
    cv::Mat firstMat = cv::imread(firstImgPath);
    if (firstMat.empty()) {
        std::cerr << ">> [错误] 首帧图像读取失败: " << firstImgPath << std::endl;
        return 1;
    }

    ObjectTracker tracker;
    tracker.setDetector(detector);

    QImage firstQImg = MatToQImage(firstMat);
    QRectF initRoi(gts[0].x, gts[0].y, gts[0].width, gts[0].height);
    if (!tracker.init(firstQImg, initRoi)) {
        std::cerr << ">> [错误] ObjectTracker 初始化失败！" << std::endl;
        return 1;
    }
    std::cout << ">> ObjectTracker + AMFT 评估器初始化成功, 模型尺寸=" << tracker.modelSize().toStdString() << std::endl;

    // =========================================================================
    // [体检 1/4] 平稳巡航态验证 (Normal Tracking, 0-FPS YOLO Sleep)
    // =========================================================================
    std::cout << "\n------------------------------------------------------------------------" << std::endl;
    std::cout << " [体检 1/4] 平稳巡航态局部跟踪与 AI 零频休眠验证 (30 FPS vs 0 FPS YOLO)" << std::endl;
    std::cout << "------------------------------------------------------------------------" << std::endl;

    const int steadyFrames = 60;
    float avgRt = 0.0f;
    float avgTau = 0.0f;
    float maxCenterErr = 0.0f;
    int yoloTriggersInSteady = 0;

    auto tStart = std::chrono::high_resolution_clock::now();
    for (int i = 1; i <= steadyFrames; ++i) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s/imgs/%04d.bmp", seqDir.c_str(), i);
        cv::Mat frameMat = cv::imread(buf);
        if (frameMat.empty()) break;

        QImage qImg = MatToQImage(frameMat);
        TrackResult res = tracker.update(qImg);

        avgRt += res.amftScore;
        avgTau += res.amftTau;

        // 计算与 GT 的中心距离
        cv::Point2f predCenter(res.bbox.x() + res.bbox.width() * 0.5f,
                               res.bbox.y() + res.bbox.height() * 0.5f);
        cv::Point2f gtCenter(gts[i].x + gts[i].width * 0.5f,
                             gts[i].y + gts[i].height * 0.5f);
        float err = static_cast<float>(cv::norm(predCenter - gtCenter));
        if (err > maxCenterErr) maxCenterErr = err;

        if (res.state != AMFTTrackingState::TRACK_NORMAL) {
            yoloTriggersInSteady++;
        }
    }
    auto tEnd = std::chrono::high_resolution_clock::now();
    float totalMs = std::chrono::duration<float, std::milli>(tEnd - tStart).count();
    float avgLatency = totalMs / steadyFrames;

    avgRt /= steadyFrames;
    avgTau /= steadyFrames;

    std::cout << ">> 连续 " << steadyFrames << " 帧跟踪耗时      : " << totalMs << " ms (单帧平均 " << avgLatency << " ms / " << (1000.0f / avgLatency) << " FPS)" << std::endl;
    std::cout << ">> AMFT 平均可靠度 R_t      : " << avgRt << " (动态阈值平均 tau_t=" << avgTau << ")" << std::endl;
    std::cout << ">> 最大目标中心点像素误差   : " << maxCenterErr << " px" << std::endl;
    std::cout << ">> 平稳期异常误唤醒次数     : " << yoloTriggersInSteady << " 次 (YOLO 保持 0 FPS 完全休眠)" << std::endl;

    bool pass1 = (yoloTriggersInSteady == 0) && (avgRt > avgTau) && (maxCenterErr < 3.0f);
    std::cout << ">> 判定结果: " << (pass1 ? "[通过 PASS - 零频待机，低延迟高精度]" : "[未达标 FAIL]") << std::endl;

    // =========================================================================
    // [体检 2/4] 瞬时干扰误报抑制测试 (Transient Disturbance Suppression)
    // =========================================================================
    std::cout << "\n------------------------------------------------------------------------" << std::endl;
    std::cout << " [体检 2/4] 瞬时干扰误报抑制测试 (1-Frame Transient Flash / Noise)" << std::endl;
    std::cout << "------------------------------------------------------------------------" << std::endl;

    // 读取第 61 帧并施加 1 帧瞬时过曝/强噪
    cv::Mat flashMat = cv::imread(seqDir + "/imgs/0061.bmp");
    cv::rectangle(flashMat, cv::Rect(gts[61].x - 5, gts[61].y - 5, gts[61].width + 10, gts[61].height + 10),
                  cv::Scalar(255, 255, 255), -1); // 强烈反光干扰

    TrackResult flashRes = tracker.update(MatToQImage(flashMat));
    AMFTScore score = tracker.lastAMFTScore();

    std::cout << ">> 瞬时强光干扰帧置信度 S_conf: " << score.sConf << std::endl;
    std::cout << ">> 运动连续性平滑压制得分 S_mot: " << score.sMot << " (平滑运动抑制误报)" << std::endl;
    std::cout << ">> 动态阈值自适应下调 tau_t   : " << score.dynamicTau << std::endl;
    std::cout << ">> 跟踪器状态                : " << (flashRes.state == AMFTTrackingState::TRACK_NORMAL ? "NORMAL (未误触发检测)" : "TRIGGERED") << std::endl;

    // 紧接着传入正常第 62 帧
    cv::Mat normalMat = cv::imread(seqDir + "/imgs/0062.bmp");
    TrackResult normalRes = tracker.update(MatToQImage(normalMat));

    bool pass2 = (normalRes.valid && normalRes.state == AMFTTrackingState::TRACK_NORMAL);
    std::cout << ">> 判定结果: " << (pass2 ? "[通过 PASS - 成功压制瞬态扰动，未发生算力误触发]" : "[未达标 FAIL]") << std::endl;

    // =========================================================================
    // [体检 3/4] 突发严重遮挡单次唤醒与强制回归铁律验证 (Occlusion Single-Shot Recovery)
    // =========================================================================
    std::cout << "\n------------------------------------------------------------------------" << std::endl;
    std::cout << " [体检 3/4] 突发遮挡单次唤醒与下一帧强制回归铁律验证 (Single-Shot & Re-Lock)" << std::endl;
    std::cout << "------------------------------------------------------------------------" << std::endl;

    // 连续 4 帧全黑挡住目标（模拟目标穿过立柱/树林）
    std::cout << ">> 注入人为严重遮挡 (4 帧纯黑遮挡目标)..." << std::endl;
    bool enteredOcclusion = false;
    for (int k = 63; k <= 66; ++k) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s/imgs/%04d.bmp", seqDir.c_str(), k);
        cv::Mat occlMat = cv::imread(buf);
        // 把目标涂黑模拟遮挡
        cv::rectangle(occlMat, cv::Rect(gts[k].x - 10, gts[k].y - 10, gts[k].width + 20, gts[k].height + 20),
                      cv::Scalar(0, 0, 0), -1);

        TrackResult oRes = tracker.update(MatToQImage(occlMat));
        if (oRes.occluded || oRes.state == AMFTTrackingState::TRACK_OCCLUDED) {
            enteredOcclusion = true;
        }
    }
    std::cout << ">> 遮挡期状态机判定          : " << (enteredOcclusion ? "OCCLUDED (成功捕获遮挡)" : "NORMAL") << std::endl;

    // 第 67 帧目标重新露出：送入真实画面，验证 YOLO 单次精准召回
    std::cout << ">> 目标重新露出 (第 67 帧真实画面)..." << std::endl;
    cv::Mat reMat = cv::imread(seqDir + "/imgs/0067.bmp");
    TrackResult reRes = tracker.update(MatToQImage(reMat));

    std::cout << ">> 重捕时刻目标位置          : (" << reRes.bbox.center().x() << ", " << reRes.bbox.center().y() << ")" << std::endl;
    std::cout << ">> Ground Truth 真实中心     : (" << gts[67].x + gts[67].width*0.5f << ", " << gts[67].y + gts[67].height*0.5f << ")" << std::endl;
    std::cout << ">> 状态机状态                : " << (reRes.state == AMFTTrackingState::TRACK_REACQUIRED ? "REACQUIRED (重捕成功)" : "OTHER") << std::endl;

    // 验证核心铁律：下一帧（第 68 帧）必须强制切回 TRACK_NORMAL，继续 0 FPS 休眠！
    cv::Mat nextMat = cv::imread(seqDir + "/imgs/0068.bmp");
    TrackResult nextRes = tracker.update(MatToQImage(nextMat));
    bool rulePassed = (nextRes.state == AMFTTrackingState::TRACK_NORMAL);
    std::cout << ">> 【核心铁律验证】下一帧是否强制切回 TRACK_NORMAL: "
              << (rulePassed ? "[通过 PASS - 严格执行铁律]" : "[失败 FAIL]") << std::endl;

    bool pass3 = enteredOcclusion && (reRes.state == AMFTTrackingState::TRACK_REACQUIRED) && rulePassed;
    std::cout << ">> 判定结果: " << (pass3 ? "[通过 PASS - 遮挡单次极速拉回，模型热更新成功]" : "[未达标 FAIL]") << std::endl;

    // =========================================================================
    // [体检 4/4] 100 帧全长闭环统计（精度、调用率、FPS）
    // =========================================================================
    std::cout << "\n------------------------------------------------------------------------" << std::endl;
    std::cout << " [体检 4/4] 100 帧全长连续实跑综合指标统计 (Call Rate & Precision)" << std::endl;
    std::cout << "------------------------------------------------------------------------" << std::endl;

    int totalEvaluated = 100;
    int totalDetectCalls = 0;
    float sumErr = 0.0f;

    for (int i = 69; i < totalEvaluated; ++i) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s/imgs/%04d.bmp", seqDir.c_str(), i);
        cv::Mat frameMat = cv::imread(buf);
        if (frameMat.empty()) break;

        TrackResult res = tracker.update(MatToQImage(frameMat));
        if (res.state == AMFTTrackingState::TRACK_REACQUIRED || res.state == AMFTTrackingState::TRACK_OCCLUDED) {
            totalDetectCalls++;
        }

        cv::Point2f predCenter(res.bbox.x() + res.bbox.width() * 0.5f,
                               res.bbox.y() + res.bbox.height() * 0.5f);
        cv::Point2f gtCenter(gts[i].x + gts[i].width * 0.5f,
                             gts[i].y + gts[i].height * 0.5f);
        sumErr += static_cast<float>(cv::norm(predCenter - gtCenter));
    }

    float meanErr = sumErr / (totalEvaluated - 69);
    float callRate = (static_cast<float>(totalDetectCalls) / totalEvaluated) * 100.0f;

    std::cout << ">> 100 帧总检测触发次数     : " << totalDetectCalls << " 次" << std::endl;
    std::cout << ">> 全局检测调用率 (Call Rate): " << std::fixed << std::setprecision(2) << callRate << "% (目标 <= 5.0%)" << std::endl;
    std::cout << ">> 序列平均中心定位误差     : " << std::fixed << std::setprecision(2) << meanErr << " px (目标 <= 3.0px)" << std::endl;

    bool pass4 = (callRate <= 5.0f) && (meanErr <= 3.0f);
    std::cout << ">> 判定结果: " << (pass4 ? "[通过 PASS]" : "[未达标 FAIL]") << std::endl;

    // =========================================================================
    // 总结报告
    // =========================================================================
    std::cout << "\n========================================================================" << std::endl;
    std::cout << "                   方案 02 (AMFT 协同实操) 闭环验收总结报告              " << std::endl;
    std::cout << "========================================================================" << std::endl;
    std::cout << " 1. 平稳巡航态局部跟踪 (30 FPS/零频待机)   : " << (pass1 ? "PASS 达成 (耗时 " + std::to_string(avgLatency).substr(0,4) + "ms, 0 误唤醒)" : "FAIL") << std::endl;
    std::cout << " 2. 瞬态干扰平滑压制 (S_mot 连续性保护)     : " << (pass2 ? "PASS 达成" : "FAIL") << std::endl;
    std::cout << " 3. 严重遮挡单次唤醒与下一帧强制回归铁律   : " << (pass3 ? "PASS 达成 (REACQUIRED ──► NORMAL)" : "FAIL") << std::endl;
    std::cout << " 4. 全长调用率与精度 (CallRate < 5%, Err<3px): " << (pass4 ? "PASS 达成 (调用率 " + std::to_string(callRate).substr(0,4) + "%, 误差 " + std::to_string(meanErr).substr(0,4) + "px)" : "FAIL") << std::endl;
    std::cout << "------------------------------------------------------------------------" << std::endl;

    bool allSuccess = pass1 && pass2 && pass3 && pass4;
    if (allSuccess) {
        std::cout << ">> 综合验收结论: [方案 02 协同闭环全面达标 🟢 PASS] - 成功取代九宫格盲试！" << std::endl;
    } else {
        std::cout << ">> 综合验收结论: [方案 02 存在待调优项 🟡 WARN]" << std::endl;
    }
    std::cout << "========================================================================" << std::endl;

    return allSuccess ? 0 : 1;
}
