# 检查点归档 #60

> 归档自 checkpoint.md 第七节。

### Checkpoint 2026-11-12 #60
- **记录人**：Antigravity (Gemini 3.8 Flash)
- **类型**：跟踪飞行黑匣子数据自动落盘、分类日志默认开启与多目标重检测判据精细化
- **核心内容**：
  1. **跟踪与控制排障日志默认落盘**：
     - 在 [main.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/main.cpp) 中调整默认日志过滤规则，默认启用 `tracker.debug` 与 `ptz.debug`，静音高频流媒体数据包，排障日志默认自动写入 `日志/` 目录。
  2. **跟踪飞行黑匣子数据自动保存 (Flight Recorder)**：
     - 在 [TrackingController](file:///d:/PyProjects/RGB_PTZ_Integrated/trackingcontroller.cpp) 中建立自动黑匣子机制，每次跟踪启动时自动创建 `日志/track_YYYYMMDD_HHmmss.csv`；
     - 逐帧记录时间戳、跟踪模式、状态机状态、目标框、卡尔曼预测、脱靶量、云台下发速度、PSR、AMFT 综合得分与阈值；停止跟踪时自动存盘并在状态栏提示路径。
  3. **多目标模式状态机联动规范化**：
     - 在 [MainWindow::on_radioModeAuto_toggled](file:///d:/PyProjects/RGB_PTZ_Integrated/mainwindow.cpp) 与点击画面目标启动跟踪处，显式调用 `setRedetectionEnabled(true)`，保证多目标模式正常使能 AI 协同重检测链路。
  4. **AMFT 遮挡判定与重检测候选筛选调优**：
     - 在 [ObjectTracker](file:///d:/PyProjects/RGB_PTZ_Integrated/objecttracker.cpp) 中将微失锁遮挡判定由 2 帧调整为 4 帧，抑制瞬态高频光照与形变波动误判；
     - 优化 `tryDetectAndRecover` 候选框筛选公式，引入二次距离衰减惩罚并约束物理搜索半径，增加候选目标置信度与打分调试输出，防止多目标同屏时误锁他靶。
  5. **构建与测试**：
     - MinGW 64-bit Release 全量构建通过（0 错误 0 告警），`run_test.ps1` 验收通过。
