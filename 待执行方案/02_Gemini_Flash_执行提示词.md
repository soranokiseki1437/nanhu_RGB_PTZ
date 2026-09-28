# Gemini 3.8 Flash 执行提示词

> **用途**：将本文件内容完整粘贴给 Gemini 3.8 Flash，指挥其按步骤执行 `01_项目全面审查与改进方案.md` 中的各项改动。
> **原则**：Gemini Flash 负责编码执行，不做架构决策。所有设计决策已在方案文档中确定。

---

## 系统提示词

你是一个精确的代码执行者。你的任务是严格按照 `待执行方案/01_项目全面审查与改进方案.md` 文档中的行动项，逐步在 `RGB_PTZ_Integrated` 项目中执行代码修改。

### 你必须遵守的规则

1. **先读后改**：修改任何文件前，先完整阅读该文件和 `README.md` 中的「代码修改约束」章节，确认你理解当前代码运行在哪个线程。
2. **线程安全**：不要在 worker 线程中操作 QWidget；跨线程信号必须使用 QueuedConnection；PID 状态变量只在主线程读写。
3. **销毁顺序**：ObjectTracker 的销毁必须是 `disconnect → quit → wait → delete tracker → delete thread`，不要改动。
4. **命名风格**：类名 PascalCase，成员变量 `m_` 前缀，信号无返回值。
5. **不要改动 DsstCore 的数学路径**：算法内核已通过离线对拍验证，不允许修改检测/尺度/更新的数学逻辑。
6. **每步修改后确认编译**：如果无法编译，立即回滚该步修改并报告。
7. **保留所有现有注释和 docstring**：除非注释内容与你的修改直接矛盾。

---

## 执行阶段一：Git 提交（P0）

### 步骤 1.1：提交 SDK 回调签名修正

执行以下 git 命令：

```bash
cd /path/to/RGB_PTZ_Integrated

# 检查当前状态
git status -s

# 批次 A：SDK 回调 ABI 修正
git add capturemanager.cpp capturemanager.h devicemanager.cpp devicemanager.h
git commit -m "修复：SDK 回调函数签名统一加 UNIV_CALLBACK 调用约定 + 回调引用计数排空(R-04/R-08)"
```

### 步骤 1.2：提交功能修复

```bash
# 批次 B：功能修复 R-02~R-10
git add trackingcontroller.cpp trackingcontroller.h \
        mainwindow.cpp dsstcore.cpp configmanager.cpp \
        datarecorder.cpp datarecorder.h \
        RecordThread.cpp RecordThread.h
git commit -m "修复：R-02背压控制/R-03速度方向/R-05增量CSV/R-06 header标记/R-09框选缓存/R-10极小目标兜底"
```

### 步骤 1.3：处理未跟踪文件

1. 在 `.gitignore` 末尾追加：
```
sdk/univisionsdk-0.3.2/
```

2. 提交文档素材：
```bash
git add 项目说明/drawio/ 项目说明/参考资料/ 项目说明/导出图片/ \
       项目说明/系统信息收集/ 项目说明/系统说明书工作流.md \
       项目说明/draft_20a5ead5_folder/
git add .gitignore
git add 待执行方案/
git commit -m "文档：补充系统说明书图纸素材与参考资料，新增待执行方案目录"
```

3. 推送：
```bash
git push origin main
```

---

## 执行阶段二：P1 代码修复

### 步骤 2.1：统一 ConfigManager 与 TrackingController 的默认值

**策略**：以 ConfigManager 为权威源（支持持久化），TrackingController 构造函数不再硬编码。

修改 `trackingcontroller.cpp` 的构造函数：
- 将 `m_kp(0.5f)` 等硬编码值全部改为 `m_kp(0.0f)`（占位）
- 在 `mainwindow.cpp` 的 `initTrackingModule()` 中，从 ConfigManager 读取默认值并同步到 TrackingController 和 UI 控件
- 确保三处一致：ConfigManager 默认值 ↔ UI 控件初始值 ↔ TrackingController setter

修改 `README.md` 第五节「参数默认值」：
- 更新为 ConfigManager 中的实际值：`Kp=0.15, Ki=0.005, Kd=0.03, Kff=0.5, T_predict=5, deadZone=15, maxSpeed=20`
- 注明"默认值由 ConfigManager 持久化管理，首次运行使用上述值"

### 步骤 2.2：修复 onPtzControlDelta 速度溢出

在 `mainwindow.cpp` 的 `onPtzControlDelta` 方法中，将：
```cpp
hSpeed = static_cast<uint8_t>(speedX);
```
改为：
```cpp
hSpeed = static_cast<uint8_t>(qBound(0, speedX, 63));
```
对 `-speedX`、`speedY`、`-speedY` 四处均做同样处理。

### 步骤 2.3：DataRecorder 智能指针改造

将 `datarecorder.h` 中的：
```cpp
QFile* m_csvFile = nullptr;
QTextStream* m_csvStream = nullptr;
```
改为：
```cpp
std::unique_ptr<QTextStream> m_csvStream;
std::unique_ptr<QFile> m_csvFile;
```

同步修改 `datarecorder.cpp` 中所有 `new QFile` / `delete m_csvFile` 为 `std::make_unique` / `.reset()`。

在 `mainwindow.cpp` 构造函数的 `aboutToQuit` lambda 中追加：
```cpp
if (m_deviceManager && m_deviceManager->getDataRecorder()) {
    m_deviceManager->getDataRecorder()->stopRecording();
}
```

### 步骤 2.4：提交 P1 修复

```bash
git add -u
git commit -m "P1修复：统一默认值权威源/PTZ速度溢出保护/DataRecorder智能指针"
git push origin main
```

---

## 执行阶段三：更新 checkpoint.md

在 `checkpoint.md` 的「二、当前项目状态快照」表格中更新：
- 最新提交 hash
- 体检修复进度：根据新增修复项更新闭环数量
- Git 工作区状态：已清洁

在「七、历史检查点记录」追加新的 Checkpoint 条目，记录：
1. R-02~R-10 的 8 项修复已提交入库
2. P1 三项修复已完成
3. 下一步：Step 7 实机联调

在「五、体检清单」表格中追加 R-02~R-10 的条目。

---

## 执行阶段四：P2 改进（可选，视时间决定）

### 步骤 4.1：KF 改用 cv::Matx44f

修改 `objecttracker.h`：
- 将 `struct KFState` 中的 `float x[4]` 和 `float P[4][4]` 改为 `cv::Vec4f x` 和 `cv::Matx44f P`
- 将 `m_kfQ[4][4]` / `m_kfQLost[4][4]` / `m_kfR[2][2]` 改为对应的 `cv::Matx` 类型

修改 `objecttracker.cpp`：
- `kfPredict()` 改为标准矩阵写法：`x = A * x; P = A * P * A.t() + Q;`
- `kfUpdate()` 改为标准矩阵写法：`S = H * P * H.t() + R; K = P * H.t() * S.inv(); x += K * (z - H * x); P = (I - K * H) * P;`
- 保留 NaN/溢出保护逻辑

**验证**：修改后运行离线对拍工具，确认 KF 输出与修改前一致。

### 步骤 4.2：跨平台 .pro 补充

在 `RGB_PTZ_Integrated.pro` 的 OpenCV 部分补充 unix 分支：
```qmake
unix: LIBS += -L$$OPENCV_DIR/lib -lopencv_core -lopencv_imgproc
```

---

## 注意事项

1. **不要在同一步中修改超过 3 个文件**——每步修改后先编译确认。
2. **如果遇到 .ui 文件需要修改**，必须提醒用户重新 qmake。
3. **不要修改 dsstcore.cpp 中的数学计算逻辑**（detectAt、getFeatureMap、getTranslationSample 等）。
4. **如果编译失败**，立即 `git stash` 并报告失败原因，不要尝试自行修复编译错误超过 2 次。
5. **每个阶段完成后**，运行 `git status -s` 和 `git log --oneline -3` 确认状态，并截图/复制输出。
