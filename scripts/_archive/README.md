# 一次性维护与历史脚本归档目录 (scripts/_archive)

> **归属工程**：[RGB_PTZ_Integrated](file:///d:/PyProjects/RGB_PTZ_Integrated)  
> **目录定位**：存放已完成使命的历史一次性数据重组、格式转换、历史修补或已淘汰的文档生成脚本，保持生产脚本目录 [scripts/](file:///d:/PyProjects/RGB_PTZ_Integrated/scripts) 的精简与专注。

---

## 归档脚本清单与历史使命记录

| 脚本文件名 | 历史使命与执行记录 | 归档原因 |
| :--- | :--- | :--- |
| **`refactor_scheme03.py`** | 一次性通过硬编码文本重写方案 03（YOLO 多模型横评与动态切换）文档 | 文档已完成重写并入库，使命结束 |
| **`patch_antigravity_history.py`** | 针对 Antigravity IDE 历史记录 URI 转码不一致问题编写的一次性修复脚本 | 历史会话记录已修复，无须保留在日常根目录 |
| **`fix_dataset_dirs.py`** | 一次性修正 DUT Anti-UAV 检测子集目录层级（兼容软链接与标准 YOLO 格式） | 数据集目录已完全固化标准，脚本执行完毕 |
| **`update_thesis_plan.py`** | 一次性根据课题最新规划更新 `反无人机云台视觉跟踪与抗遮挡重检测课题规划指南.docx` | 文档已更新完成 |
| **`export_thesis_md.py`** | 一次性将上述课题规划 docx 导出为同名 Markdown 文档 | 对应 `.md` 文件已就位 |
| **`gen_doc.py`** | 2026年9月基于 Python-docx 生成工程说明文档的旧脚本 | 项目文档体系已全面转为 Markdown 检查点体系 |
| **`generate_docx.js`** | 2026年9月基于 Node.js docx 库生成早期文档的旧脚本 | 同上，已不再维护 |
| **`setup_miniconda_env.py`** | 初始化 `anti_uav_yolo` Python 3.11 + CUDA 13.0 训练环境的一次性配置脚本 | 环境已固化就位，无需重复运行 |
| **`download_anti_uav.py` / `download_uav123.py`** | 自动化拉取 DUT Anti-UAV 与 UAV123 数据集的一次性下载脚本 | 本地标准数据集已就绪 |
| **`convert_anti_uav_voc_to_yolo.py`** | DUT Anti-UAV 7,800 份 XML 标注转标准 YOLO 格式的一次性转换脚本 | 数据集转换与缓存已固化 |
| **`init_tracking_benchmark_workspace.py`** | 异机基准仓库 `SOT_Tracking_Baselines` 初始化脚本 | 异机仓库已独立演进维护 |
| **`train_sot.py` / `dataset_sot_pair.py` / `eval_sot.py` / `run_sot_ablation.py` / `export_sot_onnx.py`** | 2026-11 上旬早期 LightUAV 试训与消融脚本 | 基准链路已切换至 LightTrack 官方权重，后续自研重训由方案 07 统筹 |
| **`benchmark_hog_vs_zncc.py` / `benchmark_cascade.py` / `benchmark_traditional_reacquisition.py` / `benchmark_routes_ab.py` / `benchmark_results.json`** | 2026-11-24 无类别保底重检 Python 端预研对比脚本与输出 | 已被 C++ DirectML 实测工具 `tools/bench_redetect.cpp` 替代并记入 Checkpoint #79 |
