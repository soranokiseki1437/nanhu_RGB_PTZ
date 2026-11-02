# Checkpoint Archive #35 (2026-11-02)

### Checkpoint 2026-11-02 #35
- **记录人**：Antigravity (Gemini 3.8 Flash)
- **类型**：感知体系定性澄清（仅目标检测为深度学习，目标跟踪仍为传统算法）、检查点与待办状态同步、Git 远程推送
- **核心内容**：
  1. **系统算法架构与物理边界明确定性**：
     - **目标检测阶段**：基于深度学习算法（YOLOv8/YOLO11，DirectML GPU 加速，5 款纯静态 ONNX 已入库，纯 C++ NMS），常态 0 FPS 休眠按需单次唤醒；
     - **目标跟踪阶段**：仍采用传统相关滤波算法（[DsstCore](file:///d:/PyProjects/RGB_PTZ_Integrated/dsstcore.h)，纯 C++17/OpenCV 连续内存实现），高频独占运行并输出单一脱靶量；
     - **调度大脑**：[AMFTEvaluator](file:///d:/PyProjects/RGB_PTZ_Integrated/amftevaluator.cpp) 三维综合可靠度评估动态连接传统跟踪与深度检测；
     - **后续规划**：可见光深度单目标跟踪（LightTrack/RepSOT）归属于方案 04，尚未实装。
  2. **状态快照与待办清单客观校准**：
     - 刷新方案 03 落地 Checklist，准确反映 03-3-B、03-4、03-5 已闭环状态；
     - 归档 Checkpoint #34 至 `checkpoints/archive/checkpoint_archive_34.md`，保持根目录 `checkpoint.md` 唯一事实源轻量化。
  3. **Git 同步与远程推送**：
     - 执行 `git add`、规范化语义提交并推送至远程仓库（`git push origin main`）。
