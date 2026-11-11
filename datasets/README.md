# 可见光光电云台反无人机目标检测与跟踪标准数据集库

> **归属工程**：[RGB_PTZ_Integrated](file:///d:/PyProjects/RGB_PTZ_Integrated)  
> **任务核心定义**：**地面/固定站/车载光电云台（RGB PTZ）对空视轴跟踪——拍摄并锁定的目标本身是空中飞行的无人机（Target is UAV）**  
> **数据场景规范**：本工程数据集以地对空（Ground-to-Air）长焦可见光空天场景为主，服务于无人机目标检测与单目标跟踪算法。

---

## 一、 数据集架构与索引

| 数据集名称 | 学术出处 / 机构 | 存储位置与状态 | 模态与规格 | 任务映射 |
| :--- | :--- | :---: | :--- | :--- |
| **DUT Anti-UAV** | IEEE T-ITS 2022<br>大连理工大学 | 🟢 **本地就绪**<br>(`datasets/Anti-UAV/`) | • 纯可见光（RGB）<br>• 检测集：10,000 张图像 (VOC XML)<br>• 跟踪集：20 段飞行序列 (`video01`~`video20`, 24,784 帧) | • **目标检测**：5,200 Train / 2,600 Val 用于 YOLO 微调<br>• **单目标跟踪**：20 组真机序列用于 SOT 验证盲测 |
| **CASIA Anti-UAV**<br>(Anti-UAV300) | CVPR / IEEE TPAMI 2021<br>中科院自动化所 / 清华大学 | 🟡 **外部准备**<br>(官方网盘/镜像) | • RGB 连续视频<br>• 100~300 段地对空长航迹序列 | • **SOT 领域微调**：100 段地空全量真彩航迹用于天空背景微调 |
| **GOT-10k** | IEEE TPAMI 2021<br>中科院自动化所 | 🟡 **外部准备**<br>(CASIA 官网) | • 通用单目标跟踪基准库<br>• 9,335 段视频，140 万精标注帧 | • **SOT 尺度预训练**：预训练细节融合与边界框尺度几何特征 |

---

## 二、 本地物理目录组织规范

```text
datasets/
├── README.md               # 本文档
└── Anti-UAV/               # DUT Anti-UAV 纯可见光检测与跟踪基准库
    ├── detection/          # 无人机静态目标检测集 (XML/YOLO 标注，目标标签为 <name>UAV</name>)
    │   ├── train/          # 训练集图像与标注 (5,200 张)
    │   └── val/            # 验证集 (2,600 张高清图像 + 2,600 份标准标注)
    └── tracking/           # 无人机单目标连续跟踪集 (SOT，20 段序列，24,784 帧)
        ├── Anti-UAV-Tracking-V0GT/ # 20 组连续无人机飞行序列官方逐帧 Ground Truth (video01_gt.txt ~ video20_gt.txt)
        └── Anti-UAV-Tracking-V0/   # 连续跟踪帧序列 (video01 ~ video20 图像帧，共 3.63 GB)
```

---

## 三、 数据集获取与维护机制

1. **本地 DUT Anti-UAV 数据源**：
   * 来源：Hugging Face 官方镜像 `aegean-ai/dut-anti-uav`；
   * 下载脚本：[scripts/download_anti_uav.py](file:///d:/PyProjects/RGB_PTZ_Integrated/scripts/download_anti_uav.py)；
   * 状态：已完整就位并参与方案 03 与 04 研发。
2. **外部准备数据源（用于方案 04B 台式机重训）**：
   * 详见专项指南文档：[待执行方案/SOT重训数据集下载与准备指南.md](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/SOT重训数据集下载与准备指南.md)。


