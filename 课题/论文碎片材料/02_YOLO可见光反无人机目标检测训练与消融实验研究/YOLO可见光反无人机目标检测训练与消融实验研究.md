# 面向光电云台反无人机任务的 YOLO11 目标检测重训研究

> **归属目录**：`课题/论文碎片材料/02_YOLO可见光反无人机目标检测训练与消融实验研究`  
> **论文对应章节**：第 2 章（地对空无人机目标检测模型重训与性能评测）  
> **核心实验底座**：RTX 5070 Laptop GPU (DirectML 硬件加速) | Python 3.11 + PyTorch 2.14.1 + CUDA 13.0  
> **基准数据集**：DUT Anti-UAV Detection Benchmark (IEEE T-ITS 2022)  

---

## 一、 系统定位与阶段职责界定

在反无人机光电跟踪系统中，目标检测模块的核心定位为**广域索敌与失锁按需重捕**，作为后续单目标跟踪与云台伺服闭环的高性能感知前哨：

```
                    【阶段一：目标检测阶段 (Target Detection Phase)】
                     - 负责模块：YOLO11 (OnnxDetector) + DirectML GPU 加速
                     - 核心职责：全图视野广域搜索 (Search)、操作员“点击锁定 (Click-to-Lock)”、
                                以及跟踪阶段失锁时的“单次按需重捕获 (Re-detection On-Demand)”
                     - 运行频度：常态 0 FPS 挂起待机；索敌/失锁时单次唤醒运行 1 帧
                                          │
                                          ▼ 传递锁定目标精确中心与尺度 (TrackResult)
                    【阶段二：目标跟踪阶段 (Target Tracking Phase)】
                     - 负责模块：DsstCore (相关滤波) / ISotTracker (深度单目标跟踪)
                     - 核心职责：以 30~60 FPS 高帧率极速输出单一目标脱靶量 (dx, dy)，闭环驱动云台电机
                     - 运行频度：高频常驻 (单帧耗时 < 4ms)
```

---

## 二、 空域弱小无人机数据集与视频域融合工程

针对地对空长焦监控场景下低慢小目标尺度极小、形态多变、背景杂波多样以及失锁重捕期间面临的三维姿态机动和空视野负样本干扰等物理特性，构建双层检测数据底座：

1. **静态空域基准（DUT Anti-UAV Detection，IEEE T-ITS 2022）**：
   - 样本规模：7,800 张真实长焦空域大图，小目标（面积 $<32\times 32$ 像素）占比达 80.9%；
   - 标准化切分：经由格式转换整理为标准单类别（`0: UAV`）YOLO 格式，分为 **5,200 张训练集** 与 **2,600 张独立测试验证集**，构建 `datasets/anti_uav.yaml`。
2. **面向跟踪重捕的视频域融合数据集（`Anti-UAV-Merged`，`datasets/anti_uav_merged.yaml`）**：
   - 为弥补纯静态检测样本对连续飞行过程中大姿态机动、低对比度微弱帧及纯背景空视野覆盖的不足，结合 `CASIA-Anti-UAV300` 连续长焦航迹，通过确定性关键帧抽样与硬链接脚本（`scripts/build_merged_uav_dataset.py`）融合构建 `Anti-UAV-Merged`（共 **16,985** 张图像）：
     - **训练集（11,772 张）**：DUT 5,200 张（5,197 正样本 / 3 负样本）+ CASIA 关键帧 6,572 张（6,434 正样本 + 138 帧目标出视野纯背景负样本）；
     - **验证集（5,213 张）**：DUT 2,600 张（2,600 正样本）+ CASIA 关键帧 2,613 张（2,562 正样本 + 51 帧纯背景负样本）。

---

## 三、 YOLO11 目标检测模型重训、重捕评测与二级防虚警校验

### 3.1 DUT 静态基准重训与端侧实时性能
采用 YOLO11 架构在 DUT Anti-UAV 基准上开展针对性重训，并在 2,600 张独立测试集与 NVIDIA RTX 5070 GPU（DirectML 硬件加速）上完成全量精度与时延评测：

| 模型规格 | 参数量 | 训练轮数 | Precision | ★ Recall *(云台核心)* | mAP@50 | mAP@50-95 | 纯 GPU 时延 | 端到端 FPS |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **YOLO11n (Nano)** | **2.6M** | 80 轮 | 95.20% | **83.17%** | **88.00%** | **52.72%** | **2.7 ms** | **86.6** |
| **YOLO11s (Small)** | **9.4M** | 80 轮 | 95.95% | **87.02%** | **91.22%** | **56.74%** | **3.6 ms** | **77.2** |

### 3.2 面向跟踪重捕的视频域融合重训与闭环冷启动评测
在 `Anti-UAV-Merged` 验证集（5,213 张：含 DUT 2,600 张与 CASIA 2,613 张关键帧/51 负样本）及原 `DUT Anti-UAV` 验证集（2,600 张）上的双集交叉验证结果如下（300 轮从零重训，SGD，`batch=16`，RTX 4080 SUPER 训练、RTX 5070 DirectML 端侧推理实测）：

| 模型规格 | 训练数据源 | 轮数 | Merged Val P / R / mAP@50 / mAP@50-95 | DUT Val P / R / mAP@50 / mAP@50-95 | 纯 GPU 时延 | 端到端 FPS |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| **YOLO11n (DUT 微调)** | `DUT Anti-UAV` | 80 | 92.76% / 73.72% / 81.52% / 45.91% | 95.20% / 83.17% / 88.00% / 52.72% | 2.7 ms | 86.6 |
| **YOLO11s (DUT 微调)** | `DUT Anti-UAV` | 80 | 93.46% / 77.69% / 84.53% / 48.99% | 95.95% / 87.02% / 91.22% / 56.74% | 3.6 ms | 77.2 |
| **YOLO11n (DUT 从零)** | `DUT Anti-UAV` | 300 | 91.40% / 71.90% / 78.68% / 43.98% | 96.12% / 82.37% / 88.50% / 53.44% | 2.7 ms | 86.6 |
| **YOLO11n (融合从零)** | `Anti-UAV-Merged` | 300 | **96.89% / 89.67% / 93.27% / 60.40%** | 94.95% / 81.34% / 87.02% / 53.91% | **2.96 ms** | **165.9** *(test_detector)* |
| **YOLO11s (融合从零)** | `Anti-UAV-Merged` | 300 | **97.15% / 91.94% / 94.68% / 62.27%** | 95.38% / 85.85% / 90.19% / **57.38%** | **4.73 ms** | **144.3** *(test_detector)* |

在 `Anti-UAV` 7 组长焦跟踪序列（925 采样有效帧 + 64 帧目标出视野缺失帧，零位置先验全图冷重捕协议，`tools/bench_exp.cpp --merged`）上，各模型在 C++ DirectML 实机链路下的冷重捕表现如下（涵盖原始几何门限 `RawBase`、`Area03_SoftLT` 放宽尺度+高置信强直方图软门限及 `Lag1` 双帧空间连续性确认）：

| 检测模型 / 重捕策略 | 训练数据源 | 重捕 AUC | 重捕 DP@20 | 漏检率 (Miss) | 有效帧劫持 (Hijack) | 命中 CLE | 缺失帧误报 (Absent FP) |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **YOLO11n_FT (`RawBase`)** | `DUT` | 56.57% | 67.78% | 31.46% | 3.03% | 43.0 px | 9.4% (6/64) |
| **YOLO11n_FT (`Area03_SoftLT`)** | `DUT` | 70.07% | 84.86% | 14.05% | 4.22% | 47.8 px | 3.1% (2/64) |
| **YOLO11s_FT (`Area03_SoftLT`)** | `DUT` | **70.89%** | **86.81%** | **12.65%** | 7.78% | 70.9 px | 7.8% (5/64) |
| **YOLO11n_Sc (`Area03_SoftLT`)** | `DUT` | 68.59% | 84.32% | 14.59% | 3.03% | 36.3 px | 7.8% (5/64) |
| **YOLO11n_Merged (`RawBase`)** | `Anti-UAV-Merged` | 53.35% | 64.22% | 34.59% | **0.65%** | **7.7 px** | 17.2% (11/64) |
| **YOLO11n_Merged (`Area03_SoftLT`)** | `Anti-UAV-Merged` | 68.38% | 83.24% | 14.49% | **0.00%** | **4.6 px** | 15.6% (10/64) |
| **YOLO11n_Merged (`Area03_SoftLT_Lag1`)** | `Anti-UAV-Merged` | 67.44% | 81.95% | 16.00% | **0.00%** | **4.4 px** | 4.7% (3/64) |
| **YOLO11s_Merged (`RawBase`)** | `Anti-UAV-Merged` | 56.96% | 69.95% | 29.19% | **0.11%** | **4.8 px** | 7.8% (5/64) |
| **YOLO11s_Merged (`RawArea03`)** | `Anti-UAV-Merged` | **72.92%** | **90.81%** | **7.89%** | **0.11%** | **4.8 px** | 7.8% (5/64) |
| **YOLO11s_Merged (`Area03_SoftLT`)** | `Anti-UAV-Merged` | 69.75% | 85.41% | 13.30% | **0.11%** | **4.9 px** | 7.8% (5/64) |
| **YOLO11s_Merged (`Area03_SoftLT_Lag1`)** | `Anti-UAV-Merged` | 69.15% | 84.43% | 14.27% | **0.11%** | **4.9 px** | **4.7% (3/64)** |

### 3.3 条件式二级抗虚警校验（`VaL_CNN`）
针对复杂低空飞鸟与高光云层诱发的假阳性虚警，系统在 YOLO11 初筛与状态机重捕之间保留**条件式轻量校验网络（`VaL_CNN`）**：在放宽低置信度候选初筛门限以提升远距离微弱目标召回率的同时，对存疑候选区裁切局部切片进行二次语义鉴伪，并在 `Drone-vs-Bird` 挑战赛数据集与融合负样本集上定量评估每帧虚警率（FP/frame）压降效果。

---

## 四、 重训效果与弱小目标感知分析

1. **视频域融合重训的精度与抗劫持收益**：
   - 在 `Anti-UAV-Merged` 融合验证集（5,213 张）上，融合重训模型 `YOLO11n_Merged` / `YOLO11s_Merged` 的 mAP@50 分别达到 **93.27%** 与 **94.68%**（Recall **89.67%** / **91.94%**），显著超越 DUT 单源微调模型的 81.52% / 84.53%（Recall 73.72% / 77.69%）；在原 `DUT` 验证集上，`YOLO11s_Merged` 的 mAP@50-95 达到 **57.38%**，超越 DUT 微调基线（56.74%）；
   - 在 C++ DirectML 7 序列冷重捕评测中，融合重训模型的**有效帧误劫持率（Hijack）与定位误差（HitCLE）获得量级压降**：`YOLO11n_Merged` 在 `Area03_SoftLT` 下将劫持率由 DUT 微调版的 4.22% 降至 **0.00%**、命中 CLE 由 47.8 px 降至 **4.6 px**；`YOLO11s_Merged` 劫持率降至 **0.11%**（vs DUT 微调版 7.78%）、命中 CLE 降至 **4.9 px**（vs 70.9 px），`RawArea03` 下漏检率低至 **7.89%**（DP@20 **90.81%**、AUC **72.92%**）。
2. **出视野纯背景虚警边界与二级校验必要性**：
   - 客观指标同时表明，在 `video07` 64 帧目标出视野空帧上，轻量 `YOLO11n_Merged` 单帧缺失误报为 15.6%–17.2%（`YOLO11s_Merged` 为 7.8%），需叠加双帧空间一致性门控（`Lag1` 将两者均压低至 **4.7% / 3/64**），并进一步依托 `VaL_CNN` 二级语义鉴伪压制瞬态云层杂波虚警。
3. **机电伺服硬实时推断保障**：
   - 在 RTX 5070 GPU DirectML 下，融合重训静态 ONNX 模型 `yolo11n_merged_scratch.onnx` 单帧纯推理 **2.96 ms**（端到端 `6.03 ms / 165.9 FPS`），`yolo11s_merged_scratch.onnx` 纯推理 **4.73 ms**（端到端 `6.93 ms / 144.3 FPS`），均低于光电云台机电伺服 15 ms 容许窗口。

---

## 五、 本地实验数据与成果资产留存说明

本研究涉及的训练流水线（`scripts/train_uav_detector.py`、`scripts/build_merged_uav_dataset.py`）、重捕离线评测工具（`tools/bench_redetect.cpp`、`tools/bench_exp.cpp`）、逐轮日志与部署级静态 ONNX 资产完整留存于本地工程中：

- **权重与模型归档**：`weights/finetuned/`、`weights/scratch/yolo11{n,s}_merged_scratch.pt`、`models/yolo11{n,s}_uav_ft.onnx`、`models/yolo11{n,s}_merged_scratch.onnx` 等；
- **逐轮训练日志**：`raw_data/yolo11n_uav_ft_results.csv`、`yolo11s_uav_ft_results.csv`、`yolo11{n,s}_merged_scratch_300e_results.csv`、`yolo_merged_eval_summary.csv` 等；
- **重捕对比原始数据**：`scripts/benchmark_yolo_vs_route_a.json`、`scripts/benchmark_merged_yolo.json`；
- **评估曲线图集**：`figures/` 目录下收录 DUT 与融合重训损失下降曲线、P-R 曲线、混淆矩阵及远距离弱小目标真实推断画中画图集（`figures/12_distant_uav_detections_gallery.png`、`figures/13~16` 融合训练图集）。
