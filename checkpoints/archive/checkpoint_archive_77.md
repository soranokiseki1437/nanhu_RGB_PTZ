# 检查点记录：Checkpoint 2026-11-24 #77

- **记录人**：Antigravity (Gemini 3.8 Flash High)
- **类型**：重检测架构误导性注释治理 + AGENTS.md 规范强化 + 编译回归闭环
- **核心内容**：
  1. **重检测架构事实澄清与方案定性**：
     - 查证并确认：上位机 UI 在手动框选（`on_radioModeManual_toggled`）与启动跟踪（`on_btnTrackStart_clicked`）时，运行时均 100% 显式使能 `setRedetectionEnabled(true)` 并由 YOLO 协同重捕；
     - 确认《临时方案_基于金字塔多尺度ZNCC模板匹配的无类别保底重捕方案.md》无需执行，其基于像素级 ZNCC 的全图粗搜索在物理实测中已证实姿态敏感（偏转 4° 漏检）且背景假峰高达 0.57，方案维持归档状态，不合入生产代码；
  2. **代码注释历史修订痕迹清理**：
     - 剔除 `objecttracker.h`、`objecttracker.cpp` 与 `trackingcontroller.h` 中与当前系统事实冲突的过渡性/辩解性误导注释，改用纯粹客观的功能定义描述；
  3. **规范强化与行为准则升级**：
     - 更新 AGENTS.md 第一章第 2 条，增加“代码注释铁律（严防误导开发）”，明文严禁在代码注释中遗留任何历史修订痕迹、与过去版本的对比或与当前运行时调用事实脱节的旧版辩解词；
  4. **编译构建与回归验收**：
     - Release 重新构建 0 错误 0 告警，`RGB_PTZ_Integrated.exe` 生成完好；
     - 执行 `.\run_test.ps1`（`test_detector.exe`），5/5 PASS 全项通过（DirectML 纯推理 3.36ms，IoU 99.0%，中心误差 0.64px，200 轮内存零泄漏）。
