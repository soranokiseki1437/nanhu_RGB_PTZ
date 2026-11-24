### Checkpoint 2026-11-24 #74
- **记录人**：Antigravity (Gemini 3.8 Flash High)
- **类型**：修复目标丢失（Lost/Occluded）状态下帧投递阻断缺陷，打通遮挡重捕多帧连续检索链路
- **核心内容**：
  1. **排查实机运行最新日志（20261124.log）**：
     - 在 19:16:44.299~44.385 目标发生遮挡，连续 4 帧异常触发 `m_lostCount >= 4`，进入 `TRACK_OCCLUDED` 并触发首次重捕 `tryDetectAndRecover()`；
     - YOLO 检出 3 个候选目标（"chair", "backpack", "tv"），均未通过外观锚点核验而被拦截，返回重捕未果；
     - 随后发出 `trackingLost()`，`TrackingController` 状态转为 `Lost`；
     - 此时 `MainWindow::onFrameReady` 中 `if (m_trackingController->isTracking())` 因 `isTracking()` 仅判断 `m_state == Tracking` 而变为 `false`，导致后续解码帧被阻断，未再向 `processFrame()` 投递任何新帧，算法状态机断流；
  2. **修复 `TrackingController::isTracking()` 判定准则**：
     - 将 [trackingcontroller.h](file:///d:/PyProjects/RGB_PTZ_Integrated/trackingcontroller.h) 中 `isTracking()` 扩展为 `m_state == Tracking || m_state == Lost`，确保在目标丢失/遮挡的尝试重捕阶段，视频流持续喂入算法工作线程；
     - 与 `TrackingController::processFrame()` 中的 `if ((m_state != Tracking && m_state != Lost) || !m_tracker) return;` 保护逻辑完全对称契合；
  3. **编译与回归验证**：
     - MinGW 64-bit Release 增量编译通过，`RGB_PTZ_Integrated.exe` 成功更新；
     - 执行 `test_amft.exe` 回归测试，5/5 项全部 PASS 通过。
- **下一步**：推进方案 05（LightTrack 深度跟踪）整机实机闭环验证；开展外场变焦与抗遮挡实机联调；筹备方案 06（RK3588 边缘端量化）与方案 07（自研网络重训消融）。
