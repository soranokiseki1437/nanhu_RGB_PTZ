# 历史检查点归档 Checkpoint #38

### Checkpoint 2026-11-02 #38
- **记录人**：Antigravity (Gemini 3.8 Flash)
- **类型**：轻量化 SOT 与反无人机前沿对比算法全面重构、学术选型清单与工作区目录同步刷新、Git 远程推送
- **核心内容**：
  1. **算法选型体系向轻量化与垂直赛道全面聚焦**：
     - 确立“同算力级公平对比与轻量化前沿对齐”原则，剔除不具备端侧低时延伺服可行性的大型通用网络；
     - 深度调研并纳编开源前沿：**UAV-DETR**（2024 前沿反无人机微小目标轻量感知网络，GitHub: `wd-sir/UAVDETR`，WTConv + SWSA-IFI 编码器）、**TCTrack**（CVPR 2022 / TPAMI 2023，同济大学 Vision4Robotics 空天时序自适应轻量跟踪器）、**HiFT**（ICCV 2021 无人机分层轻量 Transformer）、**NanoTrack**（0.4M）、**LightTrack**（1.6M 黄金基准）、**FEAR-L**（1.2M）以及 **RepViT-Track**（CVPR 2024 重参数化骨干消融组）；
  2. **双端文档与工程规范同步交付**：
     - 深度重写 [课题/可见光单目标跟踪算法选型与对比基线清单.md](file:///d:/PyProjects/RGB_PTZ_Integrated/课题/可见光单目标跟踪算法选型与对比基线清单.md)，重构梯队理论架构、开源仓库索引、三线大表模板与消融矩阵；
     - 更新 [scripts/init_tracking_benchmark_workspace.py](file:///d:/PyProjects/RGB_PTZ_Integrated/scripts/init_tracking_benchmark_workspace.py) 并实机执行，同步刷新外部基准工作区 [D:\PyProjects\SOT_Tracking_Baselines](file:///d:/PyProjects/SOT_Tracking_Baselines) 的 `README.md`、`WORKFLOW.md` 及算法与权重子目录树；
  3. **Git 同步与远程推送**：
     - 历史检查点 #37 归档至 [checkpoints/archive/checkpoint_archive_37.md](file:///d:/PyProjects/RGB_PTZ_Integrated/checkpoints/archive/checkpoint_archive_37.md)；
     - 执行 `git add`、规范语义提交并推送至远程仓库（`git push origin main`）。

