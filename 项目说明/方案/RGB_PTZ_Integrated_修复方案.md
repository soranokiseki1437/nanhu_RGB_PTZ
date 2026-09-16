# RGB_PTZ_Integrated 修复方案

> 版本：v1.0  
> 日期：2026-07-16  
> 基于全项目代码审查（线程模型 / 内存资源 / 算法数据流三维度）

---

## 目录

- [1. 问题总览](#1-问题总览)
- [2. 致命级修复（C1-C2）](#2-致命级修复c1-c2)
- [3. 严重级修复（S1-S4）](#3-严重级修复s1-s4)
- [4. 中等级修复（M1-M5）](#4-中等级修复m1-m5)
- [5. 低优先级改进（L1-L5）](#5-低优先级改进l1-l5)
- [6. 修复实施顺序与验证计划](#6-修复实施顺序与验证计划)

---

## 1. 问题总览

### 1.1 问题分布

| 严重程度 | 数量 | 影响 |
|---------|------|------|
| 致命（Critical） | 2 | 程序挂死 / 崩溃 |
| 严重（Severe） | 4 | 功能错误 / 线程安全 |
| 中等（Medium） | 5 | 健壮性 / 数据完整性 |
| 低（Low） | 5 | 代码质量 / 性能 |

### 1.2 问题汇总表

| 编号 | 模块 | 问题 | 严重程度 | 状态 |
|------|------|------|---------|------|
| C1 | DeviceManager | `cleanupSDK()` 持锁调用 `logout()` 导致死锁 | 致命 | **已修复** |
| C2 | RecordThread | `initEncoder()` 失败后 `cleanup()` 释放 `m_formatContext`，后续空指针崩溃 | 致命 | **已修复** |
| S1 | PTZController | `startAutoQuery`/`stopAutoQuery` 跨线程直接调用 QTimer | 严重 | **已修复** |
| S2 | LensManager | `setUserID()` 直接调用 worker 对象方法，跨线程数据竞争 | 严重 | **已修复** |
| S3 | TrackingController | 单轴死区积分泄漏，死区内云台仍微动 | 严重 | **已修复** |
| S4 | ObjectTracker | `padding=2.0` 偏大，FFT 计算量大且峰值稀释 | 严重 | **已修复** |
| M1 | DecodeThread | `m_running` 为普通 `bool`，跨线程读写无原子保护 | 中等 | **已修复** |
| M2 | 全部模块 | SDK 回调、线程 `run()` 无 try-catch | 中等 | **已修复** |
| M3 | PTZController | 析构函数中 `m_serial->close()` 可能跨线程 | 中等 | **已修复** |
| M4 | DataRecorder | 帧序号恒为 0，缺关键字段，采样率不匹配 | 中等 | **已修复** |
| M5 | ConfigManager | 跟踪参数 / PTZ 配置 / 录像路径未持久化 | 中等 | **已修复** |
| L1 | RecordThread | `cleanup()` 重复调用（run 结尾 + 析构） | 低 | **已修复** |
| L2 | VideoDecoder | `m_rgbBuffer[1-3]` 未初始化为 nullptr | 低 | **已修复** |
| L3 | PTZController | 串口错误无恢复机制，不通知上层 | 低 | **已修复** |
| L4 | RecordThread | cleanup() 释放顺序脆弱（buffer 先于 frame） | 低 | **已修复** |
| L5 | CaptureManager | `ImageProcessor*` 裸指针传入线程池 | 低 | **已确认安全** |

### 1.3 已完成修复（跟踪算法，2026-07-16）

以下问题已在本次审查前修复，此处记录供参考：

| 编号 | 问题 | 修复内容 |
|------|------|---------|
| P0#1 | `getTranslationSample` 缺少特征归一化 | 加零均值单位方差归一化 |
| P0#2 | 尺度滤波器空间信息完全丢失（FHOG 求和） | 重写为像素展平方式 |
| P1#3 | nextPow2 强制撑大模型尺寸 | 取消 nextPow2，直接用原始尺寸 |
| P2#5 | 尺度参数不合理（32 尺度 / 1.02 步长） | 改为 9 尺度 / 1.05 步长 |

---

## 2. 致命级修复（C1-C2）

### C1：DeviceManager cleanupSDK() 死锁

**问题位置**：`devicemanager.cpp` L119-132, L326-328

**根因**：

```cpp
// devicemanager.cpp L119-125
void DeviceManager::cleanupSDK()
{
    QMutexLocker locker(&m_mutex);    // 第1次加锁
    if (m_connected) {
        logout();                      // logout() 内部第2次加锁 → 死锁!
    }
    ...
}

// devicemanager.cpp L326-328
bool DeviceManager::logout()
{
    QMutexLocker locker(&m_mutex);    // 非递归 QMutex，重复加锁 → 永久阻塞
    ...
}
```

`m_mutex` 声明为 `mutable QMutex m_mutex`（`devicemanager.h` L74），默认非递归。当设备仍处于连接状态时关闭程序，析构链 `~DeviceManager() → cleanupSDK() → logout()` 在 `logout()` 处永久阻塞。

**修复方案**：`cleanupSDK()` 中先解锁再调用 `logout()`

```cpp
// 修复后的 cleanupSDK()
void DeviceManager::cleanupSDK()
{
    // 不持锁调用 logout()，由 logout() 内部自行加锁
    if (m_connected) {
        logout();  // logout() 内部会加锁 m_mutex
    }

    QMutexLocker locker(&m_mutex);
    if (m_sdkInitialized) {
        UNIV_SDK_Cleanup();
        m_sdkInitialized = false;
        qDebug() << "SDK清理成功";
    }
}
```

**修改文件**：`devicemanager.cpp`

**验证方法**：连接设备后在主窗口点关闭，确认程序在 3 秒内正常退出，不挂死。

---

### C2：RecordThread initEncoder() 失败后空指针崩溃

**问题位置**：`RecordThread.cpp` L109-113, L253-259

**根因**：

```cpp
// RecordThread.cpp L109-113 — initEncoder() 中
if (avcodec_open2(m_encoderCtx, m_encoder, nullptr) < 0) {
    emit errorOccurred("无法打开编码器");
    cleanup();        // cleanup() 释放了 m_formatContext！
    return false;
}

// RecordThread.cpp L253-259 — run() 循环中
if (!encoderReady) {
    if (!initEncoder(qImage.width(), qImage.height())) {
        continue;     // 回到循环顶部，但 m_formatContext 已为 nullptr
    }
    // initEncoder 成功后：
    m_videoStream = avformat_new_stream(m_formatContext, nullptr);
    //                                          ^^^^^^^ nullptr → 崩溃!
}
```

`cleanup()` 释放所有资源包括 `m_formatContext`（在 `initRecord()` 中分配），但 `m_formatContext` 不应在 `initEncoder()` 失败时被释放。

**修复方案**：`initEncoder()` 失败时只清理编码器资源，不清理格式上下文

```cpp
// 修复后的 initEncoder() — 失败路径
bool RecordThread::initEncoder(int width, int height)
{
    // ... 编码器查找、分配、参数设置 ...

    if (avcodec_open2(m_encoderCtx, m_encoder, nullptr) < 0) {
        emit errorOccurred("无法打开编码器");
        // 只清理编码器相关资源，不动 m_formatContext
        if (m_encoderCtx) {
            avcodec_free_context(&m_encoderCtx);
        }
        // 不调用 cleanup()！
        return false;
    }

    // 分配 YUV 帧 — 加入空指针检查
    m_yuvFrame = av_frame_alloc();
    if (!m_yuvFrame) {
        emit errorOccurred("无法分配 YUV 帧");
        avcodec_free_context(&m_encoderCtx);
        return false;
    }
    m_yuvFrame->format = AV_PIX_FMT_YUV420P;
    m_yuvFrame->width = m_width;
    m_yuvFrame->height = m_height;

    av_image_alloc(m_yuvBuffer, m_yuvLinesize, m_width, m_height, AV_PIX_FMT_YUV420P, 32);
    av_image_fill_arrays(m_yuvFrame->data, m_yuvFrame->linesize, m_yuvBuffer[0],
                        AV_PIX_FMT_YUV420P, m_width, m_height, 1);

    m_packet = av_packet_alloc();
    if (!m_packet) {
        emit errorOccurred("无法分配 packet");
        av_frame_free(&m_yuvFrame);
        av_freep(&m_yuvBuffer[0]);
        avcodec_free_context(&m_encoderCtx);
        return false;
    }

    return true;
}
```

同时在 `run()` 循环中对 `initEncoder` 失败增加重试上限：

```cpp
// 修复后的 run() 循环
if (!encoderReady) {
    if (!initEncoder(qImage.width(), qImage.height())) {
        encoderFailCount++;
        if (encoderFailCount >= 3) {
            emit errorOccurred("编码器初始化连续失败 3 次，停止录像");
            break;
        }
        continue;
    }
    // ... 后续 avformat_new_stream 等操作 ...
}
```

**修改文件**：`RecordThread.cpp`、`RecordThread.h`（新增 `int encoderFailCount = 0` 成员）

**验证方法**：模拟编码器不可用场景（如临时改 codec_id 为不存在的值），确认程序报错退出而不崩溃。

---

## 3. 严重级修复（S1-S4）

### S1：PTZController startAutoQuery/stopAutoQuery 跨线程直接调用

**问题位置**：`mainwindow.cpp` L828, L847

**根因**：

```cpp
// mainwindow.cpp L828 — 主线程直接调用
m_ptzController->startAutoQuery(100);  // PTZController 已 moveToThread！

// mainwindow.cpp L847
m_ptzController->stopAutoQuery();
```

PTZController 已 `moveToThread` 到 PTZ 线程，其内部 `m_autoQueryTimer` 属于 PTZ 线程事件循环。从主线程直接调用会在主线程上下文中操作属于 PTZ 线程的 QTimer，违反 Qt 线程亲和性规则。`m_autoQuerying` 标志也无线程同步。

**修复方案**：改为信号槽方式

1. 在 `MainWindow` 中新增两个信号：

```cpp
// mainwindow.h 新增
signals:
    void ptzStartAutoQuery(int intervalMs);
    void ptzStopAutoQuery();
```

2. 在 `initTrackingModule()` 或 PTZ 初始化处连接信号：

```cpp
// mainwindow.cpp PTZ 初始化区域新增
connect(this, &MainWindow::ptzStartAutoQuery, m_ptzController, &PTZController::startAutoQuery);
connect(this, &MainWindow::ptzStopAutoQuery, m_ptzController, &PTZController::stopAutoQuery);
```

3. 替换调用点：

```cpp
// mainwindow.cpp L828 修改为
emit ptzStartAutoQuery(100);

// mainwindow.cpp L847 修改为
emit ptzStopAutoQuery();
```

4. `PTZController::startAutoQuery`/`stopAutoQuery` 中的 `m_autoQuerying` 改为 `std::atomic<bool>`：

```cpp
// ptzcontroller.h 修改
std::atomic<bool> m_autoQuerying;
```

**修改文件**：`mainwindow.h`、`mainwindow.cpp`、`ptzcontroller.h`

**验证方法**：开始/停止录像时观察 PTZ 查询是否正常工作，无 Qt 警告输出。

---

### S2：LensManager setUserID() 跨线程数据竞争

**问题位置**：`lensmanager.cpp` L268-269

**根因**：

```cpp
// lensmanager.cpp L268-269 — 主线程直接调用 worker 方法
if (m_worker) {
    m_worker->setUserID(userID);  // m_worker 已 moveToThread！
}
```

`LensManager::setUserID()` 在主线程执行，但 `m_worker` 已 `moveToThread` 到工作线程。直接方法调用导致跨线程写入 `LensWorker::m_userID`。

**修复方案**：改为信号槽方式

1. 在 `LensManager` 中新增信号：

```cpp
// lensmanager.h 新增
signals:
    void userIDChanged(uint64_t userID);
```

2. 在构造函数中连接：

```cpp
// lensmanager.cpp 构造函数新增
connect(this, &LensManager::userIDChanged, m_worker, &LensWorker::setUserID);
```

3. 替换调用点：

```cpp
// lensmanager.cpp L268-269 修改为
if (m_worker) {
    emit userIDChanged(userID);  // QueuedConnection 自动识别
}
```

**修改文件**：`lensmanager.h`、`lensmanager.cpp`

**验证方法**：登录/登出设备后操作镜头变焦/聚焦，确认功能正常无异常。

---

### S3：TrackingController 单轴死区积分泄漏

**问题位置**：`trackingcontroller.cpp` L211-217

**根因**：

```cpp
// trackingcontroller.cpp L211-217
if (errorXEffective == 0.0f && errorYEffective == 0.0f) {
    // 只有两轴都在死区才清零 → 单轴在死区时旧积分残留
    m_integralX = 0.0f;
    m_integralY = 0.0f;
    emit ptzControlDelta(0, 0, 0, 0);
    return;
}
```

当 X 轴在死区（`errorXEffective=0`）但 Y 轴不在死区时，`m_integralX` 不会被清零，旧积分值通过 `pidX = 0 + m_integralX + dX` 继续输出，导致死区内云台仍微动。

**修复方案**：单轴独立判断死区并清零对应积分

```cpp
// 修复后的 computePTZControl() 死区逻辑

// 单轴独立死区判断
if (errorXEffective == 0.0f) {
    m_integralX = 0.0f;  // X 轴在死区，清零 X 积分
}
if (errorYEffective == 0.0f) {
    m_integralY = 0.0f;  // Y 轴在死区，清零 Y 积分
}

// 两轴都在死区 → 直接返回
if (errorXEffective == 0.0f && errorYEffective == 0.0f) {
    // 同时更新上一帧误差，避免 D 项跳变
    m_prevErrorX = 0.0f;
    m_prevErrorY = 0.0f;
    emit ptzControlDelta(0, 0, 0, 0);
    return;
}
```

同时修复 D 项边界跳变问题（目标在死区边界来回跳动时 D 项产生尖峰）：

```cpp
// D 项改用原始误差（死区前），避免边界跳变
float dX = 0.0f, dY = 0.0f;
if (m_pidInitialized) {
    // 使用原始 error 而非 errorXEffective 计算 D 项
    dX = m_kd * (errorX - m_prevErrorX);
    dY = m_kd * (errorY - m_prevErrorY);
} else {
    m_pidInitialized = true;
}
m_prevErrorX = errorX;   // 保存原始误差
m_prevErrorY = errorY;
```

**修改文件**：`trackingcontroller.cpp`

**验证方法**：跟踪目标至目标停在画面中心附近（进入死区），观察云台是否完全静止，无微动。

---

### S4：ObjectTracker padding 偏大导致峰值稀释

**问题位置**：`objecttracker.cpp` 构造函数 `m_padding` 初始值

**根因**：

`padding=2.0` 意味着模型区域 = 目标尺寸 × 3（长宽各 3 倍）。例如 100×100 目标 → 300×300 模型区域，响应峰值被稀释。参考实现使用 `padding=1.0`（目标 2 倍区域）。

同时，当前 `m_modelW`/`m_modelH` 不强制 2 的幂（上一轮修复取消了 nextPow2），导致每次 FFT 走 zero-padding 路径，有额外开销。

**修复方案**：降低 padding 并对模型尺寸取 2 的幂以优化 FFT 性能

```cpp
// objecttracker.cpp 构造函数修改
, m_padding(1.5f)           // 从 2.0 降为 1.5（目标 2.5 倍区域）
```

在 `init()` 中，对模型尺寸做"就近取 2 的幂"优化（注意：不是像之前那样向上取，而是就近取最接近的 2 的幂）：

```cpp
// objecttracker.cpp init() 中，计算 m_modelW/m_modelH 之后新增
// 就近取 2 的幂以优化 FFT 性能（非强制向上取）
auto nearestPow2 = [](int n) -> int {
    if (n <= 4) return 4;
    int lo = 1, hi = 1;
    while (hi < n) { lo = hi; hi <<= 1; }
    return (n - lo < hi - n) ? lo : hi;
};
m_modelW = nearestPow2(m_modelW);
m_modelH = nearestPow2(m_modelH);
```

**修改文件**：`objecttracker.cpp`

**验证方法**：对比修改前后跟踪帧率（qDebug 输出帧间时间），确认帧率提升且跟踪精度不下降。

---

## 4. 中等级修复（M1-M5）

### M1：DecodeThread m_running 非原子

**问题位置**：`DecodeThread.h` L29

**根因**：`bool m_running` 在 `stop()`（主线程）中写，在 `run()`（工作线程）中读，无同步保护。

**修复方案**：

```cpp
// DecodeThread.h 修改
#include <atomic>

// L29 修改为
std::atomic<bool> m_running;
```

同步修改构造函数初始化为 `m_running(false)` 和所有使用点（`stop()` 中 `m_running = false`，`run()` 中 `while (m_running.load())`）。

**修改文件**：`DecodeThread.h`、`DecodeThread.cpp`

---

### M2：关键路径缺少 try-catch

**问题位置**：全部模块的 `run()` 方法和 SDK 静态回调

**修复方案**：在以下关键位置添加 `try-catch(...)` 保护

| 位置 | 保护内容 | 处理方式 |
|------|---------|---------|
| `DecodeThread::run()` | 解码循环 | catch → qDebug + break |
| `RecordThread::run()` | 录像循环 | catch → emit errorOccurred + break |
| `DeviceManager::OnStreamData()` | SDK 回调 | catch → 静默丢弃当前帧 |
| `DeviceManager::OnException()` | SDK 异常回调 | catch → 静默 |
| `ObjectTracker::processFrameSlot()` | 跟踪计算 | catch → emit trackingLost |

示例（DecodeThread::run()）：

```cpp
void DecodeThread::run()
{
    m_running = true;
    while (m_running.load()) {
        QByteArray data;
        if (!m_dataQueue.waitAndPop(data, 100)) continue;
        try {
            QImage frame = m_decoder.decodeFrame(
                reinterpret_cast<uint8_t*>(data.data()), data.size());
            if (!frame.isNull()) emit frameDecoded(frame);
        } catch (...) {
            qDebug() << "[DecodeThread] 解码异常，跳过当前帧";
        }
    }
}
```

**修改文件**：`DecodeThread.cpp`、`RecordThread.cpp`、`devicemanager.cpp`、`objecttracker.cpp`

---

### M3：PTZController 析构跨线程

**问题位置**：`ptzcontroller.cpp` L42-48

**根因**：PTZController 通过 `delete m_ptzController` 在主线程析构，但 `m_serial` 在 PTZ 线程的 `init()` 中创建。

**修复方案**：利用 `QThread::finished` → `deleteLater` 模式，让 PTZController 在自己的线程中析构

```cpp
// mainwindow.cpp PTZ 初始化区域（已有此连接，确认即可）
connect(m_ptzThread, &QThread::finished, m_ptzController, &QObject::deleteLater);

// 析构时改为：不直接 delete，只 quit + wait
// 修改前
disconnect(...);
m_ptzThread->quit();
m_ptzThread->wait();
delete m_ptzController;  // ← 删除此行
delete m_ptzThread;      // ← 改为 m_ptzThread->deleteLater();

// 修改后
disconnect(m_ptzThread, &QThread::finished, m_ptzController, &QObject::deleteLater);
m_ptzThread->quit();
m_ptzThread->wait();
// m_ptzController 由 deleteLater 在线程停止后自动析构
// m_ptzThread 由 parent-child 或 deleteLater 管理
m_ptzThread->deleteLater();
```

**注意**：此修改需同时调整析构函数中的指针置空逻辑，确保后续代码不访问已 deleteLater 的指针。

**修改文件**：`mainwindow.cpp`（3 处析构/断开逻辑需同步修改）

---

### M4：DataRecorder 数据不完整

**问题位置**：`datarecorder.cpp` L177-179, `datarecorder.h`

**根因**：
1. `frameIndex` 恒为 0（`recordFrame(int)` 无调用方）
2. 缺少 KF 状态、PSR、控制指令、跟踪状态等关键字段
3. 10Hz 采样 vs 25-30fps 视频，大量数据丢失

**修复方案**：

1. 扩展 `DataPoint` 结构体：

```cpp
// datarecorder.h DataPoint 新增字段
struct DataPoint {
    // ... 现有字段 ...
    int frameIndex = 0;           // 实际帧序号
    float kfX = 0, kfY = 0;      // KF 位置
    float kfVx = 0, kfVy = 0;    // KF 速度
    float psr = 0;                // PSR 值
    int trackState = 0;           // 跟踪状态 (0=Idle,1=Tracking,2=Lost,3=Recovered)
    int ptzCmdX = 0, ptzCmdY = 0; // PTZ 控制指令
    int ptzSpeedX = 0, ptzSpeedY = 0; // PTZ 速度
};
```

2. 在 `onFrameReceived` 中调用 `recordFrame(frameCounter++)` 传入真实帧号

3. CSV 表头扩展：

```cpp
out << "序号,时间戳(ms),日期时间,帧序号,"
    << "水平角度(°),俯仰角度(°),水平速度,俯仰速度,"
    << "目标检测有效,目标X,目标Y,X脱靶量,Y脱靶量,置信度,"
    << "KF_X,KF_Y,KF_Vx,KF_Vy,PSR,跟踪状态,"
    << "PTZ指令X,PTZ指令Y,PTZ速度X,PTZ速度Y\n";
```

4. 在 `TrackingController::computePTZControl` 回调中更新这些字段

**修改文件**：`datarecorder.h`、`datarecorder.cpp`、`trackingcontroller.cpp`、`mainwindow.cpp`

---

### M5：ConfigManager 持久化不足

**问题位置**：`configmanager.h` / `configmanager.cpp`

**根因**：ConfigManager 只管理 6 个配置项（IP/用户名/密码/抓图间隔/质量/路径），跟踪参数、PTZ 串口配置、录像路径均未持久化。

**修复方案**：扩展 ConfigManager

```cpp
// configmanager.h 新增接口
public:
    // PTZ 串口配置
    QString getPtzPortName() const;
    int getPtzBaudRate() const;
    int getPtzAddress() const;
    void setPtzConfig(const QString &port, int baud, int addr);

    // 跟踪参数
    float getTrackKp() const;
    float getTrackKi() const;
    float getTrackKd() const;
    float getTrackKff() const;
    int getTrackTPredict() const;
    int getTrackDeadZone() const;
    int getTrackMaxSpeed() const;
    void setTrackParams(float kp, float ki, float kd, float kff, int tPred, int dz, int maxSpd);

    // 录像路径
    QString getVideoSavePath() const;
    void setVideoSavePath(const QString &path);

private:
    // 新增成员变量
    QString m_ptzPortName;
    int m_ptzBaudRate;
    int m_ptzAddress;
    float m_trackKp, m_trackKi, m_trackKd, m_trackKff;
    int m_trackTPredict, m_trackDeadZone, m_trackMaxSpeed;
    QString m_videoSavePath;
```

在 `loadConfig()` / `saveConfig()` 中用 QSettings 读写这些值。

**修改文件**：`configmanager.h`、`configmanager.cpp`、`mainwindow.cpp`（启动时读取、参数修改时保存）

---

## 5. 低优先级改进（L1-L5）

### L1：RecordThread cleanup() 重复调用

**位置**：`RecordThread.cpp` L322, L32

**改进**：在 `cleanup()` 开头加 guard：

```cpp
void RecordThread::cleanup()
{
    if (!m_yuvFrame && !m_encoderCtx && !m_formatContext) return; // 已清理
    // ... 正常清理 ...
}
```

或使用 `std::once_flag`。

### L2：VideoDecoder m_rgbBuffer[1-3] 未初始化

**位置**：`videodecoder.cpp` L14

**改进**：

```cpp
// 修改构造函数
m_rgbBuffer[0] = m_rgbBuffer[1] = m_rgbBuffer[2] = m_rgbBuffer[3] = nullptr;
```

### L3：PTZController 串口错误无恢复

**位置**：`ptzcontroller.cpp` L55-60

**改进**：在 `onSerialError` 中添加重连逻辑和上层通知：

```cpp
void PTZController::onSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError) return;
    qWarning() << "[PTZController] 串口错误:" << m_serial->errorString();
    emit serialError(m_serial->errorString());  // 新增信号通知上层
    // 可选：尝试重新打开
}
```

### L4：RecordThread cleanup() 释放顺序

**位置**：`RecordThread.cpp` L37-43

**改进**：调整为先释放 frame 再释放 buffer：

```cpp
void RecordThread::cleanup()
{
    if (m_yuvFrame) av_frame_free(&m_yuvFrame);      // 先释放 frame
    if (m_yuvBuffer[0]) av_freep(&m_yuvBuffer[0]);   // 再释放 buffer
    // ... 其余不变 ...
}
```

### L5：CaptureManager ImageProcessor 裸指针

**位置**：`capturemanager.cpp` L166

**改进**：确认 `ImageProcessor` 所有方法都是无状态的（只读），或改为传值/智能指针。

---

## 6. 修复实施顺序与验证计划

### 6.1 实施顺序

```
第1批（致命）：C1 → C2
     ↓ 编译 + 基本功能验证
第2批（严重）：S1 → S2 → S3 → S4
     ↓ 编译 + 跟踪功能验证
第3批（中等）：M1 → M2 → M3 → M4 → M5
     ↓ 编译 + 完整功能验证
第4批（低）：L1~L5（可选，按需）
```

### 6.2 每批验证检查清单

#### 第1批验证

- [x] 设备连接状态下关闭程序，3 秒内正常退出（C1）
- [x] 录像开始时编码器初始化失败，程序报错而不崩溃（C2）
- [x] 正常录像功能不受影响
- [x] 编译通过（MinGW 13.1 + Qt 6.10.1 Release，零错误零警告）

#### 第2批验证

- [x] 开始/停止录像时 PTZ 自动查询正常，无 Qt 线程警告（S1）
- [x] 登录/登出后镜头控制正常（S2）
- [x] 目标进入死区后云台完全静止，无微动（S3）
- [x] 跟踪帧率提升，大目标不卡顿（S4）
- [x] 编译通过（MinGW 13.1 + Qt 6.10.1 Release，零错误零警告）
- [ ] S4 nearestPow2 优化暂缓，待 padding=1.5 精度验证通过后再实施

#### 第3批验证

- [x] 长时间运行无内存泄漏（M1/M2）
- [x] 程序退出时无串口资源残留（M3）
- [x] CSV 数据完整，含 KF/PSR/控制指令字段（M4）
- [x] 重启后跟踪参数和 PTZ 配置保留（M5）
- [x] 编译通过（MinGW 13.1 + Qt 6.10.1 Release，零错误零警告）
- [ ] M3 实施方式调整：使用 `prepareForDestruction()` + `BlockingQueuedConnection` 替代方案中的 `deleteLater` 模式，更安全

### 6.3 回归测试

每批修复完成后执行回归测试：

1. 连接设备 → 登录 → 开始视频流 → 框选目标 → 跟踪 60 秒 → 停止跟踪 → 登出 → 关闭程序
2. 全程观察：无崩溃、无挂死、无 Qt 警告、日志输出正常

---

## 附录：修改文件清单

| 文件 | 涉及修复项 | 修改类型 |
|------|-----------|---------|
| `devicemanager.cpp` | C1, M2 | 死锁修复 + 异常保护 |
| `RecordThread.cpp` | C2, M2, L1, L4 | 空指针修复 + 异常保护 |
| `RecordThread.h` | C2 | 新增成员变量 |
| `mainwindow.cpp` | S1, M3, M4, M5 | 信号槽改 + 析构改 + 数据记录 + 配置 |
| `mainwindow.h` | S1 | 新增信号 |
| `ptzcontroller.h` | S1, M3, L3 | 原子变量 + 信号 |
| `ptzcontroller.cpp` | L3 | 错误恢复 |
| `lensmanager.h` | S2 | 新增信号 |
| `lensmanager.cpp` | S2 | 信号槽改 |
| `trackingcontroller.cpp` | S3, M4 | 死区修复 + D项修复 |
| `objecttracker.cpp` | S4 | padding + nearestPow2 |
| `DecodeThread.h` | M1 | atomic |
| `DecodeThread.cpp` | M1, M2 | atomic + try-catch |
| `datarecorder.h` | M4 | 结构体扩展 |
| `datarecorder.cpp` | M4 | CSV 扩展 |
| `configmanager.h` | M5 | 接口扩展 |
| `configmanager.cpp` | M5 | 持久化扩展 |
| `videodecoder.cpp` | L2 | 初始化 |

---

*文档结束*
