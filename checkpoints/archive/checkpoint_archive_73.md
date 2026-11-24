# 检查点归档记录 Checkpoint #73

- **记录时间**：2026-11-24
- **记录人**：Antigravity (Gemini 4 Hydrogen High)
- **类型**：目标丢失状态保持、不可变外观锚点防异物劫持及原目标回归重捕闭环修复
- **核心内容**：
  1. **初始目标不可变模板与外观锚点固化**：
     - `ISotTracker.h` 与 `onnxsottracker.h` / `onnxsottracker.cpp` 新增 `m_anchorTemplateFeat` 与 `peekBox()`，在 `init()` 时固化 `1×96×8×8` 初始模板特征；
     - `dsstcore.h` / `dsstcore.cpp` 新增 `detectOnlyBackup()` 与 `setScale()`，利用 `m_hfNumBackup`/`m_hfDenBackup` 支持任意尺度锚点探测；
     - `objecttracker.h` / `objecttracker.cpp` 在 `init()` 时固化 HSV 色彩-亮度直方图与 `24×24` ZNCC 模板 `m_anchor`，全程保持不可变。
  2. **实时异常监测、瞬时冻结与非破坏性重捕恢复**：
     - 在 `TRACK_NORMAL` 中引入内核置信底线（DSST `PSR >= 7.0`、LightTrack `score >= 0.70`）与初始外观一致性监测（`histSim` / `zncc`），在首个异常帧即刻冻结 DSST 学习；
     - `tryDetectAndRecover()` 对每个 YOLO 候选框强制执行 `verifyRecoveryCandidate()`（外观直方图 + ZNCC + 内核锚点探测），拦截异物劫持；`TRACK_LOST` 双帧确认增加空间连续性约束；
     - 命中原目标后通过 `recoverWithAnchorState()` 恢复状态（LightTrack 调用 `setState()`，DSST 调用 `blendBackup(0.85f)` + `setPosition()` + `setScale()`），严禁覆写初始模板；丢失期间对 KF 速度施加阻尼并将预测位置钳制在图像边界内。
  3. **编译与回归测试验收**：
     - 主工程 `RGB_PTZ_Integrated.exe`、`test_sot_tracker.exe`、`test_amft.exe` Release 编译通过；
     - `tools/test_amft.cpp` 新增第 5 项测试（DSST 与 LightTrack 丢失保持、异物 B 劫持拦截、原目标 A 远端回归锁定），实跑 5/5 PASS（序列均值误差 `2.68 px`，DSST 回归误差 `0.00 px`，LightTrack 回归误差 `1.17 px`）。
- **下一步**：推进方案 05（LightTrack 深度跟踪）整机实机闭环验证；开展外场变焦与抗遮挡实机联调；筹备方案 06（RK3588 边缘端量化）与方案 07（自研网络重训消融）。
