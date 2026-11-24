# 临时方案：基于金字塔多尺度ZNCC模板匹配的无类别保底重捕方案

> **方案性质**：待实施临时方案（实施后归档至 `待执行方案/archive/`）。  
> **适用目标**：在手动框选目标模式下（`m_redetectionEnabled == false`），解决深度学习检测器因类别受限或置信度偏低导致丢失后无法全图重捕的问题；建立基于初始框选图块的无类别传统模板匹配全图保底重捕链路。  
> **涉及模块**：[objecttracker.h](file:///d:/PyProjects/RGB_PTZ_Integrated/objecttracker.h)、[objecttracker.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/objecttracker.cpp)。  
> **前置条件**：YOLO 类别标签映射修复已闭环（Checkpoint #75）。

---

## 一、 系统架构与双轨重捕拓扑

在既有 AMFT 协同状态机的 TRACK_LOST 低频检索路径（[objecttracker.cpp L1164~L1206](file:///d:/PyProjects/RGB_PTZ_Integrated/objecttracker.cpp#L1164-L1206)）中，将丢失重捕扩展为"深度检测 + 传统金字塔模板匹配"双轨串联拓扑：

```
                       ┌──────────────────────────────┐
                       │  目标进入失锁 (TRACK_LOST)      │
                       └──────────────┬───────────────┘
                                      │ 低频唤醒 (每 10 帧, ≈3 FPS)
                                      ▼
                 ┌────────────────────────────────────────────┐
                 │  第一轨: YOLO 深度检测 (tryDetectAndRecover)  │
                 │  仅在 m_redetectionEnabled 时生效            │
                 └──────────────────┬─────────────────────────┘
                                    │
             ┌──────────────────────┴──────────────────────────┐
             ▼ (命中合格候选且通过锚点)                            ▼ (未检出/YOLO未启用/无类别)
     ┌──────────────────────────┐    ┌──────────────────────────────────────┐
     │  恢复 TRACK_REACQUIRED   │    │  第二轨: 金字塔多尺度ZNCC模板匹配       │
     └──────────────────────────┘    │  (matchTemplateRecovery, 无类别依赖)  │
                                     └──────────────────┬─────────────────┘
                                                        │
                                 ┌──────────────────────┴──────────────────────┐
                                 ▼ (峰值 >= 0.50 且通过锚点校验)                 ▼ (未通过)
                     ┌──────────────────────────┐    ┌──────────────────────────┐
                     │  恢复 TRACK_REACQUIRED   │    │ 维持 LOST 态, 等待下一周期 │
                     └──────────────────────────┘    └──────────────────────────┘
```

> 在手动框选模式（`m_redetectionEnabled == false`）下，`tryDetectAndRecover` 不被调用（L1163 条件跳过），第一轨跳空，第二轨传统模板匹配成为唯一重捕通道。

---

## 二、 数据结构设计与接口契约

### 1. `TargetAnchor` 扩展存储原分辨率图块
在 [objecttracker.h](file:///d:/PyProjects/RGB_PTZ_Integrated/objecttracker.h) 的 `TargetAnchor` 结构体（L184~L193）中追加两个字段：
```cpp
struct TargetAnchor {
    bool valid = false;
    cv::Rect2f initBbox;
    cv::Mat origPatchBgr;   // [新增] 框选时的原始高分辨率 BGR 彩色图块（用于金字塔模板匹配）
    cv::Mat origPatchGray;  // [新增] 框选时的原始灰度图块 (CV_8UC1)
    cv::Mat histHS;         // 16x8 H-S 颜色直方图（CV_32F，L1 归一化）【既有字段】
    cv::Mat histV;          // 16x1 V/灰度直方图（CV_32F，L1 归一化）【既有字段】
    cv::Mat patchNorm;      // 24x24 零均值单位范数灰度模板（用于 ZNCC）【既有字段】
    bool hasTexture = false;
    bool isColor = true;
    float meanSat = 0.0f;
};
```
> `origPatchBgr` / `origPatchGray` 为本方案新增字段，用于金字塔多尺度模板匹配搜索。既有的 `patchNorm`（24x24）仅用于锚点一致性快速 ZNCC 复核，分辨率不足以支撑全图多尺度粗搜索。

### 2. 传统重捕核心函数签名
在 `ObjectTracker` 类中声明新方法：
```cpp
bool matchTemplateRecovery(const cv::Mat& image, cv::Rect2f& outBox, float& outScore);
```

---

## 三、 核心算法执行步骤

### 步骤 1：初始化固化（`ObjectTracker::init`，L121~L191）
在既有的 `extractPatchAppearance` 调用（L182~L184）之后、`m_initialized = true`（L186）之前，追加原始图块截取：
```cpp
// 固化原始高分辨率图块（用于金字塔多尺度 ZNCC 全图重捕）
cv::Rect cropRoi = roi & cv::Rect(0, 0, image.cols, image.rows);
if (cropRoi.width >= 4 && cropRoi.height >= 4) {
    m_anchor.origPatchBgr = image(cropRoi).clone();
    cv::cvtColor(m_anchor.origPatchBgr, m_anchor.origPatchGray, cv::COLOR_BGR2GRAY);
}
```
> 注意：此处的 `roi` 变量（L162~L163）已经过边界钳制，直接复用；`image` 为 BGR 格式 `cv::Mat`（L156 由 `QImageToMat` 转换而来）。

### 步骤 2：金字塔粗搜索（`matchTemplateRecovery`）
1. **输入降采样**：
   - 降采样尺度因子设为 $s = 0.25$（$1920\times 1080 \to 480\times 270$）；
   - `cv::resize(graySearch, graySmall, cv::Size(), s, s, cv::INTER_AREA);`
2. **多尺度模板循环**：
   - 设定 3 组尺度：$scale \in \{0.85, 1.00, 1.15\}$；
   - 对每个尺度缩放模板：
     $$w_t = \text{clamp}(w_{\text{orig}} \cdot s \cdot scale, 12, \text{graySmall.cols}-2)$$
     $$h_t = \text{clamp}(h_{\text{orig}} \cdot s \cdot scale, 12, \text{graySmall.rows}-2)$$
   - 调用 `cv::matchTemplate(graySmall, tplScale, resMap, cv::TM_CCOEFF_NORMED);`；
   - 提取各尺度响应图的最大值 `maxVal` 与极值位置 `maxLoc`。
3. **最优尺度决策**：
   - 选取 3 个尺度中全局响应最大者 $(bestScore, bestLoc, bestScale)$；
   - 粗搜索阈值设定为 $bestScore \ge 0.50$（若低于此值直接返回 `false`，判定无目标）。

### 步骤 3：原图映射与候选框还原
将低分辨率下的极值点映射回原图坐标空间：
$$c_x = (bestLoc.x + w_t \cdot 0.5) / s$$
$$c_y = (bestLoc.y + h_t \cdot 0.5) / s$$
$$W_{\text{recov}} = w_{\text{orig}} \cdot bestScale, \quad H_{\text{recov}} = h_{\text{orig}} \cdot bestScale$$
构建候选框：
$$B_{\text{cand}} = \text{Rect2f}\big(c_x - W_{\text{recov}}\cdot 0.5,\; c_y - H_{\text{recov}}\cdot 0.5,\; W_{\text{recov}},\; H_{\text{recov}}\big)$$

### 步骤 4：不可变锚点一致性核验（防异物劫持）
将候选框 $B_{\text{cand}}$ 传入既有的 `verifyRecoveryCandidate`（[objecttracker.cpp L487~L521](file:///d:/PyProjects/RGB_PTZ_Integrated/objecttracker.cpp#L487-L521)）：
```cpp
float anchorScore = 0.0f;
if (!verifyRecoveryCandidate(image, candBox, true, anchorScore)) {
    qCDebug(trackerLog) << "[TemplateRecovery] 传统模板极值候选未通过锚点一致性校验，已拦截";
    return false;
}
```
HSV 色彩直方图巴氏距离 + 灰度 ZNCC + 内核特征三重校验全部通过后，才确认重捕成功。

### 步骤 5：状态机复位与伺服软启动
核验通过后，复用既有的 `recoverWithAnchorState(image, newRoi)`（[L523~L558](file:///d:/PyProjects/RGB_PTZ_Integrated/objecttracker.cpp#L523-L558)）：
1. LightTrack 内核：调用 `m_sot->setState()` 重设位置与尺寸，保留原始模板特征不被覆写；
2. DSST 内核：调用 `m_core.blendBackup(0.85f)` 从初始备份高比重恢复滤波器并重设尺度；
3. 清零卡尔曼滤波速度（`m_kf.x = cv::Vec4f(m_posX, m_posY, 0.0f, 0.0f)`）并重置协方差，杜绝超大速度跳变的机电伺服冲击；
4. 重置 AMFT 评估器（`m_evaluator.init(...)`），将 `m_trackingState` 置为 `TRACK_REACQUIRED`；
5. 发出 `trackingRecovered()` 与 `trackingStateChanged()` 信号，通知主界面与云台恢复跟踪。

> 状态恢复流程与 `tryDetectAndRecover` 中的恢复路径（L744~L762）完全对齐，确保 YOLO 重捕路径与传统模板重捕路径复用同一套机电伺服软启动逻辑。

---

## 四、 调用集成方案

### 4.1 SOT 纯手动模式下的集成入口
在 `ObjectTracker::update` 的 SOT 手动框选路径（L1040~L1161，`!m_redetectionEnabled` 分支）中，当内核 PSR 连续低于基线进入遮挡保持态后（L1098~L1105），在 `reDetect(image)` 调用（L1178 附近）失败时追加 `matchTemplateRecovery` 作为保底：

```cpp
// 旧版九宫格局部模板搜索
bool recovered = reDetect(image);

// [新增] 若九宫格未命中，启用传统金字塔多尺度模板全图搜索
if (!recovered && m_anchor.valid && !m_anchor.origPatchGray.empty()) {
    cv::Rect2f outBox;
    float outScore = 0.0f;
    if (matchTemplateRecovery(image, outBox, outScore)) {
        float anchorScore = 0.0f;
        if (verifyRecoveryCandidate(image, outBox, true, anchorScore)) {
            cv::Rect newRoi(static_cast<int>(outBox.x), static_cast<int>(outBox.y),
                            static_cast<int>(outBox.width), static_cast<int>(outBox.height));
            newRoi &= cv::Rect(0, 0, image.cols, image.rows);
            if (recoverWithAnchorState(image, newRoi)) {
                m_kf.x = cv::Vec4f(m_posX, m_posY, 0.0f, 0.0f);
                m_kf.P = cv::Matx44f::eye() * 8.0f;
                recovered = true;
            }
        }
    }
}
```

### 4.2 AMFT 模式下的集成入口
在 `tryDetectAndRecover` 返回 `false` 且处于 TRACK_LOST 态时（L1191~L1205），追加传统模板作为第二轨保底，调用逻辑同上。

---

## 五、 编译与回归验证方案

1. **增量编译**：
   使用 Qt 6.12.0 / MinGW 13.1 64-bit 重新编译上位机，验证 0 告警 0 错误；
2. **离线专项测试**：
   在 [tools/test_amft.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/tools/test_amft.cpp) 中增加对 `matchTemplateRecovery` 的单元测试用例：
   - **测试 A（正样本）**：在地对空长焦序列中框选 UAV，人工注入 50~200px 位移后，验证传统模板独立全图拉回能力；
   - **测试 B（负样本/异物拦截）**：在异物或纯黑背景下验证传统模板匹配的低误触发率，并确认通过 `verifyRecoveryCandidate` 锚点校验拦截；
3. **既有回归**：
   确认 `test_amft.exe` 原有 5/5 PASS 不受影响（DSST/LightTrack 防劫持与原目标回归用例不退化）；
4. **实机闭环验证**：
   启动 `RGB_PTZ_Integrated.exe`，在视频画面中框选无类别物体（如纸巾盒或特定标记物），移出视野后从另一侧移入，验证在 YOLO 零检测输出工况下传统模板能否将目标拉回。
