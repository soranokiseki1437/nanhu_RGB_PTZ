#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

#include "../ISotTracker.h"
#include "../onnxsottracker.h"
#include "../amftevaluator.h"

// 进程内存采样 (KB)
static void sampleMemoryKB(size_t& workingSetKB, size_t& privateUsageKB)
{
    workingSetKB = 0;
    privateUsageKB = 0;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        workingSetKB = pmc.WorkingSetSize / 1024;
        privateUsageKB = pmc.PrivateUsage / 1024;
    }
#endif
}

// 评测指标计算辅助函数
static float computeIoU(const cv::Rect2f& b1, const cv::Rect2f& b2)
{
    float x1 = std::max(b1.x, b2.x);
    float y1 = std::max(b1.y, b2.y);
    float x2 = std::min(b1.x + b1.width, b2.x + b2.width);
    float y2 = std::min(b1.y + b1.height, b2.y + b2.height);

    float interW = std::max(0.0f, x2 - x1);
    float interH = std::max(0.0f, y2 - y1);
    float interArea = interW * interH;

    float area1 = std::max(0.0f, b1.width) * std::max(0.0f, b1.height);
    float area2 = std::max(0.0f, b2.width) * std::max(0.0f, b2.height);
    float unionArea = area1 + area2 - interArea;

    if (unionArea <= 0.0f) return 0.0f;
    return interArea / unionArea;
}

static float computeCLE(const cv::Rect2f& b1, const cv::Rect2f& b2)
{
    float c1x = b1.x + b1.width * 0.5f;
    float c1y = b1.y + b1.height * 0.5f;
    float c2x = b2.x + b2.width * 0.5f;
    float c2y = b2.y + b2.height * 0.5f;
    return std::sqrt((c1x - c2x) * (c1x - c2x) + (c1y - c2y) * (c1y - c2y));
}

static float computeNormCLE(const cv::Rect2f& b1, const cv::Rect2f& b2)
{
    float c1x = b1.x + b1.width * 0.5f;
    float c1y = b1.y + b1.height * 0.5f;
    float c2x = b2.x + b2.width * 0.5f;
    float c2y = b2.y + b2.height * 0.5f;
    float w_gt = std::max(1.0f, b2.width);
    float h_gt = std::max(1.0f, b2.height);
    float dx = (c1x - c2x) / w_gt;
    float dy = (c1y - c2y) / h_gt;
    return std::sqrt(dx * dx + dy * dy);
}

static double calculatePercentile(std::vector<float>& vals, double p)
{
    if (vals.empty()) return 0.0;
    std::sort(vals.begin(), vals.end());
    double idx = (p / 100.0) * (vals.size() - 1);
    int lower = static_cast<int>(std::floor(idx));
    int upper = static_cast<int>(std::ceil(idx));
    double weight = idx - lower;
    return vals[lower] * (1.0 - weight) + vals[upper] * weight;
}

struct EvalMetrics {
    double auc = 0.0;
    double dp20 = 0.0;
    double meanCle = 0.0;
    double lostRate = 0.0;
    double sr50 = 0.0;
    double sr75 = 0.0;
    double pNorm = 0.0;
};

static EvalMetrics evaluateArrays(const std::vector<float>& ious,
                                  const std::vector<float>& cles,
                                  const std::vector<float>& normCles)
{
    EvalMetrics m;
    if (ious.empty()) {
        m.lostRate = 1.0;
        return m;
    }

    size_t N = ious.size();

    // 1. AUC (21 bins: linspace(0, 1, 21))
    double succSum = 0.0;
    for (int i = 0; i <= 20; ++i) {
        float th = static_cast<float>(i) / 20.0f;
        size_t passCount = 0;
        for (float v : ious) {
            if (v >= th) passCount++;
        }
        succSum += static_cast<double>(passCount) / N;
    }
    m.auc = succSum / 21.0;

    // 2. DP@20 (threshold <= 20.0px)
    size_t dp20Count = 0;
    double cleSum = 0.0;
    for (float v : cles) {
        cleSum += v;
        if (v <= 20.0f) dp20Count++;
    }
    m.dp20 = static_cast<double>(dp20Count) / N;
    m.meanCle = cleSum / N;

    // 3. P_norm (51 bins in [0, 0.5])
    double normSum = 0.0;
    for (int i = 0; i <= 50; ++i) {
        float th = static_cast<float>(i) * 0.5f / 50.0f;
        size_t passCount = 0;
        for (float v : normCles) {
            if (v <= th) passCount++;
        }
        normSum += static_cast<double>(passCount) / N;
    }
    m.pNorm = normSum / 51.0;

    // 4. Robustness
    size_t lostCount = 0;
    size_t sr50Count = 0;
    size_t sr75Count = 0;
    for (float v : ious) {
        if (v == 0.0f) lostCount++;
        if (v >= 0.50f) sr50Count++;
        if (v >= 0.75f) sr75Count++;
    }
    m.lostRate = static_cast<double>(lostCount) / N;
    m.sr50 = static_cast<double>(sr50Count) / N;
    m.sr75 = static_cast<double>(sr75Count) / N;

    return m;
}

struct SequenceData {
    QString name;
    QStringList imgPaths;
    std::vector<cv::Rect2f> gts;
};

static std::vector<SequenceData> loadSequences(const QString& dataRoot, const QStringList& filterSeqs)
{
    std::vector<SequenceData> seqs;
    QDir videoBase(dataRoot + "/Anti-UAV-Tracking-V0");
    QDir gtBase(dataRoot + "/Anti-UAV-Tracking-V0GT");

    QStringList dirs = videoBase.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& d : dirs) {
        if (!filterSeqs.isEmpty() && !filterSeqs.contains(d)) {
            continue;
        }

        QString gtFile = gtBase.filePath(d + "_gt.txt");
        if (!QFile::exists(gtFile)) {
            continue;
        }

        std::vector<cv::Rect2f> gts;
        QFile gf(gtFile);
        if (gf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream ts(&gf);
            while (!ts.atEnd()) {
                QString line = ts.readLine().trimmed();
                if (line.isEmpty()) continue;
                QStringList parts = line.split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts);
                if (parts.size() >= 4) {
                    gts.push_back(cv::Rect2f(parts[0].toFloat(), parts[1].toFloat(), parts[2].toFloat(), parts[3].toFloat()));
                }
            }
        }

        QDir imgDir(videoBase.filePath(d));
        QStringList imgs = imgDir.entryList(QStringList() << "*.jpg" << "*.png", QDir::Files, QDir::Name);
        if (imgs.isEmpty()) continue;

        QStringList fullImgs;
        for (const QString& im : imgs) {
            fullImgs.push_back(imgDir.filePath(im));
        }

        SequenceData sd;
        sd.name = d;
        sd.imgPaths = fullImgs;
        sd.gts = gts;
        seqs.push_back(sd);
    }
    return seqs;
}

// -------------------------------------------------------------
// 回放模式结构
// -------------------------------------------------------------
struct ReplayRow {
    QString seq;
    int frame = 0;
    double bx = 0, by = 0, bw = 0, bh = 0;
    int inst = 0;
    int cropSz = 0, cropXmin = 0, cropYmin = 0;
    double ax = 0, ay = 0, aw = 0, ah = 0;
    double clsMax = 0;
    double boxX = 0, boxY = 0, boxW = 0, boxH = 0;
    double iou = 0, cle = 0;
    int gtValid = 0;
};

static void runReplayMode(OnnxSotTracker& tracker, const QString& csvPath, const QString& dataRoot, const QString& outDir)
{
    std::cout << "\n======================================================\n";
    std::cout << " SOT OPEN-LOOP REPLAY VALIDATION (L3 Check)\n";
    std::cout << " Input CSV: " << csvPath.toStdString() << "\n";
    std::cout << "======================================================\n";

    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::cerr << "[-] Failed to open replay CSV: " << csvPath.toStdString() << "\n";
        return;
    }

    QTextStream ts(&file);
    QString header = ts.readLine(); // skip header

    // 读取所有行并按序列组织
    std::vector<ReplayRow> rows;
    while (!ts.atEnd()) {
        QString line = ts.readLine().trimmed();
        if (line.isEmpty()) continue;
        QStringList cols = line.split(',');
        if (cols.size() < 21) continue;

        ReplayRow r;
        r.seq = cols[0];
        r.frame = cols[1].toInt();
        r.bx = cols[2].toDouble();
        r.by = cols[3].toDouble();
        r.bw = cols[4].toDouble();
        r.bh = cols[5].toDouble();
        r.inst = cols[6].toInt();
        r.cropSz = cols[7].toInt();
        r.cropXmin = cols[8].toInt();
        r.cropYmin = cols[9].toInt();
        r.ax = cols[10].toDouble();
        r.ay = cols[11].toDouble();
        r.aw = cols[12].toDouble();
        r.ah = cols[13].toDouble();
        r.clsMax = cols[14].toDouble();
        r.boxX = cols[15].toDouble();
        r.boxY = cols[16].toDouble();
        r.boxW = cols[17].toDouble();
        r.boxH = cols[18].toDouble();
        r.iou = cols[19].toDouble();
        r.cle = cols[20].toDouble();
        r.gtValid = cols[21].toInt();
        rows.push_back(r);
    }

    std::cout << "[*] Loaded " << rows.size() << " frames from CSV.\n";

    // 加载序列图像映射
    auto seqDataList = loadSequences(dataRoot, QStringList());
    std::map<QString, SequenceData> seqMap;
    for (const auto& sd : seqDataList) {
        seqMap[sd.name] = sd;
    }

    size_t totalTrackFrames = 0;
    size_t instMatchCount = 0;
    size_t cropMatchCount = 0;
    size_t cropWithin1Count = 0;

    double maxErrAxAy = 0.0;
    double maxErrAwAh = 0.0;
    double maxErrCls = 0.0;
    double maxErrBox = 0.0;

    size_t failL3_3 = 0;
    size_t failL3_4 = 0;
    size_t failL3_5 = 0;

    QJsonArray topViolations;

    for (const auto& r : rows) {
        if (seqMap.find(r.seq) == seqMap.end()) continue;
        const auto& sd = seqMap[r.seq];
        if (r.frame >= sd.imgPaths.size()) continue;

        cv::Mat bgr = cv::imread(sd.imgPaths[r.frame].toStdString());
        if (bgr.empty()) continue;

        if (r.frame == 0) {
            cv::Rect2f initBox(static_cast<float>(r.boxX), static_cast<float>(r.boxY),
                               static_cast<float>(r.boxW), static_cast<float>(r.boxH));
            tracker.init(bgr, initBox);
            continue;
        }

        // 开环置位：将内部跟踪状态设定为 Python 本帧 track() 调用前的值
        tracker.setState(cv::Point2f(static_cast<float>(r.bx), static_cast<float>(r.by)),
                         cv::Size2f(static_cast<float>(r.bw), static_cast<float>(r.bh)));

        SotResult res = tracker.track(bgr);
        totalTrackFrames++;

        // L3-1: 检查 instance_size
        if (res.dbg.instanceSize == r.inst) {
            instMatchCount++;
        }

        // L3-2: 裁切参数检查
        bool exactCrop = (res.dbg.cropSz == r.cropSz &&
                          res.dbg.cropXmin == r.cropXmin &&
                          res.dbg.cropYmin == r.cropYmin);
        if (exactCrop) {
            cropMatchCount++;
        }
        bool cropWithin1 = (std::abs(res.dbg.cropSz - r.cropSz) <= 1 &&
                            std::abs(res.dbg.cropXmin - r.cropXmin) <= 1 &&
                            std::abs(res.dbg.cropYmin - r.cropYmin) <= 1);
        if (cropWithin1) {
            cropWithin1Count++;
        }

        if (totalTrackFrames % 1000 == 0) {
            std::cout << "[*] Replayed " << totalTrackFrames << " frames...\n";
        }

        // 仅在裁切参数完全一致的帧上做连续数值精度对齐 (L3-3 ~ L3-5)
        if (exactCrop) {
            double errX = std::abs(res.center.x - r.ax);
            double errY = std::abs(res.center.y - r.ay);
            double errPos = std::max(errX, errY);
            if (errPos > maxErrAxAy) maxErrAxAy = errPos;

            double errW = std::abs(res.size.width - r.aw);
            double errH = std::abs(res.size.height - r.ah);
            double errSz = std::max(errW, errH);
            if (errSz > maxErrAwAh) maxErrAwAh = errSz;

            double errCls = std::abs(res.dbg.clsMax - r.clsMax);
            if (errCls > maxErrCls) maxErrCls = errCls;

            double errBx = std::abs(res.bbox.x - r.boxX);
            double errBy = std::abs(res.bbox.y - r.boxY);
            double errBw = std::abs(res.bbox.width - r.boxW);
            double errBh = std::abs(res.bbox.height - r.boxH);
            double errBox = std::max({errBx, errBy, errBw, errBh});
            if (errBox > maxErrBox) maxErrBox = errBox;

            // 门限检查 (DML 门限: pos/sz <= 0.5px, cls <= 1e-3)
            double limitPos = tracker.isUsingGpu() ? 0.5 : 0.05;
            double limitSz = tracker.isUsingGpu() ? 0.5 : 0.05;
            double limitCls = tracker.isUsingGpu() ? 1e-3 : 1e-4;

            if (errPos > limitPos) failL3_3++;
            if (errSz > limitSz) failL3_4++;
            if (errCls > limitCls) failL3_5++;

            if (errPos > limitPos || errSz > limitSz || errCls > limitCls) {
                if (topViolations.size() < 20) {
                    QJsonObject viol;
                    viol["seq"] = r.seq;
                    viol["frame"] = r.frame;
                    viol["err_pos"] = errPos;
                    viol["err_sz"] = errSz;
                    viol["err_cls"] = errCls;
                    topViolations.append(viol);
                }
            }
        }
    }

    double instMatchRate = totalTrackFrames > 0 ? (double)instMatchCount / totalTrackFrames * 100.0 : 0.0;
    double cropMatchRate = totalTrackFrames > 0 ? (double)cropMatchCount / totalTrackFrames * 100.0 : 0.0;
    double cropWithin1Rate = totalTrackFrames > 0 ? (double)cropWithin1Count / totalTrackFrames * 100.0 : 0.0;

    std::cout << "\n------------------------------------------------------\n";
    std::cout << " REPLAY SCORECARD (L3-1 ~ L3-5)\n";
    std::cout << "------------------------------------------------------\n";
    std::cout << " Total Track Frames    : " << totalTrackFrames << "\n";
    std::cout << " L3-1 Inst Match Rate  : " << std::fixed << std::setprecision(2) << instMatchRate << "%\n";
    std::cout << " L3-2 Crop Exact Match : " << cropMatchRate << "% (Within 1px: " << cropWithin1Rate << "%)\n";
    std::cout << " L3-3 Max Err (ax, ay) : " << std::scientific << std::setprecision(4) << maxErrAxAy << " px (Fails: " << failL3_3 << ")\n";
    std::cout << " L3-4 Max Err (aw, ah) : " << maxErrAwAh << " px (Fails: " << failL3_4 << ")\n";
    std::cout << " L3-5 Max Err (cls_max): " << maxErrCls << " (Fails: " << failL3_5 << ")\n";
    std::cout << " Max Err (box)         : " << maxErrBox << " px\n";
    std::cout << "------------------------------------------------------\n";

    QJsonObject report;
    report["total_frames"] = static_cast<qint64>(totalTrackFrames);
    report["inst_match_rate"] = instMatchRate;
    report["crop_exact_match_rate"] = cropMatchRate;
    report["crop_within_1px_rate"] = cropWithin1Rate;
    report["max_err_pos"] = maxErrAxAy;
    report["max_err_sz"] = maxErrAwAh;
    report["max_err_cls"] = maxErrCls;
    report["max_err_box"] = maxErrBox;
    report["fails_l3_3"] = static_cast<qint64>(failL3_3);
    report["fails_l3_4"] = static_cast<qint64>(failL3_4);
    report["fails_l3_5"] = static_cast<qint64>(failL3_5);
    report["backend"] = tracker.isUsingGpu() ? "DirectML (GPU)" : "CPU";
    report["top_violations"] = topViolations;

    QDir().mkpath(outDir);
    QString repPath = outDir + "/replay_report.json";
    QFile rf(repPath);
    if (rf.open(QIODevice::WriteOnly)) {
        rf.write(QJsonDocument(report).toJson());
        std::cout << "[*] Saved replay report to " << repPath.toStdString() << "\n";
    }
}

// -------------------------------------------------------------
// 评测模式主函数
// -------------------------------------------------------------
int main(int argc, char* argv[])
{
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);
    QCoreApplication app(argc, argv);

    // 解析参数
    bool useGpu = true;
    bool strictGpu = false;
    int deviceId = 0;
    QString dataRoot = "datasets/Anti-UAV/tracking";
    QString outDir = "results/lighttrack/cpp_dml";
    QString replayCsv = "";
    QString filterSeqStr = "";
    bool runAmft = false;

    QString tmplModel = "models/lighttrack_template.onnx";
    QString s256Model = "models/lighttrack_search_256.onnx";
    QString s288Model = "models/lighttrack_search_288.onnx";

    for (int i = 1; i < argc; ++i) {
        QString arg = argv[i];
        if (arg == "--cpu") {
            useGpu = false;
        } else if (arg == "--gpu") {
            useGpu = true;
        } else if (arg == "--strict-gpu") {
            strictGpu = true;
        } else if (arg == "--device" && i + 1 < argc) {
            deviceId = QString(argv[++i]).toInt();
        } else if (arg == "--data-root" && i + 1 < argc) {
            dataRoot = argv[++i];
        } else if (arg == "--out" && i + 1 < argc) {
            outDir = argv[++i];
        } else if (arg == "--replay" && i + 1 < argc) {
            replayCsv = argv[++i];
        } else if (arg == "--seq" && i + 1 < argc) {
            filterSeqStr = argv[++i];
        } else if (arg == "--amft") {
            runAmft = true;
        }
    }

    if (!useGpu && outDir == "results/lighttrack/cpp_dml") {
        outDir = "results/lighttrack/cpp_cpu";
    }

    std::cout << "======================================================\n";
    std::cout << " SOT TRACKER OFFLINE EVALUATION TOOL (test_sot_tracker)\n";
    std::cout << " Backend : " << (useGpu ? (strictGpu ? "DirectML (GPU, Strict)" : "DirectML (GPU)") : "CPU") << "\n";
    std::cout << " DataRoot: " << dataRoot.toStdString() << "\n";
    std::cout << " OutDir  : " << outDir.toStdString() << "\n";
    std::cout << "======================================================\n";

    SotTrackerConfig cfg;
    cfg.templateModel = tmplModel;
    cfg.searchModelSmall = s256Model;
    cfg.searchModelBig = s288Model;
    cfg.useGpu = useGpu;
    cfg.strictGpu = strictGpu;
    cfg.deviceId = deviceId;

    OnnxSotTracker tracker(cfg);
    if (!tracker.loadModel(cfg)) {
        std::cerr << "[-] Error: Failed to load tracker models.\n";
        return 1;
    }

    // 内存采样点 1: 模型加载与预热后
    size_t memLoadWS = 0, memLoadPriv = 0;
    sampleMemoryKB(memLoadWS, memLoadPriv);

    if (!replayCsv.isEmpty()) {
        runReplayMode(tracker, replayCsv, dataRoot, outDir);
        return 0;
    }

    // 预热：对 256、288 搜索各前向 20 次
    std::cout << "[*] Warmup: running 20 inferences for search sessions...\n";
    cv::Mat dummyFrame(720, 1280, CV_8UC3, cv::Scalar(128, 128, 128));
    tracker.init(dummyFrame, cv::Rect2f(100, 100, 50, 50));
    for (int i = 0; i < 20; ++i) {
        tracker.track(dummyFrame);
    }

    QStringList filterList;
    if (!filterSeqStr.isEmpty()) {
        filterList = filterSeqStr.split(',', Qt::SkipEmptyParts);
    }
    auto seqs = loadSequences(dataRoot, filterList);
    std::cout << "[*] Discovered " << seqs.size() << " sequences to evaluate.\n\n";

    QDir().mkpath(outDir);
    QString csvPath = outDir + "/per_frame.csv";
    QFile csvFile(csvPath);
    if (!csvFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        std::cerr << "[-] Error: Failed to create CSV: " << csvPath.toStdString() << "\n";
        return 1;
    }
    QTextStream csvOut(&csvFile);
    csvOut << "seq,frame,bx,by,bw,bh,inst,crop_sz,crop_xmin,crop_ymin,ax,ay,aw,ah,cls_max,box_x,box_y,box_w,box_h,iou,cle,gt_valid,model_ms,total_ms\n";

    std::unique_ptr<QFile> amftFile;
    std::unique_ptr<QTextStream> amftOut;
    if (runAmft) {
        QString amftPath = outDir + "/amft.csv";
        amftFile = std::make_unique<QFile>(amftPath);
        if (amftFile->open(QIODevice::WriteOnly | QIODevice::Text)) {
            amftOut = std::make_unique<QTextStream>(amftFile.get());
            *amftOut << "seq,frame,iou,cls,s_conf,s_mot,s_bg,r_t,tau_t\n";
        } else {
            std::cerr << "[-] Warning: Failed to create AMFT CSV: " << amftPath.toStdString() << "\n";
        }
    }

    std::vector<float> allIous;
    std::vector<float> allCles;
    std::vector<float> allNormCles;

    std::vector<float> t1SmallList;
    std::vector<float> t1BigList;
    std::vector<float> t2List;
    std::vector<float> e2eList;

    QJsonObject perSeqJson;

    size_t memSeq1WS = 0, memSeq1Priv = 0;

    std::cout << std::left << std::setw(12) << "Sequence"
              << std::right << std::setw(8) << "Frames"
              << std::setw(6) << "Inst"
              << std::setw(10) << "AUC"
              << std::setw(10) << "DP@20"
              << std::setw(10) << "CLE(px)"
              << std::setw(10) << "Lost"
              << std::setw(12) << "T1(ms)"
              << std::setw(12) << "FPS(E2E)" << "\n";
    std::cout << QString(88, '-').toStdString() << "\n";

    for (size_t sIdx = 0; sIdx < seqs.size(); ++sIdx) {
        const auto& seq = seqs[sIdx];
        size_t nFrames = std::min<size_t>(seq.imgPaths.size(), seq.gts.size());
        if (nFrames == 0) continue;

        cv::Mat im0 = cv::imread(seq.imgPaths[0].toStdString());
        if (im0.empty()) continue;

        cv::Rect2f initBox = seq.gts[0];
        tracker.init(im0, initBox);

        int instSz = 0;
        std::vector<float> seqIous;
        std::vector<float> seqCles;
        std::vector<float> seqNormCles;
        std::vector<float> seqT1;
        std::vector<float> seqE2e;

        // 第 0 帧记录
        bool gt0Valid = (initBox.width > 0 && initBox.height > 0);
        float iou0 = gt0Valid ? 1.0f : 0.0f;
        float cle0 = 0.0f;
        float normCle0 = 0.0f;
        if (gt0Valid) {
            seqIous.push_back(iou0);
            seqCles.push_back(cle0);
            seqNormCles.push_back(normCle0);
            allIous.push_back(iou0);
            allCles.push_back(cle0);
            allNormCles.push_back(normCle0);
        }

        double cx0 = initBox.x + initBox.width * 0.5;
        double cy0 = initBox.y + initBox.height * 0.5;
        csvOut << seq.name << ",0,"
               << QString::number(cx0, 'f', 4) << "," << QString::number(cy0, 'f', 4) << ","
               << QString::number(initBox.width, 'f', 4) << "," << QString::number(initBox.height, 'f', 4) << ","
               << 0 << ",0,0,0,"
               << QString::number(cx0, 'f', 4) << "," << QString::number(cy0, 'f', 4) << ","
               << QString::number(initBox.width, 'f', 4) << "," << QString::number(initBox.height, 'f', 4) << ","
               << "1.0000,"
               << QString::number(initBox.x, 'f', 4) << "," << QString::number(initBox.y, 'f', 4) << ","
               << QString::number(initBox.width, 'f', 4) << "," << QString::number(initBox.height, 'f', 4) << ","
               << QString::number(iou0, 'f', 4) << "," << QString::number(cle0, 'f', 4) << ","
               << (gt0Valid ? 1 : 0) << ",0.0000,0.0000\n";

        AMFTEvaluator amftEval(10, 0.65f, 0.5f);
        if (runAmft && amftOut) {
            amftEval.init(cv::Rect2f(initBox.x, initBox.y, initBox.width, initBox.height));
            *amftOut << seq.name << ",0,"
                     << QString::number(iou0, 'f', 4) << ",1.0000,1.0000,1.0000,1.0000,1.0000,0.6500\n";
        }

        for (size_t fIdx = 1; fIdx < nFrames; ++fIdx) {
            auto tE2e0 = std::chrono::steady_clock::now();
            cv::Mat im = cv::imread(seq.imgPaths[fIdx].toStdString());
            SotResult res = tracker.track(im);
            auto tE2e1 = std::chrono::steady_clock::now();
            float e2eMs = std::chrono::duration<float, std::milli>(tE2e1 - tE2e0).count();

            instSz = res.dbg.instanceSize;
            float t1 = tracker.lastModelMs();
            float t2 = tracker.lastTotalMs();

            seqT1.push_back(t1);
            seqE2e.push_back(e2eMs);
            t2List.push_back(t2);
            e2eList.push_back(e2eMs);
            if (instSz == 256) t1SmallList.push_back(t1);
            else if (instSz == 288) t1BigList.push_back(t1);

            cv::Rect2f gt = seq.gts[fIdx];
            bool gtValid = (gt.width > 0 && gt.height > 0);
            float iou = 0.0f, cle = 0.0f, normCle = 0.0f;
            if (gtValid && res.valid) {
                iou = computeIoU(res.bbox, gt);
                cle = computeCLE(res.bbox, gt);
                normCle = computeNormCLE(res.bbox, gt);
                seqIous.push_back(iou);
                seqCles.push_back(cle);
                seqNormCles.push_back(normCle);
                allIous.push_back(iou);
                allCles.push_back(cle);
                allNormCles.push_back(normCle);
            }

            csvOut << seq.name << "," << fIdx << ","
                   << QString::number(res.center.x, 'f', 4) << "," << QString::number(res.center.y, 'f', 4) << ","
                   << QString::number(res.size.width, 'f', 4) << "," << QString::number(res.size.height, 'f', 4) << ","
                   << instSz << "," << res.dbg.cropSz << "," << res.dbg.cropXmin << "," << res.dbg.cropYmin << ","
                   << QString::number(res.center.x, 'f', 4) << "," << QString::number(res.center.y, 'f', 4) << ","
                   << QString::number(res.size.width, 'f', 4) << "," << QString::number(res.size.height, 'f', 4) << ","
                   << QString::number(res.dbg.clsMax, 'f', 4) << ","
                   << QString::number(res.bbox.x, 'f', 4) << "," << QString::number(res.bbox.y, 'f', 4) << ","
                   << QString::number(res.bbox.width, 'f', 4) << "," << QString::number(res.bbox.height, 'f', 4) << ","
                   << QString::number(iou, 'f', 4) << "," << QString::number(cle, 'f', 4) << ","
                   << (gtValid ? 1 : 0) << ","
                   << QString::number(t1, 'f', 4) << "," << QString::number(t2, 'f', 4) << "\n";

            if (runAmft && amftOut) {
                float cls = res.score;
                AMFTScore as = amftEval.evaluate(5.0f + 20.0f * cls, res.valid, res.bbox, im);
                *amftOut << seq.name << "," << fIdx << ","
                         << QString::number(iou, 'f', 4) << ","
                         << QString::number(cls, 'f', 4) << ","
                         << QString::number(as.sConf, 'f', 4) << ","
                         << QString::number(as.sMot, 'f', 4) << ","
                         << QString::number(as.sBg, 'f', 4) << ","
                         << QString::number(as.rTotal, 'f', 4) << ","
                         << QString::number(as.dynamicTau, 'f', 4) << "\n";
            }
        }

        EvalMetrics sm = evaluateArrays(seqIous, seqCles, seqNormCles);
        double avgT1 = seqT1.empty() ? 0.0 : std::accumulate(seqT1.begin(), seqT1.end(), 0.0) / seqT1.size();
        double avgE2e = seqE2e.empty() ? 0.0 : std::accumulate(seqE2e.begin(), seqE2e.end(), 0.0) / seqE2e.size();
        double fps = avgE2e > 0.0 ? 1000.0 / avgE2e : 0.0;

        std::cout << std::left << std::setw(12) << seq.name.toStdString()
                  << std::right << std::setw(8) << nFrames
                  << std::setw(6) << instSz
                  << std::setw(9) << std::fixed << std::setprecision(2) << (sm.auc * 100.0) << "%"
                  << std::setw(9) << (sm.dp20 * 100.0) << "%"
                  << std::setw(10) << std::setprecision(2) << sm.meanCle
                  << std::setw(9) << (sm.lostRate * 100.0) << "%"
                  << std::setw(12) << std::setprecision(2) << avgT1
                  << std::setw(12) << std::setprecision(1) << fps << "\n";

        QJsonObject sj;
        sj["frames"] = static_cast<qint64>(nFrames);
        sj["instance_size"] = instSz;
        sj["auc"] = sm.auc;
        sj["dp20"] = sm.dp20;
        sj["mean_cle"] = sm.meanCle;
        sj["lost_rate"] = sm.lostRate;
        sj["sr_50"] = sm.sr50;
        sj["sr_75"] = sm.sr75;
        sj["p_norm"] = sm.pNorm;
        sj["t1_avg_ms"] = avgT1;
        sj["e2e_fps"] = fps;
        perSeqJson[seq.name] = sj;

        if (sIdx == 0) {
            sampleMemoryKB(memSeq1WS, memSeq1Priv);
        }
    }

    csvFile.close();
    std::cout << QString(88, '-').toStdString() << "\n";

    // 内存采样点 3: 全部序列评测后
    size_t memFinalWS = 0, memFinalPriv = 0;
    sampleMemoryKB(memFinalWS, memFinalPriv);

    EvalMetrics overall = evaluateArrays(allIous, allCles, allNormCles);

    double t1SmallAvg = t1SmallList.empty() ? 0.0 : std::accumulate(t1SmallList.begin(), t1SmallList.end(), 0.0) / t1SmallList.size();
    double t1SmallP50 = calculatePercentile(t1SmallList, 50.0);
    double t1SmallP99 = calculatePercentile(t1SmallList, 99.0);

    double t1BigAvg = t1BigList.empty() ? 0.0 : std::accumulate(t1BigList.begin(), t1BigList.end(), 0.0) / t1BigList.size();
    double t1BigP50 = calculatePercentile(t1BigList, 50.0);
    double t1BigP99 = calculatePercentile(t1BigList, 99.0);

    double t2P99 = calculatePercentile(t2List, 99.0);
    double e2eAvg = e2eList.empty() ? 0.0 : std::accumulate(e2eList.begin(), e2eList.end(), 0.0) / e2eList.size();
    double e2eFps = e2eAvg > 0.0 ? 1000.0 / e2eAvg : 0.0;

    std::cout << "\n======================================================\n";
    std::cout << " OVERALL SCORECARD (C++ DirectML/CPU)\n";
    std::cout << "======================================================\n";
    std::cout << " Success AUC          : " << std::fixed << std::setprecision(2) << (overall.auc * 100.0) << "%\n";
    std::cout << " Precision DP@20      : " << (overall.dp20 * 100.0) << "%\n";
    std::cout << " Mean CLE             : " << std::setprecision(2) << overall.meanCle << " px\n";
    std::cout << " Lost Rate            : " << (overall.lostRate * 100.0) << "%\n";
    std::cout << " SR@0.50              : " << (overall.sr50 * 100.0) << "%\n";
    std::cout << " SR@0.75              : " << (overall.sr75 * 100.0) << "%\n";
    std::cout << " Norm Precision       : " << (overall.pNorm * 100.0) << "%\n";
    std::cout << "------------------------------------------------------\n";
    std::cout << " T1 (256): Avg=" << t1SmallAvg << "ms, P50=" << t1SmallP50 << "ms, P99=" << t1SmallP99 << "ms\n";
    std::cout << " T1 (288): Avg=" << t1BigAvg << "ms, P50=" << t1BigP50 << "ms, P99=" << t1BigP99 << "ms\n";
    std::cout << " T2 (P99): " << t2P99 << " ms\n";
    std::cout << " E2E FPS : " << e2eFps << " FPS\n";
    std::cout << "------------------------------------------------------\n";
    std::cout << " Memory (WorkingSet)  : Post-Load=" << memLoadWS << " KB, Seq1=" << memSeq1WS << " KB, Final=" << memFinalWS << " KB\n";
    std::cout << " Memory (PrivateUsage): Post-Load=" << memLoadPriv << " KB, Seq1=" << memSeq1Priv << " KB, Final=" << memFinalPriv << " KB\n";
    std::cout << "======================================================\n";

    QJsonObject summaryJson;
    QJsonObject ovJson;
    ovJson["auc"] = overall.auc;
    ovJson["dp20"] = overall.dp20;
    ovJson["mean_cle"] = overall.meanCle;
    ovJson["lost_rate"] = overall.lostRate;
    ovJson["sr_50"] = overall.sr50;
    ovJson["sr_75"] = overall.sr75;
    ovJson["p_norm"] = overall.pNorm;

    QJsonObject t1Json;
    t1Json["small_256_avg_ms"] = t1SmallAvg;
    t1Json["small_256_p50_ms"] = t1SmallP50;
    t1Json["small_256_p99_ms"] = t1SmallP99;
    t1Json["big_288_avg_ms"] = t1BigAvg;
    t1Json["big_288_p50_ms"] = t1BigP50;
    t1Json["big_288_p99_ms"] = t1BigP99;
    ovJson["t1_search_ms"] = t1Json;
    ovJson["t2_p99_ms"] = t2P99;
    ovJson["e2e_fps"] = e2eFps;

    QJsonObject memJson;
    memJson["load_working_set_kb"] = static_cast<qint64>(memLoadWS);
    memJson["load_private_usage_kb"] = static_cast<qint64>(memLoadPriv);
    memJson["seq1_working_set_kb"] = static_cast<qint64>(memSeq1WS);
    memJson["seq1_private_usage_kb"] = static_cast<qint64>(memSeq1Priv);
    memJson["final_working_set_kb"] = static_cast<qint64>(memFinalWS);
    memJson["final_private_usage_kb"] = static_cast<qint64>(memFinalPriv);
    ovJson["memory"] = memJson;

    summaryJson["overall"] = ovJson;
    summaryJson["per_sequence"] = perSeqJson;

    QString sumPath = outDir + "/summary.json";
    QFile sf(sumPath);
    if (sf.open(QIODevice::WriteOnly)) {
        sf.write(QJsonDocument(summaryJson).toJson());
        std::cout << "[*] Saved summary to " << sumPath.toStdString() << "\n";
    }

    return 0;
}
