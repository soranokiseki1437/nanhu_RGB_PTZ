# 历史检查点归档（#31 ~ #32）

> **归档说明**：本文件归档了 `checkpoint.md` 中的历史检查点记录（#31 ~ #32），遵循 AGENTS.md 准则中“唯一事实源轻量化与小文件分拆”原则，根目录 `checkpoint.md` 仅保留当前最新 1 次检查点。

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
