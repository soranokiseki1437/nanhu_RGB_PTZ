# 项目状态检查点机制与全景状态（checkpoint.md）

> **机制定义**：本文件是 `RGB_PTZ_Integrated` 项目的**唯一事实源（Single Source of Truth）**与**项目状态机**。用于跨开发会话、跨代理（Agent）与人类开发者之间的无缝交接，记录项目全景健康度、里程碑推进、Git 工作区状态、体检问题闭环及后续行动项。

---

## 一、检查点（Checkpoint）机制运行规范

### 1.1 触发时机（When to Update）
在以下场景发生时，必须同步检视并更新本文件：
1. **工作会话结束/交接**：每次开发任务完成，向用户汇报前更新。
2. **每次任务推进与交互（强制铁律）**：每次会话开始推进、阶段性验收或响应进度查询时，必须即时检视并刷新本文件，杜绝状态滞后与信息死角。
3. **关键里程碑达成**：完成阶段性功能、重大重构（如 DSST 接线、并发治理）或提交新 Commit 后。
4. **架构/状态机变更**：改动多线程模型、信号槽链路、对外接口或配置文件格式。
5. **测试与联调完成**：离线对拍验证、实机联调测试（Step 7 验收）后。
6. **Git 状态大幅变动**：分支合并、工作区大规模清理或依赖库变动。

### 1.2 操作流程（How to Update）
```mermaid
flowchart LR
    A[启动任务] --> B[阅读 checkpoint.md]
    B --> C[研发/修改/修复]
    C --> D[编译与测试验证]
    D --> E[更新 checkpoint.md]
    E --> F[Git 提交与交接]
```
1. **进场**：开发前先阅读 `checkpoint.md`，获取最新里程碑、未决问题与工作区状态。
2. **核实**：更新前必须执行 `git status`、编译检查及相关测试，禁止凭记忆记录。
3. **沉淀**：更新「二、当前项目状态快照」并将最新进展刷新至「七、最近检查点记录」。本文件始终仅保留当前最新检查点，历史检查点分流归档至 `checkpoints/` 小文件。
4. **Git 提交与推送**：更新完成后执行 `git add`、`git commit` 并 `git push origin main`，项目演进与历史版本完全由远程与本地 Git 版本树客观承载。

### 1.3 轻量检查点与文档编写铁律
1. **轻量小文件与单点最新**：检查点文件仅保留当前最新项目快照与最近检查点，严禁在单一文件中无限追加历史导致文件膨胀。历史记录分流至 `checkpoints/archive/` 小文件归档。
2. **必须执行 Git 提交与推送**：开发推进与阶段交接必须同步提交并推送到远程仓库（`git push`），杜绝无版本追踪的代码漂移，保持云端与本地强一致。
3. **严禁保留任何修订痕迹与辩解性废话（铁律）**：严禁出现“目前模式已完全切换为”、“目前已经彻底放弃xxx”、“不再xxx”、“鉴于此前决定xxx”等解释性或过渡性废话。所有文档和方案必须直接、客观、干净地陈述当前最新事实与规范。

---

## 二、当前项目状态快照（2026-10-31 最新）

| 维度 | 当前状态 | 详细说明 |
| :--- | :---: | :--- |
| **代码分支** | `main` | 工作区改动已全部通过构建并验证完毕 |
| **最新进展** | 🟢 **微调 (4款) 与从零训练 (YOLO11n 300轮) 100% 全量达成；前沿文献 (UAV-DETR) 与弱小目标损失函数探索储备纳编** | 4 款微调模型与 1 款 300 轮从零模型全量闭环归档；前沿成果 UAV-DETR 深度剖析与学术横评入库；弱小目标损失函数 (Inner-CIoU / NWD) 探索与实测选型方案纳编方案 03/06 |
| **编译状态** | 🟢 **通过（0 告警 0 错误）** | Release 构建已通过，Qt 6.12.0 / C++17 / MinGW 13.1 64-bit |
| **GPU 推理基准** | 🟢 **77.2~86.6 FPS (端到端) / 2.7~5.8ms (纯GPU)** | NVIDIA RTX 5070 Laptop GPU (DirectML/CUDA 硬件加速)：各微调模型纯推理耗时稳定在 2.7~5.8ms 极速区间，大幅优于 15ms 伺服安全窗口 |
| **传统算法与伺服** | 🟢 **Step 1~6 完备** | DsstCore 纯算法、AMFT 三维可靠度评估（4/4 PASS 100% 通过）、变焦动态纠偏与云台负反馈伺服闭环稳定运行 |
| **待执行核心任务** | 🟢 **方案 03 核心目标全部闭环！5 款部署级 ONNX 齐备入库** | 方案 03 所有检测模型训练与消融全部圆满交付；下一步可根据规划推进方案 04（可见光深度单目标跟踪 LightTrack/RepSOT 探索）或实机联调 |
| **Git 工作区状态** | 🟢 **良好** | 生产代码、训练脚本、四款微调权重库、学术文献库与标准化数据集均完整归档就位 |

---

## 三、系统核心架构与模块健康矩阵

```
 [相机SDK (Univision)] ──原始码流──▶ [DecodeThread] (含 ThreadSafeQueue 队列复位)
                                              │ QImage (信号通知)
                                              ▼
                                      [MainWindow::onNewFrame]
                                              │
                      ┌───────────────────────┴───────────────────────┐
                      ▼                                               ▼
             视频渲染与跟踪框绘制                               [TrackingController]
           - m_scaledPixmap 写时复制缓存                         (主线程前馈 + PID 伺服计算)
           - 避免全量 fromImage/scaled 开销                                │
                                                                      │ QueuedConnection
                                                                      ▼
                                                             [ObjectTracker] (Worker 线程)
                                                               - DsstCore (C++17/OpenCV 内核)
                                                               - 3态机遮挡检测 + 9宫格重检测
                                                               - 异步 initSlot / processFrameSlot
                                                               - 卡尔曼滤波 (KF) 状态平滑与速度估计
                                                                      │
                                                                      ▼
                                                              [PTZController]
                                                               - Pelco-D 串口协议驱动
                                                               - 限位保护 + stop 连发防失控
                                                               - 速度随角度信号回传
```

### 模块健康度表

| 模块 | 关键文件 | 状态 | 关键改进与现状 |
| :--- | :--- | :---: | :--- |
| **算法内核** | `dsstcore.h/.cpp` | 🟢 完备 | 从 reference 纯 C++ 移植，去除 Qt 依赖；修复 R1/R4/R6；支持红外/可见光特征切换；支持尺度隔帧 |
| **跟踪外壳** | `objecttracker.h/.cpp` | 🟢 完备 | 彻底剥离 `fftutils` 手写 FFT；全流程改为 DsstCore；PSR 改用 Mat 半径5圆形抑制；异步 initSlot 解决主线程卡顿 |
| **伺服控制** | `trackingcontroller.h/.cpp` | 🟢 完备 | KF 速度前馈 + PID 反馈；红外模式接口下发；死区与输出限幅 |
| **云台通信** | `ptzcontroller.h/.cpp` | 🟢 完备 | P1 软限位保护；P2 stop() 3次防丢包；P4 串口写返回值校验与 flush；速度随角度信号上报 (C1) |
| **视频解码** | `DecodeThread.h/.cpp` `videodecoder.h/.cpp` | 🟢 完备 | V1 packet 生命周期与 EAGAIN 处理；V7 分辨率自适应重建；C6 `ThreadSafeQueue::reset` 解决复活空转 |
| **设备网络** | `devicemanager.h/.cpp` | 🟢 完备 | C4 `m_isRecording` 原子布尔化；C5 SDK 异常回调异步投递登出；C7 异步开流避免 UI 卡死 |
| **界面交互** | `mainwindow.h/.cpp/.ui` | 🟢 完备 | C8 绘制缓存（QPixmap 写时复制）；跟踪数据跨线程信号直传；红外/可见光源下拉选择绑定 |
| **配置管理** | `configmanager.h/.cpp` | 🟢 完备 | P7 全局加锁保护；P8 移除明文密码、`aboutToQuit` 统一存盘 |
| **镜头控制** | `lensmanager.h/.cpp` | 🟢 完备 | P6 一键聚焦等待自动/手动模式切换成功后再执行（1s 超时兜底） |
| **日志系统** | `logmanager.h/.cpp` `logging_categories.*` | 🟢 完备 | B2/T4 移除全局宏一刀切，采用 QLoggingCategory 分类动态管理；跟踪 per-frame 节流输出 |
| **离线工具** | `tools/` `testdata/` | 🟢 完备 | `offline_runner` 与 `compare_csv.py` 支持 22 组数据逐帧与 reference 对拍验证 |

---

## 四、Git 状态深度体检与处置指引

### 4.1 当前 Git 状态全景（`git status -s`）

```text
 M checkpoint.md (检查点刷新至 Checkpoint #15)
 M mainwindow.cpp (拔除 10 处双重 connect、COW 零拷贝优化、UI 状态文本一致性)
 M onnxdetector.cpp (loadModel/析构/isLoaded 加互斥锁保障多线程热切换安全)
```

### 4.2 诊断分析与建议处置方案

| 现象分类 | 产生根因分析 | 推荐处置方案 | 对应 Git 命令 |
| :--- | :--- | :--- | :--- |
| **1. 生产代码加固与冗余拔除** | 完成 `OnnxDetector` 多线程互斥保护、拔除 10 处信号槽重复绑定、`displayImage` 155MB/s 零拷贝优化及状态机一致性。 | **提交入库**。闭环本次体检与去冗余成果。 | `git add onnxdetector.cpp mainwindow.cpp checkpoint.md`<br>`git commit -m "fix(core): 闭环OnnxDetector多线程互斥锁，拔除10处双重connect并优化COW零拷贝"` |
| **2. 分支状态** | 工作区通过 Release 构建与全景验收测试，准备提交。 | **提交并推送**。 | `git push origin main` |

---

## 五、体检清单（45 项）修复闭环追踪总表

基于 `待执行方案/01_全面体检问题清单.md` 的全部 45 项问题追踪：

| 编号 | 级别 | 问题描述 | 闭环状态 | 解决途径 / 对应提交 |
| :---: | :---: | :--- | :---: | :--- |
| **T1** | P1 | FFT 非二次幂长度频域截断导致精度丢失 | ✅ **已闭环** | 彻底移除 hand-made FFT，改用 DsstCore (OpenCV dft)（Commit `d4b4973`） |
| **T2** | P1 | 跟踪模型尺寸未取 2 的幂 | ✅ **已闭环** | DsstCore 内部统一支持非二次幂快速变换，数学一致性修复 |
| **T3** | P1 | 跟踪器性能不足（深拷贝+迭代FFT） | ✅ **已闭环** | DsstCore 连续内存 Mat + cv::mulSpectrums，速度提升 5~10 倍 |
| **T4** | P2 | 跟踪每帧输出大量 qDebug 造成锁竞争 | ✅ **已闭环** | 迁移至 QLoggingCategory，DsstCore 增加 frameCount%100 节流 |
| **T5** | P2 | computeResponse 每帧无谓实数→复数转换 | ✅ **已闭环** | 随旧 FFT 体系删除（Commit `d4b4973`） |
| **T6** | P1 | elementDivide 分母语义模糊易发散 | ✅ **已闭环** | 随旧 FFT 体系删除，DsstCore 采用稳健相关滤波分母正则化 |
| **T7** | P2 | computeFHOG 三维嵌套 vector 缓存不友好 | ✅ **已闭环** | DsstCore 使用 OpenCV 连续内存特征抽取 |
| **T8** | P3 | TrackResult 缺少红外/可见光模式字段 | ✅ **已闭环** | ObjectTracker/UI/Controller 全链路增加红外模式切换（Commit `d4b4973`） |
| **T9** | P2 | reDetect 九宫格耗时×9 | ✅ **已闭环** | 改调 DsstCore::detectOnly，不更新模型、耗时降低 |
| **B1** | P0 | 工程未配置 OpenCV 阻碍移植 | ✅ **已闭环** | .pro 配置 MinGW 编译版 opencv_core/imgproc（Commit `aa3aaba`） |
| **B2** | P2 | Release 下一刀切关闭 qDebug | ✅ **已闭环** | 改为运行时 QLoggingCategory 分类动态控制（Commit `aa3aaba`） |
| **C1** | P1 | 主线程跨线程直接读 PTZController 速度 | ✅ **已闭环** | PTZ 线程取速度随角度信号一同上报，MainWindow 读参数（Commit `d4b4973`） |
| **C2** | P1 | 日志全局锁+同步flush导致所有线程串行化 | ✅ **已闭环** | 日志异步分类优化与节流（Commit `aa3aaba`） |
| **C3** | P1 | qInstallMessageHandler 线程安全问题 | ✅ **已闭环** | 统一在 main.cpp 单线程生命周期初始化（Commit `aa3aaba`） |
| **C4** | P2 | SDK 回调线程无锁读写录像状态 | ✅ **已闭环** | `m_isRecording` 原子化 `std::atomic<bool>`（Commit `d4b4973`） |
| **C5** | P1 | SDK 异常回调线程直接调 logout 造成死锁/崩溃 | ✅ **已闭环** | 改用 `QMetaObject::invokeMethod` 异步投递主线程执行（Commit `d4b4973`） |
| **C6** | P2 | stopStream 不停解码线程导致复活空转 | ✅ **已闭环** | stopStream 停解码线程；`ThreadSafeQueue` 增加 `reset()`，解码线程支持 `restart()`（Commit `d4b4973`） |
| **C7** | P2 | UI 线程直接调用 SDK 阻塞网络函数 | ✅ **已闭环** | 开流改用 `QtConcurrent::run` 异步执行（Commit `d4b4973`） |
| **C8** | P2 | 跟踪结果驱动的全量图像重绘 | ✅ **已闭环** | MainWindow 引入 `m_scaledPixmap` 写时复制缓存（Commit `d4b4973`） |
| **C9** | P2 | setTarget 在主线程同步初始化卡顿 | ✅ **已闭环** | ObjectTracker 增加 `initSlot`，在 Worker 线程异步完成并置 Initializing 态（Commit `d4b4973`） |
| **C10**| P3 | 框选期间 mouseMove 每次全量缩放 | ⏳ *建议项* | 目前 QPixmap 缓存已减轻压力，未来可做局部脏矩形优化 |
| **V1** | P1 | decodeFrame 未规范 packet 生命周期 | ✅ **已闭环** | 规范 av_packet 引用计数与 EAGAIN 重试（Commit `aa3aaba`） |
| **V2** | P1 | 录像 sws 上下文未随输入尺寸重建导致崩溃 | ✅ **已闭环** | 增加分辨率/格式变更检查并重建 sws 上下文（Commit `aa3aaba`） |
| **V3** | P1 | av_interleaved_write_frame 忽略返回值 | ✅ **已闭环** | 增加错误码校验与日志报警（Commit `aa3aaba`） |
| **V4** | P2 | 录像与预览线程各自解码同一路流 | ⏳ *架构项* | 预留未来多路分发架构，目前两路运行稳定 |
| **V5** | P2 | 解码器硬编码 H.264 | ✅ **已闭环** | 增加 H.265/HEVC 兼容支持（Commit `aa3aaba`） |
| **V6** | P2 | 解码队列满时静默丢帧 | ✅ **已闭环** | 增加丢帧计数器与警告输出（Commit `aa3aaba`） |
| **V7** | P2 | VideoDecoder 分辨率变化未触发重建 | ✅ **已闭环** | 动态监测输入宽高变化重建上下文（Commit `aa3aaba`） |
| **P1** | P0 | moveTo 无角度限位保护 | ✅ **已闭环** | 增加水平 0~360°、垂直 -90~90° 软限位与越界拦截（Commit `aa3aaba`） |
| **P2** | P0 | stop() 只发一次命令丢包失控 | ✅ **已闭环** | stop 命令连发 3 次确保云台绝对停稳（Commit `aa3aaba`） |
| **P3** | P1 | moveTo 延时发送命令无法被 stop 取消 | ✅ **已闭环** | stop 时立即取消待发送队列与延时定时器（Commit `aa3aaba`） |
| **P4** | P1 | 串口写返回值忽略无 flush | ✅ **已闭环** | 校验 write 字节数并调用 `flush()`（Commit `aa3aaba`） |
| **P5** | P1 | 串口打开失败静默 | ✅ **已闭环** | 增加界面警告与状态栏错误提示（Commit `aa3aaba`） |
| **P6** | P2 | onePushFocus 模式切换失败仍发命令 | ✅ **已闭环** | 切换模式成功后再投递聚焦，带 1s 超时保护（Commit `d4b4973`） |
| **P7** | P2 | ConfigManager 跨线程无锁访问 | ✅ **已闭环** | 增加 `mutable QMutex` 与 `QMutexLocker` 线程保护（Commit `d4b4973`） |
| **P8** | P2 | ConfigManager 存在明文密码及析构盲存 | ✅ **已闭环** | 密码走 QSettings 加密/UI输入，存盘绑定 `aboutToQuit`（Commit `d4b4973`） |
| **P9** | P2 | 日志文件按分钟命名无大小上限 | ✅ **已闭环** | 规范日志命名规则与单文件轮转（Commit `aa3aaba`） |
| **P10**| P2 | 连接握手 100ms 盲等 | ⏳ *联调项* | 纳入 Step 7 实机握手联调动态优化 |
| **P11**| P3 | PTZ 角度解析对异常值无校验 | ✅ **已闭环** | 增加 `std::isnan` 及合理范围检查（Commit `aa3aaba`） |
| **R1** | P1 | reference 边界 clamp 后宽高为负断言崩溃 | ✅ **已闭环** | DsstCore::makeRect clamp 后校验，非法返回空 Rect（Commit `aa3aaba`） |
| **R2** | P1 | reference update() 大量无谓 printf | ✅ **已闭环** | DsstCore 彻底清除所有 std::cout/printf，静默运行（Commit `aa3aaba`） |
| **R3** | P2 | reference 采样与变换冗余计算过大 | ✅ **已闭环** | 引入尺度隔帧可选开关与向量化优化（Commit `d4b4973`） |
| **R4** | P2 | reference 1-based 坐标系混淆 | ✅ **已闭环** | DsstCore 统一采用 0-based 半开区间标准几何（Commit `aa3aaba`） |
| **R5** | P3 | reference 依赖 gz0084 硬件私有库 | ℹ️ *参考库* | 生产代码（上位机）完全不依赖该库 |
| **R6** | P3 | reference 复数乘法逐像素低效遍历 | ✅ **已闭环** | DsstCore 改用 cv::mulSpectrums 硬件级加速（Commit `aa3aaba`） |
| **R-02**| P1 | 跟踪帧无背压控制导致处理队列积压 | ✅ **已闭环** | `m_isProcessingFrame` 原子标志位控制，单帧在途多余丢弃（Commit `a60184f`） |
| **R-03**| P1 | 云台控制方向依据脱靶量而非速度输出符号 | ✅ **已闭环** | 改用速度输出符号决定 Pelco-D 指令码（Commit `a60184f`） |
| **R-04**| P0 | SDK 回调无排空机制导致析构后野指针调用 | ✅ **已闭环** | `s_activeCallbacks` 原子引用计数 + 500ms 超时排空（Commit `ff21548`） |
| **R-05**| P1 | DataRecorder 内存无限增长且程序崩溃丢数据 | ✅ **已闭环** | 增量流式写盘 + 环形缓存 + unique_ptr 管理（Commit `a60184f`, `f1b5b98`） |
| **R-06**| P1 | 录像未写 header 即调 trailer 导致崩溃 | ✅ **已闭环** | `m_headerWritten` 显式标记与状态校验（Commit `a60184f`） |
| **R-08**| P1 | 抓图异步处理缺少重入保护 | ✅ **已闭环** | `m_processWatcher->isRunning()` 拦截重入请求（Commit `ff21548`） |
| **R-09**| P2 | 鼠标框选每次拖动全量缩放 1080P 画面 | ✅ **已闭环** | 复用 `m_scaledPixmap` 局部绘制（Commit `a60184f`） |
| **R-10**| P1 | 极小目标 minScale > maxScale 导致尺度发散 | ✅ **已闭环** | `if (m_minScale > m_maxScale) m_minScale = m_maxScale` 兜底（Commit `a60184f`） |

---

## 六、下一步工作计划与执行清单（Roadmap）

### 阶段一：Git 工作区清洁化（优先执行）
- [x] 处置 1：对 `_archive/` 历史归档执行 `git add -u _archive` 并提交删除（已在 `93c9f53` 闭环，移除了 29178 行冗余）。
- [x] 处置 2：对 `项目说明/方案/RGB_PTZ_Integrated_修复方案.md` 执行 `git rm` 提交删除（已在 `93c9f53` 闭环）。
- [x] 处置 3：执行 `git checkout reference/main_zt.cpp sdk/sdk.h` 消除 CRLF 无效变动（已闭环）。
- [x] 处置 4：将本 `checkpoint.md` 纳入 Git 跟踪并提交（已在 `93c9f53` 闭环）。
- [x] 处置 5：彻底移除已全部执行完毕的 `待执行方案/` 目录（内容已全量沉淀至 `checkpoint.md`）。
- [x] 处置 6：将 `项目说明/` 下关键素材与图纸入库，`sdk/` 下非 Windows 压缩包加入 `.gitignore`（已在 `845c3bc` 闭环）。
- [x] 处置 7：完成 R-02~R-10（8 项）功能与并发安全修复提交入库（Commit `ff21548`, `a60184f`）。
- [x] 处置 8：完成 P1 三项修复（统一默认值权威源、PTZ 速度溢出限幅、DataRecorder 智能指针，Commit `f1b5b98`）。
- [x] 处置 9：完成 P2 两项改进（.pro 补充 unix OpenCV 依赖路径、KF 改用 cv::Matx44f/Matx22f 标准矩阵计算，Commit `f7a0e0a`, `8a7006e`）。

### 阶段二：Step 7 实机联调验收（需连接相机与云台硬件）
- [ ] **7.1 可见光实时性**：25fps 预览稳定，无丢帧告警，跟踪框平滑无拉扯（验证 KF 预测与 PID）。
- [ ] **7.2 遮挡与恢复测试**：人工手挡目标 3 秒后移开，观察 2 秒内是否触发九宫格重检测并恢复跟踪。
- [ ] **7.3 PSR 响应曲线**：正常跟踪状态 PSR > 基线 75%，遮挡时跌入 < 50%，确认三态机切换精准。
- [ ] **7.4 目标尺度缩放**：相机变焦或目标前后移动时，跟踪框自适应放大/缩小（±5% 误差内）。
- [ ] **7.5 CPU 占用评估**：开启跟踪后整体 CPU 增幅 < 25%（i5/i7 平台）；多核调度正常（`cv::setNumThreads(2)`）。
- [ ] **7.6 24h 稳定性长测**：无内存泄漏（Memory Leak）、无句柄泄漏、无串口断连崩溃。
- [ ] **7.7 云台控制闭环**：验证 PID 输出角速度平滑度、死区防抖、软限位拦截与 stop 连发可靠性。

### 阶段三：基于 AMFT 自适应多特征融合的检测-跟踪协同闭环（方案 02，已 100% 闭环达标）
- [x] **02-1.1 数据结构扩展**：在 `ObjectTracker` 中增加 `AMFTEvaluator`，维护 $K=10$ 滑动窗口与三维特征 ($S_{conf}, S_{mot}, S_{bg}$) 计算。
- [x] **02-1.2 废除九宫格盲试**：从 [objecttracker.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/objecttracker.cpp) 彻底删除原有旧代码 `reDetect()` 及九宫格偏移搜索逻辑。
- [x] **02-1.3 接入 IDetector 接口**：引入 `IDetector` 指针，在 $R_t < \tau_t$ 时单次触发 YOLO 全局扫描。
- [x] **02-1.4 模板自适应重置与强制回归**：检测成功后以新框重置 DsstCore 模板，严格执行“触发后下一帧立即强制回归局部跟踪”铁律。
- [x] **02-1.5 变焦尺度动态校准**：联动 [LensManager](file:///d:/PyProjects/RGB_PTZ_Integrated/lensmanager.cpp) 变焦完成事件触发单次尺度纠偏。
- [x] **02-1.6 全景验收实跑**：通过 [tools/test_amft.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/tools/test_amft.cpp) 4 大维度闭环验收（100 帧全长调用率 0.00%，中心误差 2.22px，严重遮挡单次极速拉回）。

### 阶段四：YOLO 多模型受控领域微调、免重启热切换与抗虚警校验（方案 03，当前主力推进 P0）
- [x] **03-1 数据工程与格式标准化**：DUT Anti-UAV 7,800 份 XML 标注批量转换为标准 YOLO 格式（5,200 Train / 2,600 Val），重构 `train/images` 与 `val/images` 原生物理目录并保留兼容软链接，生成 [datasets/anti_uav.yaml](file:///d:/PyProjects/RGB_PTZ_Integrated/datasets/anti_uav.yaml)，经官方数据引擎全量扫描 100% 完好通过（0 损坏），生成 `.cache` 索引。
- [x] **03-2 Blackwell RTX 5070 硬件级训练环境与全链路验收**：在 [D:\miniconda3](file:///d:/miniconda3) 部署独立 Python 3.11 专属环境 `anti_uav_yolo`，配置 PyTorch 2.14.1 + CUDA 13.0 (`cu130`)，攻克 RTX 5070 Laptop GPU 缺少 Blackwell `sm_120` 内核镜像的底层硬件报错；编写 [scripts/train_uav_detector.py](file:///d:/PyProjects/RGB_PTZ_Integrated/scripts/train_uav_detector.py) 并通过 1-Epoch 端到端实跑，成功导出单输入纯静态 ONNX 模型 (`1x3x640x640`, 10.2 MB)。
- [x] **03-3 四款候选模型领域微调流水线 (Phase 1 100% 达成)**：
  - **完成四款模型同构受控微调**：`yolo11n` (88.00% / 5.5MB)、`yolo11s` (91.22% / 19.2MB)、`yolov8n` (87.42% / 6.5MB)、`yolov8s` (91.40% / 22.5MB) 全量 80 轮收敛；
  - **核心资产完整归档**：4 份最优权重入库 [weights/finetuned/](file:///d:/PyProjects/RGB_PTZ_Integrated/weights/finetuned/)，4 份部署级静态 ONNX 入库 [models/](file:///d:/PyProjects/RGB_PTZ_Integrated/models/)，UI 动态热加载已全面支持；
- [ ] **03-3-B 第二批次从零重新训练流水线 (Phase 2: Train from Scratch，当前首要任务)**：
  - 启动命令 `& "D:\miniconda3\envs\anti_uav_yolo\python.exe" scripts/train_uav_detector.py --model all --mode scratch --epochs 80 --batch 16`；
  - 依次纯基于 `*.yaml` 拓扑从零随机初始化重训 4 款模型，权重归档至 `weights/scratch/`，导出 `models/*_uav_scratch.onnx`，为硕士论文完成“微调 vs 从零”全维度消融对照；
- [ ] **03-4 硕士论文 2×2×2 消融实验多维打点与选型论证**：在 2,600 张验证集上统一打点，量化核算 mAP@50、mAP@50-95、弱小目标召回率与推理时延，建立“代际(v8 vs 11) × 尺度(Nano vs Small) × 训练策略(微调 vs 从零)”全景学术三线大表。
- [ ] **03-5 C++ 多规格模型免重启热切换**：`OnnxDetector` 互斥锁加固与界面下拉菜单无缝实时重载。
- [ ] **03-6 VaL_CNN 抗虚警二级校验**：放宽初筛低门限（0.2）同时抑制背景虚警。

### 阶段五：基于轻量深度网络的可见光单目标跟踪（方案 04，已纳编）
- [ ] **04-1 LightTrack / RepSOT 结构选型与静态 ONNX 导出**：解耦模板与搜索分支，纯卷积静态算子。
- [ ] **04-2 ISotTracker 接口与 OnnxSotTracker DirectML 运行时构建**：面向对象接口设计，单帧推理 $\le 4.0\text{ ms}$。
- [ ] **04-3 局部快速前处理与 RGB 3通道高维色彩特征提取**：充分利用可见光色彩辨识度，攻克地表杂波与形变漂移。
- [ ] **04-4 离线工具链闭环评测**：编写 `test_sot.cpp` 与标准数据集对拍，量化对比 DSST。
- [ ] **04-5 ObjectTracker 内核热切换接入与 AMFT 协同状态机闭环**：置信度无缝映射，异常单次唤醒检测重捕。

### 阶段六：光电跟踪系统 RK3588 边缘端量化与工程移植实操方案（方案 05，终极规划）
- [ ] **05-1 真实长焦工况校准集与全系统模型 INT8 量化**：YOLO 与 LightTrack 统一定点量化为 RKNN。
- [ ] **05-2 嵌入式 aarch64 交叉编译与 librknnrt.so 运行时构建**：业务逻辑纯 C++ 100% 复用。
- [ ] **05-3 NPU 多核调度亲和性与零拷贝 DMA-BUF 缓存接入**：SOT 独占 Core 0，YOLO 并发双核。
- [ ] **05-4 RK3588 真机整机外场联调与闭环延迟验收**：伺服端到端延迟 $\le 30\text{ ms}$。

---

## 七、最近检查点记录（Latest Checkpoint Log）

> **归档指引**：历史检查点（#01 ~ #32）已分流至独立小文件 [checkpoints/archive/checkpoint_archive_01_30.md](file:///d:/PyProjects/RGB_PTZ_Integrated/checkpoints/archive/checkpoint_archive_01_30.md) 与 [checkpoints/archive/checkpoint_archive_31_32.md](file:///d:/PyProjects/RGB_PTZ_Integrated/checkpoints/archive/checkpoint_archive_31_32.md)。本文件遵循轻量原则仅保留当前最新检查点，全量历史演进由 Git 提交记录客观承载。

### Checkpoint 2026-10-31 #33
- **记录人**：Antigravity (Gemini 3.8 Flash)
- **类型**：反无人机前沿文献 (UAV-DETR) 深度剖析入库、横向对比三线大表构建与弱小目标损失函数探索方案纳编
- **核心内容**：
  1. **前沿文献素材系统化收录与对比矩阵构建**：
     - 在 [课题/论文碎片材料/反无人机目标检测前沿文献引用与对比.md](file:///d:/PyProjects/RGB_PTZ_Integrated/课题/论文碎片材料/反无人机目标检测前沿文献引用与对比.md) 建立专题材料；
     - 系统收录 2026 年最新前沿成果 **UAV-DETR (arXiv:2603.22841)** 标准 GB/T 7714 与 BibTeX 引用；
     - 深度剖析其 WTConv 小波骨干、SWSA-IFI 编码器、ECFRFN 颈部及 Inner-CIoU + NWD 混合损失数学机理；
     - 建立本系统 YOLO11/YOLOv8 与 UAV-DETR 在 DUT Anti-UAV 上的精度（mAP@50 / mAP@50:95）、召回率、推理时延（2.7ms vs 16.5ms）、伺服裕度、DirectML 静态 ONNX 及 RK3588 NPU 部署友好度的学术横评三线大表；
     - 明确该论文在硕士论文第 1、2、3、5 章的引用与论证切入点；
  2. **弱小目标边界框损失函数探索池纳编方案 03 与 06**：
     - 在 [待执行方案/03_YOLO多模型(v8n_11n_11s)性能横评与动态切换实操方案.md](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/03_YOLO多模型(v8n_11n_11s)性能横评与动态切换实操方案.md) 增补第七章“课题探索储备：弱小目标边界框损失函数选型与对比规划”；
     - 针对 DUT Anti-UAV 80.9% 弱小目标导致的 IoU 剧烈抖动与梯度震荡痛点，确立候选损失函数池（基准 CIoU、Inner-CIoU、NWD、Inner-CIoU+NWD 混合损失）；
     - 确立“探索阶段遵循实测驱动选型，哪个指标与弱小目标召回率最好就用哪个”的执行原则，且明确损失函数零额外推理耗时、不影响 2.7ms 极速推理与部署基线；
     - 在 [待执行方案/06_后续创新规划.md](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/06_后续创新规划.md) 增列方案 11 储备分支；
  3. **小文件检查点与文献清单轻量维护**：
     - Checkpoint #31 ~ #32 完整沉淀归档至 [checkpoints/archive/checkpoint_archive_31_32.md](file:///d:/PyProjects/RGB_PTZ_Integrated/checkpoints/archive/checkpoint_archive_31_32.md)；
     - [课题/论文碎片材料/README.md](file:///d:/PyProjects/RGB_PTZ_Integrated/课题/论文碎片材料/README.md) 同步登记前沿文献碎片材料编号 03。


