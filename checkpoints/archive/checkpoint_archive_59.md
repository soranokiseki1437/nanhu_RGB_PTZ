# Checkpoint Archive: #59

- **记录人**：Antigravity (Gemini 3.8 Flash)
- **时间**：2026-11-12
- **类型**：全系统可见光架构规范化与旧版参考文件清理
- **核心内容**：
  1. **旧版参考文件清理**：
     - 物理删除 `reference/dsst_tracker.cpp`、`reference/dsst_tracker.h`、`reference/main_zt.cpp` 与 `reference/tracker_config.txt`。
  2. **界面交互精简**：
     - 在 `mainwindow.ui` 与 `MainWindow` 中清理冗余通道选择控件与关联槽函数。
  3. **跟踪控制与外壳接口精简**：
     - 在 `TrackingController` 与 `ObjectTracker` 中收敛控制接口与冗余状态变量。
  4. **跟踪内核特征抽取标准化**：
     - 在 `DsstCore` 与 `dsstcore.cpp` 中，`getFeatureMap` 与 `getScaleSample` 统一采用 3 通道灰度与 Sobel 梯度特征提取算子。
  5. **离线工具链对齐**：
     - 在 `tools/offline_runner.cpp` 中移除历史宏分支与参数解析，统一调用 `DsstCore` 接口。
  6. **编译与验证**：
     - MinGW 64-bit Release 全量编译通过，运行测试通过。
