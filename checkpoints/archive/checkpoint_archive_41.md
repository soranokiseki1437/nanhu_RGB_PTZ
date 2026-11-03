# 检查点归档（Checkpoint Archive #41）

> **归档时间**：2026-11-03  
> **上级检查点**：[checkpoints/archive/checkpoint_archive_40.md](file:///d:/PyProjects/RGB_PTZ_Integrated/checkpoints/archive/checkpoint_archive_40.md)  
> **唯一事实源**：[checkpoint.md](file:///d:/PyProjects/RGB_PTZ_Integrated/checkpoint.md)

---

### Checkpoint 2026-11-03 #41
- **记录人**：Antigravity (Gemini 3.8 Flash)
- **类型**：方案 04（方向一）自研 LightUAV-SOT 创新网络研发正式立项、代码库解耦与工作区文件规范化整理、`.gitignore` 精细过滤加固、Git 远程推送
- **核心内容**：
  1. **明确双工程协同与避免代码库污染原则**：
     - 明确 [D:\PyProjects\SOT_Tracking_Baselines](file:///d:/PyProjects/SOT_Tracking_Baselines) 纯基线算法对比库定位，杜绝机械全量搬运官方第三方复杂 NAS 超网与沉余脚本；
     - 确立在主工程 `models/sot/` 原生实现模块化自研 LightUAV-SOT（Rep-LightBackbone + MFFN + DAC-Head），直接继承关键张量尺寸并支持从基线检查点跨工程按需提取初始权重；
  2. **工作区清洁度治理与五层管理规范确立**：
     - 清除根目录下由旧构建残留的临时文件（`test_detection_vis.png`、`Makefile*`、`.qmake.stash`、`ui_mainwindow.h`）；
     - 重构并固化 UTF-8 编码的 `.gitignore`，实现模型二进制大文件（`models/*.onnx`, `weights/*.pt`）自动隔离，同时确保深度学习 Python 网络源码（`models/sot/*.py`）精准受控；
     - 初始化 `models/sot/` 架构源码区与 `weights/sot/` 权重落地目录；
  3. **Git 同步与远程推送**：
     - 历史检查点 #40 归档至 [checkpoints/archive/checkpoint_archive_40.md](file:///d:/PyProjects/RGB_PTZ_Integrated/checkpoints/archive/checkpoint_archive_40.md)；
     - 执行 `git add`、规范语义提交并推送至远程仓库（`git push origin main`）。
