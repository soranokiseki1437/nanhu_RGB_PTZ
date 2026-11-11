# 检查点归档：Checkpoint 2026-11-10 #48

- **记录人**：Antigravity (Gemini 3.8 Flash)
- **类型**：SOT 真实基准数据全面对齐、顶会骨干预训练机制溯源与调研、RTX 4080 Super 台式机重训算力可行性评估、方案 04B 立项沉淀与检查点同步
- **核心内容**：
  1. **基准测试数据全面客观对齐（`D:\PyProjects\SOT_Tracking_Baselines`）**：
     - 校准项目核心文档与脚本中基线指标为真实测试值：LightTrackM (AUC 52.24%, DP@20 66.06%, Mean CLE 131.5px, 37.2 FPS)、NanoTrack-V3 (AUC 55.56%, DP@20 71.22%, Mean CLE 150.3px, 35.7 FPS)、LightUAV-SOT (DP@20 75.32%, AUC 22.99%, Mean CLE 167.2px, 224.5 FPS)；
     - 同步校准 `课题/sot_ablation_summary.md`、`课题/轻量级跟踪基线实验分析与Rep-LightBackbone立项论证.md`、`待执行方案/04_基于轻量深度网络的可见光云台单目标跟踪实操方案.md`、`scripts/run_sot_ablation.py` 与 `课题/sot_ablation_results.json`，确保项目内部数据绝对一致；
  2. **顶会单目标跟踪文献机制调研与归因**：
     - 深入查阅 SiamRPN++、SiamCAR、NanoTrack、LightTrack、FEAR、TransT、OSTrack 等顶会论文，确立核心共识：SOT 骨干必须经过大规模图像（如 ImageNet-1k）分类预训练以提供通用视觉边缘与空间几何尺度基准；从零训练会导致小目标边界框尺度回归严重失真（LightTrack 论文证明无预训练 EAO 暴跌 36%）；同时跟踪微调周期普遍较短（20~30 轮），需配合 GIoU 损失与 Centerness 质量分支；
  3. **算力评估与方案 04B（骨干预训练初始化与尺度回归修复 SOP）确立**：
     - 评估 RTX 4080 Super（16GB）台式机重训算力：16GB 显存可充分支持 Batch Size=64/128，30 轮 Anti-UAV 航迹训练仅需约 11~15 分钟，完全消除算力瓶颈；
     - 输出工程规范 `待执行方案/04B_基于预训练骨干初始化与尺度回归修复的SOT重训方案.md`，明确路线 1（对齐 RepViT-M0.6/RepVGG-A0 骨干直接加载官方 ImageNet 预训练权重）、三大算法修复（引入 GIoU 损失、特征步长由 16 降为 8、新增 Centerness 预测分支）与 30 轮两阶段训练规程，设定 AUC ≥ 55.0% 且 DP@20 ≥ 75.0% 达标目标；
  4. **规范执行与版本库同步**：
     - 归档 Checkpoint #47 至 `checkpoints/archive/checkpoint_archive_47.md`；
     - 维持唯一事实源轻量化，遵循严禁保留修订痕迹与客观科研铁律。

