# SOT 重训数据集下载与本地组织规范指南

> **归属工程**：[RGB_PTZ_Integrated](file:///d:/PyProjects/RGB_PTZ_Integrated)  
> **面向任务**：自研轻量单目标跟踪网络（LightUAV-SOT）两阶段预训练与地对空领域微调  
> **核心算力支撑**：RTX 4080 Super 台式机（16GB GDDR6X 显存，10,240 CUDA 核心）  
> **目标存储建议**：固态硬盘（SSD）分区，预留 $\ge 120\text{ GB}$ 空间  

---

## 一、 核心数据集下载链接与镜像索引

### 1.1 核心一号库：中科院 GOT-10k（通用单目标跟踪基准数据集）
* **学术定位**：单目标跟踪领域权威全视频基准（9,335 段视频，140 万帧手工标注），用于训练 MFFN 细节融合层、DAC-Head 匹配层与边界框尺度几何标尺；
* **数据体量**：压缩包约 **66 GB**（拆分为多个分卷，解压后约 68 GB）；
* **下载途径**：
  1. **中科院自动化所（CASIA）官方直连**：
     - 官网主页：[http://got-10k.aitestunion.com/](http://got-10k.aitestunion.com/)
     - 下载专属页：[http://got-10k.aitestunion.com/downloads](http://got-10k.aitestunion.com/downloads)
     - 说明：使用邮箱注册学术账号登录，下载 `train_data.zip` 分卷；
  2. **NanoTrack 作者官方国内百度网盘镜像**：
     - 数据集链接：`https://pan.baidu.com/s/10crE2uKR182fA93XRB3jyg`（提取码：`5ebm`）
     - 预裁剪与标注辅助包：`https://pan.baidu.com/s/1yzXnRUqjreSeojEQOmluQg`（提取码：`un47`）

---

### 1.2 核心二号库：CASIA Anti-UAV300（地对空长焦连续视频微调）
* **定位**：CVPR / IEEE TPAMI / IEEE T-MM 反无人机基准连续视频序列，用于方案 06-A 阶段三地对空领域微调；
* **数据体量**：压缩包约 30 GB（300 对双模态序列，真彩色可见光序列 RGB 1080p/720p + 红外序列）；
* **下载途径**：
  1. **中科院自动化所（CASIA / UCAS-VG）官方全量百度网盘**：
     - 链接：`https://pan.baidu.com/s/1A1-ynecLWfkEetRsPnh6rw`（提取码：`u4ca`）
     - 说明：包含全量视频序列与逐帧 JSON 边界框真值标注；
  2. **Kaggle 全量开源镜像（海外/高校高速通道）**：
     - 主页：`https://www.kaggle.com/datasets/mahdimoalla/anti-uav-dataset-300`
     - 说明：包含 300 段完整序列，支持直接下载或 `kaggle datasets download`；
  3. **官方 Google Drive 全量大包**：
     - 链接：`https://drive.google.com/file/d/1NPYaop35ocVTYWHOYQQHn8YHsM9jmLGr/view?usp=sharing`
  4. **挑战赛打榜子集（用户此前截图的 test-dev）**：
     - 链接：`https://pan.baidu.com/s/1dJR0VKyLyiXBNB_qfa2ZrA`（提取码：`sagx`，仅包含 `test-dev.tar` 1.97 GB，无训练真值，不用于训练）。

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
3. **CASIA Anti-UAV300 校验**：确认解压后包含对应的视频与标注文件。

---

## 四、 前沿可选扩展数据源规范（LRDDv3 4K可见光子集）

* **数据集名称**：LRDDv3 (*High-Resolution Long-Range Drone Detection Dataset*, IEEE ICRA 2026 / arXiv:2605.25942)
* **数据集定位**：可选的超远距离弱小目标泛化与极限测距扩展测试集（按需下载验证，不阻塞主航道）
* **官方开源地址**：`https://github.com/iMAPLE-Lab/LRDD`
* **建议本地存放路径**：`D:\Datasets_SOT\LRDDv3\`
* **使用准则**：仅抽取其 102,532 张 4K 分辨率纯可见光（RGB）静态图像子集，用于超远距离微小尺度（$\le 50\text{ px}$）极限召回与泛化验证。

