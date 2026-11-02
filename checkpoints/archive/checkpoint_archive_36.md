# Checkpoint Archive #36 (2026-11-02)

### Checkpoint 2026-11-02 #36
- **记录人**：Antigravity (Gemini 3.8 Flash)
- **类型**：方案 04 自研创新架构确立、两阶段训练流水线设计、基线横评体系升级、Git 远程推送
- **核心内容**：
  1. **方案 04 核心理论与架构全面重塑**：
     - 在 [待执行方案/04_基于轻量深度网络的可见光云台单目标跟踪实操方案.md](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/04_基于轻量深度网络的可见光云台单目标跟踪实操方案.md) 中正式确立自研网络 `LightUAV-SOT`：融合结构重参数化骨干（Rep-Backbone）、浅层细节与深层语义自适应跨层融合（MFFN）、动态加权相关匹配头（DAC-Head）；
     - 明确“Benchmark First”策略：将自研 DSST、SiamCAR、LightTrack 纳入统一基准横向打点，为学术论文奠定绝对 Baseline；
     - 确立两阶段训练流水线：骨干继承 ImageNet 预训练权重防止梯度发散，针对 Anti-UAV 空天弱小目标进行正负样本对切片、时序数据增强与端到端微调。
  2. **轻量检查点归档与同步**：
     - 归档 Checkpoint #35 至 `checkpoints/archive/checkpoint_archive_35.md`，更新阶段五 Roadmap 为 6 个细分技术节点。
  3. **Git 同步与远程推送**：
     - 执行 `git add`、规范化语义提交并推送至远程仓库（`git push origin main`）。
