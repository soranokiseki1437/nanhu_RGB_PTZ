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
1. **进场**：开发前先阅读 `checkpoint.md`，获取最新里程碑、未决问题与工作区脏状态。
2. **核实**：更新前必须执行 `git status`、编译检查及相关测试，禁止凭记忆记录。
3. **沉淀**：将新的检查点追加到「七、历史检查点记录」中，并刷新「二、当前项目状态快照」。

---

## 二、当前项目状态快照（2026-10-26 最新）

| 维度 | 当前状态 | 详细说明 |
| :--- | :---: | :--- |
| **代码分支** | `main` | 领先 origin/main 5 commits（含方案 01 阶段一验收、方案 02 AMFT 闭环落地） |
| **最新进展** | 🟢 **方案 02（AMFT 协同实操方案）100% 全面闭环验收** | 彻底剔除旧版九宫格盲试，构建“三维可靠度评估 ($S_{conf}, S_{mot}, S_{bg}$) + 滑动窗口动态阈值 ($\tau_t$) + 零频待机单次唤醒 + 下一帧强制回归”闭环；4/4 闭环验收项全部 PASS |
| **编译状态** | 🟢 **通过（0 告警 0 错误）** | Release 构建已通过，Qt 6.12.0 / C++17 / MinGW 13.1 64-bit |
| **GPU 推理基准** | 🟢 **154.4 FPS / 3.06ms** | NVIDIA RTX 5070 GPU：yolov8n 单帧纯推理 3.06ms (19.8x CPU 加速比)，端到端 6.48ms；yolo11s 单帧纯推理 4.99ms (21.2x CPU 加速比) |
| **传统算法与伺服** | 🟢 **Step 1~6 完备** | DsstCore 纯算法、FFmpeg 解码（av_frame_move_ref 零空帧）、PTZ 30ms 节拍保护与双向俯仰映射完全稳定运行 |
| **待执行核心任务** | 🟡 **方案 03：YOLO 多模型(v8n/11n/11s)横评、动态热切换与抗虚警增强** | 吸收顶刊《DeTrAck》成果，实现多规格模型免重启热切换与低置信度防虚警增强 |
| **Git 工作区状态** | 🟢 **良好** | 方案 02 生产代码及全景验收工具 `tools/test_amft.cpp` 就绪，编译 0 报错 |

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
 M tools/test_detector.cpp (阶段一全景质量体检与闭环验收工具扩展)
?? test_detection_vis.png (阶段一可视化测试输出图元，建议由 .gitignore 忽略或归档)
```

### 4.2 诊断分析与建议处置方案

| 现象分类 | 产生根因分析 | 推荐处置方案 | 对应 Git 命令 |
| :--- | :--- | :--- | :--- |
| **1. `tools/test_detector.cpp` 修改** | 阶段一增加 5 大维度全景质量验收代码（加速比、Ground Truth IoU 对齐、长宽比不变性、内存泄漏核验）。 | **提交入库**。作为方案 01 阶段一的核心验收工件。 | `git add tools/test_detector.cpp`<br>`git commit -m "feat(AI): 目标检测阶段一全景质量体检工具(5/5)实跑通过"` |
| **2. `test_detection_vis.png`** | 验收工具生成的实机对拍预测图元文件。 | **建议在 `.gitignore` 中忽略临时图片**。 | `echo "*.png" >> .gitignore` |
| **3. 分支状态（领先 origin/main 4 commits）** | 本地已提交 `7429ddb`、`1d0c846`、`46ede1e` 等核心功能与规划提交。 | **推送远端**。待远程连接就绪后执行推送。 | `git push origin main` |

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

### 阶段四：YOLO 多模型免重启热切换与抗虚警校验（方案 03，当前主力推进 P0）
- [ ] **03-1 多规格模型热切换**：v8n/s 与 11n/s 运行时免重启切换架构。
- [ ] **03-2 VaL_CNN 抗虚警二级校验**：放宽低置信度（0.2）同时抑制背景虚警。

### 阶段五：YOLO 架构改良、领域微调与量化适配（方案 04，已纳编）
- [ ] **04-1 P2 极小目标检测头集成**：增加 160x160 特征图检测头，攻克 50m~200m 远距弱小目标（<20像素）漏检痛点。
- [ ] **04-2 专有光电数据集迁移微调**：基于 Anti-UAV / VisDrone 强化飞行器与红外弱小目标特征。
- [ ] **04-3 EMA/BiFPN 注意力机制引入**：增强天空杂波与树林晃动下的目标信噪比，降低误检虚警。
- [ ] **04-4 离线工具链闭环评测**：导出标准 ONNX，由 `test_detector.exe` 免重启热评测延迟与召回率。
- [ ] **04-5 RK3588 INT8 量化感知微调 (QAT)**：约束算子中立性，保障嵌入式端量化无损部署。

### 阶段六：RK3588 边缘端 NPU (librknnrt) 移植落地方案（方案 05，远期规划）


---

## 七、历史检查点记录（Checkpoint Log）

### Checkpoint 2026-10-26 #9
- **记录人**：Antigravity (Gemini 3.8 Flash)
- **类型**：方案 02（基于 AMFT 自适应多特征融合的协同跟踪）全面落地与 4/4 闭环验收通过
- **核心内容**：
  1. **AMFT 核心评估器落地 ([amftevaluator.h/.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/amftevaluator.cpp))**：
     - 构建三维特征评估：$S_{conf}$（归一化 PSR）、$S_{mot}$（位移平滑度与位置 IoU 连续性）、$S_{bg}$（目标与背景上下文 16-bin 灰度直方图 Bhattacharyya 距离）；
     - 加权综合可靠度：$R_t = 0.45 S_{conf} + 0.35 S_{mot} + 0.20 S_{bg}$；
     - 滑动窗口动态阈值：维护 $K=10$ 历史得分标准差 $\sigma_t$，计算 $\tau_t = \text{std::clamp}(\tau_0 - \alpha \sigma_t, 0.35, 0.75)$，稳态时 $\tau_t \approx 0.65$。
  2. **彻底淘汰九宫格盲试与四态时序状态机**：
     - 从 [objecttracker.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/objecttracker.cpp) 彻底清除旧版 `reDetect()` 及九宫格偏移检测；
     - 确立 `AMFTTrackingState` 四态闭环（`TRACK_NORMAL` ──► `TRACK_OCCLUDED` ──► `TRACK_REACQUIRED` ──► `TRACK_LOST`）；
     - 接入 [IDetector](file:///d:/PyProjects/RGB_PTZ_Integrated/IDetector.h)，常态处于 0 FPS 休眠，仅在 $R_t < \tau_t$ 时单次唤醒 YOLO 全局检索；
     - 严格践行“下一帧强制回归局部跟踪”铁律：命中后立即重置 DsstCore 模板，下一帧强制回归 `TRACK_NORMAL`，杜绝连续检测。
  3. **光学变焦与跟踪尺度动态校准闭环**：
     - [LensManager](file:///d:/PyProjects/RGB_PTZ_Integrated/lensmanager.cpp) 变焦命令结束（`stop=true`）时发射 `zoomFinished()` 信号；
     - [MainWindow](file:///d:/PyProjects/RGB_PTZ_Integrated/mainwindow.cpp) 联动 [TrackingController](file:///d:/PyProjects/RGB_PTZ_Integrated/trackingcontroller.cpp) 下发 `triggerScaleCalibration()`，实现变焦后目标尺度自动校准纠偏。
  4. **全景闭环验收工具验证 ([tools/test_amft.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/tools/test_amft.cpp))**：
     - **体检 1（平稳巡航态局部跟踪与 0-FPS YOLO 休眠）**：60 帧连续跟踪单帧耗时 8.19ms（折合 122 FPS），$R_t=0.841 \ge \tau_t=0.640$，中心误差 1.41px，误唤醒次数 0 次（PASS）；
     - **体检 2（瞬时干扰平滑压制）**：注入 1 帧反光强噪声，运动连续性 $S_{mot}=0.11$ 成功拉低动态阈值至 0.536，保持 NORMAL 未误触发检测（PASS）；
     - **体检 3（突发严重遮挡单次唤醒与铁律验证）**：人为 4 帧纯黑遮挡，成功进入 OCCLUDED；第 67 帧目标露出单次重捕成功（检测位置 (146, 119.5) vs GT (145, 121)），重置模板，第 68 帧严格切回 TRACK_NORMAL（PASS）；
     - **体检 4（100 帧全长闭环统计）**：总检测调用率 0.00%（目标 $\le 5\%$），平均定位误差 2.22px（目标 $\le 3\text{ px}$）（PASS）。
  5. **工程构建状态**：Release 编译 **0 告警 0 错误**，二进制与所有依赖 DLL 完整自动打包分发至 `build/release/`。
  6. **下一步计划**：进入方案 03，推进 YOLO 多模型(v8n/11n/11s)动态热切换与 VaL_CNN 抗虚警校验。

### Checkpoint 2026-10-26 #8
- **记录人**：Antigravity (Gemini 3.8 Flash)
- **类型**：吸收海军工程大学顶会 AMFT 理论，方案排期重大升级（提拔方案 02 优先执行）
- **核心内容**：
  1. **深度吸收顶会论文《AMFT》精髓**：全面吸收海军工程大学 & 空军预警学院 2025 IEEE 顶会论文《Adaptive Multi-Feature Fusion-Based UAV Detection and Tracking Collaboration Framework Trigger Mechanism》，确立三大核心数学模型：
     - **三维可靠度评估**：$R_t = \omega_1 S_{conf} + \omega_2 S_{mot} + \omega_3 S_{bg}$（结合 DsstCore 峰旁比、运动平滑度、目标/背景 FHOG 余弦差异），彻底解决单指标瞬时波动误触发痛点；
     - **滑动窗口动态阈值**：$\tau_t = \tau_0 - \alpha \sigma_t$（$K=10$ 窗口标准差），实现平稳巡航不误报、突发遮挡快速响应；
     - **强制回归局部铁律**：全局检测单次触发后，下一帧强制切回局部轻量跟踪，彻底杜绝连续大模型推理。
  2. **方案库次序调整**：
     - 正式编写并发布 [待执行方案/02_基于自适应多特征融合(AMFT)的光电云台检测-跟踪协同实操方案.md](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/02_基于自适应多特征融合(AMFT)的光电云台检测-跟踪协同实操方案.md)，将其提拔为最高优先级 P0 推进；
     - 原多模型热切换顺延为方案 03（[03_YOLO多模型(v8n_11n_11s)性能横评与动态切换实操方案.md](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/03_YOLO多模型(v8n_11n_11s)性能横评与动态切换实操方案.md)）；
     - 原 RK3588 移植顺延为方案 05；同步更新 [00_方案总览与执行推进管理规范.md](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/00_方案总览与执行推进管理规范.md)。
  3. **下一步计划**：进入方案 02 阶段一，在 [ObjectTracker](file:///d:/PyProjects/RGB_PTZ_Integrated/objecttracker.cpp) 中构建 `AMFTEvaluator` 并废除九宫格盲试。

### Checkpoint 2026-10-26 #7
- **记录人**：Antigravity (Gemini 3.8 Flash)
- **类型**：架构重大纠偏与方案 05（复合目标跟踪与按需检测协同方案）正式立项
- **核心内容**：
  1. **技术路线纠偏**：采纳用户精准工程洞见，彻底推翻“定期每隔数帧跑一次大模型检测”的算力浪费模式，确立“局部跟踪为主（Local Tracking 30 FPS）、全局深度检测零频休眠（Zero-FPS Idle）、异常时纯事件驱动单次唤醒（Event-Driven Single-Shot）”的工业级光电跟踪范式。
  2. **正式立项《方案 05》**：创建 [待执行方案/05_光电云台复合目标跟踪算法演进与状态机协同方案.md](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/05_光电云台复合目标跟踪算法演进与状态机协同方案.md)，设计四态协同状态机（NORMAL 稳态 ──► OCCLUDED 遮挡 ──► LOST 丢失 ──► RECOVERED 恢复），确立将 `ObjectTracker` 中过时的九宫格 `reDetect()` 盲试替换为毫秒级单次 YOLO 全局扫描。
  3. **方案总览与跨方案联动更新**：同步更新 [00_方案总览与执行推进管理规范.md](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/00_方案总览与执行推进管理规范.md) 与 [01_Windows平台深度学习目标检测与跟踪集成实操方案.md](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/01_Windows平台深度学习目标检测与跟踪集成实操方案.md)，明确 SOT 强锁定与 MOT 态势巡逻的分工协同。
  4. **下一步计划**：进入方案 05 阶段一，在 [ObjectTracker](file:///d:/PyProjects/RGB_PTZ_Integrated/objecttracker.cpp) 中改造遮挡三态机，接入 [OnnxDetector](file:///d:/PyProjects/RGB_PTZ_Integrated/onnxdetector.cpp) 单次重检接口。

### Checkpoint 2026-10-26 #6
- **记录人**：Antigravity (Gemini 3.8 Flash)
- **类型**：方案 01 阶段一全景质量体检（5/5项）闭环验收通过，阶段二（ByteTrack 跟踪器集成）就绪
- **核心内容**：
  1. **全景质量体检 5 项闭环验收工具落地 ([test_detector.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/tools/test_detector.cpp))**：
     - **体检 1（推理速度与加速比）**：RTX 5070 DirectML GPU 纯推理耗时 **3.06 ms**，单帧端到端总耗时 **6.48 ms**（**154.4 FPS**）；CPU 降级耗时 60.70 ms；GPU 硬件加速比 **19.81x**（PASS，指标 $\le 15\text{ ms}$）。
     - **体检 2（真实标注 Ground Truth 坐标对齐）**：读取 `testdata/sequences/01/` 第一帧实测，预测目标与 Ground Truth 标注重合度 **IoU 达 72.7%**，中心点位置误差仅 **1.50 像素**（PASS，指标 IoU $\ge 50\%$ 且误差 $\le 5\text{ px}$）。
     - **体检 3（图元可视化落盘）**：生成直观标注图 `test_detection_vis.png`，绿色真实框与红色预测框对齐严密（PASS）。
     - **体检 4（极端长宽比与边界条件自适应）**：验证 1080P（16:9 横屏，上下黑边 padH=140）、480x640（3:4 竖屏，左右黑边 padW=80）Letterbox 缩放及空帧防御性拦截（PASS）。
     - **体检 5（长稳运行与内存泄漏排查）**：连续 200 轮单帧推理测试，进程物理工作集内存净变化为 **-872 KB**（无任何增长），彻底排除句柄与张量内存泄漏隐患（PASS，指标增量 $< 2\text{ MB}$）。
  2. **方案 01 阶段一正式进入 100% 验收达标闭环状态**。
  3. **下一步计划**：
     - 正式启动方案 01 阶段二：引入纯 C++ ByteTrack 多目标航迹关联算法（`ByteTracker`、`STrack`、`KalmanFilter`），打通多目标航迹稳定管理与连续 ID 分配。

### Checkpoint 2026-10-23 #5
- **记录人**：Antigravity (Gemini 3.8 Flash)
- **类型**：方案 01 阶段一里程碑达成（ONNX Runtime DirectML GPU 硬件加速 + 纯 C++ NMS 管道闭环）
- **核心内容**：
  1. **跨平台解耦抽象接口落地**：
     - 创建纯虚基类 [IDetector.h](file:///d:/PyProjects/RGB_PTZ_Integrated/IDetector.h)，统一规范 `DetectionBox`、`DetectorConfig` 与推理生命周期接口，为未来面向 RK3588 NPU (librknnrt) 扩展奠定零冲突架构基础。
  2. **纯 C++ 独立后处理与 Letterbox 引擎**：
     - 实现 [yolopostprocessor.h/.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/yolopostprocessor.cpp)，包含保持宽高比 Letterbox 居中前处理、标准 YOLOv8/v11 静态张量（$1 \times (4+80) \times 8400$）解码、坐标逆映射及纯 C++ class-specific Non-Maximum Suppression (NMS)。
  3. **DirectML GPU 硬件加速推理引擎 [OnnxDetector](file:///d:/PyProjects/RGB_PTZ_Integrated/onnxdetector.cpp)**：
     - 基于微软官方 ONNX Runtime C-API，避开 MinGW 与 MSVC 的 C++ ABI 冲突，优先启用 DirectML 调用宿主机 **NVIDIA GeForce RTX 5070 GPU** 进行硬件加速；在 GPU 不可用时自动降级 CPU 多线程执行。
  4. **工程构建与 DLL 自动部署**：
     - [RGB_PTZ_Integrated.pro](file:///d:/PyProjects/RGB_PTZ_Integrated/RGB_PTZ_Integrated.pro) 补充 ONNX Runtime 与 DirectML 依赖路径，并在 `QMAKE_POST_LINK` 自动分发 `onnxruntime.dll`、`DirectML.dll`、`onnxruntime_providers_shared.dll` 至输出目录。Release 构建 **0 Error / 0 Warning**。
  5. **实机性能横评与基准测试 ([test_detector.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/tools/test_detector.cpp))**：
     - **YOLOv8n (静态 640x640)**：
       - RTX 5070 DirectML 纯推理耗时：**3.08 ms**（含前后处理总耗时 **6.78 ms**，折合 **147.5 FPS**）；
       - CPU 多线程推理耗时：**47.59 ms**（20.0 FPS）；
       - GPU 硬件加速比：**15.45x**，[通过 PASS] 方案验收指标（$\le 15\text{ ms}$）。
     - **YOLO11s (静态 640x640)**：
       - RTX 5070 DirectML 纯推理耗时：**4.99 ms**（含前后处理总耗时 **7.69 ms**，折合 **130.0 FPS**）；
       - CPU 多线程推理耗时：**105.84 ms**（9.2 FPS）；
       - GPU 硬件加速比：**21.21x**，[通过 PASS] 方案验收指标。
  6. **下一步计划**：
     - 推进方案 01 阶段二：引入纯 C++ ByteTrack 多目标航迹关联算法，实现多目标连续稳定跟踪。

### Checkpoint 2026-10-13 (HEAD @ `fb11105`)
- **记录人**：soranokiseki1437
- **类型**：视频解码空帧修复、云台通信节拍与双向俯仰映射修正
- **核心内容**：
  1. **实时解码空帧根因修复 (Commit `e6c16c1`)**：
     - `videodecoder`: 修复 `avcodec_receive_frame` 循环入口未留存帧、EAGAIN 导致 sws 全空帧的 bug，改用 `av_frame_move_ref` 留存最后一帧，彻底解决界面无画面问题。
     - `mainwindow`: 方向键按住增加 200ms 周期重发定时器，维持云台连续平滑转动。
     - `logmanager`: 控制台输出改用 `toLocal8Bit`，彻底消除 Windows 终端中文乱码。
  2. **云台通信与映射严密修正 (Commit `fb11105`)**：
     - 修复云台定位命令帧间隔不足被固件丢弃问题，队列保护节拍由 10ms 提升至 30ms。
     - 严格对照机芯与 Pelco-D 手册修正俯仰方向映射（实现命令下发与角度回传双向一致）。
     - 速度默认值由 0.5 统一修正为 1.0。

### Checkpoint 2026-09-28 #4 (HEAD @ `f1b5b98`)
- **记录人**：Gemini 3.8 Flash
- **类型**：P0 缺陷提交 + P1 代码修复与权威源统一
- **核心内容**：
  1. **SDK 回调签名与安全排空 (P0, Commit `ff21548`)**：
     - 回调函数签名统一加上 `UNIV_CALLBACK` 调用约定，消除 ABI 崩溃风险（R-04）。
     - 增加 `s_activeCallbacks` 原子引用计数并在 `~DeviceManager` 中设置 500ms 超时排空机制，杜绝析构后野指针崩溃（R-04）。
     - CaptureManager 增加异步任务重入保护（R-08）。
  2. **功能缺陷修复入库 (P0, Commit `a60184f`)**：
     - R-02：TrackingController 增加 `m_isProcessingFrame` 标志位实现背压控制，防止多帧在队列堆积造成延迟。
     - R-03：PTZ 转向根据速度输出符号（而非脱靶量）决定，保证负反馈闭环正确性。
     - R-05：DataRecorder 改造为流式增量写入 CSV + 内存环形缓冲（上限 3600 条），防止爆内存及崩溃丢数据。
     - R-06：RecordThread 增加 `m_headerWritten` 标志，防止未成功写头时调用 trailer 导致 FFmpeg 崩溃。
     - R-09：鼠标框选拖动复用 `m_scaledPixmap` 局部绘制，消除高频 1080P 缩放卡顿。
     - R-10：DsstCore 增加极小目标尺度下溢兜底 `m_minScale = m_maxScale`。
  3. **未跟踪文件与文档素材整理 (P0, Commit `845c3bc`)**：
     - 将 `sdk/univisionsdk-0.3.2/` 补充进 `.gitignore`。
     - 将系统说明书图纸素材、参考资料、待执行方案全量入库并推送到远程仓库。
  4. **P1 修复与参数权威源统一 (P1, Commit `f1b5b98`)**：
     - 统一以 ConfigManager 为参数唯一权威源，TrackingController 构造参数设为占位值，在 `initTrackingModule()` 阶段从 ConfigManager 读取并同步到 UI 控件与 TrackingController，同步更新 `README.md`。
     - `onPtzControlDelta()` 中对速度输出增加 `qBound(0, speed, 63)` 安全限幅，防止有符号负数强转 uint8_t 造成云台全速失控。
     - `DataRecorder` 内部 `m_csvFile` 和 `m_csvStream` 升级为 `std::unique_ptr` 自动管理；并在 `aboutToQuit` 中显式调用 `stopRecording()` 确保异常退出时文件正常关闭。
  5. **P2 优化推进 (P2, Commit `f7a0e0a`, `8a7006e`)**：
     - `RGB_PTZ_Integrated.pro` 增加 `unix:` 下 OpenCV 依赖分支，提升跨平台工程通用性。
     - `ObjectTracker` 内 Kalman Filter 状态与协方差矩阵全面改用 `cv::Matx44f`、`cv::Matx22f`、`cv::Vec4f`、`cv::Vec2f` 标准矩阵代数写法，替代原有 120 余行易错展开循环，零堆内存分配且可维护性大幅提升。
  6. **下一步计划**：
     - 推进 Step 7 实机联调验收。

### Checkpoint 2026-09-21 #3
- **记录人**：Antigravity AI
- **类型**：目录精简与方案清理
- **核心内容**：
  1. 彻底移除 `待执行方案/` 目录（3 个方案文件中的全部代码改造均已合入，45 项体检闭环与 Step 7 验收标准已由 `checkpoint.md` 全量继承）。
  2. 根目录恢复至纯净状态，不再保留过时的“待执行”语义目录。

### Checkpoint 2026-09-21 #2 (HEAD @ `93c9f53`)
- **记录人**：Antigravity AI
- **类型**：Git 工作区深度清洁
- **核心内容**：
  1. 彻底清空工作区中 68 个被删历史归档文件（`_archive/`，减少 2.9 万行无用代码跟踪）与旧方案文件。
  2. 还原 `reference/main_zt.cpp` 与 `sdk/sdk.h` 的 CRLF 换行符变动，消除无用 Diff。
  3. 将 `checkpoint.md` 正式加入版本库管理并提交。
  4. 评估 `待执行方案/` 目录：已完成历史使命，准备清理。

### Checkpoint 2026-09-21 #1 (HEAD @ `d4b4973`)
- **记录人**：Antigravity AI
- **类型**：机制建立 & 里程碑巩固
- **核心内容**：
  1. 建立工程根目录 `checkpoint.md` 标准机制与操作规范。
  2. 梳理并核实 Milestone 2（DSST 全面接线 Step 4~6、线程安全 C1~C7、绘制缓存 C8/C9、镜头时序 P6、配置加锁 P7/P8）的代码交付。
  3. 全面诊断 Git 工作区状态：排查出 68 个被删历史归档文件、CRLF 虚假差异及未跟踪文档材料，并制定清晰的清理方案。
  4. 汇总 45 项体检清单闭环状态，当前闭环率达 91%，项目具备开展 Step 7 实机联调条件。
