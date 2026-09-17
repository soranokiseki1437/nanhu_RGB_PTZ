// 离线对拍驱动：把同一段图像序列喂给 reference 原版 / 移植版 DSST，逐帧输出目标框 CSV
//
// 编译（reference 版）：
//   g++ -std=c++17 -O2 -DUSE_REFERENCE -I<opencv>/include tools/offline_runner.cpp reference/dsst_tracker.cpp \
//       -o tools/runner_ref.exe -L<opencv>/x64/mingw/lib -lopencv_core4100 -lopencv_imgproc4100 -lopencv_imgcodecs4100
// 编译（移植版）：
//   g++ -std=c++17 -O2 -I<opencv>/include tools/offline_runner.cpp dsstcore.cpp \
//       -o tools/runner_port.exe -L<opencv>/x64/mingw/lib -lopencv_core4100 -lopencv_imgproc4100 -lopencv_imgcodecs4100
//
// 用法：offline_runner <序列目录> <初始框文件|-> <输出CSV> [ir]
//   初始框来源优先级：
//     1) 显式传入的 initial_box.txt（"x y w h"，也兼容 "x,y,w,h"）
//     2) 传 "-" 或文件无效时：自动在序列目录同级找 <序号>_gt.txt，取其第一行
//     3) 都没有则退化为画面中心 64x64
//   末尾加 ir 参数则按红外模式运行（GST 特征），默认可见光模式。
//   第 0 帧直接使用初始框，从第 1 帧开始调用 update()。

#include <opencv2/opencv.hpp>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifdef USE_REFERENCE
#include "dsst_tracker.h"
using Tracker = DSSTTracker;
static void setMode(Tracker& t, bool ir) { t.set_infrared_mode(ir); }
static bool initTracker(Tracker& t, const cv::Mat& img, const cv::Rect& r)
{
    t.init(img, r);
    return t.is_initialized;
}
#else
#include "dsstcore.h"
using Tracker = DsstCore;
static void setMode(Tracker& t, bool ir) { t.setInfraredMode(ir); }
static bool initTracker(Tracker& t, const cv::Mat& img, const cv::Rect& r)
{
    return t.init(img, r);
}
#endif

// 读取 "x y w h" 或 "x,y,w,h"，跳过空行与 # 注释
static bool readBox(const std::string& path, int& x, int& y, int& w, int& h)
{
    std::ifstream in(path);
    if (!in) {
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream ss(line);
        if (ss >> x >> y >> w >> h) {
            return true;
        }
    }
    return false;
}

// 查找真值文件：先看序列目录本身，再看其父目录（兼容两种数据集布局）
// 优先 <序号>_gt.txt（目录名去掉前导 0），否则取找到的第一个 *_gt.txt
static std::string scanGt(const std::filesystem::path& where, const std::string& preferred)
{
    std::error_code ec;
    std::string fallback;
    for (const auto& entry : std::filesystem::directory_iterator(where, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (name.size() < 7 || name.compare(name.size() - 7, 7, "_gt.txt") != 0) {
            continue;
        }
        if (name == preferred) {
            return entry.path().string();
        }
        if (fallback.empty()) {
            fallback = entry.path().string();
        }
    }
    return fallback;
}

static std::string findGtFile(const std::string& seqDir)
{
    namespace fs = std::filesystem;
    fs::path dir(seqDir);
    if (dir.filename().empty()) {
        dir = dir.parent_path();   // 传入以 / 结尾时
    }

    std::string stripped = dir.filename().string();
    const size_t firstNonZero = stripped.find_first_not_of('0');
    if (firstNonZero != std::string::npos) {
        stripped = stripped.substr(firstNonZero);
    }
    const std::string preferred = stripped + "_gt.txt";

    std::string found = scanGt(dir, preferred);       // 数据集布局：01/1_gt.txt
    if (found.empty() && dir.has_parent_path()) {
        found = scanGt(dir.parent_path(), preferred); // 备选布局：与 imgs 同级
    }
    return found;
}

int main(int argc, char** argv)
{
    if (argc < 4) {
        std::fprintf(stderr, "用法: %s <序列目录> <初始框文件|-> <输出CSV> [ir]\n", argv[0]);
        return 1;
    }

    const std::string dir = argv[1];
    const std::string boxFile = argv[2];
    const std::string csvPath = argv[3];
    const bool infrared = (argc >= 5 && std::string(argv[4]) == "ir");

    // 帧目录：若给定目录下有 imgs/ 子目录则自动进入（数据集约定）
    std::string frameDir = dir;
    {
        std::error_code ec0;
        const std::filesystem::path sub = std::filesystem::path(dir) / "imgs";
        if (std::filesystem::is_directory(sub, ec0)) {
            frameDir = sub.string();
        }
    }

    // 只收集图片文件（避免把 1_gt.txt / 1_frames.txt 当成帧），按文件名排序
    static const std::vector<std::string> exts = { ".bmp", ".png", ".jpg", ".jpeg", ".tif", ".tiff", ".pgm" };
    std::vector<std::string> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(frameDir, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (std::find(exts.begin(), exts.end(), ext) != exts.end()) {
            files.push_back(entry.path().string());
        }
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        std::fprintf(stderr, "目录中没有图片: %s\n", frameDir.c_str());
        return 2;
    }

    const cv::Mat first = cv::imread(files.front(), cv::IMREAD_COLOR);
    if (first.empty()) {
        std::fprintf(stderr, "首帧读取失败: %s\n", files.front().c_str());
        return 2;
    }

    // 初始框：显式文件 → GT 首行 → 画面中心 64x64
    int bx = 0, by = 0, bw = 0, bh = 0;
    bool boxOk = (boxFile != "-") && readBox(boxFile, bx, by, bw, bh);
    if (!boxOk) {
        const std::string gt = findGtFile(dir);
        if (!gt.empty() && readBox(gt, bx, by, bw, bh)) {
            boxOk = true;
            std::printf("[info] 初始框取自真值文件: %s (x=%d y=%d w=%d h=%d)\n",
                        gt.c_str(), bx, by, bw, bh);
        }
    }
    if (!boxOk || bw <= 0 || bh <= 0 || bx < 0 || by < 0
        || bx + bw > first.cols || by + bh > first.rows) {
        bw = bh = 64;
        bx = first.cols / 2 - bw / 2;
        by = first.rows / 2 - bh / 2;
        std::fprintf(stderr, "[warn] 无有效初始框，使用默认框 x=%d y=%d w=%d h=%d\n", bx, by, bw, bh);
    }

    Tracker tracker;
    setMode(tracker, infrared);
    if (!initTracker(tracker, first, cv::Rect(bx, by, bw, bh))) {
        std::fprintf(stderr, "跟踪器初始化失败\n");
        return 3;
    }

    std::ofstream csv(csvPath);
    if (!csv) {
        std::fprintf(stderr, "无法写入 CSV: %s\n", csvPath.c_str());
        return 4;
    }
    csv << "frame,x,y,w,h\n";

    cv::Rect last(bx, by, bw, bh);
    for (size_t i = 0; i < files.size(); ++i) {
        cv::Mat img = (i == 0) ? first : cv::imread(files[i], cv::IMREAD_COLOR);
        if (img.empty()) {
            continue;
        }
        if (i > 0) {
            last = tracker.update(img);
        }
        csv << i << ',' << last.x << ',' << last.y << ',' << last.width << ',' << last.height << '\n';
    }

    std::printf("完成: %zu 帧 -> %s (mode=%s, 分辨率 %dx%d)\n",
                files.size(), csvPath.c_str(), infrared ? "infrared" : "visible",
                first.cols, first.rows);
    return 0;
}