# -*- coding: utf-8 -*-
"""
脚本名称: init_tracking_benchmark_workspace.py
功能: 在 D:\PyProjects\不知道取什么名字 目录下初始化标准科研工程规范，
      写入 UTF-8 编码的 README.md 与 WORKFLOW.md。
"""

import os
from pathlib import Path

TARGET_DIR = Path(r"D:\PyProjects\不知道取什么名字")

README_CONTENT = """# 可见光反无人机单目标跟踪算法横评、科研创新与消融实验基准库 (LightUAV-Tracking-Benchmark)

> **工程定位**：面向地面/车载长焦可见光光电云台反无人机（Anti-UAV）场景的**纯算法科研、开源基线横向打点、自研网络两阶段训练、消融实验矩阵与学术图表生产工作区**。  
> **上游主工程**：[RGB_PTZ_Integrated](file:///d:/PyProjects/RGB_PTZ_Integrated)（C++ / Qt 6 上位机、视频解码与 Pelco-D 云台机电伺服闭环）  
> **下游交付物**：纯静态解耦 ONNX 模型（`sot_template.onnx` + `sot_search.onnx`）交付至主工程 `models/` 目录，供 DirectML 硬件加速与 C++ `ISotTracker` 运行时加载  
> **算力硬件与环境**：NVIDIA GeForce RTX 5070 Laptop GPU（Blackwell 架构，PyTorch 2.14.1 + CUDA 13.0，Conda 环境：`anti_uav_yolo`）  
> **权威基准数据集**：[datasets/Anti-UAV/tracking/](file:///d:/PyProjects/不知道取什么名字/datasets/Anti-UAV/tracking/)（20 组真机长焦飞行连续序列 + 官方逐帧 Ground Truth，已通过系统 Junction 共享无冗余链接）

---

## 一、 项目体系架构与全景目录树

为了保持纯算法科研与工程生产代码的清晰解耦，本项目所有开源代码、数据记录、论文图表与权重统一规范管理：

```text
D:\\PyProjects\\不知道取什么名字/ (推荐重命名为: LightUAV-Tracking-Benchmark)
├── README.md                   # 本文件：项目学术定位、全景架构与算法体系说明
├── WORKFLOW.md                 # 核心工作流：从拉取开源、打点评测到消融训练的可执行操作指南
├── datasets/                   # [Junction 软链接] 映射至主工程 datasets/，零硬盘拷贝
│   └── Anti-UAV/tracking/      # 20 组真机对空无人机连续跟踪序列与逐帧 GT 标注
├── tracking_benchmarks/        # 核心对比算法开源仓库集中纳编库
│   ├── 01_traditional/         # 第一梯队：传统滤波基准 (KCF, DSST)
│   ├── 02_heavyweight/         # 第二梯队：经典重型深度网络 (DaSiamRPN, SiamRPN++)
│   ├── 03_lightweight/         # 第三梯队：同算力级顶会轻量竞品 (NanoTrack, FEAR, OSTrack, LightTrack)
│   └── 04_reparam/             # 第四梯队：重参数化直接对照组 (RepViT-Track)
├── models/                     # 本课题自研创新网络代码库
│   └── lightuav_sot/           # Rep-LightTrack 源码 (RepViT 骨干 + MFFN 细节融合 + DAC-Head 动态匹配)
├── experiments/                # 自动化实验流水线脚本
│   ├── eval_benchmarks.py      # 一键批量执行 OPE 协议横向打点脚本 (输出逐帧数据与汇总 CSV)
│   ├── dataset_sot_pair.py     # 正负样本对切片与空天时序增强数据加载器
│   ├── train_lightuav.py       # 两阶段端到端自研网络训练脚本 (Warm-up 冻结 + Joint 微调)
│   ├── run_ablation.py         # Exp 1 ~ Exp 5 自动化消融实验调度矩阵
│   └── export_onnx.py          # 纯静态双分支 ONNX 导出脚本 (折叠重参数化结构，输出生产模型)
├── results/                    # 实验成果沉淀 (严禁随意覆盖)
│   ├── raw_csv/                # 各算法在 20 组序列上的逐帧预测框、时延与重叠度原始打点
│   ├── figures/                # 硕士论文高清图表 (Success Plots, Precision Plots, 属性雷达图)
│   └── tables/                 # 硕士论文标准学术三线大表 (Markdown / Excel / Word / LaTeX)
├── checkpoints/                # 模型权重归档
│   ├── pretrained/             # 各开源算法官方权威预训练权重 (*.pth)
│   └── trained/                # 本课题自研模型各消融阶段与最终收敛最优权重
└── outputs/                    # 交付物与工程桥接库
    └── onnx/                   # 纯静态部署模型：sot_template.onnx 与 sot_search.onnx
```

---

## 二、 学术对比体系：实测 8 款 + 文献 3 款豪华阵容

本项目构建了层次极其分明的学术实验体系，彻底封死答辩与盲审专家的各类质疑：

```
                             ┌──────────────────────────────────────┐
                             │       可见光单目标跟踪学术对比体系        │
                             └──────────────────────────────────────┘
                                                 │
         ┌───────────────────────────┬───────────┴───────────┬───────────────────────────┐
         ▼                           ▼                       ▼                           ▼
  【第一梯队：传统滤波】       【第二梯队：经典重型深度】    【第三梯队：同级轻量顶会】    【第四梯队：重参数化前沿】
  • KCF (TPAMI 2015)          • DaSiamRPN (ECCV 2018)     • NanoTrack (ICCV 2021)     • RepViT-Track (CVPR 2024)
  • DSST (工程自研 C++)       • SiamRPN++ (CVPR 2019)     • FEAR-L (ECCV 2022)        (直接回应仅换骨干疑问)
  (证明深度学习代差优势)      (证明轻量化极致能效比)       • OSTrack-Tiny (ECCV 2022)               │
                                                          • LightTrack (ICCV/TPAMI)                 ▼
                                                          (同算力级核心竞品横评)      【本课题：Rep-LightTrack】
                                                                                     (自研创新多模块协同网络)
```

### 锦上添花：大表末尾文献对比（免代码调试）
在最终汇总的论文大表中，表底补充 3 款业界知名大模型（TransT、SwinTrack、SiamFC）作为背景文献参考数据，使全表呈现 **11~12 款算法的豪华学术阵容**。

---

## 三、 本课题自研创新网络：`Rep-LightTrack` (LightUAV-SOT)

针对长焦对空反无人机中目标弱小、长焦剧烈晃动、空地杂波强烈的痛点，网络设计了三大核心创新：
1. **RepViT 结构重参数化骨干**：训练态多分支（$3\\times 3$ 卷积 + $1\\times 1$ 卷积 + 残差）丰富表征，部署态等价折叠为单路 $3\\times 3$ 卷积，保障端侧极致推断；
2. **MFFN 多尺度跨层细节融合**：融合浅层高分辨率细节特征与深层抽象语义特征，防止弱小无人机在深层下采样中淹没；
3. **DAC-Head 动态加权相关匹配头**：引入空间与通道自适应双重注意力门控，抑制云层与地表林木背景伪峰，强化目标主体关联度。

---

## 四、 成果交接机制（如何与上位机 C++ 工程无缝桥接）

本工作区最终收官时，执行 `experiments/export_onnx.py` 将自研收敛最优权重导出为纯静态 ONNX：
- `outputs/onnx/sot_template.onnx`（输入尺寸固定：`1x3x127x127`，RGB 归一化）
- `outputs/onnx/sot_search.onnx`（输入尺寸固定：`1x3x255x255`，RGB 归一化）

生成的文件直接拷贝回 [RGB_PTZ_Integrated/models/](file:///d:/PyProjects/RGB_PTZ_Integrated/models/)，由上位机的 `OnnxSotTracker` 动态加载并以 DirectML 硬件加速运行，实现 $\\ge 60\\text{ FPS}$ 伺服闭环！
"""

WORKFLOW_CONTENT = """# 可见光单目标跟踪基准评测、自研创新训练与消融实验标准工作流 (WORKFLOW.md)

> 本工作流是本工作区研发与实验推进的**保姆级、可执行操作指南**。  
> 按照以下 6 大标准阶段依次执行，即可完整产出硕士论文所需的所有学术图表、大表数据与上位机生产级 ONNX 模型。

---

## 阶段一：运行环境激活与专属依赖打通

本项目直接复用已验证 Blackwell RTX 5070 (sm_120) 硬件特性的 Python 3.11 专属环境：

### 1.1 激活专属环境
在终端执行：
```powershell
# 切换至专属环境
& "D:\\miniconda3\\Scripts\\activate.bat" anti_uav_yolo

# 验证 GPU 与 PyTorch 状态
python -c "import torch; print('PyTorch:', torch.__version__, '| GPU:', torch.cuda.get_device_name(0))"
# 预期输出: PyTorch: 2.14.1+cu130 | GPU: NVIDIA GeForce RTX 5070 Laptop GPU
```

### 1.2 增量安装跟踪评测与通用学术库
```powershell
pip install easydict pyyaml timm opencv-python matplotlib pandas scipy got10k openpyxl onnx onnxruntime-directml
```

---

## 阶段二：基线算法源码拉取与权威预训练权重归档

将各对比算法统一拉取至 `tracking_benchmarks/` 对应梯队目录，并下载其官方预训练权重至 `checkpoints/pretrained/`：

| 算法名称 | 建议目录 | 官方 GitHub 仓库 | 预训练模型与权重放置路径 |
| :--- | :--- | :--- | :--- |
| **LightTrack** | `tracking_benchmarks/03_lightweight/LightTrack` | `https://github.com/researchmm/LightTrack` | `checkpoints/pretrained/lighttrack_mobile.pth` |
| **FEAR-Tracker** | `tracking_benchmarks/03_lightweight/FEAR-Tracker` | `https://github.com/vasgaowei/FEAR-Tracker` | `checkpoints/pretrained/FEAR_M.pth` |
| **NanoTrack** | `tracking_benchmarks/03_lightweight/NanoTrack` | `https://github.com/HonglinChu/NanoTrack` | `checkpoints/pretrained/nanotrack.pth` |
| **OSTrack** | `tracking_benchmarks/03_lightweight/OSTrack` | `https://github.com/bactrl/OSTrack` | `checkpoints/pretrained/ostrack_tiny.pth` |
| **PySOT (SiamRPN++)** | `tracking_benchmarks/02_heavyweight/pysot` | `https://github.com/STVIR/pysot` | `checkpoints/pretrained/siamrpn_r50_l234_dwxcorr.pth` |

---

## 阶段三：一键执行自动化基线横向打点（OPE 协议）

编写统一评测入口 `experiments/eval_benchmarks.py`，全自动读取 [datasets/Anti-UAV/tracking/](file:///d:/PyProjects/不知道取什么名字/datasets/Anti-UAV/tracking/) 序列：

### 3.1 运行评测脚本
```powershell
python experiments/eval_benchmarks.py --dataset_dir datasets/Anti-UAV/tracking --output_dir results/raw_csv
```

### 3.2 自动产出成果
1. **原始打点数据**：`results/raw_csv/` 中保存各算法在每组航迹上的逐帧中心误差 (CLE) 与 IoU 重叠度；
2. **论文高清曲线**：`results/figures/` 中自动绘制 `success_plots.png` 与 `precision_plots.png`；
3. **学术三线大表**：`results/tables/benchmark_summary.md` 自动聚合 Success (AUC)、Precision (DP@20px) 与 GPU 单帧推理时延。

---

## 阶段四：自研网络 `Rep-LightTrack` 两阶段端到端训练

自研网络代码放置于 `models/lightuav_sot/`。采用分层科学初始化与两阶段训练流水线：

### 4.1 阶段一：头部预热训练（Warm-up 阶段，10 Epochs）
冻结骨干网络，仅训练自研 MFFN 细节融合层与 DAC-Head 动态匹配头：
```powershell
python experiments/train_lightuav.py --stage 1 --epochs 10 --batch_size 32 --lr 1e-3 --freeze_backbone
```

### 4.2 阶段二：全网络联合微调（Joint Fine-tuning 阶段，40 Epochs）
解冻全部网络权重，针对长焦反无人机空天背景、大角度机动完成域适应对齐：
```powershell
python experiments/train_lightuav.py --stage 2 --epochs 40 --batch_size 16 --lr 1e-4 --unfreeze_all --pretrained checkpoints/trained/stage1_best.pth
```
收敛最优权重自动保存至：`checkpoints/trained/lightuav_best.pth`。

---

## 阶段五：第 3 章分级递进消融实验矩阵（Ablation Study Matrix）

执行自动化消融调度脚本 `experiments/run_ablation.py`，一键运行 5 组实验并打点：
- **Exp 1**：Baseline (LightTrack 原版)
- **Exp 2**：Baseline + Rep-Backbone (重参数化骨干)
- **Exp 3**：Baseline + Rep-Backbone + MFFN (浅层多尺度细节融合)
- **Exp 4**：Baseline + Rep-Backbone + MFFN + DAC-Head (动态相关匹配头)
- **Exp 5**：Full Model (Rep-LightTrack，完整集成模型)

脚本将自动生成硕士论文必备的消融数据大表 `results/tables/ablation_matrix.md` 与雷达对比图。

---

## 阶段六：纯静态 ONNX 解耦导出与上位机 C++ 交付

自研网络训练收敛后，执行解耦导出脚本：
```powershell
python experiments/export_onnx.py --weights checkpoints/trained/lightuav_best.pth --output_dir outputs/onnx
```

### 6.1 导出标准与验证
1. **重参数化结构折叠**：自动将训练态多分支等价融合为单路平铺的 $3\\times 3$ 卷积；
2. **纯静态双分支**：
   - `outputs/onnx/sot_template.onnx` (`1x3x127x127`)
   - `outputs/onnx/sot_search.onnx` (`1x3x255x255`)
3. **交付物回归**：
   将导出的两份 `.onnx` 拷贝至 [D:\\PyProjects\\RGB_PTZ_Integrated\\models\\](file:///d:/PyProjects/RGB_PTZ_Integrated/models/)，即可无缝启动上位机 DirectML 60 FPS 实机闭环伺服！
"""

def main():
    TARGET_DIR.mkdir(parents=True, exist_ok=True)
    
    readme_path = TARGET_DIR / "README.md"
    readme_path.write_text(README_CONTENT.strip(), encoding="utf-8")
    print(f"[OK] Successfully created: {readme_path}")
    
    workflow_path = TARGET_DIR / "WORKFLOW.md"
    workflow_path.write_text(WORKFLOW_CONTENT.strip(), encoding="utf-8")
    print(f"[OK] Successfully created: {workflow_path}")

if __name__ == "__main__":
    main()
