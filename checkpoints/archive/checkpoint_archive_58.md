# Checkpoint Archive: #58

- **记录人**：Antigravity (Gemini 3.8 Flash)
- **时间**：2026-11-12
- **类型**：Qt 上位机多级 Shadow Build 模型检索鲁棒化与多目标检测跟踪退出交互闭环
- **核心内容**：
  1. **多级目录模型检索与筛选优化**：
     - 在 `MainWindow::resolveModelPath` 与 `MainWindow::refreshModelList` 中增加 `../../models` 与 `../../../models` 等多级父路径搜索，兼容 Qt Creator 深度 Shadow Build 目录结构；
     - 在 `MainWindow::refreshModelList` 增加 SOT 双分支权重（`sot_*.onnx`）排除过滤，确保 `comboAiModel` 仅列出 YOLO 检测模型；
     - 同步增强 `ObjectTracker` 与 `TrackingController` 默认检测模型的候选路径探测深度。
  2. **多目标检测模式“退出跟踪”控制交互闭环**：
     - 在 `mainwindow.ui` 的“多目标检测”组中增加专用 `btnMotTrackStop`（退出跟踪）控件；
     - 在 `MainWindow` 中建立槽函数关联与生命周期状态机驱动：锁定目标并触发闭环伺服时使能该按钮，点击退出时重置检测与云台伺服；
     - 在 `MainWindow::on_btnAutoDetect_clicked` 增加前置状态保护，重新触发巡检时自动停止既有跟踪与 PTZ 机电伺服。
  3. **单目标框选模式（SOT）回归原生滤波**：
     - 在 `ObjectTracker` 与 `TrackingController` 引入 `setRedetectionEnabled` 模式开关，框选跟踪时关闭全局重检测；
     - 框选模式依据 30 帧平稳 PSR 均值基线判定（连续 5 帧低于 0.75×基线进入遮挡）；
     - 遮挡期间执行卡尔曼滤波（KF Coasting）平滑外推，冻结相关滤波学习；局部恢复基于既有 DSST 模板执行九宫格探测（`reDetect`）；
     - 修复 `TrackingController::processFrame` 在 `Lost` 状态下丢弃帧导致无法恢复的缺陷，持续向跟踪器投递视频帧。
  4. **事实源与版本库强一致同步**：
     - 更新唯一事实源 `checkpoint.md`，MinGW 完整编译验证无误。
