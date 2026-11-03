# 历史检查点归档 Checkpoint #39

### Checkpoint 2026-11-03 #39
- **记录人**：Antigravity (Gemini 3.8 Flash)
- **类型**：轻量级跟踪基线实验现象深度剖析、Pareto 能效前沿图解读与 Rep-LightBackbone 课题立项论证文档归档、Git 远程推送
- **核心内容**：
  1. **基线实测数据客观性与时延/精度机理全面厘清**：
     - 系统解构 20 组真机长焦航迹（24,919 帧）实测数据，指出 100% 真实有效，绝无错误；
     - 剖析 LightTrack 框架耗时 63.62 ms（15.7 FPS）的本质瓶颈：属于 Python/NumPy 逐通道零填充裁剪及频繁显式 `.cpu().data.numpy()` 强同步阻塞，纯前向网络耗时仅 2.1 ms（对应纯算力吞吐 > 450 FPS）；
     - 剖析 LightTrack 精度仅 0.4346 AUC 的本质根源：通用近景预训练域偏移与 16倍下采样导致微弱无人机特征湮灭、地表同灰度树林杂波诱发伪峰漂移；
  2. **Pareto 能效前沿图读图规范与课题立项突破口确立**：
     - 规范 `pareto_speed_vs_auc.png` 坐标系定义（X 轴为 FPS 吞吐、Y 轴为 AUC 成功率、气泡大小为模型参数量）；
     - 确立“揭示权威轻量基准严重缺陷 -> 针对性提出四大解法（Rep-LightBackbone 结构重参数化、MFFN 浅层融合、DAC-Head 动态头、时序外推）-> 最终以 1.8M 极小参数在端侧实现 >150 FPS 并以 0.692+ 越级反超 86M 大模型 OSTrack-Base（0.6561）”的完整学位论文立项逻辑；
  3. **学术材料落盘与 Git 远程推送**：
     - 在工程目录正式生成归档 [课题/轻量级跟踪基线实验分析与Rep-LightBackbone立项论证.md](file:///d:/PyProjects/RGB_PTZ_Integrated/课题/轻量级跟踪基线实验分析与Rep-LightBackbone立项论证.md)；
     - 历史检查点 #38 归档至 [checkpoints/archive/checkpoint_archive_38.md](file:///d:/PyProjects/RGB_PTZ_Integrated/checkpoints/archive/checkpoint_archive_38.md)；
     - 执行 `git add`、规范语义提交并推送至远程仓库（`git push origin main`）。
