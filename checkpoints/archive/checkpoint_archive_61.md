# Checkpoint 2026-11-14 #61（归档）

- **记录人**：Antigravity (Claude Opus 5.5)
- **类型**：方案 04 LightTrack 上位机嵌入执行方案编制与基准事实核实
- **核心内容**：
  1. **执行方案交付**：编制 [Qt上位机嵌入LightTrack轻量单目标跟踪算法执行方案.md](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/Qt上位机嵌入LightTrack轻量单目标跟踪算法执行方案.md)，覆盖任务书六项任务（OnnxSotTracker 引擎、双内核切换、AMFT 接线、test_sot_tracker 工具、文件清单、阶段与 22 项验收矩阵）。
  2. **基准事实实地打点**（`SOT_Tracking_Baselines`，`anti_uav_yolo` 环境）：
     - LightTrackM 张量：模板 127 → `[1,96,8,8]`；搜索 255/256 → 16×16，288 → 18×18；权重加载 missing keys = 0。
     - 基准超参（OTB yaml + adapter）：`penalty_k=0.15`、`window_influence=0.45`、`lr=0.35`、`instance_size` 按面积比 0.004 在 256/288 间切换；输入 ImageNet mean/std 归一化。
     - Anti-UAV 20 组中 15 组使用 288、5 组使用 256；任务书单一 255 搜索规格无法复现 52.24% 基准，方案改为 `lighttrack_search_256.onnx` + `lighttrack_search_288.onnx` 双静态搜索图。
     - 基准 26.88 ms / 37.2 FPS 计时含 `cv2.imread` 与 Python 前后处理；纯模型 2.1 ms 待 Phase 2 C++ 工具实测确认。
     - 评测协议：AUC = 21 阈值成功率均值，首帧计入 GT 初始化框，Lost Rate = IoU==0 帧比例。
  3. **代码状态**：本检查点仅交付方案文档，生产代码未改动，未执行编译；LightTrack ONNX 尚未导出。
- **下一步**：按执行方案 P0 导出三份 ONNX 并完成 E1–E4 校验。

