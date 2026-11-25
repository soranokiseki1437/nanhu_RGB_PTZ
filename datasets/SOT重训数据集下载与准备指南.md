# SOT 重训与抗虚警评估数据集下载与准备指南

> **归属工程**：[RGB_PTZ_Integrated](file:///d:/PyProjects/RGB_PTZ_Integrated)  
> **覆盖数据集**：`Drone-vs-Bird`（地对空飞鸟干扰与抗虚警校验集）、`LaSOT`（大规模长时单目标跟踪通用基准）

---

## 一、 Drone-vs-Bird Detection Challenge 数据集

### 1.1 数据集规格与用途
* **学术出处**：WOSDETC *Drone-vs-Bird Detection Grand Challenge*（IEEE AVSS / ICASSP / IJCNN 挑战赛基准）
* **数据体量与特征**：包含 **77 段**训练视频序列（平均每段约 **1,384 帧**），分辨率覆盖标清至 **4K（3840×2160）**；画面包含多型号旋翼/固定翼无人机、远距离弱小目标（低对比度、强逆光、复杂地表与天空背景）及大量飞鸟群干扰；仅对无人机目标进行边界框标注（飞鸟作为纯负样本干扰存在）。
* **工程定位**：用于验证条件式二级抗虚警校验模块（`VaL_CNN`）及检测重捕链路在复杂飞鸟干扰下的虚警抑制能力（FP/frame）。

### 1.2 获取途径与官方资源
* **官方 GitHub 仓库（标注文件、评测工具与目录规范）**：  
  `https://github.com/wosdetc/challenge`
* **原始视频序列申请流程（受科研使用协议 DUA 保护，需邮件申请）**：
  1. 从官方仓库获取并填写签署 **Data Usage Agreement（DUA，数据使用协议）**；
  2. 将签字后的 DUA 发送至官方竞赛组委会邮箱：**`wosdetc@googlegroups.com`**；
  3. 审核通过后，组委会将通过邮件回复训练与测试视频序列的下载链接。
* **辅助标注与格式转换开源工具参考**：
  * 辅助标签规范与帧转换说明：`https://raysonlaroca.github.io/supp/drone-vs-bird/`
  * 竞赛数据加载参考仓库：`https://github.com/yjwong1999/IJCNN2025-DvB`、`https://github.com/i-sahajmistry/BvD-ICASSP-2023`

### 1.3 标注格式与本地目录组织
* **官方逐帧文本标注格式**（每个视频对应一份 `.txt` 文件，仅记录存在无人机目标的帧）：
  ```text
  <framenum> <num_objs> <top_x_1> <top_y_1> <width_1> <height_1> [<top_x_2> <top_y_2> <width_2> <height_2> ...]
  ```
* **本地建议目录结构**：
  ```text
  d:\PyProjects\RGB_PTZ_Integrated\datasets\Drone-vs-Bird\
  ├── videos\                # 原始视频序列文件
  ├── annotations\           # 逐序列官方 txt 标注文件
  └── frames_yolo\           # 按需抽帧转换后的图像与 YOLO 格式标签（可选）
  ```
* **完整性校验**：核对 `annotations/` 与 `videos/` 中 **77 段**训练序列一一对应，确保无空文件或损坏视频流。

---

## 二、 LaSOT（Large-scale Single Object Tracking）数据集

### 2.1 数据集规格与用途
* **学术出处**：Heng Fan 等人提出的大规模长时单目标跟踪基准（CVPR 2019 / IJCV 2021）
* **数据体量与版本划分**：
  * **会议版（LaSOT，70 类 / 1,400 段视频）**：体积约 **227 GB**，覆盖长时连续跟踪、全遮挡重现、视野出界与显著尺度变化；
  * **期刊扩展子集（LaSOT-ext，新增 15 类 / 150 段视频）**：体积约 **59 GB**，包含高难度相似干扰物与小尺度运动目标。
* **工程定位**：用于自研轻量 SOT 网络（`Rep-LightTrack`）的长时表观表征、尺度回归与抗遮挡能力预训练及长时跟踪评测。

### 2.2 下载途径与命令行拉取
* **官方项目主页与评测工具**：
  * 官方网站：`https://vision.cs.stonybrook.edu/~lasot/`
  * 镜像汇总页（含 **Google Drive / OneDrive / 百度网盘** 全量及分类别分卷下载链接）：`https://hengfan2010.github.io/LaSOT/`
  * 官方评测工具库：`https://github.com/HengLan/LaSOT_Evaluation_Toolkit`
* **Hugging Face 开源镜像直拉（支持断点续传）**：
  ```python
  from huggingface_hub import snapshot_download

  # 1. 拉取 LaSOT 主集（70 类 / 1,400 序列，约 227 GB）
  snapshot_download(
      repo_id="l-lt/LaSOT",
      repo_type="dataset",
      local_dir=r"D:\Datasets_SOT\LaSOT",
  )

  # 2. 按需拉取 LaSOT-ext 扩展集（15 类 / 150 序列，约 59 GB）
  snapshot_download(
      repo_id="l-lt/LaSOT-ext",
      repo_type="dataset",
      local_dir=r"D:\Datasets_SOT\LaSOT-ext",
  )
  ```

### 2.3 本地目录组织与完整性校验
* **本地建议目录结构（预留 $\ge 320\text{ GB}$ SSD 空间）**：
  ```text
  D:\Datasets_SOT\
  ├── LaSOT\
  │   ├── airplane\
  │   │   ├── airplane-1\
  │   │   │   ├── img\               # 连续视频帧 (*.jpg)
  │   │   │   ├── groundtruth.txt    # 逐帧 [x, y, w, h] 标注
  │   │   │   ├── full_occlusion.txt # 全遮挡属性标志
  │   │   │   ├── out_of_view.txt    # 出视野属性标志
  │   │   │   └── nlp.txt            # 文本描述
  │   │   └── ...
  │   └── ... (共 70 个类别目录)
  └── LaSOT-ext\                     # 扩展集 15 个类别目录（可选）
  ```
* **完整性校验**：
  1. 确认 `LaSOT\` 根目录下共包含 **70** 个类别文件夹，累计 **1,400** 个子序列目录；
  2. 每个序列目录下均包含 `img\` 帧目录与行数一致的 `groundtruth.txt`、`full_occlusion.txt`、`out_of_view.txt`；
  3. 若拉取 `LaSOT-ext\`，确认包含 **15** 个类别目录、累计 **150** 个子序列。
