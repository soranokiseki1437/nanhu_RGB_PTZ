# 检查点记录：Checkpoint 2026-11-25 #80

- **记录人**：Antigravity (Gemini 4 Hydrogen High)
- **类型**：工作区过时文件分级归档与引用链接统一校准
- **核心内容**：
  1. **顶层临时排查目录与已闭环方案归档**：
     - 将 2026-11-16 遗留的 [思考工作区/](file:///d:/PyProjects/RGB_PTZ_Integrated/_archive/思考工作区_20261116) 迁入 `_archive/思考工作区_20261116/`（通过 `.gitignore` 白名单保留版本跟踪）；
     - 将已全面闭环的方案 01~04 移入 [待执行方案/archive/](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/archive)，使 `待执行方案/` 主目录仅保留总览规范 `00` 与待推进方案 `05~07`。
  2. **根目录、工具与脚本清理归档**：
     - 移除根目录历史 `docx` 生成器遗留配置 `package.json` / `package-lock.json`、临时图 `test_detection_vis.png` 及空目录 `日志/`；
     - 清理 `tools/` 下旧版未跟踪可执行文件（`runner_port.exe`、`runner_ref.exe`、`test_amft.exe`），将早期 DSST 对拍工具 `offline_runner.cpp`、`compare_csv.py`、`ocv_stub/` 归档至 `tools/_archive/`；
     - 将 15 份已完成或被 C++ 替代的历史脚本归档至 [scripts/_archive/](file:///d:/PyProjects/RGB_PTZ_Integrated/scripts/_archive/README.md) 并更新归档清单。
  3. **课题、说明与导出素材归档及链接校准**：
     - 将早期 `课题/sot_ablation_*` 试训结果移入 `课题/_archive/sot_ablation_20261110/`；将旧版 `RGB_PTZ_Integrated项目说明.docx`、`draft_20a5ead5_folder/` 与 `导出材料/20261026/` 迁入 `_archive/` 并删除 `.bkp` 临时备份；
     - 同步更新 [README.md](file:///d:/PyProjects/RGB_PTZ_Integrated/README.md)、[00_方案总览与执行推进管理规范.md](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/00_方案总览与执行推进管理规范.md)、[05](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/05_轻量单目标跟踪基准模型交付与工程链路跑通方案.md)、[06](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/06_光电跟踪系统RK3588边缘端量化与工程移植实操方案.md) 及 [datasets/README.md](file:///d:/PyProjects/RGB_PTZ_Integrated/datasets/README.md) 中的引用路径。
- **下一步**：继续按方案 05 推进整机闭环验证，并依据 Checkpoint #79 评测结论决策后续重检测阈值调整与合并数据集检测重训工作。
