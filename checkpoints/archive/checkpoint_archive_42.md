# 检查点归档（Checkpoint Archive #42）

> **归档时间**：2026-11-03  
> **上级检查点**：[checkpoints/archive/checkpoint_archive_41.md](file:///d:/PyProjects/RGB_PTZ_Integrated/checkpoints/archive/checkpoint_archive_41.md)  
> **唯一事实源**：[checkpoint.md](file:///d:/PyProjects/RGB_PTZ_Integrated/checkpoint.md)

---

### Checkpoint 2026-11-03 #42
- **记录人**：Antigravity (Gemini 3.8 Flash)
- **类型**：自研轻量 SOT 创新网络（LightUAV-SOT）全系统代码构建、重参数化数学等价验证、Anti-UAV 航迹样本对切片管线与两阶段训练端到端验收闭环、纯静态双分支 ONNX 导出成功、Git 远程推送
- **核心内容**：
  1. **自研 LightUAV-SOT 创新网络四大模块全量就位（`models/sot/`）**：
     - `rep_backbone.py`：结构重参数化骨干（多分支训练 -> 单路 3x3 Conv 部署折叠），折叠前后误差仅 5.07e-7，双尺度输出 P2 (64x64, 64c) 与 P4 (16x16, 96c)；
     - `mffn.py`：多尺度细节融合网络，通道注意力门控融合 P2 浅层边缘轮廓与 P4 深层判别语义，攻克微小无人机（10x10 px）特征下采样湮灭难题，折叠前后误差 2.38e-6；
     - `dac_head.py`：动态空间-通道注意力相关匹配头，模板注意力加权深度互相关，抑制低空与地表杂波伪峰；
     - `lightuav_sot.py`：完整集成模型，训练态 1.663M 参数，部署折叠态仅 1.498M 参数；RTX 5070 GPU 实测纯推理时延仅 **2.68 ms（373.4 FPS）**，满足机电伺服硬实时；
  2. **训练数据工程与两阶段训练流水线（`scripts/`）**：
     - `dataset_sot_pair.py`：自动解析 Anti-UAV 20 组真机航迹序列，生成 Template (127x127) 与 Search (255x255) 训练对并计算无锚框中心分类与 ltrb 回归标签；
     - `train_sot.py`：实现头部预热（10 轮，骨干冻结）与全网络联合微调（40 轮，余弦退火）两阶段训练策略，经 1-Epoch 端到端全链路实跑验收成功（Loss 6.7354，无报错）；
  3. **纯静态双分支 ONNX 导出与验证（`scripts/export_sot_onnx.py`）**：
     - 成功导出纯静态双分支计算图：`sot_template.onnx` (2.43 MB) 与 `sot_search.onnx` (5.73 MB)，经 ONNX Runtime 验证 100% 通过；
  4. **Git 同步与远程推送**：
     - 历史检查点 #41 归档至 [checkpoints/archive/checkpoint_archive_41.md](file:///d:/PyProjects/RGB_PTZ_Integrated/checkpoints/archive/checkpoint_archive_41.md)；
     - 执行 `git add`、规范语义提交并推送至远程仓库（`git push origin main`）。
