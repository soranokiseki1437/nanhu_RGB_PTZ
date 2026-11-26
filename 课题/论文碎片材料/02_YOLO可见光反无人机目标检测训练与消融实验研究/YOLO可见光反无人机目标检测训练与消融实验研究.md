# 面向光电云台反无人机任务的 YOLO11 目标检测重训研究

> **归属目录**：`课题/论文碎片材料/02_YOLO可见光反无人机目标检测训练与消融实验研究`  
> **论文对应章节**：第 2 章（地对空无人机目标检测模型重训与性能评测）  
> **核心实验底座**：RTX 4080 SUPER (训练) / RTX 5070 Laptop GPU (DirectML 硬件加速推理)  
> **基准数据集**：DUT Anti-UAV Detection (IEEE T-ITS 2022) & `Anti-UAV-Merged` (16,985 张)  

---

## 一、 系统定位与阶段职责界定

在反无人机光电跟踪系统中，目标检测模块的核心定位为**广域索敌与失锁按需重捕**，作为后续单目标跟踪与云台伺服闭环的高性能感知前哨：

```text
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

## 三、 面向视频域重捕的 YOLO11 融合重训与闭环评测

### 3.1 双验证集交叉精度与端侧实时性能
在 `Anti-UAV-Merged` 上对 **YOLO11n (2.6M)** 与 **YOLO11s (9.4M)** 执行全量 300 轮从零训练（SGD，`lr0=0.01`，`batch=16`，`cos_lr=True`，`close_mosaic=15`），并在 `Anti-UAV-Merged` 验证集（5,213 张）与原 `DUT Anti-UAV` 验证集（2,600 张）及 RTX 5070 C++ DirectML 端侧完成全面评测：

| 模型规格 | 参数量 | 训练轮数 | Merged Val P / R / mAP@50 / mAP@50-95 | DUT Val P / R / mAP@50 / mAP@50-95 | 纯 GPU 时延 | 端到端 FPS |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **YOLO11n (`yolo11n_merged_scratch`)** | **2.6M** | 300 | 96.89% / 89.67% / 93.27% / 60.40% | 94.95% / 81.34% / 87.02% / 53.91% | **2.96 ms** | **165.9** |
| **YOLO11s (`yolo11s_merged_scratch`)** | **9.4M** | 300 | **97.15% / 91.94% / 94.68% / 62.27%** | **95.38% / 85.85% / 90.19% / 57.38%** | **4.73 ms** | **144.3** |

### 3.2 闭环冷启动重捕协议评测（C++ DirectML）
在 `Anti-UAV` 7 组长焦跟踪序列（925 采样有效帧 + 64 帧目标出视野缺失帧，零位置先验全图冷重捕协议，`tools/bench_exp.cpp --merged`）上，对比融合重训模型与通用 COCO 基线（`yolov8n.onnx`）在原生几何门限（`RawBase`：`conf>=0.35, areaRatio∈[0.15, 6.0]`）、放宽尺度几何门限（`RawArea03`：`areaRatio∈[0.03, 16.0]`）、高置信强直方图软核门控（`Area03_SoftLT`）及双帧空间一致性（`Lag1`）下的实测表现：

| 检测模型 / 重捕策略 | 重捕 AUC | 重捕 DP@20 | 漏检率 (Miss) | 有效帧劫持 (Hijack) | 命中 CLE | 缺失帧误报 (Absent FP) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **YOLOv8n COCO (`Area03_SoftLT`)** | 35.29% | 38.59% | 57.95% | 7.89% | 78.6 px | 0.0% (0/64) |
| **YOLO11n_Merged (`RawBase`)** | 53.35% | 64.22% | 34.59% | 0.65% | 7.7 px | 17.2% (11/64) |
| **YOLO11n_Merged (`RawArea03`)** | 69.56% | 85.19% | 12.54% | 0.54% | 6.9 px | 17.2% (11/64) |
| **YOLO11n_Merged (`Area03_SoftLT`)** | 68.38% | 83.24% | 14.49% | **0.00%** | **4.6 px** | 15.6% (10/64) |
| **YOLO11n_Merged (`Area03_SoftLT_Lag1`)** | 67.44% | 81.95% | 16.00% | **0.00%** | **4.4 px** | 4.7% (3/64) |
| **YOLO11s_Merged (`RawBase`)** | 56.96% | 69.95% | 29.19% | **0.11%** | **4.8 px** | 7.8% (5/64) |
| **YOLO11s_Merged (`RawArea03`)** | **72.92%** | **90.81%** | **7.89%** | **0.11%** | **4.8 px** | 7.8% (5/64) |
| **YOLO11s_Merged (`Area03_SoftLT`)** | 69.75% | 85.41% | 13.30% | **0.11%** | **4.9 px** | 7.8% (5/64) |
| **YOLO11s_Merged (`Area03_SoftLT_Lag1`)** | 69.15% | 84.43% | 14.27% | **0.11%** | **4.9 px** | **4.7% (3/64)** |

### 3.3 条件式二级抗虚警校验（`VaL_CNN`）
针对复杂低空飞鸟与高光云层诱发的假阳性虚警，系统在 YOLO11 初筛与状态机重捕之间保留**条件式轻量校验网络（`VaL_CNN`）**：在放宽低置信度候选初筛门限以提升远距离微弱目标召回率的同时，对存疑候选区裁切局部切片进行二次语义鉴伪，并在 `Drone-vs-Bird` 挑战赛数据集与融合负样本集上定量评估每帧虚警率（FP/frame）压降效果。

---

## 四、 重训效果与弱小目标感知分析

1. **视频域融合重训的精度与抗劫持收益**：
   - 在 `Anti-UAV-Merged` 验证集（5,213 张）上，`YOLO11n_Merged` 与 `YOLO11s_Merged` 的 mAP@50 分别达到 **93.27%** 与 **94.68%**（Recall **89.67%** / **91.94%**）；在原 `DUT` 验证集上，`YOLO11s_Merged` 达到 **90.19%** mAP@50 与 **57.38%** mAP@50-95；
   - 在 C++ DirectML 7 序列冷重捕评测中，融合重训模型展现出极高的语义纯度与定位精度：`YOLO11n_Merged` 在 `Area03_SoftLT` 下有效帧劫持率为 **0.00%**、命中 CLE 为 **4.6 px**；`YOLO11s_Merged` 有效帧劫持率仅 **0.11%**、命中 CLE 为 **4.9 px**，`RawArea03` 下漏检率低至 **7.89%**（DP@20 **90.81%**、AUC **72.92%**）。
2. **出视野纯背景虚警边界与二级校验必要性**：
   - 客观指标同时表明，在 `video07` 64 帧目标出视野空帧上，轻量 `YOLO11n_Merged` 单帧缺失误报为 15.6%–17.2%（`YOLO11s_Merged` 为 7.8%），需叠加双帧空间一致性门控（`Lag1` 将两者均压低至 **4.7% / 3/64**），并进一步依托 `VaL_CNN` 二级语义鉴伪压制瞬态云层杂波虚警。
3. **机电伺服硬实时推断保障**：
   - 在 RTX 5070 GPU DirectML 下，`yolo11n_merged_scratch.onnx` 单帧纯推理 **2.96 ms**（端到端 `6.03 ms / 165.9 FPS`），`yolo11s_merged_scratch.onnx` 纯推理 **4.73 ms**（端到端 `6.93 ms / 144.3 FPS`），均远低于光电云台机电伺服 15 ms 容许窗口。

---

## 五、 本地实验数据与成果资产留存说明

本研究涉及的训练流水线（`scripts/train_uav_detector.py`、`scripts/build_merged_uav_dataset.py`）、重捕离线评测工具（`tools/bench_redetect.cpp`、`tools/bench_exp.cpp`）、逐轮日志与部署级静态 ONNX 资产完整留存于本地工程中：

- **权重与模型归档**：`weights/scratch/yolo11{n,s}_merged_scratch.pt`、`models/yolo11{n,s}_merged_scratch.onnx`；
- **逐轮训练日志**：`raw_data/yolo11n_merged_scratch_300e_results.csv`、`raw_data/yolo11s_merged_scratch_300e_results.csv`、`raw_data/yolo_merged_eval_summary.csv`；
- **重捕对比原始数据**：`scripts/benchmark_merged_yolo.json`；
- **评估曲线图集**：`figures/` 目录下收录融合重训损失下降曲线、P-R 曲线、混淆矩阵及远距离弱小目标真实推断画中画图集（`figures/12_distant_uav_detections_gallery.png`、`figures/13~16` 融合训练图集）。
