# 检查点归档：Checkpoint 2026-11-09 #47

- **记录人**：Antigravity (Gemini 3.8 Flash)
- **类型**：四阶递进式模块消融实验全闭环、20 组真机航迹全量 OPE 客观打点（DP@20 75.32%, AUC 22.99%, 224.5 FPS）、学术三线表与 300-DPI Pareto 能效前沿图沉淀、AGENTS.md 准则加固与 Git 远程推送
- **核心内容**：
  1. **完成四阶递进式模块消融实验与真机航迹全景打点（`scripts/run_sot_ablation.py`）**：
     - 对比验证 Baseline (Naive XCorr, DP@20 0.79%, AUC 0.50%)、+MFFN (DP@20 0.18%, AUC 0.23%) 与 Full Model (+DAC-Head, DP@20 75.32%, AUC 22.99%)，证明 DAC 动态注意力门控在复杂地表/空天杂波下的决定性抗漂移能力；
     - 客观正视全景短板：虽然 DP@20 达到 75.32%，模型 FPS 达 224.5，但**综合成功率 Success (AUC) 仅为 22.99%（垫底，落后于 NanoTrack-V3 55.56%、LightTrack-M 52.24% 与 DSST 45.20%）**，Mean CLE 达 167.2 px，深度剖析根因为“从零训练导致边界框尺度回归缺乏先验”；
     - 验证结构重参数化折叠部署模型 (`lightuav_sot_deploy.pth`) 精度 100% 保持，纯模型推理吞吐率达 231.8 FPS；
  2. **学术资产与论文材料输出**：
     - 生成 300-DPI Pareto 能效前沿对比图 `课题/sot_ablation_pareto.png`；
     - 生成 300-DPI 100 轮训练收敛轨迹曲线图 `课题/sot_training_loss_curve.png`；
     - 产出学术三线表（Markdown 与 LaTeX 源码）`课题/sot_ablation_summary.md` 及全量指标 JSON `课题/sot_ablation_results.json`；
  3. **行为准则加固、历史检查点归档与 Git 远程同步**：
     - 在 `AGENTS.md` 增补第五章“严谨客观科研与指标汇报铁律”，坚决杜绝选择性失明与粉饰成果；
     - 归档 Checkpoint #46 至 `checkpoints/archive/checkpoint_archive_46.md`；
     - 执行 `git add`、规范语义提交并推送至远程仓库（`git push origin main`）。

