# RGB_PTZ_Integrated

> **说明（给 AI 代理/维护者）**：本文件是项目维护的**权威参考**。任何代码修改前请先读完「架构与线程模型」和「代码修改约束」。
>
> **人类开发者**请阅读：`项目说明/RGB_PTZ_Integrated项目说明.docx`

---

## 一、项目概览

基于 Qt 的桌面应用，接入可见光相机 SDK 进行视频流显示，并通过 Pelco-D 串口协议控制 PTZ 云台。
核心亮点是实现了一套 **DSST + 卡尔曼滤波** 的目标跟踪算法，并配套 **KF 预测前馈 + PID** 的伺服控制，可在视频画面中框选任意目标并让云台自动跟随。

| 模块 | 功能 |
|------|------|
| 视频解码 | 第三方 SDK → YUV420 → QImage（主线程绘制） |
| PTZ 控制 | Pelco-D 协议（2400/9600 bps，默认 9600），支持预置位/定时查询 |
| 目标跟踪 | DSST（FHOG 特征 + 33 尺度 + 相关滤波）+ 线性 KF（位置+速度） |
| 伺服控制 | KF 预测前馈 + PID 反馈（P+I+D+速度前馈），死区 + 限幅 |
| 录像 | FFmpeg（MP4/MOV） |
| 镜头 | SDK 变焦/聚焦/光圈控制，支持自动/手动聚焦模式 |
| 数据记录 | PTZ 与目标检测数据记录，支持 CSV/Excel 导出 |
| 日志 | 统一日志管理，输出到 `日志/` 目录，Release 模式自动禁用 qDebug |

**技术栈**：Qt 6.10 / C++17 / OpenMP（可选）/ FFmpeg 8.1 / Univision SDK（Windows .lib）

---

## 二、目录速览

```
RGB_PTZ_Integrated/
├── main.cpp                        # 入口：注册元类型
├── mainwindow.h/.cpp/.ui           # 主界面 + 所有控件绑定
├── objecttracker.h/.cpp            # ★ DSST + KF 跟踪核心（worker thread）
├── trackingcontroller.h/.cpp       # ★ 伺服控制器（前馈+PID，主线程）
├── ptzcontroller.h/.cpp            # Pelco-D 串口收发
├── fftutils.h/.cpp                 # FFT / Hanning 窗 / 复数运算
├── devicemanager.h/.cpp            # 相机登录/连接/视频流分发
├── capturemanager.h/.cpp           # 抓图
├── imageprocessor.h/.cpp           # 图像工具
├── videodecoder.h/.cpp             # 视频解码封装
├── DecodeThread.h/.cpp             # 解码线程
├── RecordThread.h/.cpp             # 录像线程
├── ThreadSafeQueue.h               # 线程安全队列（模板）
├── configmanager.h/.cpp            # 配置读写
├── lensmanager.h/.cpp              # 镜头控制（变焦/聚焦/光圈模式）
├── datarecorder.h/.cpp             # 数据记录（CSV/Excel 导出）
├── logmanager.h/.cpp               # 日志管理（输出到"日志"目录）
├── datatypes.h                     # TrackResult / PTZData / DetectionData 等公共结构体
├── sdk/                            # 第三方 SDK（univisionsdk，.h/.lib/.dll 已入库，PDF 说明本地保留）
├── ffmpeg-8.1-full_build-shared/   # FFmpeg 头/库（不入库，见「环境准备」）
├── reference/                     # DSST 算法参考实现
├── scripts/                       # 文档生成工具链（generate_docx.js / gen_doc.py）
├── _archive/                      # 历史版本备份（backup 文件归档，入库一次作记录）
├── package.json                   # scripts/generate_docx.js 的 Node 依赖声明（docx）
└── 项目说明/
    ├── RGB_PTZ_Integrated项目说明.docx   # ★ 人类开发者完整文档（框图/算法/调试）
    └── 方案/RGB_PTZ_Integrated_修复方案.md
```

---

## 三、架构与数据流向

```
 [相机SDK] ──帧──▶ [DecodeThread]
                           │ QImage (信号)
                           ▼
                   [MainWindow::onNewFrame]
                           │
              ┌────────────┴────────────────┐
              ▼                             ▼
       视频显示（QImage→QLabel）     [TrackingController]
                                          │
              ┌──────────────────QueuedConnection──────────────────┐
              ▼                                                     │
       [ObjectTracker::processFrameSlot]          (worker thread)   │
            │  DSST响应图 + FHOG尺度估计                              │
            │  KF 预测 → 检测 → KF 更新                              │
            ▼                                                         │
       TrackResult (含 kfX/Y/Vx/Vy)  ◀──────────────────────────────┘
                                          │
                                          ▼
                                  computePTZControl（主线程）
                                    前馈: K_ff × kfV
                                    PID:  Kp·e + Ki·Σe + Kd·Δe
                                    死区 + 限幅
                                          │
                                          ▼
                                   ptzControlDelta()
                                          │
                                          ▼
                                  [PTZController::moveDirection]
                                          │ Pelco-D 帧
                                          ▼
                                      QSerialPort
```

**状态机**：`Idle → Selecting → Tracking ⇄ Lost(重检测) → Idle`，另支持 `Paused` 暂停状态
（定义在 [trackingcontroller.h](file:///d:/lx/RGB_PTZ/RGB_PTZ_Integrated/trackingcontroller.h#L30-L36)）

---

## 四、线程模型与并发约束

**关键信息：本项目有 8 个活跃线程。** 修改任何代码前先弄清楚当前代码运行在哪个线程。

| 线程 | 职责 | 生命周期 | 关键信号 |
|------|------|---------|---------|
| 主线程 (QThread::main) | UI 绘制、按钮响应、PTZ 控制、TrackingController 的 computePTZControl | 全程 | `btnTrackStart clicked` |
| DecodeThread | SDK 视频解码、YUV→RGB | 登录后启动 | `newFrame(QImage)` |
| RecordThread | FFmpeg 视频写入 | 用户点击录像 | 内部队列 |
| ObjectTracker worker thread | DSST/FHOG/FFT 计算量大的跟踪 | 框选后创建，停止后销毁 | `trackingDone(TrackResult)` |
| PTZController worker thread | 串口阻塞读写 | 串口打开期间 | `moveDirection()` |
| QSerialPort 内部线程 | 串口事件循环 | Qt 内部管理 | `readyRead` |
| Qt 事件派发线程 | UI 事件 | 全程 | 内部 |
| 线程池/Concurrent | 图像处理 | 按需 | `QtConcurrent` |

### 必须遵守的并发约束

1. **TrackingController 必须生活在主线程**：它既接收 worker 的结果信号，又触发 UI 和 PTZ。不要 moveToThread。
2. **ObjectTracker 必须生活在 worker thread**：`init()` 在主线程同步调用（只做内存分配），`processFrameSlot()` 通过 `QueuedConnection` 跨线程调用。不要在主线程调用 update()。
3. **ObjectTracker 的销毁顺序**：必须是 `disconnect → quit() → wait() → delete tracker → delete thread`。见 [trackingcontroller.cpp L43-L62](file:///d:/lx/RGB_PTZ/RGB_PTZ_Integrated/trackingcontroller.cpp#L43-L62)（setTarget）和 [L255-L277](file:///d:/lx/RGB_PTZ/RGB_PTZ_Integrated/trackingcontroller.cpp#L255-L277)（stopTracking）。**不要改动这个顺序。**
4. **信号与槽连接方式**：跨线程务必用 `Qt::QueuedConnection`（Qt 自动识别，但若手动 `connect` 第 5 个参数必须指定）。
5. **PID 状态变量只在主线程 computePTZControl 里读写**：`m_integralX/Y`、`m_prevErrorX/Y`、`m_pidInitialized` 都在主线程访问，不需要锁。新功能如果要读这些值，要么在主线程槽函数里，要么加 `QMutex`。
6. **不要在 worker 线程里改 UI**：ObjectTracker 不能直接触碰任何 QWidget。通过信号/ TrackResult 传数据。

### TrackResult 结构体
定义在 [objecttracker.h](file:///d:/lx/RGB_PTZ/RGB_PTZ_Integrated/objecttracker.h#L12-L28)，包含：
- `bbox`：检测到的目标框
- `confidence`：PSR 值（≈响应图峰旁比）
- `kfX, kfY, kfVx, kfVy`：KF 平滑后的位置与速度（像素）—— 伺服控制主要用这 4 个字段
- `occluded / recovered / fps`

**元类型**：`main.cpp` 中注册以下跨线程类型：
- `qRegisterMetaType<TrackResult>("TrackResult")` — 跟踪结果
- `qRegisterMetaType<ProcessResult>("ProcessResult")` — 处理结果
- `qRegisterMetaType<LoginResult>("LoginResult")` — 登录结果

若新增字段要跨线程，需确认此处已注册。

---

## 五、伺服控制算法（KF 前馈 + PID）

这是本项目最核心的数学部分。修改参数前请理解以下公式。

**每帧在 TrackingController::computePTZControl 中执行**（[trackingcontroller.cpp L173-L253](file:///d:/lx/RGB_PTZ/RGB_PTZ_Integrated/trackingcontroller.cpp#L173-L253)）：

```
 1. 从 TrackResult 读取 KF 状态: kfX, kfY, kfVx, kfVy
 2. 预测未来 T_predict 帧位置:
      predictX = kfX + kfVx * T_predict
      predictY = kfY + kfVy * T_predict
    error = predict - 画面中心
 3. 死区: |error| < deadZone → error = 0，清零积分
 4. 速度前馈:
      vFeedX = K_ff * kfVx
      vFeedY = m_k_ff * kfVy
 5. PID:
      P = Kp * error
      I = I_prev + Ki * error    (clamp ±I_max)
      D = Kd * (error - error_prev)
      PID_out = P + I + D
 6. 混合: speed = vFeed + PID_out
 7. 限幅: speed = clamp(speed, -maxSpeed, +maxSpeed)
 8. 发射 ptzControlDelta(Δx, Δy, |speedX|, |speedY|)
```

**参数默认值**（在 TrackingController 构造函数初始化）：
- Kp = 0.5, Ki = 0.02, Kd = 0.3, K_ff = 0.15
- T_predict = 2 帧（补偿云台延迟 ≈ 80 ms @ 25 fps）
- deadZone = 20 px, maxSpeed = 20, I_max = 15

**UI 调参入口**：`mainwindow.cpp on_btnTrackStart_clicked` 读取 7 个 `spin*` 控件并调用 `setDeadZonePixels / setMaxSpeed / setProportionalGain / setIntegralGain / setDerivativeGain / setFeedforwardGain / setPredictHorizon`。运行中不实时生效，需重新点击"开始跟踪"。**注意**：`I_max`（积分饱和上限，默认 15）无 UI 调参入口，需在 `TrackingController` 构造函数中修改。

**方向约定**：deltaX>0 → 目标在右侧 → 云台右转 (Pelco-D cmd2 位 0x02)。见 [mainwindow.cpp L1077-L1114](file:///d:/lx/RGB_PTZ/RGB_PTZ_Integrated/mainwindow.cpp#L1077-L1114)。

---

## 六、代码修改约束

### 命名与风格
- 类名：`PascalCase`，成员变量 `m_` 前缀（如 `m_kp`, `m_tracker`）
- 信号没有返回值，参数用值传递或 const ref
- UI 控件 objectName 用 `spinTrackKp` / `btnTrackStart` 风格，保持与代码里的 `ui->spinTrackKp` 一致
- .ui 文件改动后 **必须重新 qmake** 才能生成新的 `ui_mainwindow.h`

### 错误处理
- 跨线程用信号传递错误信息，不要抛异常
- 串口/网络失败：`emit statusMessage("xxx")` + 状态机退回 Idle
- Tracker 初始化失败：`stopTracking()` + UI 提示

### 参数与默认值
- 默认值写在构造函数初始化列表，并在 `initTrackingModule()` 用同样的值同步 UI
- 新增参数必须同时在 3 处出现：① TrackingController 成员 + 默认值 ② setter 函数 ③ mainwindow.cpp 读取 UI 并调用 setter

### 性能
- FFT（N×N 复数矩阵）是热点，不要在 FFT 路径里分配 `std::vector`
- 所有跟踪计算在独立 worker thread，不阻塞 UI
- 禁止在 `computePTZControl` 里加 IO 操作（写日志/读文件）

### 安全
- PTZ 速度上限 `[0, 63]`（Pelco-D 规范）。所有速度输出必须经 `qBound(0, ..., 63)` 或等价检查
- 死区是防止云台抖动的硬门槛，不要设成 0

### 向后兼容
- 修改 TrackResult 字段不会破坏老代码（是 POD 结构体，默认构造为 0）
- 新 UI 控件默认值需写回 `initTrackingModule()`，防止旧 .ui 文件加载时显示空值

---

## 七、维护清单（AI 代理每次改代码前检查）

- [ ] 我理解我改的代码运行在哪个线程
- [ ] 若新增成员变量，确认它的线程归属与访问方式（主线程？worker？需要锁吗？）
- [ ] 若修改 TrackResult，确认 main.cpp 的 `qRegisterMetaType` 仍有效
- [ ] 若修改 .ui 新增/改名控件，确认 mainwindow.cpp 里对应的 `ui->xxx` 也同步改名
- [ ] 若调整 PID 参数，同步更新 README 第五节里的默认值说明（以及项目说明.docx）
- [ ] 若修改 ObjectTracker 线程生命周期，确认销毁顺序不变（disconnect → quit → wait → delete tracker → delete thread）
- [ ] 改动后更新「变更记录」章节

---

## 八、环境准备与构建

### 依赖准备

| 依赖 | 说明 | 获取方式 |
|------|------|---------|
| Qt ≥ 6.10 | 含 serialport / concurrent 模块 | Qt 官方安装器 |
| MinGW 64-bit | 与 Qt 版本匹配的编译工具链 | 随 Qt 安装 |
| FFmpeg 8.1 shared | 编译需 include/lib，运行需 bin 下 DLL | 见下方下载说明，**不入 git 仓库** |
| Univision SDK | `.h/.lib/.dll` 已随仓库提供（`sdk/`） | 无需另行下载；`SDK 说明文档.pdf` 体积较大未入库，本地保留 |

**FFmpeg 下载与放置**（约 263 MB，故不入库）：

1. 从 [gyan.dev FFmpeg releases](https://www.gyan.dev/ffmpeg/builds/) 下载 `ffmpeg-8.1-full_build-shared.7z`（full build, shared 版本）
2. 解压到仓库根目录，并确认目录名为 `ffmpeg-8.1-full_build-shared/`（与 `.pro` 中 `FFMPEG_DIR` 一致）
3. 运行时需将 `ffmpeg-8.1-full_build-shared/bin` 加入 PATH，或把 bin 下 DLL 拷到可执行文件目录

### 构建

```bat
qmake RGB_PTZ_Integrated.pro
mingw32-make -j4        # 或 MSVC nmake / jom
```

**版本宏**：`PROJECT_VERSION=\"1.0.0\"`、`PROJECT_NAME=\"RGB_PTZ_Integrated\"` 在 `.pro` 中定义，可在代码中引用。

### 文档生成工具链（可选）

用于重新生成 `项目说明/RGB_PTZ_Integrated项目说明.docx`：

```bat
npm install                # 安装 docx 依赖（见 package.json）
node scripts/generate_docx.js
```

另有早期 Python 实现 `scripts/gen_doc.py`（需 `pip install python-docx`），功能与 JS 版重复，保留仅作参考。

### 历史备份说明

`_archive/` 存放整理工作区前的手动 backup 文件（按日期批次归档，文件名已还原）。已作为上一个版本的记录提交进 git；此后**请勿再手动创建 backup 文件**，直接依赖 git 版本管理。确认 git 历史完整后，可在后续提交中删除该目录。

---

## 九、变更记录

- `[YYYY-MM-DD]` 项目初始化 + DSST+KF 跟踪器 + PTZ 控制
- `[YYYY-MM-DD]` 伺服升级为 KF 预测前馈 + PID；修复 KF 采样中心从旧位置改为 KF 预测位置；PTZ 速度改用 qRound；添加 Kp/Ki/Kd/Kff/预测时域 UI 控件
- `[YYYY-MM-DD]` 重构文档体系：README.md（AI/维护者）+ 项目说明/项目说明.docx（人类开发者）
- `[2026-07-15]` 功能扩展与稳定性修复：
  - 新增 `LogManager` 统一日志管理（输出到 `日志/` 目录，Release 模式自动禁用 qDebug）
  - 新增 `DataRecorder` 数据记录与导出（CSV/Excel）
  - `LensManager` 扩展：支持自动/手动聚焦模式、光圈模式控制
  - `PTZController` 扩展：预置位（callPreset/setPreset）、定时角度查询（startAutoQuery）
  - `DeviceManager` 扩展：OSD 时间/星期/位置/字体大小控制
  - `TrackingState` 新增 `Paused` 暂停状态
  - 修复 P2#23：死区范围校验（`qBound(1, pixels, 100)`）
  - 修复 P2#13：空帧防护（`frame.isNull()` 检查）
  - 修复 P3#30：添加版本信息宏（`PROJECT_VERSION=1.0.0`）
  - 修复 P3#29：Release 模式禁用 qDebug 输出
  - 修复 P0#2：严格线程销毁顺序（disconnect → quit → wait → delete）
  - 修复 P3#25：默认波特率常量（`DEFAULT_PTZ_BAUDRATE = 9600`）
- `[2026-09-16]` 工作区整理（接入 git 版本管理）：
  - 全部 `*_backup_*` 文件按日期批次归档至 `_archive/`，文件名还原为原名，入库一次作历史记录
  - 删除构建产物（build/ debug/ release/ Makefile* ui_mainwindow.h 等，约 477 MB）
  - 新增 `.gitignore`：排除构建产物、IDE 配置、`ffmpeg-8.1-full_build-shared/`（263 MB，本地保留）、SDK PDF、运行日志
  - FFmpeg 改为按 README「环境准备」手动下载放置；`sdk/` 的 .h/.lib/.dll 继续随仓库分发
  - 文档生成工具链整理至 `scripts/`（generate_docx.js + gen_doc.py），package.json 保留在根目录
  - docx 解包临时目录、旧运行日志移入 `_archive/`
