# Checkpoint 2026-11-16 #69

- **记录人**：Antigravity (Gemini 3.8 Flash High)
- **类型**：WP0~WP7 全量达标收官（Qt 上位机嵌入 LightTrack 轻量单目标跟踪算法工程链路跑通）
- **核心内容**：
  1. **WP0~WP2 离线对齐与评测基线建立**：
     - 官方权重归档至 `weights/sot/lighttrack_m.pth`；导出三份 opset 17 纯静态 ONNX 模型（127/256/288）并通过 E1~E3 校验；
     - Python 参考评测完成全量 20 组真机序列（PyTorch AUC 52.24%、ONNX AUC 50.96%）；
     - C++ DirectML 离线工具 `test_sot_tracker` 完成 24,784 帧开环回放（L3 几何一致性 99.98%，分类置信度误差 $2.48\times 10^{-4}$）与闭环全量评测（AUC 50.53%，DP@20 63.49%，T1 纯前向 P50 1.72ms，P99 3.65ms）。
  2. **WP3 `ObjectTracker` 跟踪主干双内核集成**：
     - 在 [objecttracker.h](file:///d:/PyProjects/RGB_PTZ_Integrated/objecttracker.h) / [objecttracker.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/objecttracker.cpp) 中新增 `TrackResult::procMs`、`TrackingKernel` 枚举（`KERNEL_DSST` vs `KERNEL_LIGHTTRACK`）；
     - 实现 `reinitKernel` 与 `runKernel` 分派逻辑，完成目标尺寸映射（`m_currentScale=1.0`）、KF 状态保护与 LightTrack 下的 `peek()` 遮挡重捕；
     - 严格保持 DSST 传统相关滤波原有逻辑路径不受干扰。
  3. **WP4 `TrackingController` 协同闭环**：
     - 管理 `OnnxSotTracker` 单例生命周期与动态注入；通过 `resolveModelPath()` 解析多候选绝对路径；
     - 飞行记录黑匣子 CSV 表头与数据行末尾扩展 `proc_ms` 与 `kernel` 两列，提供完整外场飞行复盘数据源。
  4. **WP5 UI 交互与配置持久化**：
     - [mainwindow.ui](file:///d:/PyProjects/RGB_PTZ_Integrated/mainwindow.ui) 在模式组中新增 `comboTrackKernel` 下拉菜单（支持实时切换 DSST / LightTrack）；
     - [configmanager.h](file:///d:/PyProjects/RGB_PTZ_Integrated/configmanager.h) / [configmanager.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/configmanager.cpp) 增加 `tracking/kernel` 读写；
     - `refreshModelList()` 增加 `lighttrack_` 前缀过滤，避免 SOT 权重混入 YOLO 列表。
  5. **WP6 AMFT 网格标定与回归测试**：
     - `test_sot_tracker --amft` 在 20 组序列生成全量 `results/lighttrack/cpp_dml/amft.csv`（24,805 行）；
     - 执行 [scripts/calib_amft_lighttrack.py](file:///d:/PyProjects/RGB_PTZ_Integrated/scripts/calib_amft_lighttrack.py) 离线网格统计，输出 [results/lighttrack/amft_calibration.json](file:///d:/PyProjects/RGB_PTZ_Integrated/results/lighttrack/amft_calibration.json)；在 FAR $\le 1.0/1\text{k}$ 条件下候选覆盖率均 $<50\%$，按规则沿用 $\tau_0=0.65$；
     - 构建并运行 `test_amft.exe`，4 项体检 100% 全部 PASS；运行 `test_detector.exe`，5 项体检 100% 全部 PASS。
  6. **WP7 产物分发与收口**：
     - 主工程 `RGB_PTZ_Integrated.pro` 引入 `onnxsottracker.cpp` / `onnxsottracker.h` / `ISotTracker.h`；
     - Release 模式编译 0 告警 0 错误生成 `build/release/RGB_PTZ_Integrated.exe`（635,904 字节），DLL 与模型全自动同步；
     - 回填 [04 方案](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/04_轻量单目标跟踪基准模型交付与工程链路跑通方案.md) 实测指标，更新 [README.md](file:///d:/PyProjects/RGB_PTZ_Integrated/README.md) 工具清单，归档 Checkpoint #68，建立本检查点 #69。
- **下一步**：方案 04 圆满收官，移交方案 05（光电跟踪系统 RK3588 边缘端量化与工程移植实操方案）。
