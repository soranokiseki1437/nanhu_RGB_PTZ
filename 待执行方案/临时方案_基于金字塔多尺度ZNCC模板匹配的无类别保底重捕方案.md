# 临时方案：基于金字塔多尺度ZNCC模板匹配的无类别保底重捕方案

> **方案性质**：临时实施方案（用完即归档至 `待执行方案/archive/`）。  
> **适用目标**：在手动框选目标模式下，解决深度学习检测器因类别受限或置信度偏低导致丢失无法全图重捕的问题；建立基于初始框选图块的无类别传统模板匹配全图保底重捕链路。  
> **涉及模块**：[objecttracker.h](file:///d:/PyProjects/RGB_PTZ_Integrated/objecttracker.h)、[objecttracker.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/objecttracker.cpp)。

---

## 一、 系统架构与双轨重捕拓扑

在现有的 AMFT 协同状态机中，将丢失重捕扩展为“深度检测 + 传统金字塔模板匹配”双轨串联拓扑：

```
                    ┌─────────────────────────┐
                    │ 目标进入失锁 (TRACK_LOST) │
                    └────────────┬────────────┘
                                 │ 定期唤醒 (3 FPS)
                                 ▼
                    ┌─────────────────────────┐
                    │   第一轨: YOLO 深度检测  │
                    └────────────┬────────────┘
                                 │
                 ┌───────────────┴───────────────┐
                 ▼ (命中合格候选且通过锚点)           ▼ (未检出 / 候选全被拦截 / 无类别)
    ┌──────────────────────────┐    ┌──────────────────────────┐
    │ 状态恢复 TRACK_REACQUIRED │    │  第二轨: 金字塔多尺度ZNCC  │
    └──────────────────────────┘    └────────────┬─────────────┘
                                                 │
                                 ┌───────────────┴───────────────┐
                                 ▼ (峰值相关度 > 门限且通过锚点)    ▼ (未通过)
                    ┌──────────────────────────┐    ┌──────────────────────────┐
                    │ 状态恢复 TRACK_REACQUIRED │    │ 维持 LOST 态，等待下一周期 │
                    └──────────────────────────┘    └──────────────────────────┘
```

---

## 二、 数据结构设计与接口契约

### 1. `TargetAnchor` 扩展存储原分辨率图块
在 [objecttracker.h](file:///d:/PyProjects/RGB_PTZ_Integrated/objecttracker.h) 的 `TargetAnchor` 结构体中扩展原始图块：
```cpp
struct TargetAnchor {
    bool valid = false;
    cv::Rect2f initBbox;
    cv::Mat origPatchBgr;   // 框选时的原始高分辨率 BGR 彩色图块
    cv::Mat origPatchGray;  // 框选时的原始灰度图块 (CV_8UC1)
    cv::Mat histHS;         // 16x8 H-S 颜色直方图
    cv::Mat histV;          // 16x1 V 灰度直方图
    cv::Mat patchNorm;      // 24x24 归一化小图 (用于高频 ZNCC 快速复核)
    bool hasTexture = false;
    bool isColor = true;
    float meanSat = 0.0f;
};
```

### 2. 传统重捕核心函数签名
在 `ObjectTracker` 类中声明新方法：
```cpp
bool matchTemplateRecovery(const cv::Mat& image, cv::Rect2f& outBox, float& outScore);
```

---

## 三、 核心算法执行步骤

### 步骤 1：初始化固化（`ObjectTracker::init`）
在接收用户框选区域初始化时：
1. 对输入的原始帧 $I$ 与框选框 $B$ 取整，截取有效区域并做边界越界钳制：
   ```cpp
   cv::Rect cropRoi = cv::Rect(targetRect.toRect()) & cv::Rect(0, 0, image.cols, image.rows);
   m_anchor.origPatchBgr = image(cropRoi).clone();
   cv::cvtColor(m_anchor.origPatchBgr, m_anchor.origPatchGray, cv::COLOR_BGR2GRAY);
   ```
2. 保留原有的直方图与 `patchNorm` 提取逻辑，完成不可变基准构建。

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
将候选框 $B_{\text{cand}}$ 传入既有的严格防护网：
```cpp
float anchorScore = 0.0f;
if (!verifyRecoveryCandidate(image, candBox, true, anchorScore)) {
    qCDebug(trackerLog) << "[TemplateRecovery] 传统模板极值候选未通过锚点一致性校验，已拦截";
    return false;
}
```
只有当其 HSV 色彩直方图、灰度结构与初始模板高度一致时，才确认重捕成功。

### 步骤 5：状态机复位与伺服软启动
核验通过后，调用 `recoverWithAnchorState(image, newRoi)`：
1. 若为 LightTrack，重设 `setState`，清除历史速度扰动；
2. 若为 DSST，调用 `blendBackup(0.85f)` 恢复滤波器并重设尺度；
3. 清零卡尔曼滤波速度历史，将 `m_trackingState` 置为 `TRACK_REACQUIRED`；
4. 发出 `trackingRecovered()` 信号，通知主界面与云台恢复跟踪。

---

## 四、 编译与回归验证方案

1. **增量编译**：
   使用 Qt 6.12.0 / MinGW 13.1 64-bit 重新编译上位机，验证 0 告警 0 错误；
2. **离线专项测试**：
   在 `tools/test_amft.cpp` 中增加对 `matchTemplateRecovery` 的单元测试：在纯黑或异物背景下注入位移目标，验证传统模板匹配的独立全图拉回能力与异物拦截率；
3. **实机闭环验证**：
   启动 `RGB_PTZ_Integrated.exe`，在视频画面中框选无类别物体（如一盒纸巾或特定标记物），移出视野再从另一侧移入，验证在 YOLO 零检测输出的工况下，传统模板能否将目标拉回。
