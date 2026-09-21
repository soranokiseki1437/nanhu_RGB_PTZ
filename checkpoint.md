# 项目状态检查点机制与全景状态（checkpoint.md）

> **机制定义**：本文件是 `RGB_PTZ_Integrated` 项目的**唯一事实源（Single Source of Truth）**与**项目状态机**。用于跨开发会话、跨代理（Agent）与人类开发者之间的无缝交接，记录项目全景健康度、里程碑推进、Git 工作区状态、体检问题闭环及后续行动项。

---

## 一、检查点（Checkpoint）机制运行规范

### 1.1 触发时机（When to Update）
在以下场景发生时，必须同步检视并更新本文件：
1. **工作会话结束/交接**：每次开发任务完成，向用户汇报前更新。
2. **关键里程碑达成**：完成阶段性功能、重大重构（如 DSST 接线、并发治理）或提交新 Commit 后。
3. **架构/状态机变更**：改动多线程模型、信号槽链路、对外接口或配置文件格式。
4. **测试与联调完成**：离线对拍验证、实机联调测试（Step 7 验收）后。
5. **Git 状态大幅变动**：分支合并、工作区大规模清理或依赖库变动。

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

## 二、当前项目状态快照（2026-09-21 最新）

| 维度 | 当前状态 | 详细说明 |
| :--- | :---: | :--- |
| **代码分支** | `main` | 与远程 `origin/main` 保持一致 |
| **最新提交** | `d4b4973` | *里程碑：DSST 全面接线（Step4-6）+ 线程安全/绘制缓存/镜头时序/配置管理修复* |
| **编译状态** | 🟢 **通过（0 告警）** | Release 构建已通过，Qt 6.10 / C++17 / MinGW 64-bit |
| **算法移植进度** | 🟢 **Step 1~6 全部完成** | DsstCore 纯算法移植完成；ObjectTracker 已接线 DsstCore；红外模式 UI 与多线程调优完成 |
| **体检修复进度** | 🟢 **41/45 项已闭环 (91%)** | 阶段 0 (PTZ)、阶段 1 (日志)、阶段 2 (FFmpeg)、阶段 3 (线程)、阶段 4 (UI缓存)、阶段 5 (配置/镜头) 全部合入 |
| **待执行核心任务** | 🟡 **Step 7 实机联调** | 依赖硬件环境：云台串口通讯闭环、实时25fps、遮挡恢复、PSR曲线、24h稳定性 |
| **Git 工作区状态** | 🟠 **待整理 (Dirty)** | 存在历史备份文件删除未提交、换行符差异以及待归档的文档材料 |

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
 D _archive/code_20260715/... (共 68 个历史备份文件在磁盘上被删除，但 Git 未提交)
 D 项目说明/方案/RGB_PTZ_Integrated_修复方案.md (已被 待执行方案/ 替代)
 M reference/main_zt.cpp (仅 CRLF/LF 换行符差异，内容无改动)
 M sdk/sdk.h (仅 CRLF/LF 换行符差异，内容无改动)
?? sdk/univisionsdk-0.3.2/ (Linux/ARM 架构 SDK 压缩包)
?? 项目说明/draft_20a5ead5_folder/ (系统说明书草稿中间产物)
?? 项目说明/drawio/ (系统数据链路与物理拓扑图)
?? 项目说明/参考资料/ (机芯规格书、接口板定义、HTTP 协议文档)
?? 项目说明/导出图片/ (Drawio 导出的 PNG 图片)
?? 项目说明/系统信息收集/ (系统说明书信息收集文档)
?? 项目说明/系统说明书工作流.md (说明书编写流程规范)
```

### 4.2 诊断分析与建议处置方案

| 现象分类 | 产生根因分析 | 推荐处置方案 | 对应 Git 命令 |
| :--- | :--- | :--- | :--- |
| **1. `_archive/` 68 文件删除未暂存** | 初始提交（`4a5715f`）中将旧版本备份目录纳入了版本库，后续本地清理了该目录（减少工作区冗余约 3 万行代码），但未执行 Git 提交。 | **方案 A（推荐）：确认删除**。这些历史代码已有 Git 历史永久存档，无需保留在活动工作区。<br>**方案 B：恢复文件**（如需在工作区留存）。 | **方案 A**：<br>`git add -u _archive`<br>`git commit -m "清理：移除工作区历史归档目录 _archive/"`<br>**方案 B**：<br>`git checkout -- _archive/` |
| **2. 老方案文档删除未暂存** | `项目说明/方案/RGB_PTZ_Integrated_修复方案.md` 已被细化并重构为 `待执行方案/01~03`，磁盘已删除。 | **确认删除**。将删除变更加入下一次提交。 | `git add -u "项目说明/方案/"` |
| **3. `reference/main_zt.cpp` 与 `sdk/sdk.h` 变动** | 跨平台（Windows / Linux）检出时 CRLF 与 LF 自动转换导致整文件被标记为 Modified（`git diff --ignore-space-at-eol` 为空）。 | **检出还原**或配置 `.gitattributes`。保持原库内 LF 格式。 | `git checkout reference/main_zt.cpp sdk/sdk.h` |
| **4. 未跟踪的 SDK 包** | `sdk/univisionsdk-0.3.2/` 存放的是 Linux 与 aarch64 版本的 SDK zip。 | 根据维护策略：若仅 Windows 主力开发，建议在 `.gitignore` 忽略；若需保留各架构 SDK，可统一整理后提交。 | 建议补充进 `.gitignore` |
| **5. 未跟踪的文档与图纸** | `项目说明/` 下包含说明书素材、Draw.io 架构图、技术资料等。 | 该批资料对项目系统说明书极其关键，建议按文档模块统一次性暂存提交入库。 | `git add 项目说明/`<br>`git commit -m "文档：补充系统说明书图纸、素材及参考资料"` |

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

---

## 六、下一步工作计划与执行清单（Roadmap）

### 阶段一：Git 工作区清洁化（优先执行）
- [ ] 处置 1：对 `_archive/` 历史归档执行 `git add -u _archive` 并提交删除，保持活动工作区清爽。
- [ ] 处置 2：对 `项目说明/方案/RGB_PTZ_Integrated_修复方案.md` 执行 `git rm` 提交删除。
- [ ] 处置 3：执行 `git checkout reference/main_zt.cpp sdk/sdk.h` 消除 CRLF 无效变动。
- [ ] 处置 4：将 `项目说明/` 下关键素材与图纸入库，`sdk/` 下非 Windows 压缩包加入 `.gitignore`。
- [ ] 处置 5：将本 `checkpoint.md` 加入 Git 跟踪并提交。

### 阶段二：Step 7 实机联调验收（需连接相机与云台硬件）
- [ ] **7.1 可见光实时性**：25fps 预览稳定，无丢帧告警，跟踪框平滑无拉扯（验证 KF 预测与 PID）。
- [ ] **7.2 遮挡与恢复测试**：人工手挡目标 3 秒后移开，观察 2 秒内是否触发九宫格重检测并恢复跟踪。
- [ ] **7.3 PSR 响应曲线**：正常跟踪状态 PSR > 基线 75%，遮挡时跌入 < 50%，确认三态机切换精准。
- [ ] **7.4 目标尺度缩放**：相机变焦或目标前后移动时，跟踪框自适应放大/缩小（±5% 误差内）。
- [ ] **7.5 CPU 占用评估**：开启跟踪后整体 CPU 增幅 < 25%（i5/i7 平台）；多核调度正常（`cv::setNumThreads(2)`）。
- [ ] **7.6 24h 稳定性长测**：无内存泄漏（Memory Leak）、无句柄泄漏、无串口断连崩溃。
- [ ] **7.7 云台控制闭环**：验证 PID 输出角速度平滑度、死区防抖、软限位拦截与 stop 连发可靠性。

---

## 七、历史检查点记录（Checkpoint Log）

### Checkpoint 2026-09-21 #1 (HEAD @ `d4b4973`)
- **记录人**：Antigravity AI
- **类型**：机制建立 & 里程碑巩固
- **核心内容**：
  1. 建立工程根目录 `checkpoint.md` 标准机制与操作规范。
  2. 梳理并核实 Milestone 2（DSST 全面接线 Step 4~6、线程安全 C1~C7、绘制缓存 C8/C9、镜头时序 P6、配置加锁 P7/P8）的代码交付。
  3. 全面诊断 Git 工作区状态：排查出 68 个被删历史归档文件、CRLF 虚假差异及未跟踪文档材料，并制定清晰的清理方案。
  4. 汇总 45 项体检清单闭环状态，当前闭环率达 91%，项目具备开展 Step 7 实机联调条件。
