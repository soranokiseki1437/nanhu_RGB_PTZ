# Windows 平台 Qt 深度学习目标检测与 ByteTrack 跟踪集成方案

> **方案编号**：01  
> **归属工程**：[RGB_PTZ_Integrated](file:///d:/PyProjects/RGB_PTZ_Integrated)  
> **当前宿主环境**：Windows 11 (MinGW 13.1 64-bit / Qt 6.x, C++17)  
> **加速硬件**：**NVIDIA GeForce RTX 5070 Laptop GPU**（优先通过 ONNX Runtime DirectML 开启 GPU 硬件加速，单帧 3~5ms；同时支持无显卡环境自动回退 CPU）  
> **未来硬件考量**：前瞻性解耦设计，严格遵守模型尺寸与纯 C++ 算法约束，确保与未来 Linux ARM64 (RK3588 6 TOPS NPU) 部署完全兼容且零冲突。  
> **核心目标**：在当前安全稳定运行的架构（FFmpeg 解码、DSST 跟踪、Pelco-D 伺服闭环）基础之上，平滑接入轻量级深度学习目标检测模型（YOLOv8n / YOLO11s 静态尺寸）与纯 C++ ByteTrack 多目标跟踪算法，构建“双模式可切换、低延迟、零阻塞”的工程化感知伺服管道。

---

## 一、 系统架构与双引擎共存机制

### 1.1 与现有系统的关系：双模式平滑切换
当前工程已拥有成熟的 [ObjectTracker.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/objecttracker.cpp)（基于 [DsstCore](file:///d:/PyProjects/RGB_PTZ_Integrated/dsstcore.cpp) 的相关滤波单目标跟踪）及完整的云台伺服闭环。深度学习**不破坏、不废弃**现有成果，而是采用**双引擎模式并存**的设计：

```
                            [ 相机解码流 DecodeThread ]
                                         │  QImage
                  ┌──────────────────────┴──────────────────────┐
                  ▼                                             ▼
       【模式 A：传统单目标跟踪】                      【模式 B：AI 智能检测跟踪】
     (ObjectTracker / DsstCore)                  (InferenceWorker / ByteTrack)
       - 用户手动拉框选择 ROI                          - YOLO 全图多目标自动检测
       - 模板匹配，无类别先验                          - ByteTrack 维护全局 ID 与轨迹
       - 极轻量 CPU 计算                               - 智能锁定策略 (TargetSelector)
                  │                                             │
                  └──────────────────────┬──────────────────────┘
                                         ▼ 单目标平滑状态 (TrackResult)
                              [ TrackingController ]
                             (统一前馈 + PID 伺服计算)
                                         │
                                         ▼ PTZ 速度指令
                                 [ PTZController ]
```

* **统一输出标准**：无论是传统 DSST 还是深度学习 ByteTrack，对主锁定目标的输出最终均封装为统一的 `TrackResult`（包含中心像素坐标与卡尔曼平滑速度矢量 $(kf_X, kf_Y, kf_{V_x}, kf_{V_y})$）。
* **伺服算法零改动**：现有 [TrackingController.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/trackingcontroller.cpp) 的**预测前馈 + 误差 PID 算法、死区控制、串口通信** 100% 原样复用。
* **架构定调与能耗约束（强联动方案 05）**：
  - **模式 A（经典 SOT 模式）**：以局部相关滤波（DSST）独占高频伺服（30 FPS，<1ms），YOLO 深度检测处于**零频休眠（0 FPS Idle）**，仅在遮挡失锁时按需触发单次全局重捕；
  - **模式 B（AI 智能 MOT 模式）**：YOLO + ByteTrack 组合用于广域自主多目标态势感知与自动索敌，选定主目标后即可切回模式 A 进行高精稳态死锁。

---

### 1.2 总体并发拓扑与线程安全

系统严格遵循**UI主线程、视频解码线程、算法推理线程三权分立**原则：

```
                   [ Univision 相机 / RTSP / 本地视频 ]
                                    │  原始 H.264/H.265 码流
                                    ▼
                             [ DecodeThread ] (FFmpeg 软解)
                                    │  QImage (原始帧率 30/60 fps)
             ┌──────────────────────┴──────────────────────┐
             │ Qt::QueuedConnection                        │ 线程安全最新帧覆写
             ▼                                             ▼
       [ UI 主线程 ]                              [ InferenceWorkerThread ]
    (MainWindow::onNewFrame)                     (独立异步算法推理工作线程)
             │                                             │
             │                                   ┌─────────┴─────────┐
             │                                   │ IDetector 推理    │ (YOLO 静态 640x640)
             │                                   │ DirectML GPU 加速 │ (RTX 5070 耗时 3~5ms)
             │                                   │ Letterbox / NMS   │ (纯 C++ 后处理)
             │                                   └─────────┬─────────┘
             │                                             ▼ std::vector<DetectionBox>
             │                                   ┌───────────────────┐
             │                                   │ ByteTracker 关联  │ (纯 C++ 航迹匹配)
             │                                   └─────────┬─────────┘
             │                                             ▼ std::vector<TrackTarget>
             │                                   ┌───────────────────┐
             │                                   │ TargetSelector    │ (锁定主控目标)
             │                                   └─────────┬─────────┘
             │                                             ▼ 单目标 TrackResult
             ├─────────────────────────────────────────────┤ (跨线程 QueuedConnection)
             ▼                                             ▼
       [ 画面叠加渲染 ]                            [ TrackingController ]
   (QPainter 绘制检测框与ID)                       (目标预测 + 速度前馈 + PID)
                                                           │
                                                           ▼ (speedX, speedY)
                                                   [ PTZController ]
                                                (Pelco-D / 串口指令下发)
```

### 1.3 最新帧保护策略（Drop-oldest）
1. **绝不阻塞解码与 UI**：`InferenceWorkerThread` 内部维护一个带互斥锁的单帧槽位 `std::atomic<bool> m_hasNewFrame`。当推理正在执行时，解码线程新送达的帧直接覆写旧帧，跳过堆积帧，彻底消除延迟累积。
2. **零拷贝格式转换**：`QImage` 传入子线程后，通过指针包装转换为 `cv::Mat`，并在子线程中独立完成 Letterbox 缩放，避免占用主线程 CPU。

---

## 二、 编译依赖与跨平台解耦设计

为了保证**当前 Windows (MinGW 13.1 + RTX 5070) 极致性能接入**，同时确保**未来移植到 Linux ARM64 (RK3588) 完全不矛盾、零重构**，底层依赖作如下规划：

### 2.1 依赖库选型：ONNX Runtime (DirectML / C-API)
* **现状分析**：当前工程的 OpenCV 仅编译了 `core` 和 `imgproc` 模块（无 `opencv_dnn`）。如果使用 OpenCV DNN 需重新耗时编译整个 OpenCV。
* **推荐解法**：直接引入微软官方的 **ONNX Runtime (C API) + DirectML 执行提供者**。
  - **GPU 极速加速**：通过 DirectML（DirectX 12）调用宿主机的 **NVIDIA GeForce RTX 5070** 显卡进行硬件加速，完全免去配置复杂的 CUDA Toolkit 与 cuDNN 版本匹配地狱！
  - **MinGW 100% 兼容**：ONNX Runtime 导出纯 C 接口（`onnxruntime_c_api.h`），在 MinGW 下可以直接动态链接，完美避开 MSVC 与 MinGW 的 C++ ABI 符号不兼容陷阱。
  - **优雅降级兜底**：在 C 接口中设置 DirectML 优先：
    ```cpp
    // 优先开启 DirectML (GPU 硬件加速，设备号 0)
    OrtStatus* status = OrtSessionOptionsAppendExecutionProvider_DML(sessionOptions, 0);
    if (status != nullptr) {
        // 若 GPU 不可用，则自动平滑回退为 CPU 多线程推理
        OrtSessionOptionsAppendExecutionProvider_CPU(sessionOptions, 1);
    }
    ```
* **面向 RK3588 的无冲突前瞻性**：
  - 算法业务层统一面向纯虚基类 [IDetector.h](file:///d:/PyProjects/RGB_PTZ_Integrated/IDetector.h) 编程；
  - Windows 端实现 [OnnxDetector](file:///d:/PyProjects/RGB_PTZ_Integrated/onnxdetector.h)；未来在 RK3588 上只需新增 `RknnDetector`（使用 `librknnrt.so` 调用 NPU），业务层和跟踪层代码**一行不改**。

### 2.2 构建体系配置（`RGB_PTZ_Integrated.pro`）

在工程配置文件中采用条件分支隔离平台依赖：

```qmake
# ==================== 深度学习目标检测引擎 ====================
win32 {
    # Windows 平台：使用 ONNX Runtime C-API (兼容 MinGW 64-bit 与 MSVC)
    DEFINES += USE_ONNXRUNTIME
    INCLUDEPATH += $$PWD/3rdparty/onnxruntime/include
    LIBS += -L$$PWD/3rdparty/onnxruntime/lib -lonnxruntime
    
    HEADERS += onnxdetector.h
    SOURCES += onnxdetector.cpp
}

unix:!macx {
    # 预留：未来 RK3588 / Linux ARM64 平台接入 (与当前规划完全不冲突)
    DEFINES += USE_RKNN
    INCLUDEPATH += /usr/include/rknn
    LIBS += -lrknnrt
    
    HEADERS += rknndetector.h
    SOURCES += rknndetector.cpp
}

# 跨平台完全共用部分（纯 C++ 算法，无任何硬件平台依赖）
HEADERS += \
    IDetector.h \
    yolopostprocessor.h \
    bytetracker/ByteTracker.h \
    bytetracker/KalmanFilter.h \
    bytetracker/STrack.h

SOURCES += \
    yolopostprocessor.cpp \
    bytetracker/ByteTracker.cpp \
    bytetracker/KalmanFilter.cpp \
    bytetracker/STrack.cpp
```

---

## 三、 核心模块与抽象接口规范

### 3.1 抽象推理接口 `IDetector.h`

```cpp
#ifndef IDETECTOR_H
#define IDETECTOR_H

#include <QString>
#include <QRectF>
#include <vector>
#include <string>
#include <opencv2/core.hpp>

// 单个目标检测框
struct DetectionBox {
    QRectF rect;          // 目标边界框（图像绝对像素坐标：x, y, w, h）
    float confidence;     // 检测置信度 (0.0f ~ 1.0f)
    int classId;          // 目标类别索引
    std::string className;// 目标类别标签名称

    DetectionBox() : rect(0, 0, 0, 0), confidence(0.0f), classId(-1) {}
    DetectionBox(const QRectF& r, float conf, int cls, const std::string& name = "")
        : rect(r), confidence(conf), classId(cls), className(name) {}
};

// 检测器配置
struct DetectorConfig {
    int inputWidth = 640;                  // 模型输入宽（严格静态尺寸）
    int inputHeight = 640;                 // 模型输入高（严格静态尺寸）
    float confThreshold = 0.25f;           // 置信度筛选阈值
    float nmsThreshold = 0.45f;            // NMS 抑制阈值
    std::vector<std::string> classNames;   // 类别映射表
};

// 抽象检测器接口
class IDetector {
public:
    virtual ~IDetector() = default;

    virtual bool loadModel(const QString& modelPath, const DetectorConfig& config = DetectorConfig()) = 0;
    virtual bool isLoaded() const = 0;
    virtual std::vector<DetectionBox> detect(const cv::Mat& frame) = 0;
    virtual float getInferenceTimeMs() const = 0;
    virtual cv::Size getInputSize() const = 0;
};

#endif // IDETECTOR_H
```

---

### 3.2 独立跨平台纯 C++ 后处理 `YoloPostProcessor`

将模型输出张量的解码与 NMS 逻辑完全从推理引擎中抽离为独立的工具类。无论是 Windows ONNX Runtime 还是未来 RK3588 NPU，只要模型导出结构一致（输出格式 $[1, 84, 8400]$），该后处理模块**100% 跨平台复用**。

```cpp
class YoloPostProcessor {
public:
    // 解码原始模型张量并执行纯 C++ NMS
    static std::vector<DetectionBox> process(
        const float* outputTensor,
        int tensorChannels,      // 84 (4 坐标 + 80 类别得分)
        int tensorAnchors,       // 8400
        int srcWidth, int srcHeight,
        int netWidth, int netHeight,
        float confThresh, float nmsThresh,
        const std::vector<std::string>& classNames);
};
```

---

### 3.3 纯 C++ ByteTrack 多目标跟踪算法

传统单目标跟踪（DSST）一旦被建筑物、树木短暂遮挡极易彻底跟丢。引入纯 C++ 实现的 **ByteTrack**：
1. **零第三方硬件依赖**：仅使用 C++ 标准库与 OpenCV core（矩阵运算），在 Windows MinGW 和 ARM64 上均可极速运行（单帧关联耗时 $< 1\text{ ms}$）。
2. **双阶段数据关联（Two-stage Association）**：
   - 第一阶段：高分检测框与存续航迹进行空间 IoU 匹配；
   - 第二阶段：低分检测框与未匹配航迹二次关联，有效保障遮挡与模糊时的轨迹连续性。
3. **输出数据结构**：
   ```cpp
   struct TrackTarget {
       int trackId;          // 目标全局持久化 ID
       QRectF bbox;          // 当前平滑边界框
       float confidence;     // 综合置信度
       float kfVx;           // 卡尔曼估计速度 Vx (像素/帧)
       float kfVy;           // 卡尔曼估计速度 Vy (像素/帧)
       bool isConfirmed;     // 航迹是否已稳定确认
       int lostFrames;       // 连续丢失帧数
   };
   ```

---

### 3.4 目标锁定策略与伺服闭环无缝接入 (`TargetSelector`)

ByteTrack 输出多目标列表 `std::vector<TrackTarget>`，而云台伺服（`TrackingController`）一次只能跟踪一个主控目标。通过目标选择策略完成桥接：

1. **手动锁定模式**：用户在 UI 画面上点击任意检测框，拾取其 `trackId` 并锁定；
2. **自动锁定模式**：按优先级规则（如：类别优先级 人员/车辆/无人机，或中心距离最近、面积最大）自动选定主目标；
3. **转换为标准 `TrackResult` 并发送**：
   ```cpp
   // 将主锁定目标的航迹状态封装为现有 TrackingController 兼容的 TrackResult
   TrackResult result;
   result.valid = true;
   result.bbox = lockedTarget.bbox;
   result.confidence = lockedTarget.confidence;
   result.kfX = lockedTarget.bbox.center().x();
   result.kfY = lockedTarget.bbox.center().y();
   result.kfVx = lockedTarget.kfVx;
   result.kfVy = lockedTarget.kfVy;
   result.occluded = (lockedTarget.lostFrames > 0);

   // 触发信号，现有 TrackingController::trackingResult 槽函数直接响应！
   emit mainTargetTracked(result);
   ```

---

## 四、 UI 交互与工程配置集成

### 4.1 UI 控制扩展
在 [mainwindow.ui](file:///d:/PyProjects/RGB_PTZ_Integrated/mainwindow.ui) 中补充：
1. **跟踪模式下拉切换**：
   - `模式 1：传统框选跟踪 (DSST)`（原有流程，拉框即跟）；
   - `模式 2：AI智能识别跟踪 (YOLO + ByteTrack)`。
2. **AI 目标高亮渲染**：
   - 主界面 `paintEvent` 接收所有目标的边界框，绘制半透明绿框、类别名称与全局 ID；
   - 被当前锁定的主目标使用**加粗红框**标注。
3. **点击选目标交互**：
   - 在 AI 模式下，画面接收鼠标左键点击，几何判断点击位置是否命中某个目标框，命中后立即切换锁定目标。

### 4.2 配置项扩展 ([ConfigManager](file:///d:/PyProjects/RGB_PTZ_Integrated/configmanager.cpp))
在配置文件中增加如下持久化参数：
```ini
[AI]
model_path=./models/yolov8n.onnx
conf_threshold=0.25
nms_threshold=0.45
auto_lock_class=person
```

---

## 五、 四阶段实操落地路线图

```
 阶段 1：ONNX Runtime 依赖引入与静态推理打通
   - 引入 onnxruntime C-API 库与头文件至 3rdparty/
   - 创建 IDetector.h 与 YoloPostProcessor (纯 C++ NMS)
   - 编写 OnnxDetector.cpp，实现静态 640x640 图像单步检测验证
                    │
                    ▼
 阶段 2：引入纯 C++ ByteTrack 多目标跟踪内核
   - 引入 bytetracker/ 源码（KalmanFilter, STrack, ByteTracker）
   - 编写单测：连续多帧输入检测结果，验证 ID 连续性与关联正确性
                    │
                    ▼
 阶段 3：构建异步工作线程与视频流接线
   - 编写 InferenceWorkerThread.h/cpp
   - 接线 DecodeThread：实现 Drop-oldest 策略，打通视频流检测
   - 主界面完成多目标 QPainter 平滑绘制
                    │
                    ▼
 阶段 4：目标锁定策略与现有 TrackingController 伺服联调
   - 实现 TargetSelector（点击手动锁定与优先级自动锁定）
   - 锁定目标状态转译为 TrackResult，输入现有前馈+PID 闭环
   - 外场/录像全流程联调与长稳压测
```

---

## 六、 成果验收与指标定义

| 评估维度 | 指标基准 | 验收方法 |
| :--- | :--- | :--- |
| **检测推理耗时** | $\le 5\text{ ms}$ (RTX 5070 DirectML GPU 加速) / $\le 20\text{ ms}$ (CPU 回退兜底) | `IDetector::getInferenceTimeMs()` 连续 500 帧统计平均值 |
| **跟踪处理耗时** | $\le 2\text{ ms}$ (ByteTrack C++ 纯数学运算) | 多目标航迹更新单次耗时打点统计 |
| **视频显示流畅度** | 稳定 $\ge 30\text{ fps}$，UI 零卡顿 | 主界面刷新率计数器实时监测 |
| **端到端伺服响应** | $\le 40\text{ ms}$ (GPU模式下从相机出图到串口下发 PTZ 指令) | 对拍时间戳打点比对 |
| **跨平台兼容度** | 核心算法代码无 Windows/CUDA 专有 API 绑定 | 代码架构审查，确保 IDetector/ByteTrack 随时可被 Linux aarch64 编译 |
