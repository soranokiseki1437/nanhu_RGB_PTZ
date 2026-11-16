# 历史检查点归档 Checkpoint #66 (2026-11-16)

- **记录人**：Antigravity (Gemini 3.8 Flash High)
- **类型**：20261114 组会汇报材料全量归档至 _archive/ 并维护版本控制契约
- **核心内容**：
  1. **材料整体归档**：将根目录下 `20261114组会/` 全量材料（包含修改版 PPTX、实测剪辑短视频、图表生成脚本及高清图件）完整平移至 `_archive/20261114组会/`，根目录恢复整洁；
  2. **版本控制规则维护**：在 [.gitignore](file:///d:/PyProjects/RGB_PTZ_Integrated/.gitignore) 中添加 `!_archive/20261114组会/` 白名单，保持脚本、大纲与标注样图轻量跟踪；大体积实测视频（*.mp4）与演示文稿（*.pptx）及临时图片渲染缓存受全局规则忽略，本地完整留存且不增加远程仓库负担；
  3. **检查点分流与状态刷新**：将上一检查点（#65）归档至 [checkpoints/archive/checkpoint_archive_65.md](file:///d:/PyProjects/RGB_PTZ_Integrated/checkpoints/archive/checkpoint_archive_65.md)，建立本检查点 #66。
- **下一步**：按方案 04（WP0 → WP7）推进 LightTrack 纯静态 ONNX 导出、DirectML 部署与 C++ 嵌入测试。
