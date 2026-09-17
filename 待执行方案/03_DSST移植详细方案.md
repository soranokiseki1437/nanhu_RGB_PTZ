# DSST 跟踪器移植详细方案（reference → 上位机）

目标：把 `reference/` 中经过验证的 DSST 跟踪算法（可见光：灰度+梯度 3 通道特征；红外：GST 特征）移植进上位机，替换现有 ObjectTracker 的自研 FFT 核心，同时保留现有工程中成熟的 KF 平滑、遮挡检测/重检测、PSR 置信度、PID+前馈 PTZ 控制外壳。

审核修订：2026-09-17（§1.1 改为源码编译最小 OpenCV——官方 prebuilt 无 MinGW 库；库名后缀修正为 4100；§6.1 新增"离线驱动 + 序列数据"前置交付物；附录补 scale_sigma_factor 与空框约定）

---

## 一、前置条件：引入 OpenCV（解决体检 B1）

### 1.1 获取 OpenCV（本机工具链：Qt MinGW 13.1）

> 环境核实（2026-09-17）：本机 Qt 为 `D:\Qt\6.11.2\mingw_64`，编译器 `D:\Qt\Tools\mingw1310_64`，`.pro.user` 中注册的 Qt 6.10.1 MinGW kit 指向的旧构建目录已失效——以下以本机实际工具链为准。

**方案 A（推荐）：源码编译最小 OpenCV**

原方案（下载官方 `opencv-4.10.0-windows.exe` 解压即用）**不可行**：该包 `build/x64/` 下只有 `vc16`（MSVC）目录，没有 mingw 库，官方文档也明确 prebuilt 仅面向 Visual Studio。

1. 下载 OpenCV 源码（GitHub releases → Source code (zip)），解压到工程根目录：`RGB_PTZ_Integrated/opencv/sources`。
2. 用 Qt 自带的 CMake + MinGW 13.1 编译，只构建算法实际需要的模块（已核对 reference：仅用 Mat/dft/mulSpectrums/filter2D/Sobel/resize/meanStdDev/minMaxLoc/cvtColor，无 imread/imshow，离线工具用 QImage 读图即可）：

```bat
set PATH=D:\Qt\Tools\mingw1310_64\bin;D:\Qt\Tools\CMake_64\bin;%PATH%
cmake -S opencv\sources -B opencv\build -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ ^
  -DBUILD_LIST=core,imgproc ^
  -DBUILD_SHARED_LIBS=ON -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF ^
  -DBUILD_EXAMPLES=OFF -DBUILD_opencv_apps=OFF -DWITH_IPP=OFF ^
  -DCMAKE_INSTALL_PREFIX=opencv\install
cmake --build opencv\build --target install
```

3. 产物目录：`opencv/install/x64/mingw/{include, bin, lib}`。库名形如 `libopencv_core4100.dll.a` / `libopencv_imgproc4100.dll.a`，DLL 为 `libopencv_core4100.dll` 等——**版本号后缀是 4100（4.10.0），不是 410**。
4. 工程配套：`.gitignore` 增加 `opencv/`（与 ffmpeg 同规格，不入库）；README「环境准备」补下载与编译说明。

**方案 B（备选）**：vcpkg `vcpkg install opencv4[core]:x64-mingw-dynamic`（需自备 vcpkg，构建耗时不比方案 A 少）。

### 1.2 .pro 修改（RGB_PTZ_Integrated.pro）

```pro
# ===== OpenCV 依赖（DSST 跟踪器） =====
# 指向 1.1 方案 A 的安装目录（源码编译产物）
OPENCV_DIR = $$PWD/opencv/install
INCLUDEPATH += $$OPENCV_DIR/include

win32: LIBS += -L$$OPENCV_DIR/x64/mingw/lib \
    -lopencv_core4100 \
    -lopencv_imgproc4100

# 运行时把 libopencv_core4100.dll / libopencv_imgproc4100.dll 及其依赖拷到 exe 同目录
# 注意版本号后缀是 4100（OpenCV 4.10.0），不是 410；若改用 world 库则为 opencv_world4100
```
同时在 `SOURCES/HEADERS` 增加新文件（见第二章文件清单）。

> 若将来改用 MSVC 工具链，则 LIBS 指向 `x64/vc16/lib/opencv_world4100.lib`，并用 `CONFIG(debug, debug|release)` 区分 `opencv_world4100d`。

---

## 二、总体架构设计

### 2.1 设计原则：内核替换、外壳保留

```
现有架构（保留）：
  MainWindow ──> TrackingController(KF前馈+PID+死区) ──> ObjectTracker(壳: 遮挡/PSR/KF)
                                                            │
变更点：                                                    ▼
                                              DsstCore(新, OpenCV Mat 版 DSST)
                                              ├─ 可见光模式: 灰度+gradX+gradY 3通道
                                              └─ 红外模式: GST 特征 1通道
```

- **DsstCore（新文件）**：reference/dsst_tracker.cpp 的移植版，职责单一——纯算法：`init(Mat, Rect) → update(Mat) → Rect` + 暴露响应图供 PSR 计算。
- **ObjectTracker（改造）**：保留 KF/遮挡/PSR/重检测逻辑（objecttracker.cpp:463-956 这部分质量不错），把「特征提取+FFT+滤波器训练/检测」全部委托给 DsstCore；`vector<vector<float>>` 数据通路换成 `cv::Mat`。
- **TrackingController / MainWindow**：对外接口零变化（TrackResult 结构不变），仅新增红外模式参数传递。

### 2.2 为什么不用整类替换（备选方案对比）

| 方案 | 改动量 | 风险 | 结论 |
|------|--------|------|------|
| A：内核替换（本方案） | 新增2文件+改2文件 | 低，KF/遮挡/PID 全保留 | 采用 |
| B：直接用 DSSTTracker 替换 ObjectTracker | 小 | 丢失 KF 平滑/遮挡重检测/PSR，PTZ 前馈失去速度估计源 | 否决 |
| C：两套并存可切换 | 大 | 维护两套，FFT 数学缺陷仍在 | 否决 |

---

## 三、文件级变更清单

### 3.1 新增 `dsstcore.h / dsstcore.cpp`（从 reference 移植）

从 `reference/dsst_tracker.h/cpp` 复制后做以下改造（对应体检 R1~R6）：

1. **命名空间与头文件卫生**：去掉头文件中的 `using namespace cv/std`；类名改为 `DsstCore`；包含改为 `<opencv2/opencv.hpp>`。
2. **printf 全部替换**：
   ```cpp
   #include <QLoggingCategory>
   Q_DECLARE_LOGGING_CATEGORY(trackerCore)
   // cpp: Q_LOGGING_CATEGORY(trackerCore, "tracker.core")
   // 原 printf("Feature map created...") → qCDebug(trackerCore) << "feature channels" << features.size();
   // 每帧的 [Pos]/[Scale] 调试输出改为节流：仅 frame%100==0 时输出
   ```
3. **gst_process 保留原算法**（sigma1=sigma2=0.3、filterSize=5、边界裁剪 5px 不变），内部 `CV_64F` 计算链保持（数值稳定性依赖双精度，勿改 float）。
4. **复数运算换 cv::mulSpectrums**（修 R6）：
   ```cpp
   // 原 complex_multiply(yf, conj_xlf) →
   cv::mulSpectrums(yf, xlf, hf_num_d, 0, true);   // 第4参true=对B取共轭
   // 原 complex_conj 手写循环全部删除
   ```
5. **get_subwindow 重写为 0-based 标准写法**（修 R4）：
   ```cpp
   cv::Mat DsstCore::getSubwindow(const cv::Mat& im, cv::Point2f pos, cv::Size sz, float scale)
   {
       cv::Size patch(std::max(2, int(sz.width * scale)), std::max(2, int(sz.height * scale)));
       // 中心对齐取 patch 尺寸，允许越界（replicate 边界由下式处理）
       int x = cvRound(pos.x - patch.width / 2.0);
       int y = cvRound(pos.y - patch.height / 2.0);
       int w = patch.width, h = patch.height;
       // 边界收缩（修 R1：先 clamp 左上，再 clamp 宽高，保证 >=2）
       x = cv::min(cv::max(0, x), im.cols - 2);
       y = cv::min(cv::max(0, y), im.rows - 2);
       w = cv::min(w, im.cols - x);
       h = cv::min(h, im.rows - y);
       if (w < 2 || h < 2) return {};
       cv::Mat roi = im(cv::Rect(x, y, w, h));
       cv::Mat out;
       cv::resize(roi, out, sz);      // 收缩后拉伸回模型尺寸，与 reference 行为一致
       return out;
   }
   ```
   > 注意：reference 的 x1/y1 1-based 换算保留原语义经离线序列回放对齐后再删（见 6.2 测试）。
6. **update() 尾部 roi clamp 修复**（修 R1）：
   ```cpp
   roi.x = cv::max(0, roi.x);
   roi.y = cv::max(0, roi.y);
   roi.width  = cv::min(image.cols - roi.x, roi.width);
   roi.height = cv::min(image.rows - roi.y, roi.height);
   if (roi.width <= 0 || roi.height <= 0) return {};   // 返回失效框
   ```
   > 约定（外壳必须遵守）：返回空 Rect 表示本帧失效，ObjectTracker 应视为"未检测到目标"，进入遮挡/重检测分支，**不得把空框送入 KF/PID**。
7. **暴露响应图与 PSR 支持**（新增，外壳需要）：
   ```cpp
   class DsstCore {
   public:
       bool init(const cv::Mat& grayOrBgr, const cv::Rect& bbox);
       // 返回目标框；response 输出平移相关响应图（供 PSR 计算）
       cv::Rect update(const cv::Mat& grayOrBgr, cv::Mat* response = nullptr);
       void setInfraredMode(bool ir);
       bool isInfraredMode() const;
       float currentScale() const;
       cv::Point2f position() const;
       bool isInitialized() const;
   private:
       // reference 全部私有成员与方法迁移
       cv::Mat m_response;    // update 内部保存平移响应，供 PSR
   };
   ```
   update() 中 `minMaxLoc(response,...)` 之后加 `m_response = response;`，PSR 由外壳用该 Mat 计算。
8. **尺度估计隔帧执行（性能，修 R3）**：update 增加 `m_frameCount`，`if (m_frameCount % 2 == 0)` 才做尺度采样/更新（尺度变化慢，隔帧足够）；红外 GST 模式下可放宽到每 3 帧。

### 3.2 新增 `imagematconvert.h / imagematconvert.cpp`（QImage↔cv::Mat）

```cpp
// QImage(Format_RGB888/Grayscale8) → cv::Mat，无拷贝包装+深拷贝可选
// 1) Qt 每行字节对齐(4B)，OpenCV Mat 需要 stride，因此统一走 copy：
cv::Mat QImageToMat(const QImage& img);
QImage MatToQImage(const cv::Mat& mat);   // 显示叠加框时用

// 实现要点：
// cv::Mat mat(img.height(), img.width(), CV_8UC3, (uchar*)img.bits(), img.bytesPerLine());
// cvtColor → BGR；灰度直接 CV_8UC1
```

### 3.3 改造 `objecttracker.h / objecttracker.cpp`

删除：`fftutils.h` 依赖、全部 `vector<vector<float>>` 平移/尺度成员（m_hfNum/m_hfDen/m_cosWindow/m_yf/m_sfNum/m_sfDen/m_ysf/m_scaleFactors...）、`imageToGray/getTranslationSample/computeResponse/updateFilter/getScaleSample/initScaleFilter/computeScaleResponse/updateScaleFilter`。

保留：KF（m_kf/kfPredict/kfUpdate）、遮挡三态机（update 决策逻辑 objecttracker.cpp:844-914）、PSR（computePSR 改吃 cv::Mat）、reDetect（内部调 DsstCore）、TrackResult、processFrameSlot 线程模型。

新结构：

```cpp
class ObjectTracker : public QObject {
    // 对外接口完全不变：init / update / reset / getCurrentBBox / processFrameSlot
private:
    DsstCore m_core;                  // 新内核
    bool m_infraredMode = false;

    // KF / 遮挡 / PSR 相关成员原样保留（m_kf, m_kfQ, m_psrHistory...）
    float computePSR(const cv::Mat& response);   // 签名改 Mat
    bool reDetect(const cv::Mat& image);
};
```

update() 新流程（与旧版逐段对应）：

```cpp
TrackResult ObjectTracker::update(const QImage& frame)
{
    cv::Mat image = QImageToMat(frame);              // 1. 转换（BGR 或灰度）
    kfPredict();                                      // 2. KF 预测（不变）
    cv::Mat response;
    cv::Rect bbox = m_core.update(image, &response);  // 3. 内核检测+尺度估计
    float psr = computePSR(response);                 // 4. PSR（用内核响应图）
    // 5. detX/detY = bbox 中心；后续遮挡三态机、KF 更新、模板更新逻辑
    //    "模板更新"改为无操作——reference 的 DSST 内部已做滑动平均更新，
    //    外壳只保留遮挡期间"冻结更新"的语义（DsstCore 增加 setLearningFrozen(bool)，
    //    遮挡时置 true，内核 update 跳过模型更新）
}
```

PSR 的 Mat 版本（沿用原半径5抑制逻辑）：
```cpp
float ObjectTracker::computePSR(const cv::Mat& response)
{
    if (response.empty()) return 0.0f;
    cv::Point maxLoc; double maxVal;
    cv::minMaxLoc(response, nullptr, &maxVal, nullptr, &maxLoc);
    cv::Mat mask = cv::Mat::ones(response.size(), CV_8U);
    cv::circle(mask, maxLoc, 5, 0, -1);
    cv::Scalar mean, stddev;
    cv::meanStdDev(response, mean, stddev, mask);
    double sd = std::max(stddev[0], 1e-6);
    return float((maxVal - mean[0]) / sd);
}
```

reDetect 改造：九宫格偏移循环保留，内部改调 `m_core.updateFromOffset()`（DsstCore 增加一个"以指定位置为中心仅做检测、不更新模型"的方法，即把 reference update() 的前半段拆出为 `detectOnly(Mat, Point2f pos)`），恢复逻辑（备份混合 0.6/0.4）改为 `m_core.blendBackup()`（DsstCore 内部保存 init 时刻滤波器副本，供恢复混合）。

### 3.4 改造 `trackingcontroller.h / trackingcontroller.cpp`

```cpp
public slots:
    void setInfraredMode(bool ir);      // 新增，转调 m_tracker 参数
```
setTarget 时透传模式；`setTarget` 同步实施《02》5.4 的异步化改造。

### 3.5 改造 `mainwindow.cpp`

1. `initTrackingModule()` 增加模式选择：
   ```cpp
   // UI 增加一个 QComboBox(cmbTrackMode) 或 QCheckBox：可见光/红外
   // （.ui 中加入 groupBox 跟踪区，项目名 cmbTrackSource）
   connect(ui->cmbTrackSource, &QComboBox::currentIndexChanged, this, [this](int idx){
       m_trackingController->setInfraredMode(idx == 1);
   });
   ```
2. onTrackResult 中 `updateDetectionData` 数据结构不变。
3. 默认模式：可见光（当前仅有 RGB 流）。红外流接入属于后续二期（见 5.1）。

### 3.6 reference 命令协议的映射（main_zt.cpp 不移植，仅对照）

| reference 语义 | 上位机对应 |
|---|---|
| 0x10 停止跟踪 | MainWindow::on_btnTrackStop_clicked → stopTracking |
| 0x11/0x12/0x13 按目标尺寸档位启动（可见光64/48/32，红外44/28/12） | 框选启动（自由框选，天然覆盖）；最小尺寸校验用 `minBox=12px` |
| 0x60/0x84 外部框选启动 | MainWindow 鼠标框选 |
| SIFU_CTR track_center_x/y、track_status | TrackingController::computePTZControl → ptzControlDelta 信号 |
| getSensorDev() 0=可见光/1=红外 | cmbTrackSource 选择 |

---

## 四、数据流与线程模型（移植后）

```
[DecodeThread] frameDecoded(QImage RGB888 1920×1080)
      │ (queued)
MainWindow::onFrameReceived ── displayImage
      │ TrackingController::processFrame(frame)
      │ (invokeMethod queued)
[TrackerWorker线程]
ObjectTracker::processFrameSlot
      ├─ QImageToMat            （~2ms, 1080p 整帧，仅一次）
      ├─ DsstCore::update
      │    ├─ getSubwindow + 归一化加窗       （可见光3通道 / 红外GST）
      │    ├─ cv::dft + mulSpectrums + idft  （模型 ~192×192）
      │    ├─ 尺度采样（隔帧）+ 尺度FFT
      │    └─ 模型滑动平均更新
      ├─ computePSR(Mat)
      ├─ KF 预测/更新 + 遮挡三态机（原逻辑不变）
      └─ emit trackingDone(TrackResult)      （结构不变，含 kfX/kfVx）
```

性能预估（可见光，1080p，目标100×100 → 模型 250×250）：
- 平移采样+3通道特征：~6ms
- 2×dft/idft(250×250)：~4ms
- 尺度估计（隔帧）：~5ms
- 合计 <20ms/帧，满足 25fps。（红外 GST 更重，隔帧尺度+GST 内部 5×5 核固定，预计 25~40ms，必要时尺度估计降为每 3 帧。）

---

## 五、边界情况与风险控制

### 5.1 红外流的接入（二期，本方案预留）
当前工程只有 RGB 相机流（UNIV_DEV_RealPlay MAIN）。reference 程序的 IR 来自 gz0084 硬件库（640×512），本机没有该库。移植后红外模式先支持「对可见光画面手动选红外模式」跑算法验证；真正红外流接入需要：
- DeviceManager 增加第二路 RealPlay（EXTRA1/第二设备），第二 DecodeThread；
- MainWindow 双画面 + 框选来源选择。
> 此为独立工作量，单独立项，不影响本次移植验收。

### 5.2 风险与回退
| 风险 | 缓解 |
|---|---|
| OpenCV 与 Qt 工具链 ABI 不匹配 | 用 `D:\Qt\Tools\mingw1310_64` 从源码编译（与 Qt 同编译器，见 1.1）；编译后先做最小验证工程（qmake + 一个 main 调 cv::dft）确认可链接再动主工程 |
| 离线验证数据缺失（`D:/DSST-KF-SA-MD` 本机不存在），精度门禁无法执行 | Step2 前先落地一份序列数据（相机录像导帧+手标首帧 / 公开基准序列）；若确无数据，退化为合成序列一致性验证，并在验收记录中标注该限制 |
| GST 红外特征在可见光画面上效果差 | 模式仅由用户显式选择，默认可见光 3 通道 |
| 移植后跟踪精度回退 | 6.2 离线回放对比（中心误差/成功率）通过才合入 |
| CPU 抢占解码线程 | cv::setNumThreads(2)；跟踪 worker 线程 QThread::LowPriority |
| 移植引入崩溃 | 保留旧 ObjectTracker 实现一个 Git 分支，回退即切分支 |

### 5.3 部署
- 打包时带上 `libopencv_core4100.dll`、`libopencv_imgproc4100.dll`、`avcodec-62.dll` 等（FFmpeg 已有惯例，加一个批处理拷贝到 release 目录；具体 DLL 名以 1.1 编译产物为准）；
- `windeployqt` 后核对 DLL 清单。

---

## 六、实施步骤与验收

### 6.1 实施顺序（建议按此提交粒度）
1. **Step1 构建验证**：按 1.1 编译 OpenCV（core,imgproc）→ 最小工程链接测试 → 主工程 .pro 加依赖（此时工程尚无 OpenCV 代码，零风险合入）；同步更新 .gitignore 与 README。
2. **Step2 离线驱动与数据（前置交付物，必须先落地）**：`reference/tracker_config.txt` 指向的 `D:/DSST-KF-SA-MD/sequences/02/imgs/` 本机不存在，且 `reference/main_zt.cpp` 依赖 gz0084FuncLib 硬件库、没有离线读图代码——因此需要**新建一个最小离线驱动**（读图 → 跟踪 → 逐帧框写 CSV），reference 版与移植版各接一次，保证同一份数据、同一初始框。
   数据来源三选一（先用 (a) 或 (b) 落地几十帧即可）：
   (a) 相机录像导出的帧序列 + 手工标注首帧框（tracker_config.txt 的首帧仅 9×9，标注成本极低）；
   (b) 公开基准序列（OTB/UAV123 等，脚本化下载）；
   (c) 合成序列（平移+缩放+遮挡），只能验一致性，不能验绝对效果。
3. **Step3 DsstCore 移植与精度对齐**：新增 dsstcore.* 与 imagematconvert.*（不接线）；用同一序列分别跑 reference 版驱动与移植版驱动，逐帧对比目标框中心误差 < 2px（允许 resize 边界差异导致的 1~2px 抖动）。若差异大，优先排查 getSubwindow 的 0-based 重写（R4）。
4. **Step4 外壳接线**：ObjectTracker 改造（删 FFT 内核、接 DsstCore、PSR/KF/遮挡适配）。回归：框选→跟踪→遮挡→恢复全流程手测。
5. **Step5 模式与 UI**：TrackingController/MainWindow 增加红外模式选择。
6. **Step6 性能调优**：尺度隔帧、cv::setNumThreads、日志节流；实测帧率达标（可见光 ≥25fps）。
7. **Step7 联调验收**：连接真实相机 + 云台，闭环跟踪（借 02 号文档阶段0 的 PTZ 安全修复）。

### 6.2 验收标准
- [ ] 离线序列：移植版与 reference 输出中心误差 < 2px（Step3）
- [ ] 可见光实时：25fps 稳定、无丢帧告警、跟踪框平滑（KF 生效）
- [ ] 遮挡测试：手挡目标 3 秒 → 移开 → 2 秒内恢复跟踪（三态机 + 重检测生效）
- [ ] PSR 曲线：正常跟踪 PSR > 基线 75%，遮挡时跌至 <50%（用日志验证）
- [ ] 尺度测试：目标前后移动（近大远小）目标框尺寸跟随 ±5% 内
- [ ] CPU：跟踪开启后整体占用增幅 < 25%（i5 级别 CPU）
- [ ] 24h 稳定性：无内存增长（任务管理器观察）、无崩溃

### 6.3 工作量分解
| 任务 | 产出 |
|---|---|
| OpenCV 编译（core,imgproc）与 .pro | 构建通过 + 最小链接验证 |
| 离线驱动 + 序列数据（Step2 前置） | 1 个最小驱动（reference 版与移植版共用接口约定）+ 1 份可复现的序列数据 |
| dsstcore 移植+单测 | 2 个新文件 + CSV 输出 |
| 精度对齐 | 对比报告（脚本：diff 两个 CSV） |
| ObjectTracker 改造 | objecttracker.* 精简 ~600 行 |
| UI/Controller 模式接入 | 红外/可见光切换 |
| 性能与联调 | 验收清单全绿 |

---

## 附：dsstcore.h 最终形态（接口预览）

```cpp
#ifndef DSSTCORE_H
#define DSSTCORE_H
#include <opencv2/opencv.hpp>

class DsstCore {
public:
    void setInfraredMode(bool ir);
    bool isInfraredMode() const { return m_infrared; }

    bool init(const cv::Mat& image, const cv::Rect& bbox);
    // response: 输出平移响应图（CV_32F，model_sz 尺寸），供 PSR
    cv::Rect update(const cv::Mat& image, cv::Mat* response = nullptr);

    // 重检测支持（外壳九宫格调用）：仅检测不更新模型
    cv::Rect detectOnly(const cv::Mat& image, cv::Point2f center, cv::Mat* response = nullptr);
    void setLearningFrozen(bool frozen);   // 遮挡期间冻结模型更新
    void blendBackup(float oldWeight);     // 恢复时混合备份模型（0.6=备份权重）

    bool isInitialized() const { return m_initialized; }
    cv::Point2f position() const { return m_pos; }
    float currentScale() const { return m_scale; }
    cv::Size targetSize() const { return m_targetSz; }

private:
    // ==== reference DSSTTracker 全部私有成员迁移（Mat 化）====
    float m_padding = 1.5f;          // 沿用现工程调优值（reference 为 2.0，见对齐试验后定）
    float m_outputSigmaFactor = 1.f/16.f;
    float m_scaleSigmaFactor = 1.f/4.f;   // scale_sigma_factor：reference dsst_tracker.h:21，尺度滤波器 ysf 生成必需（cpp:390）
    float m_lambda = 1e-2f;
    float m_learningRate = 0.025f;
    int   m_numScales = 9;
    float m_scaleStep = 1.05f;
    float m_scaleModelMaxArea = 512.f;
    bool  m_infrared = false;
    int   m_numFeatures = 3;
    int   m_frameCount = 0;

    cv::Mat m_yf, m_hfDen;
    std::vector<cv::Mat> m_cosWindow, m_hfNum;
    cv::Mat m_hfNumBackup, m_hfDenBackup;         // 恢复混合用
    cv::Mat m_ysf, m_scaleCosWindow, m_sfNum, m_sfDen;
    cv::Point2f m_pos;
    cv::Size m_targetSz, m_modelSz, m_scaleModelSz;
    float m_scale = 1.0f;
    std::vector<float> m_scaleFactors;
    float m_minScale = 0.f, m_maxScale = 100.f;

    cv::Mat gstProcess(const cv::Mat& img);        // reference gst_process 原样迁移
    std::vector<cv::Mat> getFeatureMap(const cv::Mat& patch);
    std::vector<cv::Mat> getTranslationSample(const cv::Mat& im, cv::Point2f pos);
    cv::Mat getScaleSample(const cv::Mat& im, cv::Point2f pos, const std::vector<float>& factors);
    cv::Mat getSubwindow(const cv::Mat& im, cv::Point2f pos, cv::Size sz, float scale);
    static cv::Mat hann1d(int n);
    static cv::Mat gaussianPeak(cv::Size sz, float sigma);
};
#endif
```

> padding 参数：reference 用 2.0，现工程调优到 1.5（objecttracker.cpp:22 注释"减少峰值稀释"）。移植时保持 1.5 起步，对齐测试阶段两组都跑取优者。
