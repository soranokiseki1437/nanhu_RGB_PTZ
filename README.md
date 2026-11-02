# RGB_PTZ_Integrated：基于深度学习与多特征融合的光电云台自适应检测-跟踪系统

> **维护说明**：本文件是工程的最高权威技术总览。任何架构调整与代码维护请先查阅方案库 [待执行方案/](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案) 与核心事实源 [checkpoint.md](file:///d:/PyProjects/RGB_PTZ_Integrated/checkpoint.md)。

---

## 一、 系统定位与核心技术亮点

本项目是一套运行于 Windows（MinGW 13.1 64-bit / Qt 6.x / NVIDIA GeForce RTX 5070 GPU）并最终面向嵌入式（RK3588 NPU）落地的高性能光电云台可见光目标检测与连续跟踪伺服系统。系统针对长焦相机大变倍、低空复杂地貌杂波与弱小机动目标的实战工况，确立了**“业务双轨协同（MOT 态势广域感知 + SOT 视轴极速死锁）”**与**“局部跟踪为主、全局按需单次重捕”**的先进架构。

| 核心子系统 | 关键模块 / 理论支撑 | 核心性能指标与实现机制 |
| :--- | :--- | :--- |
| **视频采集与解码** | `DecodeThread` / FFmpeg 8.1 / Univision SDK | • 1080P ($1920 \times 1080$) 30/60 FPS 满帧硬解码<br>• 线程安全无锁队列与双缓冲，端到端延迟 $< 15\text{ ms}$ |
| **深度学习感知底座** | [OnnxDetector](file:///d:/PyProjects/RGB_PTZ_Integrated/onnxdetector.h) + DirectML GPU 加速 | • 微软官方 ONNX Runtime C-API + DirectML 硬件加速<br>• RTX 5070 纯推理 **$3.06\text{ ms}$ (154 FPS)**，相比 CPU 加速比 **19.8x** |
| **纯 C++ 独立后处理** | [YoloPostProcessor](file:///d:/PyProjects/RGB_PTZ_Integrated/yolopostprocessor.h) | • 动态锚点自适应逆映射 + 纯 C++ 高速 NMS，耗时 $< 0.8\text{ ms}$<br>• 无动态依赖，无缝兼容量化移植 |
| **AMFT 协同状态机** | [AMFTEvaluator](file:///d:/PyProjects/RGB_PTZ_Integrated/amftevaluator.h) (2025 IEEE 顶会论文) | • 三维可靠度评估（$S_{conf}$ 峰旁比 + $S_{mot}$ 运动连续性 + $S_{bg}$ HSV 颜色可区分度）<br>• 滑动窗口动态自适应阈值 $\tau_t$<br>• 全局大模型 0 FPS 冷态待机，异常时单次事件驱动唤醒 |
| **高频局部跟踪内核** | [DsstCore](file:///d:/PyProjects/RGB_PTZ_Integrated/dsstcore.h) (方案 02) / `ISotTracker` (方案 04) | • 30/60 FPS 满帧率极速刷新，单帧耗时 $< 2.0\text{ ms}$<br>• 进阶规划原生 3 通道 RGB 深度轻量 SOT（LightTrack / RepSOT） |
| **视轴伺服闭环控制** | [TrackingController](file:///d:/PyProjects/RGB_PTZ_Integrated/trackingcontroller.h) + [PTZController](file:///d:/PyProjects/RGB_PTZ_Integrated/ptzcontroller.h) | • 卡尔曼滤波（KF）多步速度前馈 + 抗饱和积分分离 PID<br>• Pelco-D 9600bps 连续死锁，输出脱靶量误差 RMSE $\le 2.2\text{ px}$ |

---

## 二、 系统架构与双轨协同拓扑

```text
                    【主线 A：广域目标搜索与态势感知 (MOT)】
                     (方案 01 / 方案 03: YOLOv8 / YOLO11 多规格动态热切换)
                     • 广域搜索视场内全部目标，显示类别置信度与坐标框
                     • 支持操作员画面“点击锁定 (Click-to-Lock)”
                                         │
                                         ▼ (交接主锁定目标 TrackResult)
                    【主线 B：视轴跟踪与伺服死锁 (SOT)】
                     (方案 02: DsstCore / 方案 04: LightTrack 深度 SOT)
                     • 独占 30~60 FPS 高频主循环，单帧极速输出 (< 2ms)
                     • 实时计算单一脱靶量 (dx, dy)，直接驱动电机跟随
                                         │
                                         ▼ (当跟踪可靠度突降跌破动态门限时)
                    【顶层仲裁：方案 02 AMFT 状态机】
                     • 常态下全局检测器处于 0 FPS 休眠 (零 GPU 占用)
                     • 异常时单次异步唤醒 1 帧全局扫描，重捕后下一帧强制切回局部！
```

---

## 三、 工程核心目录全景

```text
RGB_PTZ_Integrated/
├── main.cpp                        # 应用程序入口，注册跨线程元类型
├── mainwindow.h/.cpp/.ui           # 主界面与控件绑定，集成模型热切换与遥测监控
├── objecttracker.h/.cpp            # ★ 检测-跟踪协同调度中枢 (运行于独立 Worker 线程)
├── amftevaluator.h/.cpp            # ★ AMFT 评估器：三维特征融合与滑动窗口动态阈值
├── onnxdetector.h/.cpp             # ★ DirectML GPU 硬件加速目标检测器 (IDetector 抽象实现)
├── yolopostprocessor.h/.cpp        # ★ 纯 C++ 动态锚点解析与高性能 NMS 算子
├── dsstcore.h/.cpp                 # 判别式尺度空间相关滤波算法内核 (OpenCV 矩阵运算)
├── trackingcontroller.h/.cpp       # 伺服控制器：KF 速度前馈 + 积分分离 PID + 死区限幅
├── ptzcontroller.h/.cpp            # Pelco-D 串口协议收发与云台驱动
├── devicemanager.h/.cpp            # Univision 相机登录、心跳与参数控制
├── DecodeThread.h/.cpp             # FFmpeg 视频流高优先级解码线程
├── RecordThread.h/.cpp             # FFmpeg 异步录像线程
├── lensmanager.h/.cpp              # 镜头控制（变倍/对焦/变焦尺度校准联动）
├── datarecorder.h/.cpp             # 航迹脱靶量与控制数据记录器 (CSV 导出)
├── logmanager.h/.cpp               # 分类分级统一日志管理器
├── datatypes.h                     # TrackResult / DetectionBox / PTZData 等公共结构体
├── IDetector.h                     # 目标检测器面向对象统一纯虚抽象接口
├── models/                         # ★ 4 款标准可见光静态 ONNX 模型库 (v8n/s, 11n/s)
├── datasets/                       # ★ 全高清 1080P/4K 可见光数据集库 (VisDrone/UAV123/Real_CCD)
├── testdata/                       # 历史 256x256 灰度算法数值对拍序列 (01~22)
├── tools/                          # 阶段全景质量体检与闭环验收工具链源码
│   ├── test_detector.cpp           # 检测引擎速度、精度、内存长稳等 5 项体检工具
│   ├── test_amft.cpp               # AMFT 检测-跟踪协同 4 大维度闭环验收工具
│   └── offline_runner.cpp          # 算法内核离线数据对拍工具
├── scripts/                        # 工程脚本库
│   ├── export_yolo.py              # 标准化静态 ONNX 导出工具
│   └── download_uav123.py          # UAV123 权威基准自动化拉取与解压流水线
├── sdk/                            # Univision 相机 SDK 头文件与导入库
├── 待执行方案/                      # ★ 全系统演进推进方案库 (方案 00 ~ 05)
└── 项目说明/                        # 系统物理连接、数据链路图与硬件规格书
```

---

## 四、 核心状态机设计（AMFT 四态流转机）

在 [objecttracker.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/objecttracker.cpp) 中维护工业级闭环状态流转：

1. **TRACK_NORMAL（正常稳态，占比 98%）**：
   - 局部跟踪器独占 30 FPS 高频运行，YOLO 模型 0 FPS 彻底休眠（0% GPU 占用）；
2. **TRACK_OCCLUDED（疑似遮挡/瞬时失锁态）**：
   - 触发条件：综合可靠度 $R_t < \tau_t$；
   - 动作：停止跟踪模板更新，保持 KF 速度外推前馈，**异步单次唤醒 YOLO 执行 1 帧全局检索**；
3. **TRACK_REACQUIRED（重捕恢复态）**：
   - 触发条件：YOLO 检出高置信度目标；
   - 动作：以新检测框调用 `init()` 热重置跟踪模板，消除累积误差，**下一帧强制切回局部跟踪**；
4. **TRACK_LOST（彻底丢失态）**：
   - 触发条件：连续 15 帧重检失败；
   - 动作：云台匀速搜索或停稳，转入低频检索（3 FPS 待机巡检）。

---

## 五、 伺服控制算法（KF 速度前馈 + PID）

在 [TrackingController::computePTZControl](file:///d:/PyProjects/RGB_PTZ_Integrated/trackingcontroller.cpp) 中逐帧计算控制量：

1. **读取状态**：从 `TrackResult` 读取卡尔曼滤波估计的目标位置 $(X, Y)$ 与像素速度 $(V_x, V_y)$；
2. **时延补偿预测**：预测未来 $T_{predict}=5$ 帧（补偿云台机械延迟约 $80\text{ ms}$）的目标位置：
   $$X_{\text{pred}} = X + V_x \cdot T_{\text{predict}}, \quad Y_{\text{pred}} = Y + V_y \cdot T_{\text{predict}}$$
3. **死区防抖**：脱靶量误差 $e = \text{Target}_{\text{pred}} - \text{Center}$，当 $|e| < \text{deadZone}$（默认 15 px）时强制置零并复位积分；
4. **速度前馈 + PID 混合伺服**：
   $$\text{Speed} = K_{\text{ff}} \cdot V + \left( K_p \cdot e + K_i \int e\,dt + K_d \frac{de}{dt} \right)$$
5. **安全限幅**：经 `qBound(0, speed, 63)` 限幅后编码为 Pelco-D 指令下发驱动电机。

---

## 六、 方案演进总控全景看板

本工程技术演进严格受控于 [待执行方案/00_方案总览与执行推进管理规范.md](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/00_方案总览与执行推进管理规范.md)：

| 方案编号 | 方案名称 | 当前状态 | 达成成果 / 核心目标 |
| :---: | :--- | :---: | :--- |
| **01** | [Windows平台深度学习目标检测引擎接入方案](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/01_Windows平台深度学习目标检测与跟踪集成实操方案.md) | 🟢 **已全面闭环** | DirectML 3.06ms 极速底座打通，5/5 项全景体检全部 PASS |
| **02** | [基于自适应多特征融合(AMFT)的协同实操方案](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/02_基于自适应多特征融合(AMFT)的光电云台检测-跟踪协同实操方案.md) | 🟢 **已全面闭环** | AMFT 状态机落地，0 FPS 冷态待机与单次唤醒闭环验收 PASS |
| **03** | [YOLO多模型横评、免重启热切换与微尺度增强方案](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/03_YOLO多模型(v8n_11n_11s)性能横评与动态切换实操方案.md) | 🟡 **进行中 (当前主力 P0)** | v8n/s 与 11n/s 4 款模型就绪，UI 免重启热切换与消融测试 |
| **04** | [基于轻量深度网络的可见光单目标跟踪实操方案](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/04_基于轻量深度网络的可见光云台单目标跟踪实操方案.md) | ⚪ **已纳编 (Planned)** | LightTrack / RepSOT 3 通道高维色彩特征攻坚地表杂波 |
| **05** | [光电跟踪系统RK3588边缘端量化与工程移植实操方案](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/05_光电跟踪系统RK3588边缘端量化与工程移植实操方案.md) | ⚪ **终极规划 (Planned)** | INT8 RKNN 模型量化、NPU 三核亲和性调度与 ARM64 闭环 |

---

## 七、 环境准备与构建

### 1. 开发环境要求
- **系统**：Windows 11 64-bit
- **编译器**：MinGW 13.1.0 64-bit（随 Qt 安装：`D:\Qt\Tools\mingw1310_64`）
- **框架**：Qt 6.12.0（`core gui widgets serialport concurrent`）
- **加速硬件**：NVIDIA GeForce RTX 5070 Laptop GPU（通过 DirectML 驱动，无需安装沉重 CUDA Toolkit）

### 2. 编译与运行
```bat
# 1. 生成 Makefile
D:\Qt\6.12.0\mingw_64\bin\qmake.exe RGB_PTZ_Integrated.pro "CONFIG+=release"

# 2. 编译构建
D:\Qt\Tools\mingw1310_64\bin\mingw32-make.exe -j8

# 3. 运行主程序 (所有依赖 DLL 与 models/*.onnx 将由 QMAKE_POST_LINK 自动同步分发至 build/release/)
.\build\release\RGB_PTZ_Integrated.exe
```
