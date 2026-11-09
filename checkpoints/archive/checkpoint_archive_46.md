# 检查点归档（Checkpoint Archive #46）

> **归档时间**：2026-11-09  
> **上级检查点**：[checkpoints/archive/checkpoint_archive_45.md](file:///d:/PyProjects/RGB_PTZ_Integrated/checkpoints/archive/checkpoint_archive_45.md)  
> **唯一事实源**：[checkpoint.md](file:///d:/PyProjects/RGB_PTZ_Integrated/checkpoint.md)

---

### Checkpoint 2026-11-09 #46
- **记录人**：Antigravity (Gemini 3.8 Flash)
- **类型**：LightUAV-SOT 100 轮两阶段全量训练圆满收敛、消融里程碑快照归档、重参数化折叠部署权重导出、训练收敛指标沉淀与 Git 远程推送
- **核心内容**：
  1. **LightUAV-SOT 100 轮两阶段端到端训练圆满收敛**：
     - 基于 RTX 5070 Laptop GPU (CUDA 13.0) 纯硬件加速，耗时 53.2 分钟完成全量训练（累计采样 300,000 对空天增强时序样本）；
     - Stage 1（前 15 轮骨干冻结预热）：Loss 由 11.7293 稳定降至 3.9974，单轮平均耗时 27.2 秒；
     - Stage 2（85 轮全网联合微调）：余弦退火学习率调度（$2\times 10^{-4} \rightarrow 1\times 10^{-6}$），总 Loss 收敛至 **1.8829**（分类损失 0.0020，Smooth-L1 回归损失 1.5674），单轮平均耗时 30.6 秒；
  2. **模型核心资产、消融快照与折叠部署权重归档**：
     - 自动归档四组消融里程碑快照：`lightuav_sot_epoch_30.pth` (Loss: 2.7035)、`epoch_50.pth` (Loss: 2.2037)、`epoch_80.pth` (Loss: 1.9600)、`epoch_100.pth` (Loss: 1.8829)；
     - 刷新全局最优权重 `weights/sot/lightuav_sot_best.pth`；
     - 结构重参数化权重等价折叠（Folded Conv3x3），导出纯单分支部署权重 `weights/sot/lightuav_sot_deploy.pth`（6.0 MB）；
     - 完整训练指标曲线入库 `weights/sot/train_metrics.csv`；
  3. **历史检查点归档与 Git 远程同步**：
     - Checkpoint #45 归档至 [checkpoints/archive/checkpoint_archive_45.md](file:///d:/PyProjects/RGB_PTZ_Integrated/checkpoints/archive/checkpoint_archive_45.md)；
     - 执行 `git add`、规范语义提交并推送至远程仓库（`git push origin main`）。
