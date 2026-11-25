# 可见光光电云台反无人机目标检测与跟踪标准数据集库

> **归属工程**：[RGB_PTZ_Integrated](file:///d:/PyProjects/RGB_PTZ_Integrated)  
> **任务核心定义**：**地面/固定站/车载光电云台（RGB PTZ）对空视轴跟踪——拍摄并锁定的目标本身是空中飞行的无人机（Target is UAV）**  
> **数据场景规范**：本工程数据集以地对空（Ground-to-Air）长焦可见光空天场景为主，服务于无人机目标检测与单目标跟踪算法。

---

## 一、 数据集架构与索引

| 数据集名称 | 学术出处 / 机构 | 存储位置与状态 | 模态与规格 | 任务映射 |
| :--- | :--- | :---: | :--- | :--- |
| **DUT Anti-UAV** | IEEE T-ITS 2022<br>大连理工大学 | 🟢 **本地就绪**<br>(`datasets/Anti-UAV/`) | • 纯可见光（RGB）<br>• 检测集：10,000 张图像 (VOC XML)<br>• 跟踪集：20 段飞行序列 (`video01`~`video20`, 24,784 帧) | • **目标检测**：5,200 Train / 2,600 Val 用于 YOLO 微调<br>• **单目标跟踪**：20 组真机序列用于 SOT 验证盲测 |
| **CASIA Anti-UAV**<br>(Anti-UAV300) | CVPR / IEEE TPAMI 2021<br>中科院自动化所 / 清华大学 | 🟢 **本地就绪**<br>(`datasets/CASIA-Anti-UAV300/`) | • 纯可见光（RGB）<br>• 训练集 160 序列 (`train/`, 149,688 文件) + 验证集 67 序列 (`val/`, 62,066 文件)<br>• 含 `got10k_format/` 与 `fear_annotations/` 标注 (总计 23.27 GB) | • **SOT 领域微调与消融**：地空全量真彩长航迹用于跟踪模型微调与泛化评测 |
| **Anti-UAV-Merged** | 本工程融合构建 (`scripts/build_merged_uav_dataset.py`) | 🟢 **本地就绪**<br>(`datasets/Anti-UAV-Merged/`) | • 纯可见光（RGB）<br>• 训练集 11,772 张 (DUT 5,200 + CASIA 关键帧 6,434 pos / 138 neg)<br>• 验证集 5,213 张 (DUT 2,600 + CASIA 关键帧 2,562 pos / 51 neg) | • **面向重捕的检测重训**：提升复杂机动召回并压低空视野负样本误报 |
| **Drone-vs-Bird / LaSOT** | IEEE AVSS/ICASSP/IJCNN；CVPR 2019 / IJCV 2021 | 🟡 **外部准备**<br>(待按指南拉取) | • `Drone-vs-Bird`：77 段含飞鸟干扰的无人机视频<br>• `LaSOT`：70 类 1,400 段长时视频（+ 扩展 150 段） | • **抗虚警评测 (`VaL_CNN`) 与长时 SOT 预训练/评测** |

---

## 二、 本地物理目录组织规范

```text
datasets/
├── README.md               # 本文档
├── anti_uav.yaml           # DUT Anti-UAV 检测配置
├── anti_uav_merged.yaml    # 融合检测数据集配置 (DUT + CASIA 关键帧与负样本)
├── SOT重训数据集下载与准备指南.md  # Drone-vs-Bird 与 LaSOT 获取及准备指南
├── Anti-UAV/               # DUT Anti-UAV 纯可见光检测与跟踪基准库
│   ├── detection/          # 无人机静态目标检测集 (XML/YOLO 标注，目标标签为 <name>UAV</name>)
│   │   ├── train/          # 训练集图像与标注 (5,200 张)
│   │   └── val/            # 验证集 (2,600 张高清图像 + 2,600 份标准标注)
│   └── tracking/           # 无人机单目标连续跟踪集 (SOT，20 段序列，24,784 帧)
│       ├── Anti-UAV-Tracking-V0GT/ # 20 组连续无人机飞行序列官方逐帧 Ground Truth (video01_gt.txt ~ video20_gt.txt)
│       └── Anti-UAV-Tracking-V0/   # 连续跟踪帧序列 (video01 ~ video20 图像帧，共 3.63 GB)
├── Anti-UAV-Merged/        # DUT + CASIA 确定性关键帧融合检测数据集 (16,985 张)
└── CASIA-Anti-UAV300/      # CASIA Anti-UAV300 可见光长航迹跟踪库 (共 23.27 GB，213,119 文件)
    ├── train/              # 160 段训练序列 RGB 帧与 visible.json 标注
    ├── val/                # 67 段验证序列 RGB 帧与 visible.json 标注
    ├── got10k_format/      # GOT-10k 标准格式文本标注 (train 160 + val 67 序列)
    └── fear_annotations/   # FEAR-XS 专有单文件 CSV 标注 (train.csv)
```

---

## 三、 数据集获取与维护机制

1. **本地 DUT Anti-UAV 数据源**：
   * 来源：Hugging Face 官方镜像 `aegean-ai/dut-anti-uav`；
   * 下载脚本：[scripts/_archive/download_anti_uav.py](file:///d:/PyProjects/RGB_PTZ_Integrated/scripts/_archive/download_anti_uav.py)；
   * 状态：已完整就位并参与方案 03 与 04 研发。
2. **外部准备数据源（`Drone-vs-Bird` 与 `LaSOT`）**：
   * 详见专项指南文档：[datasets/SOT重训数据集下载与准备指南.md](file:///d:/PyProjects/RGB_PTZ_Integrated/datasets/SOT重训数据集下载与准备指南.md)。


