# 检查点归档（Checkpoint Archive #45）

> **归档时间**：2026-11-09  
> **上级检查点**：[checkpoints/archive/checkpoint_archive_44.md](file:///d:/PyProjects/RGB_PTZ_Integrated/checkpoints/archive/checkpoint_archive_44.md)  
> **唯一事实源**：[checkpoint.md](file:///d:/PyProjects/RGB_PTZ_Integrated/checkpoint.md)

---

### Checkpoint 2026-11-09 #45
- **记录人**：Antigravity (Gemini 3.8 Flash)
- **类型**：LightUAV-SOT 100 轮两阶段训练协议与消融快照机制交付、训练/评测全流程就绪、当前工程进展全景梳理
- **核心内容**：
  1. **交付 100 轮两阶段训练管线与里程碑快照机制（Commit `ed3d13c`，`scripts/train_sot.py`）**：
     - Stage 1: 15 Epochs 冻结骨干网络，预热特征融合与预测头（AdamW, lr=1e-3）；
     - Stage 2: 85 Epochs 联合端到端微调，余弦退火学习率衰减（lr=2e-4 -> 1e-6）；
     - 自动捕获保存 Epoch 30、50、80、100 消融里程碑快照与全局最优权重 `lightuav_sot_best.pth`，流式输出 `train_sot_log.csv`；
  2. **归档历史检查点 #44**：
     - 历史检查点 #44 分流归档至 [checkpoints/archive/checkpoint_archive_44.md](file:///d:/PyProjects/RGB_PTZ_Integrated/checkpoints/archive/checkpoint_archive_44.md)；
  3. **明确当前工程推进节点**：
     - 系统目前处于方案 04（基于轻量深度网络的可见光云台单目标跟踪）全量训练与消融打点启动节点。
