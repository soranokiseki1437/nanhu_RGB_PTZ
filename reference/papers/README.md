# 反无人机长焦可见光视觉感知核心前沿文献库

> **目录定位**：本目录归属于 [RGB_PTZ_Integrated](file:///d:/PyProjects/RGB_PTZ_Integrated) 工程的理论基石与参考文献库，专用于支撑硕士学位论文各章节的理论引证、基线复现与横向对比。  
> **分类体系**：按照传感器模态、算法范式与工程控制体系划分为五个专项子目录。

---

## 一、 子文件夹分类体系与论文索引

```
reference/papers/
├── 01_综述与数据基准_Surveys_Benchmarks/
│   ├── DUT_AntiUAV_TITS2022.pdf       # 大连理工大学 IEEE T-ITS 2022 原生基石论文
│   ├── LRDDv3_ICRA2026.pdf            # IEEE ICRA 2026 4K 可见光 10万大图基准
│   └── SkyEV_2026.pdf                 # 2026 极限高动态相机自身运动基准
│
├── 02_经典与重型跟踪基准_SOT_Baselines/
│   ├── SiamRPNpp_CVPR2019.pdf         # CVPR 2019 商汤深度孪生网络开山之作 (ResNet50)
│   ├── DaSiamRPN_ECCV2018.pdf         # ECCV 2018 干扰感知孪生网络
│   └── OSTrack_ECCV2022.pdf           # ECCV 2022 单流 Vision Transformer 现代基准
│
├── 03_轻量结构与重参数化SOT_Lightweight_Rep/
│   ├── LightTrack_ICCV2021.pdf        # ICCV 2021 Oral 微软轻量 NAS 跟踪网络 (核心基准)
│   ├── NanoTrack_ICCV2021.pdf         # ICCV 2021 超轻移动端 DW-CNN (0.4M 极速基准)
│   ├── RepViT_CVPR2024.pdf            # CVPR 2024 结构重参数化移动端卷积骨干理论
│   ├── FEAR_ECCV2022.pdf              # ECCV 2022 动态双模板机制
│   ├── ECA-Net_CVPR2020.pdf           # CVPR 2020 非降维高效通道注意力理论
│   └── KeepTrack_ICCV2021.pdf         # ICCV 2021 显式候选目标与干扰解耦
│
├── 04_反无人机专属模型与小目标损失_AntiUAV_Perception/
│   ├── Dist-Tracker_CVPRW2025.pdf     # CVPR 2025 Workshop 协方差 Wasserstein 距离与 FLIT
│   └── UAV-DETR_2026.pdf              # arXiv 2026 小波卷积与 Inner-CIoU+NWD 混合损失
│
└── 05_光电云台机电伺服与协同闭环_PTZ_Servo_AMFT/
    ├── AMFT_IMCEC2025.pdf             # 自适应多特征融合重检测协同触发机制
    └── HCLT_Electronics2026.pdf       # RK3588 卡尔曼状态观测器闭环跟踪实操
```

---

## 二、 开放获取（Open Access）直链速查表

针对受反爬网关（Akamai / Cloudflare）防护的期刊出版物，提供官方开放获取直链，支持浏览器单点下载：

| 论文名称 | 发表出处与年份 | 官方开放获取链接 | 建议归档子目录 |
| :--- | :--- | :--- | :--- |
| **地对空纯视觉反无人机综述** | MDPI *Drones*, 2025 | [MDPI 官方直接下载页面](https://www.mdpi.com/2504-446X/9/1/58) | `01_综述与数据基准_Surveys_Benchmarks/` |
| **RL-PTZ-Track** | MDPI *Drones*, 2024 | [MDPI 官方直接下载页面](https://www.mdpi.com/2504-446X/8/6/235) | `05_光电云台机电伺服与协同闭环_PTZ_Servo_AMFT/` |
| **Ground-Visual-Edge** | MDPI *Sensors*, 2026 | [MDPI 官方直接下载页面](https://www.mdpi.com/1424-8220/26/1/205) | `04_反无人机专属模型与小目标损失_AntiUAV_Perception/` |
| **HEDD (Edge-Aware锐化)** | Taylor & Francis, 2026 | [T&F 官方文章主页](https://doi.org/10.1080/24751839.2025.2616897) | `04_反无人机专属模型与小目标损失_AntiUAV_Perception/` |
| **PTZ-Latency-Study** | Springer *MVA*, 2025 | [Springer Link 页面](https://doi.org/10.1007/s00138-024-01625-y) | `05_光电云台机电伺服与协同闭环_PTZ_Servo_AMFT/` |
