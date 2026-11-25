# -*- coding: utf-8 -*-
r"""
脚本名称: init_tracking_benchmark_workspace.py
功能: 在 D:\PyProjects\SOT_Tracking_Baselines 目录下初始化【轻量化单目标跟踪与反无人机基准库 (SOT Benchmark Zoo)】，
      包含纯开源轻量算法归档、统一适配评测层、实验数据指标保存与标准工作流。
      严格对齐同算力级轻量对比与反无人机前沿垂直赛道（如 UAV-DETR, TCTrack, HiFT, LightTrack, FEAR, NanoTrack）。
"""

import os
import shutil
from pathlib import Path

TARGET_DIR = Path(r"D:\PyProjects\SOT_Tracking_Baselines")

README_CONTENT = """# 可见光单目标跟踪与反无人机开源轻量基线算法库 (SOT_Tracking_Baselines)

> **工程定位**：面向地面/车载长焦可见光反无人机（Anti-UAV）及机电伺服场景的**纯开源轻量基线算法集中纳编、环境部署、统一评测与实验指标沉淀平台**。  
> **核心原则**：**专注同算力级轻量化网络与反无人机垂直赛道**。剔除不具备端侧低时延伺服可行性的重型通用大模型，全面聚焦于**轻量化 SOT、空天时序跟踪（UAV Tracking）、反无人机前沿感知（Anti-UAV Perception）及结构重参数化（Rep-Backbone）开源算法**，跑通、评测、打点并沉淀客观数据。  
> **算力硬件与环境**：NVIDIA GeForce RTX 5070 Laptop GPU（Blackwell 架构，PyTorch 2.14.1 + CUDA 13.0，Conda 环境：`anti_uav_yolo`）  
> **评测基准数据集**：[datasets/Anti-UAV/tracking/](file:///d:/PyProjects/SOT_Tracking_Baselines/datasets/Anti-UAV/tracking/)（20 组真机长焦飞行连续序列 + 官方逐帧 Ground Truth，已通过系统 Junction 零拷贝软链接打通）

---

## 一、 纯轻量基线工作区目录结构树

工程所有目录均服务于“**扒轻量算法源码 -> 下官方权重 -> 统一接口评测 -> 保存预测与指标**”：

```text
D:\\PyProjects\\SOT_Tracking_Baselines/
├── README.md                   # 本文件：纯轻量基线算法库定位、收录清单与目录规范
├── WORKFLOW.md                 # 核心工作流：逐个克隆源码、下载权重、运行打点与导出图表的保姆级指南
├── datasets/                   # [Junction 软链接] 映射至主工程 datasets/，直接读取 Anti-UAV 航迹
├── algorithms/                 # ★ 核心轻量对比算法官方开源源码集中克隆目录
│   ├── LightTrack/             # ICCV 2021 Oral (微软亚洲研究院轻量 NAS 跟踪器, 1.6M)
│   ├── FEAR-Tracker/           # ECCV 2022 (动态双模板轻量跟踪器, 1.2M)
│   ├── NanoTrack/              # ICCV 2021 (超轻量级移动端跟踪器, 0.4M)
│   ├── TCTrack/                # CVPR 2022 / TPAMI 2023 (同济大学空天无人机时序自适应轻量跟踪器)
│   ├── HiFT/                   # ICCV 2021 (同济大学无人机分层轻量 Transformer 跟踪器)
│   ├── UAVDETR/                # 2024 前沿 (专用于 Anti-Drone 弱小目标感知的轻量 DETR)
│   ├── RepViT/                 # CVPR 2024 (结构重参数化骨干对比组)
│   ├── pysot/                  # CVPR 2019 Oral (商汤经典 SiamRPN++，仅作为重型极端能效比反面对照)
│   └── traditional/            # 工业传统滤波对拍脚本 (KCF, DSST)
├── checkpoints/                # 对应各算法官方权威预训练权重 (*.pth)
│   ├── lighttrack/             # lighttrack_mobile.pth
│   ├── fear/                   # FEAR_M.pth / FEAR_L.pth
│   ├── nanotrack/              # nanotrack.pth
│   ├── tctrack/                # TCTrack.pth
│   ├── hift/                   # hift.pth
│   ├── uavdetr/                # uav_detr.pth (DUT-Anti-UAV 权重)
│   ├── repvit/                 # repvit_m0_9.pth
│   └── pysot/                  # siamrpn_r50_l234_dwxcorr.pth (辅助对照)
├── evaluation/                 # 统一评测框架与算法适配层 (统一接口)
│   ├── run_eval.py             # 统一入口批量评测脚本 (支持单模型或一键批量运行)
│   ├── trackers/               # 各算法统一适配器 (继承自 BaseTracker，统一输入输出)
│   │   ├── base_tracker.py     # 纯虚适配基类 (init / track)
│   │   ├── lighttrack_adapter.py
│   │   ├── fear_adapter.py
│   │   ├── nanotrack_adapter.py
│   │   ├── tctrack_adapter.py
│   │   ├── hift_adapter.py
│   │   ├── uavdetr_adapter.py
│   │   ├── repvit_adapter.py
│   │   ├── pysot_adapter.py
│   │   └── traditional_adapter.py
│   └── metrics.py              # OPE 协议核算函数 (AUC, DP@20px, 纯 GPU 时延与绘图)
└── results/                    # 实验成果沉淀 (每次运行自动归档，不覆盖历史)
    ├── raw_predictions/        # 各算法逐帧预测框输出 (*.txt)
    ├── figures/                # 硕士论文高清对比图 (Success Plots, Precision Plots)
    └── benchmark_summary.csv   # 最终核心指标汇总三线大表 (AUC, CLE, 时延, FPS, 参数量, FLOPs)
```

---

## 二、 纳入横评的开源轻量算法梯队清单

| 梯队分类 | 算法名称 | 会议/期刊出处 | 官方开源 GitHub 仓库 | 预训练模型与权重类型 | 参数量与定位 |
| :--- | :--- | :--- | :--- | :--- | :---: |
| **传统滤波基准** | **KCF / DSST** | TPAMI 2015 / 2017 | `opencv-python` / 纯数学实现 | 纯数学无权重 | 0.0M (论证深度特征代差) |
| **移动超轻量** | **NanoTrack** | ICCV 2021 | `https://github.com/HonglinChu/NanoTrack` | 官方超轻量权重 (`nanotrack.pth`) | **0.4 M** (超轻量级端侧) |
| **经典轻量** | **LightTrack** | ICCV 2021 Oral | `https://github.com/researchmm/LightTrack` | 官方 MobileNet-NAS 权重 (`lighttrack_mobile.pth`) | **1.6 M** (方案 04 主力基准) |
| **经典轻量** | **FEAR-L** | ECCV 2022 | `https://github.com/vasgaowei/FEAR-Tracker` | 官方 FEAR-M / FEAR-L 权重 | **1.2 M** (动态双模板注意力) |
| **空天时序前沿** | **HiFT** | ICCV 2021 | `https://github.com/vision4robotics/HiFT` | 官方 HiFT 权重 (`hift.pth`) | **3.5 M** (无人机分层Transformer) |
| **空天时序前沿** | **TCTrack** | CVPR 2022 / TPAMI 2023 | `https://github.com/vision4robotics/TCTrack` | 官方 TCTrack 权重 (`TCTrack.pth`) | **6.2 M** (时序自适应空天跟踪) |
| **反无人机前沿** | **UAV-DETR** | arXiv 2024 | `https://github.com/wd-sir/UAVDETR` | 官方 DUT-Anti-UAV 权重 (`uav_detr.pth`) | **8.8 M** (反无人机微小目标感知) |
| **重参数化对照** | **RepViT-Track**| CVPR 2024 | `https://github.com/jameslahm/RepViT` | 官方 RepViT-M0.9 权重 | **1.8 M** (骨干迁移核心消融) |
| **极端能效对照** | **SiamRPN++** | CVPR 2019 Oral | `https://github.com/STVIR/pysot` | ResNet-50 骨干官方权重 | 53.9 M (反向能效比对比) |

---

## 三、 数据保存与论文图表生产机制

1. **逐帧轨迹保存**：每个算法在 20 组航迹上跑完后，将逐帧预测框 `[x, y, w, h]` 保存在 `results/raw_predictions/<算法名>/<序列名>.txt`；
2. **标准指标核算**：
   - **Success (AUC)**：IoU 阈值从 0 到 1 的曲线下面积；
   - **Precision (DP@20px)**：中心位置误差小于 20 像素的帧数比例；
   - **GPU 纯推理时延 (ms)** 与 **端到端 FPS**；
3. **成果导出**：
   - 自动生成符合 IEEE / CVPR 标准配色的 Success Plot 与 Precision Plot；
   - 自动导出 `benchmark_summary.csv` 与 Markdown 三线大表。
"""

WORKFLOW_CONTENT = """# SOT 轻量基线算法库环境配置、代码拉取与批量评测工作流 (WORKFLOW.md)

> 本指南指导如何在当前工作区中**逐个下载各开源轻量跟踪与反无人机感知算法、放置预训练权重、运行统一打点并沉淀指标数据**。

---

## 阶段一：运行环境激活与通用工具库就绪

直接复用主工程打通的 `anti_uav_yolo` 环境（Python 3.11 + PyTorch 2.14.1 + CUDA 13.0，适配 RTX 5070）：

```powershell
# 1. 激活专属环境
& "D:\\miniconda3\\Scripts\\activate.bat" anti_uav_yolo

# 2. 补装跟踪评测与通用依赖库
pip install easydict pyyaml timm opencv-python matplotlib pandas scipy got10k openpyxl yacs
```

---

## 阶段二：逐个拉取开源轻量对比算法源码与下载官方权重

在 `algorithms/` 目录下克隆各算法官方仓库，并将下载的权重文件放入 `checkpoints/`：

### 2.1 拉取 LightTrack (ICCV 2021 Oral - 微软亚洲研究院)
```powershell
cd algorithms
git clone https://github.com/researchmm/LightTrack.git
# 官方预训练权重 lighttrack_mobile.pth 下载后放入:
# checkpoints/lighttrack/lighttrack_mobile.pth
```

### 2.2 拉取 FEAR-Tracker (ECCV 2022)
```powershell
git clone https://github.com/vasgaowei/FEAR-Tracker.git
# 官方预训练权重 FEAR_M.pth / FEAR_L.pth 下载后放入:
# checkpoints/fear/FEAR_M.pth
```

### 2.3 拉取 NanoTrack (ICCV 2021 超轻量移动端)
```powershell
git clone https://github.com/HonglinChu/NanoTrack.git
# 官方预训练权重 nanotrack.pth 下载后放入:
# checkpoints/nanotrack/nanotrack.pth
```

### 2.4 拉取 TCTrack / TCTrack++ (CVPR 2022 / TPAMI 2023 - 同济大学空天时序跟踪)
```powershell
git clone https://github.com/vision4robotics/TCTrack.git
# 官方预训练权重 TCTrack.pth 下载后放入:
# checkpoints/tctrack/TCTrack.pth
```

### 2.5 拉取 HiFT (ICCV 2021 - 同济大学无人机分层 Transformer)
```powershell
git clone https://github.com/vision4robotics/HiFT.git
# 官方预训练权重 hift.pth 下载后放入:
# checkpoints/hift/hift.pth
```

### 2.6 拉取 UAV-DETR (2024 前沿 - 反无人机专属轻量 DETR)
```powershell
git clone https://github.com/wd-sir/UAVDETR.git
# 官方 DUT-Anti-UAV 权重下载后放入:
# checkpoints/uavdetr/uav_detr.pth
```

### 2.7 拉取 RepViT (CVPR 2024 - 结构重参数化骨干消融组)
```powershell
git clone https://github.com/jameslahm/RepViT.git
# 官方权重下载后放入:
# checkpoints/repvit/repvit_m0_9.pth
```

### 2.8 拉取 PySOT (商汤 SiamRPN++，仅作为极端能效比反面对照)
```powershell
git clone https://github.com/STVIR/pysot.git
# 官方预训练权重 siamrpn_r50_l234_dwxcorr.pth 下载后放入:
# checkpoints/pysot/siamrpn_r50_l234_dwxcorr.pth
```

---

## 阶段三：运行统一评测脚本批量打点

通过 `evaluation/run_eval.py` 驱动各算法在 [datasets/Anti-UAV/tracking/](file:///d:/PyProjects/SOT_Tracking_Baselines/datasets/Anti-UAV/tracking/) 序列上运行：

### 3.1 单个轻量算法快速试跑验证
```powershell
# 仅评测 LightTrack
python evaluation/run_eval.py --tracker lighttrack --dataset_dir datasets/Anti-UAV/tracking

# 仅评测 TCTrack 空天时序模型
python evaluation/run_eval.py --tracker tctrack --dataset_dir datasets/Anti-UAV/tracking
```

### 3.2 全量轻量基线一键批量横评打点
```powershell
# 一键批量评测全部已就绪轻量算法，自动计时并核算指标
python evaluation/run_eval.py --tracker all --dataset_dir datasets/Anti-UAV/tracking
```

---

## 阶段四：产出并检视评测指标与图表

运行完毕后，检查成果沉淀目录：
1. **指标汇总大表**：打开 `results/benchmark_summary.csv`，查看各算法的 Success(AUC)、DP@20px、GPU耗时与吞吐帧率；
2. **学术曲线图**：打开 `results/figures/success_plots.png` 与 `results/figures/precision_plots.png`；
3. **逐帧原始数据**：`results/raw_predictions/` 下已妥善留存每帧的预测位置，供撰写学位论文时按需绘制跟踪轨迹对拍图。
"""

def main():
    TARGET_DIR.mkdir(parents=True, exist_ok=True)
    
    # 清理掉之前可能创建的不合理大模型或陈旧目录 (保持纯轻量与前沿)
    obsolete_dirs = [
        TARGET_DIR / "checkpoints" / "ostrack",
        TARGET_DIR / "algorithms" / "OSTrack"
    ]
    for d in obsolete_dirs:
        if d.exists() and d.is_dir():
            shutil.rmtree(d, ignore_errors=True)
            print(f"[CLEAN] Removed obsolete dir: {d.name}")
            
    # 创建纯轻量基线评测的标准子目录
    required_dirs = [
        TARGET_DIR / "algorithms" / "traditional",
        TARGET_DIR / "checkpoints" / "lighttrack",
        TARGET_DIR / "checkpoints" / "fear",
        TARGET_DIR / "checkpoints" / "nanotrack",
        TARGET_DIR / "checkpoints" / "tctrack",
        TARGET_DIR / "checkpoints" / "hift",
        TARGET_DIR / "checkpoints" / "uavdetr",
        TARGET_DIR / "checkpoints" / "repvit",
        TARGET_DIR / "checkpoints" / "pysot",
        TARGET_DIR / "evaluation" / "trackers",
        TARGET_DIR / "results" / "raw_predictions",
        TARGET_DIR / "results" / "figures"
    ]
    for rd in required_dirs:
        rd.mkdir(parents=True, exist_ok=True)
        
    print("[OK] Directories structured for Pure Lightweight SOT Baseline Zoo.")
    
    # 写入规范的 README.md 与 WORKFLOW.md
    readme_path = TARGET_DIR / "README.md"
    readme_path.write_text(README_CONTENT.strip(), encoding="utf-8")
    print(f"[OK] Successfully updated: {readme_path}")
    
    workflow_path = TARGET_DIR / "WORKFLOW.md"
    workflow_path.write_text(WORKFLOW_CONTENT.strip(), encoding="utf-8")
    print(f"[OK] Successfully updated: {workflow_path}")

if __name__ == "__main__":
    main()
