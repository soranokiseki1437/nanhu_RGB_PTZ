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

### 1.2 核心二号库：Anti-UAV 官方全量航迹集（地对空长焦连续视频微调）
* **学术定位**：清华大学与中科院自动化所联合打造的 CVPR 官方反无人机挑战赛基准，地对空长焦实拍连续视频序列，用于第三层特定域微调；
* **数据体量**：压缩包约 **15 ~ 30 GB**；
* **下载途径**：
  1. **阿里 ModelScope（魔搭社区）官方国内极速镜像（免翻墙满速，首选推荐）**：
     - 社区直达链接：[https://modelscope.cn/datasets/ly261666/3rd_Anti-UAV/files](https://modelscope.cn/datasets/ly261666/3rd_Anti-UAV/files)
     - 说明：提供第 3 届 Anti-UAV 挑战赛全量数据包，包含完整的 100+ 段连续长视频帧；
  2. **CASIA 官方 Anti-UAV300（RGB+IR 双光 300 段全量航迹，约 30 GB）**：
     - 百度网盘链接：`https://pan.baidu.com/s/1dJR0VKyLyiXBNB_qfa2ZrA`（提取码：`sagx`）
     - Google Drive 备用源：`https://drive.google.com/file/d/1NPYaop35ocVTYWHOYQQHn8YHsM9jmLGr/view`
  3. **CASIA 官方 Anti-UAV410（纯红外 410 段航迹，备用）**：
     - 百度网盘链接：`https://pan.baidu.com/s/1PbINXhxc-722NWoO8P2AdQ`（提取码：`wfds`）
  4. **官方挑战赛主页**：
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
├── Anti-UAV-Full\                  # 官方全量地对空长焦连续视频航迹
│   ├── train\                      # 官方 100 段全量连续训练长视频序列
│   └── val\                        # 20 组标准验证序列
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

