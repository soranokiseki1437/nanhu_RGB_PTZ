# Checkpoint 2026-11-24 #75

- **记录人**：Antigravity (Gemini 3.8 Flash High)
- **类型**：执行临时修复方案，彻底修复反无人机 YOLO 专属模型类别标签映射缺陷（person -> UAV）
- **核心内容**：
  1. **定位并归因缺陷根因**：
     - 反无人机专属模型（`yolo11n_uav_ft.onnx` 等）基于单类别 $nc=1$ 训练，输出张量通道为 5（4 坐标 + 1 UAV 置信度），`maxClassId` 恒为 0；
     - 上位机调用 `loadModel()` 时未设置 `DetectorConfig.classNames`；
     - `yolopostprocessor.cpp` 缺省无条件回退为 COCO 80 类别，`s_cocoNames[0]` 对应为 `"person"`，导致识别标签被覆写为 `"person"`；
  2. **工程落地三级自适应防御体系**：
     - **优先级 1（显式传参）**：调用方若在 `DetectorConfig.classNames` 指定，优先使用；
     - **优先级 2（动态元数据解析）**：在 `OnnxDetector::initializeSession` 中通过 ONNX Runtime C API 读取 `OrtModelMetadata` 中的 `names` 键，正则解析 `{0: 'UAV'}` 或 COCO 80 类映射表；
     - **优先级 3（张量拓扑通道保底）**：无元数据时，根据 $channels == 5$ 自动判定 $numClasses == 1$ 并映射为 `{"UAV"}`，若 $channels == 84$ 映射为 COCO 80 类；并在 `YoloPostProcessor::process` 同步单类防御；
  3. **编译与全项客观论证验证**：
     - MinGW 64-bit Release 全量构建通过，`RGB_PTZ_Integrated.exe` 与 `test_detector.exe` 更新就位；
     - 实测真实地对空图像（`video01/00001.jpg`）：
       - `models/yolo11n_uav_ft.onnx`：输出 `[1] 类别: UAV | 置信度: 0.868`，IoU=88.2%，中心误差=2.81px，标签修复成功；
       - `models/yolov8n.onnx`：输出 `[1] 类别: airplane | 置信度: 0.906`，IoU=99.0%，COCO 模型零退化；
       - `test_amft.exe` 协同回归：5/5 PASS 全部通过。
- **下一步**：推进方案 05（LightTrack 深度跟踪）整机实机闭环验证；开展外场变焦与抗遮挡实机联调；筹备方案 06（RK3588 边缘端量化）与方案 07（自研网络重训消融）。
