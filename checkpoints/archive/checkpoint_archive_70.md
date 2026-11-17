# 检查点归档记录 Checkpoint #70

- **记录时间**：2026-11-16
- **记录人**：Antigravity (Gemini 3.8 Flash High)
- **类型**：异机反无人机基线微调实验排查、Bug 修复与重训交接完成（`SOT_Tracking_Baselines` 同步推送远程）
- **核心内容**：
  1. **异机 47 次微调/重训运行记录与日志全维度复盘**：
     - 还原客观学术事实：在 CASIA Anti-UAV300 val 验证集（67 序列 / 61,999 帧）上，所有 14 款基线在微调后全部取得正向增益（AUC 提升 +2.1% ~ +13.9%，如 SiamGAT 0.4974 -> 0.6365，LightTrackM 0.4709 -> 0.5357）；
     - 厘清跨域负迁移根因：CASIA 训练集中位目标尺度为 ~80~85px（中近景），DUT 测试集中位目标尺度仅 56.3px（面积小约 50%）且包含超长序列（最高 2635 帧），小模型（0.3M~1.6M）在单源 160 条序列微调后容易向 80px 尺度偏置收缩，丧失弱小目标尺度不变性。
  2. **定位并修复 LightTrackM 训练管线致命中心退化 Bug**：
     - 在 `D:/PyProjects/SOT_Tracking_Baselines/baselines/finetune/train_lighttrack.py` 中补齐空间数据增强，显式注入 `SHIFTs=64.0` 与 `SCALEs=0.18`，杜绝 `SHIFTs=0, SCALEs=0` 导致目标 100% 居中及分类标签恒为常量的退化解；显式关闭未校准坐标镜像的翻转（`flip=0.0`）。
  3. **定位并修复 FEAR-XS 训练破坏性毁权与负样本缺失缺陷**：
     - 在 `D:/PyProjects/SOT_Tracking_Baselines/baselines/finetune/train_fear.py` 中引入默认 `negative_ratio=0.15` 采样负样本对；学习率由 1e-4 降为更温和的 2e-5；重构为 Per-step Linear Warmup + Cosine 退火平滑调度器；过滤 Albumentations 2.x 警告。
  4. **明确 NanoTrack-V1/V2 重训 BAN 结构匹配规范**：
     - 确认 `configv1_casia_ft.yaml` 与 `configv2_casia_ft.yaml` 显式绑定 `DepthwiseBAN_v1` / `DepthwiseBAN_v2`，杜绝调用端误用默认 V3 结构导致 Size Mismatch。
  5. **自动化脚本与工程交接说明书落地**：
     - 编写 `baselines/finetune/run_repaired_trainings.sh`，支持一键自动执行微调、CASIA-val 选优与 DUT 盲测；
     - 编写 `交接_反无人机基线微调重训与数据整理指引.md`，明确执行步骤、统一评测协议（CASIA-val 统一 `--seq-stride 4 --max-frames 0`）与 `实验总表.csv` 归档规范。
  6. **Git 版本控制同步（强制 Commit & Push）**：
     - 异机关联仓库 `D:\PyProjects\SOT_Tracking_Baselines` 完成语义化提交（`534a224`）并成功推送到远程仓库 `origin/main`；云端代码绝对同步。
