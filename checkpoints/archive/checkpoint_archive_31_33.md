# 历史检查点归档（#31 ~ #33）

> **归档说明**：本文件归档了 `checkpoint.md` 中的历史检查点记录（#31 ~ #33），遵循 AGENTS.md 准则中“唯一事实源轻量化与小文件分拆”原则，根目录 `checkpoint.md` 仅保留当前最新 1 次检查点。

---

### Checkpoint 2026-10-29 #32
- **记录人**：Antigravity (Gemini 3.8 Flash)
- **类型**：YOLO 训练阶段圆满告一段落、全套图文 Word 报告与论文碎片素材全量归档入库
- **核心内容**：
  1. **YOLO 阶段训练成果系统化沉淀与归档**：
     - 在 [课题/论文碎片材料/02_YOLO可见光反无人机目标检测训练与消融实验研究/](file:///d:/PyProjects/RGB_PTZ_Integrated/课题/论文碎片材料/02_YOLO可见光反无人机目标检测训练与消融实验研究/) 建立专题素材库；
     - 编写并生成 **100 万字节高质量图文详解 Word 文档** [YOLO可见光反无人机目标检测训练与消融实验研究.docx](file:///d:/PyProjects/RGB_PTZ_Integrated/课题/论文碎片材料/02_YOLO可见光反无人机目标检测训练与消融实验研究/YOLO可见光反无人机目标检测训练与消融实验研究.docx) 及同名 Markdown 文档，涵盖系统职责、数据工程、2×2×2 学术三线大表、收敛瓶颈剖析与选型论证；
  2. **实验图表与原始数据全集入库**：
     - **图表资产 (`figures/`)**：归档 11 张关键图表（含独立绘制的微调 vs 重训收敛三联对比大图、300 轮从零全景 Loss 衰减图、PR 曲线、混淆矩阵及外场实机推断可视化图）；
     - **原始数据 (`raw_data/`)**：归档 5 款模型全生命周期逐轮打点 CSV 及全景消融汇总表 [yolo_models_ablation_summary.csv](file:///d:/PyProjects/RGB_PTZ_Integrated/课题/论文碎片材料/02_YOLO可见光反无人机目标检测训练与消融实验研究/raw_data/yolo_models_ablation_summary.csv)；
  3. **关键选型论证定稿**：
     - 明确在不计训练成本下，微调权重仍具有压倒性工程优势：其 **83.17% 召回率**（领先重训 300 轮的 82.37%）有效降低漏检概率，底层通用视觉先验对暗弱微小无人机具有关键保护作用；
  4. **方案 03 核心目标全部闭环达成**：
     - 上位机生产主力模型首选 [models/yolo11n_uav.onnx](file:///d:/PyProjects/RGB_PTZ_Integrated/models/yolo11n_uav.onnx)（2.7ms 极速），高精度备选项 [models/yolo11s_uav.onnx](file:///d:/PyProjects/RGB_PTZ_Integrated/models/yolo11s_uav.onnx)（3.6ms / 91.22%），所有 5 套 ONNX 资产与论文素材全部就绪。

---

### Checkpoint 2026-10-31 #33
- **记录人**：Antigravity (Gemini 3.8 Flash)
- **类型**：反无人机前沿文献 (UAV-DETR) 深度剖析入库、横向对比三线大表构建与弱小目标损失函数探索方案纳编
- **核心内容**：
  1. **前沿文献素材系统化收录与对比矩阵构建**：
     - 在 [课题/论文碎片材料/反无人机目标检测前沿文献引用与对比.md](file:///d:/PyProjects/RGB_PTZ_Integrated/课题/论文碎片材料/反无人机目标检测前沿文献引用与对比.md) 建立专题材料；
     - 系统收录 2026 年最新前沿成果 **UAV-DETR (arXiv:2603.22841)** 标准 GB/T 7714 与 BibTeX 引用；
     - 深度剖析其 WTConv 小波骨干、SWSA-IFI 编码器、ECFRFN 颈部及 Inner-CIoU + NWD 混合损失数学机理；
     - 建立本系统 YOLO11/YOLOv8 与 UAV-DETR 在 DUT Anti-UAV 上的精度（mAP@50 / mAP@50:95）、召回率、推理时延（2.7ms vs 16.5ms）、伺服裕度、DirectML 静态 ONNX 及 RK3588 NPU 部署友好度的学术横评三线大表；
     - 明确该论文在硕士论文第 1、2、3、5 章的引用与论证切入点；
  2. **弱小目标边界框损失函数探索方案统筹纳编方案 06**：
     - 在 [待执行方案/06_后续创新规划.md](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/06_后续创新规划.md) 确立“方案 11 弱小目标边界框损失函数选型与消融探索实操规划”专项储备，保持方案 03 专注基线交付；
     - 针对 DUT Anti-UAV 80.9% 弱小目标导致的 IoU 剧烈抖动与梯度震荡痛点，确立候选损失函数池（基准 CIoU、Inner-CIoU、NWD、Inner-CIoU+NWD 混合损失）；
     - 确立“探索阶段遵循实测驱动选型，哪个指标与弱小目标召回率最好就用哪个”的执行原则，且明确损失函数零额外推理耗时、不影响 2.7ms 极速推理与部署基线；
  3. **小文件检查点与文献清单轻量维护**：
     - Checkpoint #31 ~ #32 完整沉淀归档至 [checkpoints/archive/checkpoint_archive_31_32.md](file:///d:/PyProjects/RGB_PTZ_Integrated/checkpoints/archive/checkpoint_archive_31_32.md)；
     - [课题/论文碎片材料/README.md](file:///d:/PyProjects/RGB_PTZ_Integrated/课题/论文碎片材料/README.md) 同步登记前沿文献碎片材料编号 03。
