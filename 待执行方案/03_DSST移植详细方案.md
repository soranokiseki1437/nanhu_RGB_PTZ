# DSST 跟踪器移植方案（剩余任务）

> 2026-09-21 状态更新：Step1（OpenCV 源码编译接入）、Step2（离线驱动 tools/offline_runner + 22 序列数据）、Step3（DsstCore 移植 + 精度对齐）已完成并提交（里程碑 aa3aaba），相关章节已删除。
> Step3 全量回归结论：22 序列中 12 个与 reference 逐帧一致；10 个困难序列双方都大幅偏离 GT（纯内核无外壳所致，无移植缺陷证据，seq19 在 GT 覆盖区间逐行一致）。困难序列待 Step4 接上 KF/遮挡/重检测外壳后按"对 GT 误差"复评。
> 剩余：Step4（外壳接线）→ Step5（模式 UI）→ Step6（性能调优）→ Step7（实机联调）。

---

## 一、已完成交付物速查（供 Step4 使用）

- `dsstcore.h / dsstcore.cpp`：纯算法内核，接口（均为已实现）：
  - `setInfraredMode(bool)`（须在 init 前调）/ `isInfraredMode()`
  - `init(const cv::Mat&, const cv::Rect&)`
  - `cv::Rect update(const cv::Mat& image, cv::Mat* response = nullptr)` —— **空 Rect = 本帧失效**
  - `cv::Rect detectOnly(const cv::Mat&, cv::Point2f center, cv::Mat* response = nullptr)` —— 重检测用，不动模型
  - `setLearningFrozen(bool)`（遮挡期冻结模型滑动平均）/ `blendBackup(float backupWeight)`（模型混合 init 时刻备份）
  - `setScaleEstimateEveryOtherFrame(bool)` / `setPadding(float)`
  - `isInitialized() / position() / currentScale() / targetSize() / modelSize()`
- `imagematconvert.h / cpp`：`QImageToMat(const QImage&)`（Grayscale8→CV_8UC1，其它→BGR CV_8UC3，深拷贝）；`MatToQImage(const cv::Mat&)`
- 工程已链接 opencv_core4100/imgproc4100（MinGW 源码编译，`opencv/install`）

## 二、Step4 外壳接线（ObjectTracker 改造）

### 2.1 删除
`fftutils.h` 依赖、全部 `vector<vector<float>>` 平移/尺度成员（m_hfNum/m_hfDen/m_cosWindow/m_yf/m_sfNum/m_sfDen/m_ysf/m_scaleFactors...）、`imageToGray/getTranslationSample/computeResponse/updateFilter/getScaleSample/initScaleFilter/computeScaleResponse/updateScaleFilter`。

### 2.2 保留
KF（m_kf/kfPredict/kfUpdate）、遮挡三态机（update 决策逻辑 objecttracker.cpp:844-914）、PSR（computePSR 改吃 cv::Mat）、reDetect（内部调 DsstCore）、TrackResult、processFrameSlot 线程模型。

### 2.3 新结构

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

### 2.4 update() 新流程

```cpp
TrackResult ObjectTracker::update(const QImage& frame)
{
    cv::Mat image = QImageToMat(frame);              // 1. 转换（BGR 或灰度）
    kfPredict();                                      // 2. KF 预测（不变）
    cv::Mat response;
    cv::Rect bbox = m_core.update(image, &response);  // 3. 内核检测+尺度估计
    if (bbox.width <= 0) { /* 失效：进遮挡/重检测分支，不得送 KF/PID */ }
    float psr = computePSR(response);                 // 4. PSR（用内核响应图）
    // 5. detX/detY = bbox 中心；后续遮挡三态机、KF 更新、模板更新逻辑
    //    "模板更新"改为无操作——内核内部已做滑动平均更新，
    //    外壳只保留遮挡期间"冻结更新"语义（遮挡时 m_core.setLearningFrozen(true)）
}
```

### 2.5 PSR 的 Mat 版本（沿用原半径5抑制逻辑）

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

### 2.6 reDetect 改造
九宫格偏移循环保留，内部改调 `m_core.detectOnly(image, offsetCenter)`；恢复逻辑（备份混合 0.6/0.4）改为 `m_core.blendBackup(0.6f)`。恢复成功后解除 `setLearningFrozen(false)`。

### 2.7 回归
框选→跟踪→遮挡→恢复全流程手测；离线复评：用外壳版跑 10 个困难序列对 GT 误差（对比 Step3 纯内核基线）。

## 三、Step5 模式与 UI

1. `trackingcontroller.h/cpp` 新增：
   ```cpp
   public slots:
       void setInfraredMode(bool ir);      // 转调 m_tracker 参数
   ```
   setTarget 时透传模式；同步实施《02》2.2 的 setTarget 异步化。
2. `mainwindow.cpp` `initTrackingModule()` 增加模式选择（.ui 跟踪区加 QComboBox `cmbTrackSource`：可见光/红外）：
   ```cpp
   connect(ui->cmbTrackSource, &QComboBox::currentIndexChanged, this, [this](int idx){
       m_trackingController->setInfraredMode(idx == 1);
   });
   ```
3. 默认可见光；红外流接入属二期（双路 RealPlay + 第二 DecodeThread，单独立项）。

## 四、Step6 性能调优

- 尺度隔帧：`m_core.setScaleEstimateEveryOtherFrame(true)`（红外 GST 可每 3 帧）——注意与对拍口径冲突，仅在实机帧率不足时开启；
- `cv::setNumThreads(2)`（main.cpp 初始化处，避免抢占解码线程）；跟踪 worker 线程 `QThread::LowPriority`；
- DsstCore 内 per-frame 日志节流（frame%100==0 才输出）；
- 实测帧率达标：可见光 ≥25fps。

## 五、Step7 联调验收（需实机）

- [ ] 可见光实时：25fps 稳定、无丢帧告警、跟踪框平滑（KF 生效）
- [ ] 遮挡测试：手挡目标 3 秒 → 移开 → 2 秒内恢复跟踪
- [ ] PSR 曲线：正常跟踪 PSR > 基线 75%，遮挡时跌至 <50%（用日志验证）
- [ ] 尺度测试：目标前后移动目标框尺寸跟随 ±5% 内
- [ ] CPU：跟踪开启后整体占用增幅 < 25%（i5 级别）
- [ ] 24h 稳定性：无内存增长、无崩溃
- [ ] 云台闭环：借阶段0 的 PTZ 安全修复（限位/stop 连发）

## 六、部署备注

打包带上 `libopencv_core4100.dll`、`libopencv_imgproc4100.dll`、FFmpeg DLL；`windeployqt` 后核对 DLL 清单。
