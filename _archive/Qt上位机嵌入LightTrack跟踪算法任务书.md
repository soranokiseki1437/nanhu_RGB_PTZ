# Qt上位机工程嵌入LightTrack轻量单目标跟踪算法任务书

> **文件性质**：工程任务交接书  
> **归属工程**：[RGB_PTZ_Integrated](file:///d:/PyProjects/RGB_PTZ_Integrated)  
> **存放路径**：[待执行方案/Qt上位机嵌入LightTrack跟踪算法任务书.md](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/Qt上位机嵌入LightTrack跟踪算法任务书.md)  
> **目标接收方**：后续开发会话（用于编制具体的工程实施方案与代码接线方案）

---

## 一、 系统现状与工程基准

### 1.1 硬件与编译运行环境
- **操作系统**：Windows 11 64-bit
- **推理硬件**：NVIDIA GeForce RTX 5070 Laptop GPU (8GB VRAM, Blackwell 架构, sm_120)
- **Qt 开发框架**：Qt 6.12.0
- **编译器**：MinGW 13.1.0 64-bit
- **C++ 语言标准**：C++17 (`CONFIG += c++17`)
- **关键依赖库配置**：
  - **OpenCV**：MinGW 源码编译版本 4.10.0，仅链接最小运行集：
    - 头文件路径：`opencv/install/include`
    - 链接库：`-lopencv_core4100 -lopencv_imgproc4100`（位于 `opencv/install/x64/mingw/lib`）
  - **ONNX Runtime (DirectML)**：
    - 头文件路径：`3rdparty/onnxruntime/include`
    - 链接库：`-lonnxruntime -lDirectML`（位于 `3rdparty/onnxruntime/lib`）
    - 运行时 DLL：`3rdparty/onnxruntime/bin/` (`onnxruntime.dll`, `DirectML.dll`, `onnxruntime_providers_shared.dll`)
    - 宏定义：`DEFINES += USE_ONNXRUNTIME`
  - **视频编解码**：FFmpeg 8.1 共享库 (`avcodec`, `avformat`, `avutil`, `swscale`, `swresample`)

### 1.2 现有软件架构与控制拓扑
工程采用多线程解耦架构，包含四个核心层次：

```
[视频解码线程 (DecodeThread)]
            │ QImage
            ▼
[主界面 (MainWindow)] ──UI事件/目标框选──▶ [控制中枢 (TrackingController)] (主线程)
                                                     │ 异步调度 (QThread)
                                                     ▼
                                            [跟踪执行器 (ObjectTracker)] (Worker线程)
                                              - 当前内核: DsstCore (传统相关滤波)
                                              - 状态机: AMFTEvaluator (可靠度评估)
                                              - 重捕接口: IDetector / OnnxDetector
                                                     │ 结果回传 (TrackResult)
                                                     ▼
                                            [PID/KF 控制计算] ──Pelco-D──▶ [云台驱动 (PTZController)]
```

- **主界面与交互层 (`MainWindow`)**：
  - 运行于主线程，负责视频流渲染（写时复制缓存 `m_scaledPixmap`）、目标手动框选事件捕获与跟踪轨迹重绘；
  - 包含 AI 模型选择下拉框 (`comboAiModel`)，已支持 YOLO 检测模型的动态热加载；
- **伺服控制层 (`TrackingController`)**：
  - 运行于主线程，维护跟踪状态机（`Idle`, `Selecting`, `Initializing`, `Tracking`, `Lost`）；
  - 控制参数包含 PID 增益（`kp`, `ki`, `kd`, `k_ff`）、死区范围（`m_deadZonePixels`）与限速（`m_maxSpeed <= 63`）；
  - 负责 `ObjectTracker` 及其宿主工作线程 `QThread` 的创建、初始化、启停与安全销毁；
  - 持有 `std::shared_ptr<IDetector>` 实例，并在初始化时注入 `ObjectTracker`；
- **跟踪执行层 (`ObjectTracker`)**：
  - 运行于独立的 Worker 线程（`QThread::LowPriority`），避免阻塞 UI 与视频取流；
  - 槽函数接口：`initSlot(const QImage& frame, const QRectF& targetRect)` 与 `processFrameSlot(const QImage& frame)`；
  - **当前跟踪内核实现**：成员变量 `DsstCore m_core`，纯依赖传统相关滤波算法（二维位置相关滤波 + 一维一维尺度滤波，特征为 HOG + Color Names）；
  - **当前协同与重捕机制**：维护 `AMFTEvaluator`（基于滑动窗口计算响应峰值、运动连续性与背景平峰比，输出可靠度得分 $R_t$ 与动态门限 $\tau_t$）；当置信度低于阈值时，单次调用 `m_detector->detect(image)` 执行全局扫描；
  - **滤波平滑**：内置 $4\times 4$ 卡尔曼滤波器（`cv::Matx44f`），估计并平滑目标中心位置与速度；
- **目标检测层 (`IDetector` / `OnnxDetector`)**：
  - 纯虚接口类 `IDetector.h`：定义 `loadModel`、`isLoaded`、`detect`、`getInferenceTimeMs`、`getInputSize`；
  - 实现类 `OnnxDetector.h/.cpp`：基于 ONNX Runtime C-API (`OrtApi`) 与 DirectML 执行提供者，加载纯静态单输入 YOLO 模型（输入尺寸 `1x3x640x640`）。

### 1.3 待接入算法（LightTrack）资产与技术规格
- **模型定位**：微软亚洲研究院轻量化单目标跟踪基准网络 LightTrackM（基于 NAS 搜索的轻量 MobileNet-NAS 骨干与互相关预测头）；
- **官方预训练权重路径**：
  - 本地源路径：`D:\PyProjects\SOT_Tracking_Baselines\checkpoints\lighttrack\LightTrackM.pth`
  - 工程归档路径：`weights/sot/lighttrack_m.pth`
- **解耦纯静态 ONNX 计算图规格**（无动态维度展开，满足 DirectML 与 RKNN 算子直通）：
  - **模板分支 (`models/lighttrack_template.onnx`)**：
    - 输入张量：`template_img`，数据类型 `float32`，张量尺寸 `[1, 3, 127, 127]`，数值归一化至 $[0.0, 1.0]$；
    - 输出张量：`template_feat`，数据类型 `float32`，张量尺寸 `[1, 96, 8, 8]`；
    - 运行特征：在目标框选初始化或重捕成功时单次运行，特征张量驻留内存/显存；
  - **搜索分支 (`models/lighttrack_search.onnx`)**：
    - 输入张量 1：`search_img`，数据类型 `float32`，张量尺寸 `[1, 3, 255, 255]`，数值归一化至 $[0.0, 1.0]$；
    - 输入张量 2：`template_feat`，数据类型 `float32`，张量尺寸 `[1, 96, 8, 8]`；
    - 输出张量 1：`cls_score`，数据类型 `float32`，张量尺寸 `[1, 1, 16, 16]`（目标中心分类响应得分）；
    - 输出张量 2：`reg_dist`，数据类型 `float32`，张量尺寸 `[1, 4, 16, 16]`（边界偏移回归量 $l, t, r, b$）；
    - 运行特征：高频逐帧连续推断；
- **基础理论与实测指标**：
  - 搜索分支计算量：0.58 GFLOPs，全网络参数量：1.60 M（权重文件约 7.68 MB）；
  - 本地 RTX 5070 纯模型推理耗时：2.1 ms；
  - 本地真实地对空长焦航迹（DUT Anti-UAV 20 组真机跟踪序列，共 24,919 帧 OPE 协议）实测指标：
    - 成功率 (AUC)：52.24%
    - 距离精度 (DP@20)：66.06%
    - 跟丢率 (Lost Rate)：14.16%
- **验证数据集与 Ground Truth 本地路径**：
  - 图像帧序列目录：`datasets/Anti-UAV/tracking/Anti-UAV-Tracking-V0/video01` ~ `video20`
  - 逐帧真值标注目录：`datasets/Anti-UAV/tracking/Anti-UAV-Tracking-V0GT/video01_gt.txt` ~ `video20_gt.txt`

---

## 二、 工程目的

1. **跟踪能力升级**：在保持 C++ 上位机高帧率与低时延的前提下，将深度单目标跟踪（LightTrack）引入上位机，弥补传统相关滤波（DSST）在复杂长焦弱小目标与形变场景下特征表达能力的不足；
2. **多引擎解耦与双内核架构**：在 C++ 跟踪层建立统一的跟踪引擎抽象，支持“传统相关滤波（DSST）”与“深度神经网络（LightTrack）”双内核共存，支持运行期配置选择与切换；
3. **闭环协同架构适配**：实现 LightTrack 深度跟踪引擎与现有卡尔曼滤波平滑（KF）、多特征可靠度评估状态机（AMFT）、异步全局检测重捕（YOLO）以及机电伺服 PID 控制器的全链路闭环；
4. **硬实时约束保障**：基于 ONNX Runtime DirectML 硬件加速，确保整机端到端运行帧率 $\ge 60\text{ FPS}$，单帧前向耗时 $\le 2.5\text{ ms}$，满足机电伺服控制周期（$\le 20\text{ ms}$）的物理约束；
5. **部署资产规范化**：通过纯静态双分支 ONNX 计算图规范，为后续方案 05 边缘端（Rockchip RK3588 NPU, librknnrt）量化与无缝移植提供标准化的模型交付物。

---

## 三、 交接任务（执行方案编制范围）

接手代理（Claude）需在新会话中输出一份**《Qt上位机嵌入LightTrack轻量单目标跟踪算法执行方案.md》**，方案必须严格基于上述工程现状、依赖库与张量规格编写，涵盖以下六项具体技术任务的完整技术规范：

### 任务 1：C++ 深度跟踪引擎 (`OnnxSotTracker`) 架构与接口设计
- 设计基于 ONNX Runtime C-API (`OrtApi`) 与 DirectML 加速的 C++ 跟踪引擎类；
- 明确两个 `OrtSession`（模板分支与搜索分支）的生命周期管理、内存分配与特征缓存机制；
- 明确图像前后处理算法数学定义：
  - 目标上下文扩展边长计算（$s_z = \sqrt{(w+2p)(h+2p)}$）与搜索区域计算（$s_x = 2 \times s_z$）；
  - 双线性插值缩放至固定静态输入维度（$127\times 127$ 与 $255\times 255$）；
  - 余弦窗/汉宁窗位移惩罚计算逻辑；
  - $16\times 16$ 响应图峰值搜索与回归框 $(l, t, r, b)$ 的尺度逆映射解码还原公式；
- 明确异常捕获、内存防泄漏与多线程调用安全规范。

### 任务 2：跟踪器抽象与双引擎热切换机制设计
- 设计跟踪器抽象基类或统一的跟踪内核接口规范（接收统一图像输入并输出统一边界框与置信度）；
- 规范跟踪算法选择枚举（如 `KERNEL_DSST` 与 `KERNEL_LIGHTTRACK`）；
- 规范在 `ObjectTracker` 或 `TrackingController` 中进行跟踪引擎切换的生命周期管理（如何安全释放旧引擎、初始化新引擎、状态重置）；
- 规范 UI 交互层（`MainWindow`）的配置选项与状态同步。

### 任务 3：深度跟踪置信度与 AMFT 协同状态机接线设计
- 明确 LightTrack 分类得分（`cls_score`）与 `AMFTEvaluator` 综合可靠度评估器（$S_{conf}$）的数值对接与归一化协议；
- 明确当深度跟踪置信度跌破阈值进入遮挡/丢失态时，单次触发 `IDetector` (YOLO) 全局重捕的状态流转时序；
- 明确 YOLO 目标重捕成功后，以检出新目标框重新调用 LightTrack 模板分支更新特征缓存的完整时序与接口约定。

### 任务 4：C++ 离线独立验证工具链 (`test_sot_tracker.cpp`) 设计
- 参照现有 `tools/test_detector.cpp` 与 `tools/test_amft.cpp` 规范，设计专用的离线批处理评测工具 `tools/test_sot_tracker.cpp`；
- 工具需直接遍历本地 `datasets/Anti-UAV/tracking` 序列与真值标注（Ground Truth）；
- 明确测试输出的量化指标项：成功率 (AUC)、距离精度 (DP@20)、中心位置误差 (CLE)、纯模型前向耗时、端到端处理帧率 (FPS) 及系统内存占用。

### 任务 5：工程文件变动清单与构建配置
- 列出接入 LightTrack 所需新增、修改的全部源代码文件、头文件与测试工具文件清单；
- 明确 `RGB_PTZ_Integrated.pro` 中需要补充的宏定义、头文件引用或库文件变动（若有）；
- 明确模型部署目录 `models/` 下相关 ONNX 文件的放置与依赖路径。

### 任务 6：实施阶段划分与客观验收指标矩阵
- 划分清晰的实施阶段顺序（模型导出与对齐 $\to$ C++ 引擎编写 $\to$ 离线工具单体验收 $\to$ 上位机主干接线 $\to$ 整机联调验证）；
- 建立量化的客观验收指标矩阵，覆盖：
  - 编译指标：0 错误、0 警告；
  - 精度指标：在本地 20 组航迹上对齐 Python 评测基准；
  - 性能指标：纯模型推理耗时 $\le 2.5\text{ ms}$，整机运行帧率 $\ge 60\text{ FPS}$；
  - 稳定性指标：长时间运行无内存与显存泄漏。

