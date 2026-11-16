# 检查点归档 #65

> 归档自 checkpoint.md 第七节。

### Checkpoint 2026-11-16 #65
- **记录人**：Antigravity (Claude Sonnet 5.5)
- **类型**：方案 04 LightTrack 上位机嵌入执行方案定稿、任务书归档与基准事实核实
- **核心内容**：
  1. **执行方案定稿**：[Qt上位机嵌入LightTrack轻量单目标跟踪算法执行方案.md](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/Qt上位机嵌入LightTrack轻量单目标跟踪算法执行方案.md) 划分 WP0–WP7，含 22 项验收矩阵与 7 项停止条件；04、05 方案的 SOT 张量规格同步为三份静态 ONNX（模板、搜索 256、搜索 288），RKNN 输入归一化配置修正为 `mean=0 / std=255`。
  2. **任务书归档**：`Qt上位机嵌入LightTrack跟踪算法任务书.md` 移至 `_archive/`。
  3. **实地核实（只读 + scratch 探针）**：
     - LightTrack 源码、权重、旧评测代码位于 `D:\PyProjects\SOT_Tracking_Baselines_官方权重试跑`；`SOT_Tracking_Baselines` 已重组为 `baselines/` 结构，含 LightTrackM 官方权重新基准（AUC 51.69%、DP@20 65.31%、Mean CLE 146.74 px、Lost Rate 15.57%），与旧基准（52.24% / 66.06% / 131.49 px / 14.16%）的差异量作为闭环对齐容差的标定依据；
     - `get_subwindow_tracking` 的裁切角点使用内建 `round`（银行家舍入），搜索边长使用 `python2round`；边界填充值按截断取整；评测输出框左上角钳制 ≥ 0；
     - `torch.onnx.export(..., opset_version=17, dynamo=False)` 导出模板与 256/288 搜索图实测成功，算子均为标准算子，无动态维度，与 PyTorch 的相对误差 ≤ 2.6e-6；Python `onnxruntime` 仅含 CPU EP，DirectML 验证在 C++ 工具中执行；
     - `.gitignore` 忽略 `models/*.onnx`、`weights/`、`*.pth`：模型与权重为本地资产。
  4. **代码状态**：生产代码未改动，未执行编译，LightTrack ONNX 尚未导出。
- **下一步**：由执行代理按实施规范 WP0 → WP7 逐包实施。
