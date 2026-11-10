# 可见光光电云台反无人机目标检测与跟踪标准数据集库

> **归属工程**：[RGB_PTZ_Integrated](file:///d:/PyProjects/RGB_PTZ_Integrated)  
> **任务核心定义**：**地面/固定站/车载光电云台（RGB PTZ）对空视轴跟踪——拍摄并锁定的目标本身是空中飞行的无人机（Target is UAV）**  
> **数据场景规范**：本工程数据集以地对空（Ground-to-Air）长焦与红外空天场景为主，服务于无人机目标检测与跟踪算法。

---

## 一、 数据集架构与索引

| 数据集名称 | 学术出处 / 顶刊顶会 | 目标类型 | 拍摄视角 / 场景 | 在本工程中的任务映射 |
| :--- | :--- | :---: | :--- | :--- |
| **Anti-UAV**<br>(DUT & CASIA) | • IEEE TPAMI 2021<br>• CVPR / ICCV Workshop<br>• 大连理工 / 中科院自动化所 | **空中飞行的无人机**<br>(DJI各类多旋翼、固定翼) | • 地面/转台仰拍与对空长焦连续跟踪<br>• 蓝天、复杂云层、地平线、林木边缘穿行 | **方案 03 / 方案 04 核心基准**：<br>1. DET: 包含 2,600+ 高清无人机检测标注<br>2. SOT: 20+ 连续空域飞行动力学跟踪序列与逐帧 GT |
| **Real_CCD** | 外场光电云台真机实测录像 | **真实飞行无人机** | • 真实光电云台 1080P RGB 长焦变焦对空实拍 | **真机全链路闭环实测基准** |
| **IRSTD_22**<br>(原 testdata) | Science Data Bank (ScienceDB)<br>国防科大 / 空工大 (Hui et al.) | **地空弱小飞行器/无人机**<br>(6×6 ~ 24×24 px) | • 22 个连续序列、16,000+ 帧低空/高空弱小航迹 | **算法底层跨模态数值对齐验证** |

---

## 二、 目录组织规范

```text
datasets/
├── README.md               # 本规范文档
├── Anti-UAV/               # 方案 03/04 核心基准：目标为无人机的可见光检测与跟踪库
│   ├── detection/          # 无人机目标检测集 (XML/YOLO 标注，目标标签为 <name>UAV</name>)
│   │   ├── train/          # 训练集图像与标注 (5,200 张)
│   │   └── val/            # 验证集 (2,600 张高清图像 + 2,600 份标准标注)
│   └── tracking/           # 无人机单目标连续跟踪集 (SOT)
│       ├── Anti-UAV-Tracking-V0GT/ # 20 组连续无人机飞行序列官方逐帧 Ground Truth (*_gt.txt)
│       └── Anti-UAV-Tracking-V0/   # 连续跟踪帧序列 (3.63 GB 官方视频帧)
├── Real_CCD/               # 云台实机 1080P 外场彩色视频 (真实无人机长焦跟踪)
│   ├── CCD_2026_05_27_16_31_21_017.mp4
│   └── CCD_2026_05_27_16_32_29_299.mp4
└── IRSTD_22/               # 地空背景弱小飞行器基准 (01~22，16,000+ 帧)
    └── (根目录 testdata 建立系统级 Junction 软链接映射至此，兼容既有测试工具)
```

---

## 三、 Anti-UAV 数据集获取与维护机制

1. **自动拉取脚本**：
   运行 `python scripts/download_anti_uav.py` 从 Hugging Face 镜像源拉取 `DUT Anti-UAV` 并解压就位。
2. **官方多源备用链接**：
   - CASIA 官方 Anti-UAV300 (RGB+IR): 百度网盘 (`https://pan.baidu.com/s/1dJR0VKyLyiXBNB_qfa2ZrA`, 提取码 `sagx`) / Google Drive (`https://drive.google.com/file/d/1NPYaop35ocVTYWHOYQQHn8YHsM9jmLGr/view`)
   - CASIA 官方 Anti-UAV410 (IR): 百度网盘 (`https://pan.baidu.com/s/1PbINXhxc-722NWoO8P2AdQ`, 提取码 `wfds`)
   - 官方 ModelScope 镜像: `https://modelscope.cn/datasets/ly261666/3rd_Anti-UAV/files`

