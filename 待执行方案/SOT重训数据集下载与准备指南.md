# SOT 重训数据集下载与本地组织规范指南

> **归属工程**：[RGB_PTZ_Integrated](file:///d:/PyProjects/RGB_PTZ_Integrated)  
> **面向任务**：自研轻量单目标跟踪网络（LightUAV-SOT）两阶段预训练与地对空领域微调  
> **核心算力支撑**：RTX 4080 Super 台式机（16GB GDDR6X 显存，10,240 CUDA 核心）  
> **目标存储建议**：固态硬盘（SSD）分区，预留 $\ge 120\text{ GB}$ 空间  

---

## 一、 核心数据集下载链接与镜像索引

### 1.1 核心一号库：中科院 GOT-10k（通用单目标跟踪黄金基准大库）
* **学术定位**：单目标跟踪领域权威全视频基准（9,335 段视频，140 万帧手工精标注），用于训练 MFFN 细节融合层、DAC-Head 匹配层与边界框尺度几何标尺；
* **数据体量**：压缩包约 **66 GB**（拆分为多个分卷，解压后约 68 GB）；
* **下载途径**：
  1. **中科院自动化所（CASIA）官方直连（国内极速源，首选推荐）**：
     - 官网主页：[http://got-10k.aitestunion.com/](http://got-10k.aitestunion.com/)
     - 下载专属页：[http://got-10k.aitestunion.com/downloads](http://got-10k.aitestunion.com/downloads)
     - 说明：使用邮箱注册学术账号登录，直接多线程满速下载 `train_data.zip` 分卷；
  2. **NanoTrack 作者官方国内百度网盘镜像**：
     - 数据集链接：`https://pan.baidu.com/s/10crE2uKR182fA93XRB3jyg`（提取码：`5ebm`）
     - 预裁剪与标注辅助包：`https://pan.baidu.com/s/1yzXnRUqjreSeojEQOmluQg`（提取码：`un47`）

---

### 1.2 核心二号库：CASIA Anti-UAV300 官方航迹集（地对空长焦连续视频微调）
* **学术定位**：清华大学与中科院自动化所（CASIA）联合打造的 CVPR 官方反无人机挑战赛基准，提供地对空长焦实拍连续视频序列，用于方案 04B 第三阶段领域微调；
* **数据体量**：全量压缩包约 **30 GB**；
* **模态区分与获取途径（严禁模态混淆）**：
  1. **CASIA 官方 Anti-UAV300（RGB 可见光真彩航迹，必须使用本库）**：
     - **学术出处**：CVPR 2020 / TPAMI 2021《Anti-UAV: A Large-Scale Benchmark for Vision-based UAV Tracking》；
     - **模态规格**：真彩色可见光（RGB 1080p/720p），与项目本地已就绪的 DUT Anti-UAV 20 组真机跟踪验证序列及 RGB PTZ 云台机芯物理域完全一致；
     - **百度网盘链接**：`https://pan.baidu.com/s/1dJR0VKyLyiXBNB_qfa2ZrA`（提取码：`sagx`）；
     - **Google Drive 备用源**：`https://drive.google.com/file/d/1NPYaop35ocVTYWHOYQQHn8YHsM9jmLGr/view`；
  2. **Anti-UAV410 / 3rd Anti-UAV（纯热红外 TIR 基准，红外赛道专属，禁止用于可见光微调）**：
     - **学术出处**：IEEE TPAMI 2023《Anti-UAV410: A Thermal Infrared Benchmark for Tracking Drones in the Wild》与 CVPR 2023 第三届挑战赛；
     - **模态规格**：纯单模态长波热红外（Thermal Infrared，标注全为 `IR_label.json`），仅适用于热成像硬件与红外跟踪赛道；
     - **下载源（仅作红外赛道存档）**：魔搭社区 `ly261666/3rd_Anti-UAV`、百度网盘（提取码：`wfds`）；
  3. **官方挑战赛主页**：
     - GitHub 主页：[https://anti-uav.github.io/](https://anti-uav.github.io/)

---

### 1.3 辅助尺度增强库：MS-COCO 2017（静态多尺度扩增）
* **学术定位**：提供极端目标长宽比与多尺度形变扩增，防范单一视频序列过拟合；
* **数据体量**：图像包约 **18 GB**，标注包约 **241 MB**；
* **下载途径（清华大学 TUNA 开源镜像站，千兆满速直链）**：
  - **训练图像大包（train2017.zip，18 GB）**：
    [https://mirrors.tuna.tsinghua.edu.cn/coco/zips/train2017.zip](https://mirrors.tuna.tsinghua.edu.cn/coco/zips/train2017.zip)
  - **训练标注包（annotations_trainval2017.zip，241 MB）**：
    [https://mirrors.tuna.tsinghua.edu.cn/coco/annotations/annotations_trainval2017.zip](https://mirrors.tuna.tsinghua.edu.cn/coco/annotations/annotations_trainval2017.zip)

---

## 二、 台式机本地目录组织规范

为保障训练数据加载效率与数据管理规范，建议在 RTX 4080 Super 台式机的固态硬盘（SSD）上建立如下目录层级：

```text
D:\Datasets_SOT\
├── GOT-10k\                        # 中科院 GOT-10k 解压根目录
│   ├── train\                      # 包含 9,335 个视频序列目录 (GOT-10k_Train_000001 ...)
│   │   ├── GOT-10k_Train_000001\
│   │   │   ├── 00000001.jpg
│   │   │   ├── groundtruth.txt
│   │   │   └── ...
│   │   └── ...
│   └── val\                        # 官方验证集目录 (包含 GOT-10k_Val_000001 ...)
│
├── CASIA-Anti-UAV300\             # CASIA 官方 Anti-UAV300 全量地对空连续长视频航迹
│   ├── train\                      # 官方 100 段全量连续长视频序列 (visible.mp4 + visible.json)
│   └── ...
│
└── DUT-Anti-UAV-Val\               # 本地已就绪的 DUT Anti-UAV 20 组真机跟踪验证序列
    ├── Anti-UAV-Tracking-V0\       # video01 ~ video20 图像帧 (24,784 帧)
    └── Anti-UAV-Tracking-V0GT\     # video01_gt.txt ~ video20_gt.txt 逐帧真值标注
│
└── COCO2017\                       # MS-COCO 2017 静态数据集
    ├── train2017\                  # 11.8 万张图片
    └── annotations\                # instances_train2017.json
```

---

## 三、 数据完整性与校验

1. **GOT-10k 校验**：解压完成后，确认 `train/` 目录下子文件夹数量为 `9335`；
2. **COCO 2017 校验**：解压完成后，确认 `train2017/` 目录下图片数量为 `118287`；
3. **Anti-UAV 校验**：确认包含连续视频帧目录及每段视频对应的 `*_gt.txt` 或 `groundtruth.txt` 标注文件。

