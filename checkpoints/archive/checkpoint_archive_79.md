# 检查点记录：Checkpoint 2026-11-25 #79

- **记录人**：Antigravity (Gemini 4 Hydrogen XHigh)
- **类型**：现有 YOLO 重检测链路 (`tryDetectAndRecover`) 与路线 A (金字塔 ZNCC) C++ 端到端基准对比与门限归因
- **核心内容**：
  1. **C++ DirectML 实机链路基准搭建与复现验证**：
     - 编写独立离线评测工具 [tools/bench_redetect.pro](file:///d:/PyProjects/RGB_PTZ_Integrated/tools/bench_redetect.pro) 与 [tools/bench_redetect.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/tools/bench_redetect.cpp)，直接调用生产组件 `OnnxDetector` (DirectML RTX 5070)、`DsstCore::detectOnlyBackup`、`OnnxSotTracker::peekBox` 与路线 A OpenCV 链路，结果归档至 [scripts/benchmark_yolo_vs_route_a.json](file:///d:/PyProjects/RGB_PTZ_Integrated/scripts/benchmark_yolo_vs_route_a.json)；
     - 评测集与协议：Anti-UAV 7 组序列（925 采样有效帧 + 64 目标出视野缺失帧，Frame 0 初始锚点，零位置先验全图冷重捕，单帧候选验收）。
  2. **核心实测对比指标**：
     - `YOLO11n_UAV_Raw` (`yolo11n_uav_ft.onnx` + 几何滤波)：AUC **56.57%**、DP@20 **67.78%**、mIoU **0.5614**、有效帧劫持 **3.03%**、缺失帧误报 **9.4% (6/64)**、耗时 **4.87 ms**；
     - `YOLO11n_UAV_Exist_LT` (结合 LightTrack `peekBox >= 0.68` 锚点校验)：AUC **53.15%**、DP@20 **62.59%**、mIoU **0.5240**、HitCLE **55.4 px**、有效帧劫持 **3.46%**、缺失帧误报 **3.1% (2/64)**、耗时 **7.35 ms**；
     - `YOLO11n_UAV_Exist_DSST` (结合 DSST `PSR >= 6.0` 锚点校验)：AUC **38.20%**、DP@20 **42.81%**、mIoU **0.3616**、HitCLE **22.0 px**、有效帧劫持 **0.86%**、缺失帧误报 **0.0% (0/64)**、耗时 **11.68 ms**；
     - `YOLOv8n_COCO_Exist_LT` / `Exist_DSST` (默认 COCO-80 权重)：AUC **22.05% / 17.10%**、DP@20 **21.95% / 15.78%**、有效帧劫持 **3.68% / 0.86%**、缺失帧误报 **0.0%**、耗时 **7.43 / 9.98 ms**；
     - `RouteA_ZNCC_Color` (金字塔 ZNCC + 颜色锚点)：AUC **24.07%**、DP@20 **26.38%**、mIoU **0.2096**、HitCLE **292.0 px**、有效帧劫持 **21.19%**、缺失帧误报 **0.0%**、耗时 **7.34 ms**；
     - `RouteA_ZNCC_ExistVerify_DSST` (金字塔 ZNCC + 完整 DSST 锚点校验)：AUC **22.91%**、DP@20 **24.22%**、HitCLE **262.2 px**、有效帧劫持 **14.70%**、耗时 **11.03 ms**；
     - **串联路线 A 保底效果**：
       - `Hybrid_YOLO11n_UAV_LT_RouteA`：AUC 从 53.15% 升至 **53.24% (+0.09%)**，DP@20 从 62.59% 升至 **62.81% (+0.22%)**，但有效帧误劫持率由 3.46% 恶化至 **12.54% (+9.08%)**，HitCLE 由 55.4 px 升至 **134.0 px**；
       - `Hybrid_YOLO11n_UAV_DSST_RouteA`：AUC 由 38.20% 升至 **39.19% (+0.99%)**，有效帧误劫持率由 0.86% 恶化至 **15.68% (+14.82%)**；
       - `Hybrid_YOLOv8n_COCO_DSST_RouteA`：AUC 由 17.10% 升至 **29.88% (+12.78%)**，有效帧误劫持率由 0.86% 恶化至 **20.43%**。
  3. **既有链路损耗根因定位**：
     - **`areaRatio >= 0.15` 截断远距缩放目标**：在 `video01` 中，`yolo11n_uav_ft.onnx` 在 700/1040 帧置信度达 0.78~0.79（中心误差 <5px），但因目标面积相对 Frame 0 缩至 `0.08~0.12`，被 `objecttracker.cpp` `areaRatio < 0.15` 门限直接过滤；
     - **DSST 静态备份对 3D 旋转过严**：在 `video05` 机动转向序列中，`YOLO11n_UAV_Raw` 取得 **94.69%** DP@20（LightTrack 锚点分支保留 **78.76%**），而 DSST 锚点分支因 `PSR < 6.0` 截断导致 DP@20 降至 **0.00%**。
