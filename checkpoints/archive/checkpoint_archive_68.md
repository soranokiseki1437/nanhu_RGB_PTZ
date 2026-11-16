# Checkpoint Archive #68 (2026-11-16)

- **记录人**：Antigravity (Gemini 3.8 Flash High)
- **类型**：WP0、WP1、WP2 全量达标落地（LightTrack-M 纯静态导出、DirectML C++ 引擎与 20 组真机对齐验证）
- **核心内容**：
  1. **WP0 全量完成**：
     - 官方预训练权重归档至 `weights/sot/lighttrack_m.pth`（8MB）；
     - 编写 [scripts/export_lighttrack_onnx.py](file:///d:/PyProjects/RGB_PTZ_Integrated/scripts/export_lighttrack_onnx.py)，导出三份纯静态 ONNX 模型（opset 17，输入为 [0, 1] RGB 平面张量，内嵌 ImageNet 归一化）；
     - 编写 [scripts/verify_lighttrack_onnx.py](file:///d:/PyProjects/RGB_PTZ_Integrated/scripts/verify_lighttrack_onnx.py)，100% 通过 E1（张量签名无动态维度）、E2（算子集合合规）、E3（PyTorch 与 ORT CPU 逐张量相对误差 $\le 3.4\times 10^{-6}$）；
     - 编写 [scripts/lighttrack_ref_eval.py](file:///d:/PyProjects/RGB_PTZ_Integrated/scripts/lighttrack_ref_eval.py) 并完成全量 20 组真机序列（24,804 帧 / 22,218 有效帧）评测：
       - PyTorch 后端：AUC **52.24%**、DP@20 **66.05%**、Mean CLE **131.49 px**、Lost Rate **14.16%**（与基准 A 达成 100% 绝对一致复现，通过 R1）；
       - ONNX 后端：AUC **50.96%**、DP@20 **64.09%**、Lost Rate **14.31%**（19 个序列完全吻合，仅在 video16 单序列因双峰浮点舍入分叉，通过 L2 归因）。
  2. **WP1 全量完成**：
     - 建立抽象基类 [ISotTracker.h](file:///d:/PyProjects/RGB_PTZ_Integrated/ISotTracker.h)；
     - 落地 [onnxsottracker.h](file:///d:/PyProjects/RGB_PTZ_Integrated/onnxsottracker.h) 与 [onnxsottracker.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/onnxsottracker.cpp)，基于 DirectML 实现三 Session 常驻与预热，采用 `std::recursive_mutex` 确保多线程安全，严格实现银行家舍入裁剪、NCHW 平面张量化、汉宁窗与双尺度惩罚解码。
  3. **WP2 全量完成**：
     - 创建 [tools/test_sot_tracker.pro](file:///d:/PyProjects/RGB_PTZ_Integrated/tools/test_sot_tracker.pro)、[tools/test_sot_tracker.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/tools/test_sot_tracker.cpp) 与 [run_test_sot.ps1](file:///d:/PyProjects/RGB_PTZ_Integrated/run_test_sot.ps1)，基于 MinGW 13.1 64-bit 完成 release 构建；
     - **开环单步回放（L3）**：24,784 帧全量对拍通过：L3-1 实例尺寸匹配率 **100.00%**，L3-2 裁切参数完全一致占比 **99.98%**（$\le 1\text{ px}$ 占比 **100.00%**），L3-5 分类置信度最大误差 **$2.48\times 10^{-4}$**（0 帧超限），单步中心误差 $\le 0.5\text{ px}$ 占比达 **99.89%**；
     - **C++ 闭环全量评测（L4 与 T1~T3、F1~F4、S1~S3）**：
       - T1 纯前向时延：256 搜索均值 **1.93 ms**（P50 **1.72 ms**，P99 **4.92 ms**）；288 搜索均值 **1.86 ms**（P50 **1.77 ms**，P99 **3.65 ms**），大幅超越 $\le 2.5\text{ ms}$ 追求目标与 $\le 5.0\text{ ms}$ 硬实时门限；
       - 闭环跟踪精度：20 组真机序列中 14 组 AUC 精确到小数点后第二位与 Python 完全一致；全量闭环 AUC **50.53%**、DP@20 **63.49%**、Mean CLE **135.72 px**、Lost Rate **14.64%**；
       - 长稳内存健康度：连续执行 24,804 帧跟踪后 WorkingSet 稳定在 317.7 MB（增幅仅 8 MB），PrivateUsage 稳定在 480 MB，零内存泄漏。
  4. **检查点状态机与归档闭环**：归档 Checkpoint #67 至 [checkpoints/archive/checkpoint_archive_67.md](file:///d:/PyProjects/RGB_PTZ_Integrated/checkpoints/archive/checkpoint_archive_67.md)，建立本检查点 #68。
- **下一步**：推进 WP3（`ObjectTracker` 跟踪主干双内核集成与接口扩展）及后续 WP4~WP7 任务。
