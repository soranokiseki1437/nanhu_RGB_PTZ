# Checkpoint Archive #67 (2026-11-16)

- **记录人**：Antigravity (Gemini 3.8 Flash High)
- **类型**：SOT 重训数据集指南与历史维护脚本全量归档、待执行方案精简与临时缓存清理
- **核心内容**：
  1. **方案体系精简**：将 `待执行方案/SOT重训数据集下载与准备指南.md` 平移至 `datasets/SOT重训数据集下载与准备指南.md`（标定归属方案 06-A 自研重训储备），同步更新 `datasets/README.md` 索引与 `.gitignore` 白名单规则（`!datasets/*.md`），使 `待执行方案/` 目录完全精简聚焦于 00~06 体系与当前 LightTrack 嵌入方案；
  2. **历史脚本分流**：在 `scripts/` 下建立 `_archive/` 子目录，并将 7 个已彻底完成历史使命的一次性修补与早期生成脚本（`refactor_scheme03.py`、`patch_antigravity_history.py`、`fix_dataset_dirs.py`、`update_thesis_plan.py`、`export_thesis_md.py`、`gen_doc.py`、`generate_docx.js`）统一移入归档，建立索引清单 `scripts/_archive/README.md`，生产脚本目录恢复整洁；
  3. **环境与散落产物清理**：清理根目录下散落的 `test_detection_vis.png` 临时测试图片，全量清理 Python 运行缓存 `__pycache__`；
  4. **检查点分流与状态机闭环**：将上一检查点（#66）归档至 `checkpoints/archive/checkpoint_archive_66.md`，建立本检查点 #67。
- **下一步**：按方案 04（WP0 → WP7）推进 LightTrack 纯静态 ONNX 导出、DirectML 部署与 C++ 嵌入测试。
