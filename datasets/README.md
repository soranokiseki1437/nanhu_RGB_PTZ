# 可见光光电云台反无人机目标检测与跟踪标准数据集库

> **归属工程**：[RGB_PTZ_Integrated](file:///d:/PyProjects/RGB_PTZ_Integrated)  
> **任务核心定义**：**地面/固定站/车载光电云台（RGB PTZ）对空视轴跟踪——拍摄并锁定的目标本身是空中飞行的无人机（Target is UAV）**  
> **数据场景规范**：本工程数据集以地对空（Ground-to-Air）长焦与红外空天场景为主，服务于无人机目标检测与单目标跟踪算法。

---

## 一、 数据集架构与标准术语规范

为消除学术出处与工程映射混淆，工程严格区分并规范以下三大核心数据集的术语定义与物理边界：

| 数据集规范全称 | 学术出处 / 机构 | 物理存储状态 | 数据模态与规格 | 本工程任务映射 |
| :--- | :--- | :---: | :--- | :--- |
| **DUT Anti-UAV** | IEEE T-ITS 2022<br>大连理工大学（王栋团队） | 🟢 **本地全量就绪**<br>(`datasets/Anti-UAV/`) | • **纯可见光（RGB Only）**<br>• 检测集：10,000 张静态地空图像 (VOC XML)<br>• 跟踪集：20 段连续飞行序列 (`video01`~`video20`, 24,784 帧) | • **方案 03 检测主力**：5,200 Train / 2,600 Val 用于 YOLO 多模型微调<br>• **方案 04 跟踪盲测**：20 组真机序列作为 SOT 权威验证集 |
| **CASIA Anti-UAV**<br>(Anti-UAV300) | CVPR / IEEE TPAMI 2021<br>中科院自动化所 / 清华大学 | 🟡 **台式机外部准备**<br>(网盘/镜像源) | • **双模态对齐 (RGB + TIR)**<br>• 100~300 段地对空全量长视频航迹<br>• 命名采用时间戳，标注为 JSON 格式 | • **方案 04B 阶段三领域微调**：提供 100 段全量地空真彩航迹，防止网络仅拟合 20 段短序列发生尺度过拟合 |
| **GOT-10k** | IEEE TPAMI 2021<br>中科院自动化所（CASIA） | 🟡 **台式机外部准备**<br>(CASIA 官网/网盘) | • 通用跨类别单目标跟踪黄金基准大库<br>• 9,335 段连续视频，140 万精标注帧 | • **方案 04B 阶段二尺度预训练**：训练 MFFN 细节融合与 DAC-Head 尺度几何标尺 |
| **Real_CCD** | 外场光电云台真机实测录像 | 🟢 **本地就绪**<br>(`datasets/Real_CCD/`) | • 真实光电云台 1080P RGB 长焦变焦对空实拍 | • **真机全链路闭环实测基准** |
| **IRSTD_22** | Science Data Bank (Hui et al.)<br>国防科大 / 空工大 | 🟢 **本地就绪**<br>(`datasets/IRSTD_22/`) | • 22 个连续序列、16,000+ 帧低空/高空弱小航迹 (6×6 ~ 24×24 px) | • **算法底层跨模态数值对齐验证** |

---

## 二、 本地物理目录组织规范

```text
datasets/
├── README.md               # 本规范文档（术语与数据集事实源）
├── Anti-UAV/               # 【DUT Anti-UAV】大连理工纯可见光检测与跟踪基准库
│   ├── detection/          # 无人机静态目标检测集 (XML/YOLO 标注，目标标签为 <name>UAV</name>)
│   │   ├── train/          # 训练集图像与标注 (5,200 张)
│   │   └── val/            # 验证集 (2,600 张高清图像 + 2,600 份标准标注)
│   └── tracking/           # 无人机单目标连续跟踪集 (SOT，20 段序列，24,784 帧)
│       ├── Anti-UAV-Tracking-V0GT/ # 20 组连续无人机飞行序列官方逐帧 Ground Truth (video01_gt.txt ~ video20_gt.txt)
│       └── Anti-UAV-Tracking-V0/   # 连续跟踪帧序列 (video01 ~ video20 图像帧，共 3.63 GB)
├── Real_CCD/               # 云台实机 1080P 外场彩色视频 (真实无人机长焦跟踪)
│   ├── CCD_2026_05_27_16_31_21_017.mp4
│   └── CCD_2026_05_27_16_32_29_299.mp4
└── IRSTD_22/               # 地空背景弱小飞行器基准 (01~22，16,000+ 帧)
    └── (根目录 testdata 建立系统级 Junction 软链接映射至此，兼容既有测试工具)
```

---

## 三、 数据集获取与维护机制

1. **本地 DUT Anti-UAV 数据源**：
   * 来源：Hugging Face 官方镜像 `aegean-ai/dut-anti-uav`；
   * 下载脚本：[scripts/download_anti_uav.py](file:///d:/PyProjects/RGB_PTZ_Integrated/scripts/download_anti_uav.py)；
   * 状态：已完整就位并参与方案 03 与 04 研发。
2. **外部准备数据源（用于方案 04B 台式机重训）**：
   * 详见专项指南文档：[待执行方案/SOT重训数据集下载与准备指南.md](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/SOT重训数据集下载与准备指南.md)。


