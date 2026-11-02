# 可见光光电云台反无人机目标检测与跟踪标准数据集库

> **归属工程**：[RGB_PTZ_Integrated](file:///d:/PyProjects/RGB_PTZ_Integrated)  
> **任务核心定义**：**地面/固定站/车载光电云台（RGB PTZ）对空视轴跟踪——拍摄并锁定的目标本身是空中飞行的无人机（Target is UAV）**  
> **重要认知澄清**：严格区分“**目标为无人机（Anti-UAV）**”与“**无人机机载俯拍（UAV-view）**”的本质差异，本系统核心算法指标以 Anti-UAV 专属基准为准。

---

## 一、 数据集架构与学术权威性索引

### 1.1 核心一号基准：目标为无人机（Anti-UAV 专属空域与地空数据）

| 数据集名称 | 学术出处 / 顶刊顶会 | 目标类型 | 拍摄视角 / 场景 | 在本工程中的任务映射 |
| :--- | :--- | :---: | :--- | :--- |
| **Anti-UAV**<br>(DUT & CASIA) | • IEEE TPAMI 2021<br>• CVPR / ICCV Workshop<br>• 大连理工 / 中科院自动化所 | **空中飞行的无人机**<br>(DJI各类多旋翼、固定翼) | • 地面/转台仰拍与对空长焦连续跟踪<br>• 蓝天、复杂云层、地平线、林木边缘穿行 | **方案 03 / 方案 04 核心黄金基准**：<br>1. DET: 包含 2600+ 高清无人机检测标注<br>2. SOT: 20+ 连续空域飞行动力学跟踪序列与逐帧 GT |
| **Real_CCD** | 外场光电云台真机实测录像 | **真实飞行无人机** | • 真实光电云台 1080P RGB 长焦变焦对空实拍 | **真机全链路闭环实测基准** |
| **IRSTD_22**<br>(原 testdata) | Science Data Bank (ScienceDB)<br>国防科大 / 空工大 (Hui et al.) | **地空弱小飞行器/无人机**<br>(6×6 ~ 24×24 px) | • 22 个连续序列、16,000+ 帧低空/高空弱小航迹 | **算法底层跨模态数值对齐验证** |

### 1.2 辅助泛化基准：机载俯拍地面目标（非无人机目标，仅供参考）

| 数据集名称 | 学术出处 | 真实视角与目标说明 | 适用边界 |
| :--- | :--- | :--- | :--- |
| **UAV123** | ECCV 2016 (KAUST) | **机载俯拍地面目标**：镜头装在低空无人机上，向下俯拍汽车 (`car`)、行人 (`person`)、船只 (`boat`)、建筑物等。 | 仅供通用跟踪器算法在强扰动、快速尺度变化下的泛化性参考，**严禁作为反无人机主指标**。 |
| **VisDrone** | ECCV / TPAMI (天大) | **机载俯拍城市交通**：无人机俯拍地面密集街道的汽车、卡车、行人。 | 仅供 P2 微小目标检测头在密集地表杂波下的召回率对照，**不代表反无人机真实场景**。 |

---

## 二、 目录组织规范

```text
datasets/
├── README.md               # 本规范文档
├── Anti-UAV/               # ★ 方案 03/04 核心基准：目标为无人机的可见光检测与跟踪库
│   ├── detection/          # 无人机目标检测集 (XML/YOLO 标注，目标标签为 <name>UAV</name>)
│   │   ├── train/          # 训练集图像与标注
│   │   └── val/            # 验证集 (2600 张高清图像 + 2600 份标准标注已就绪)
│   └── tracking/           # 无人机单目标连续跟踪集 (SOT)
│       ├── Anti-UAV-Tracking-V0GT/ # 20 组连续无人机飞行序列官方逐帧 Ground Truth (*_gt.txt)
│       └── Anti-UAV-Tracking-V0/   # 连续跟踪帧序列 (3.63 GB 官方全量视频帧)
├── Real_CCD/               # 云台实机 1080P 外场彩色视频 (真实无人机长焦跟踪)
│   ├── CCD_2026_05_27_16_31_21_017.mp4
│   └── CCD_2026_05_27_16_32_29_299.mp4
├── IRSTD_22/               # 原 testdata：地空背景弱小飞行器基准 (01~22，16,000+ 帧)
│   └── (根目录 testdata 建立系统级 Junction 软链接映射至此，完全兼容既有测试工具)
├── VisDrone/               # (辅助) 机载俯视街道交通集 (8,629 张)
└── UAV123/                 # (辅助) 机载俯视地面车辆跟踪集 (123 序列)
```

---

## 三、 Anti-UAV 数据集获取与维护机制

1. **自动拉取脚本**：
   运行 `python scripts/download_anti_uav.py` 即可自动从 Hugging Face 官方镜像源高速拉取 `DUT Anti-UAV` 并解压就位。
2. **官方多源备用链接**：
   - CASIA 官方 Anti-UAV300 (RGB+IR): 百度网盘 (`https://pan.baidu.com/s/1dJR0VKyLyiXBNB_qfa2ZrA`, 提取码 `sagx`) / Google Drive (`https://drive.google.com/file/d/1NPYaop35ocVTYWHOYQQHn8YHsM9jmLGr/view`)
   - CASIA 官方 Anti-UAV410 (IR): 百度网盘 (`https://pan.baidu.com/s/1PbINXhxc-722NWoO8P2AdQ`, 提取码 `wfds`)
   - 官方 ModelScope 镜像: `https://modelscope.cn/datasets/ly261666/3rd_Anti-UAV/files`
