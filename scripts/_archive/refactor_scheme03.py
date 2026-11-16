# -*- coding: utf-8 -*-
import os

content = '''# 面向目标检测阶段的 YOLO11 可见光反无人机模型规划、数据集体系构建、训练与消融实验方案

> **方案编号**：03  
> **归属工程**：[RGB_PTZ_Integrated](file:///d:/PyProjects/RGB_PTZ_Integrated)  
> **依赖前置**：[02_基于自适应多特征融合(AMFT)的光电云台检测-跟踪协同实操方案.md](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/02_基于自适应多特征融合(AMFT)的光电云台检测-跟踪协同实操方案.md) (已闭环)  
> **理论支撑**：  
> 1. IEEE T-ITS 2022 顶刊论文 *《Vision-Based Anti-UAV Detection and Tracking》* (DOI: 10.1109/TITS.2022.3177627)  
> 2. IEEE Access 2026 顶刊论文 *《DeTrAck: UAV Detection and Tracking Using Neural Networks Ensemble on Surveillance Systems》* (DOI: 10.1109/ACCESS.2025.3649960)  
> 3. IEEE AVSS / ICASSP / IJCNN 全球挑战赛基准 *《The Drone-vs-Bird Detection Grand Challenge》*  
> 4. IEEE RA-L / ICRA 2021 顶会顶刊论文 *《Air-to-Air Visual Detection of Micro-UAVs (Det-Fly)》* (DOI: 10.1109/LRA.2021.3056345)  
> **核心运行环境**：Windows 11 (MinGW 13.1 64-bit / Qt 6.x / NVIDIA GeForce RTX 5070 Laptop GPU / DirectML)  

---

## 一、 系统定位与阶段职责界定（YOLO 在系统中的物理与逻辑边界）

在反无人机光电云台系统中，必须严格界定**目标检测阶段**与**目标跟踪阶段**的物理分工、算法定位与数据集适用范围：

```
                    【阶段一：目标检测阶段 (Target Detection Phase)】
                     - 负责模块：YOLO11 (OnnxDetector) + ByteTrack 广域感知
                     - 核心职责：全图视野搜索 (Search)、多目标候选识别、操作员交互“点击锁定 (Click-to-Lock)”、
                                以及跟踪阶段失锁时的“单次按需重捕获 (Re-detection On-Demand)”
                     - 运行频度：常态 0 FPS 挂起待机；索敌/失锁时单次唤醒运行 1 帧
                     - ★ 数据集铁律：必须且只能使用【目标检测数据集 (Object Detection Dataset)】训练与评估！
                                     严禁使用单目标连续跟踪切片混淆检测训练！
                                          │
                                          ▼ 传递锁定目标精确中心与尺度 (TrackResult)
                    【阶段二：目标跟踪阶段 (Target Tracking Phase)】
                     - 负责模块：DsstCore / LightTrack (纯单目标深度跟踪 SOT)
                     - 核心职责：以 30~60 FPS 高帧率极速输出单一目标脱靶量 (dx, dy)，直接闭环驱动云台电机
                     - 运行频度：高频常驻 (单帧耗时 < 4ms)
                     - 数据集范围：单目标跟踪基准 (SOT Video Sequences)
```

### 1.1 目标检测阶段的四大硬性职责
1. **全图广域搜索（Search）**：在空域监控初始状态下，以 $640\times 640$ 静态分辨率对相机视野进行全图扫描，输出视场内所有疑似飞行器的位置与置信度；
2. **多目标态势感知与候选（Candidate Association）**：在操作员未指定攻击目标前，维护视场内多个目标的航迹与局部特征；
3. **点击锁定前置输入（Click-to-Lock Input）**：操作员在界面单击任意检测框，检测器将该目标坐标交接给跟踪器作为初始化模板；
4. **失锁单次按需重捕（Re-detection On-Demand）**：当跟踪器因树木遮挡、高速机动丢锁后，由 [AMFTEvaluator](file:///d:/PyProjects/RGB_PTZ_Integrated/amftevaluator.cpp) 协同状态机单次异步唤醒检测器进行 1 帧全局扫描，找回目标后下一帧立即回归跟踪阶段。

### 1.2 数据集使用的绝对法则
* **YOLO 只能使用目标检测数据集（Object Detection Dataset）**：
  - 数据集样本必须是全景大图视野，包含完整地空背景（蓝天、积云、建筑物、树丛等）；
  - 标注格式必须为多目标边界框（如 Pascal VOC XML 或 YOLO 归一化 TXT），且包含丰富的负样本与小目标；
  - 严禁将裁剪后的局部目标块、紧凑跟踪切片混入检测训练，否则会导致全图全景下的背景严重虚警！

---

## 二、 可见光反无人机目标检测数据集体系与单/多目标属性剖析

为了支撑 YOLO 模型的系统性训练与严谨的学术消融实验，规划建立覆盖“核心主基准、抗虚警补充、多姿态泛化、真机域闭环”的四大可见光目标检测数据集体系：

```
                    ┌────────────────────────────────────────────────────────┐
                    │          可见光反无人机【目标检测数据集】体系          │
                    └───────────────────────────┬────────────────────────────┘
                                                │
         ┌──────────────────┬───────────────────┴───────────────────┬──────────────────┐
         ▼                  ▼                                       ▼                  ▼
 【检测数据集 1】    【检测数据集 2】                        【检测数据集 3】    【检测数据集 4】
   DUT Anti-UAV       Drone-vs-Bird                           Det-Fly             Real_CCD
(IEEE T-ITS 2022)  (IEEE AVSS/ICASSP)                      (IEEE RA-L 2021)   (外场真机实拍切片)
  • 10,000 张静态      • 专攻“飞鸟 vs 无人机”                   • 13,271 张 4K      • 1,000 张真机切片
  • 80.9% 弱小目标    • 评估低置信度抗虚警                     • 多姿态多光照      • 实机域泛化评测
  • [当前主训主评]    • [计划下载补充]                         • [计划下载补充]    • [内部抽帧构建]
```

### 2.1 四大检测数据集单/多目标属性对比表

关于数据集的单/多目标属性，各基准在真实物理场景与标注定义上存在明确分工：

| 数据集名称 | 单目标 / 多目标定义 | 目标分布全量统计与场景特征 | 负样本与杂波特征 | 在本工程中的角色定位 |
| :--- | :---: | :--- | :--- | :--- |
| **DUT Anti-UAV**<br>(IEEE T-ITS 2022) | **多目标检测格式**<br>(以单目标为主，99.4%) | • **全量扫描本地 7,800 张标注**：<br>  - 1 目标图：7,755 张 (99.4%)<br>  - 2 目标图：27 张<br>  - 3~6 机编队图：18 张<br>  - 纯背景负样本图：3 张<br>• 符合低空入侵绝大多数为单架次黑飞、偶有多机编队的现实工况。 | 包含大面积积云、强逆光、地平线山林杂波，以及纯背景负样本切片。 | **核心主训主评基准**：<br>承担 4 款模型同构微调与正式 2×2 消融横评。 |
| **Drone-vs-Bird**<br>(IEEE AVSS/ICASSP) | **密集多目标**<br>(Multi-Target) | • 画面中常同时出现**数只至数十只飞鸟**（标注为 Bird 负样本），同时伴随 **0 架、1 架或多架** 入侵无人机；<br>• 目标尺度极小（大多在 $10\times 10 \sim 30\times 30$ 像素）。 | 极具挑战性的高空微弱动态杂波，专攻“飞鸟与无人机”混淆。 | **抗虚警消融专用测试集**：<br>用于量化评估 VaL_CNN 对飞鸟误检（FP/frame）的压制能力。 |
| **Det-Fly**<br>(IEEE RA-L 2021) | **多目标检测格式**<br>(以单机为主) | • 机载相机拍摄对向飞行的目标无人机，绝大多数为单目标（1 架），极少数双机交汇；<br>• 覆盖 88 种飞行轨迹与多视角姿态翻滚（4K 分辨率）。 | 复杂城市建筑物、田野、山脉背景，强光照突变。 | **多源泛化训练补充**：<br>增强模型在大俯仰变焦下的无人机轮廓辨识度。 |
| **Real_CCD**<br>(本地实机长焦集) | **多目标检测格式**<br>(以单机为主) | • 外场真实光电云台长焦对空实拍，单架无人机穿行为主，偶有鸟类飞入视场；<br>• 1,000 张抽帧切片。 | 真实长焦镜头畸变、大气抖动与传感器噪点。 | **实机域闭环验证集**：<br>真机部署前的域内精度把关。 |

> 💡 **核心概念澄清**：  
> YOLO 本身是纯粹的**全图多目标检测网络（Multi-Object Detector）**。无论输入图片中包含 1 架、3 架还是 10 架无人机，只要它们落入全图视场，YOLO 均会在其 $8400$ 个输出锚点上并发预测出所有目标的边界框；而在跟踪阶段，系统通过 `TargetSelector` 选择主威胁目标交接给单目标跟踪器（SOT）进行单一视轴伺服。

---

## 三、 基于数据集的 YOLO 模型系统化训练规划

为确保硕士论文消融实验的严密性，模型训练决不能停留于 COCO 通用预训练权重的简单推理，必须建立规范的训练推进流水线：

```
 【阶段 1：格式标准化】          【阶段 2：四模型同构基础训练】      【阶段 3：多源联合鲁棒训练】        【阶段 4：静态 ONNX 导出】
 7,800 张 VOC XML 标注         v8n/v8s/11n/11s 同构微调         DUT + Det-Fly + Drone-vs-Bird     标准化算子导出
       │                             │                                 │                               │
       ▼                             ▼                                 ▼                               ▼
 scripts/convert_voc_to_yolo.py  产生 4 款真正的微调模型           yolo11s_anti_uav_robust.pt        models/yolo11s_uav.onnx
 生成 anti_uav.yaml             支撑真正的学术 2x2 消融            强光/飞鸟环境下抗虚警大幅提升       DirectML GPU 毫秒级闭环
```

### 3.1 阶段 1：数据集格式标准化转换与标签清洗
* **执行脚本**：`scripts/convert_voc_to_yolo.py`；
* **操作内容**：
  1. 解析 `datasets/Anti-UAV/detection/train/xml/` 与 `val/xml/` 中的 7,800 份 XML 文件；
  2. 提取 `<name>UAV</name>` 目标，执行长宽与中心坐标归一化：
     $$c_x = \frac{x_{\min} + x_{\max}}{2 W}, \quad c_y = \frac{y_{\min} + y_{\max}}{2 H}, \quad w = \frac{x_{\max} - x_{\min}}{W}, \quad h = \frac{y_{\max} - y_{\min}}{H}$$
  3. 在对应目录下生成 `labels/train/*.txt` 与 `labels/val/*.txt`；
  4. 生成工程标准化描述文件 `datasets/anti_uav.yaml`。

### 3.2 阶段 2：四款候选模型在 Anti-UAV 上的同构领域微调
为了在毕业论文中进行**具备百分之百学术说服力的真实 2×2 消融横评**，必须在相同的软硬件与超参协议下，对四款模型开展同构重训：
* **训练模型对象**：
  1. `yolov8n`（经典架构轻量级基线，3.2M）
  2. `yolov8s`（经典架构高精级基线，11.2M）
  3. `yolo11n`（现代 C2PSA 架构轻量级，2.6M）
  4. `yolo11s`（现代 C2PSA 架构高精级，9.4M）
* **受控训练协议（Strict Controlled Protocol）**：
  - **训练集**：DUT Anti-UAV 训练集（5,200 张原生高清可见光图像）；
  - **输入尺寸**：$640 \times 640$ 静态分辨率；
  - **单类别设定**：`nc: 1`, `names: ['UAV']`；
  - **超参数一致性**：Epochs = 80，Batch Size = 16，初始学习率 $lr_0 = 0.01$，余弦退火衰减；
  - **小目标数据增强**：Mosaic = 1.0（增加微小目标密度），Copy-Paste = 0.3（强化小目标随机背景鲁棒性），MixUp = 0.15；
  - **硬件底座**：本地 NVIDIA GeForce RTX 5070 Laptop GPU (8GB GDDR6)。

### 3.3 阶段 3：多源联合鲁棒性训练
在增补下载 `Drone-vs-Bird` 与 `Det-Fly` 后，构建 25,000+ 样本的大规模空域检测库：
* 注入飞鸟负样本（作为负类别或纯背景），使 YOLO 原生具备区分“飞鸟杂波 vs 无人机入侵”的能力；
* 验证在强逆光、高光云层背景下的全天候适应性。

### 3.4 阶段 4：标准化静态 ONNX 导出
* **导出规范**：
  - 输入：$1 \times 3 \times 640 \times 640$ 纯静态三维张量；
  - 算子集：OPSET 12/17；
  - 剥离 NMS：由上位机纯 C++ 独立执行快速 NMS，避免算子绑定；
* **产出物**：`models/yolo11s_uav.onnx`（约 38MB）与 `models/yolo11n_uav.onnx`（约 10MB）。

---

## 四、 硕士论文全流程消融实验设计与对应测试数据集矩阵

为全面评估架构创新、模型尺度、领域微调与抗虚警机制对空域弱小无人机感知性能的影响，设立以下五项控制变量消融实验：

| 实验编号 | 消融自变量与对照组 | 核心考察目标 | ★ 对应的测试数据集 | 关键评价指标 | 推进状态与规划 |
| :---: | :--- | :--- | :--- | :--- | :---: |
| **消融 1** | **代际架构演进**<br>微调后 YOLOv8<br>vs 微调后 YOLO11 | 验证在无人机专属权重下，C3k2 + C2PSA 空间注意力对微弱高频特征与抗杂波的真实增益 | **DUT Anti-UAV 验证集**<br>(2,600 帧全量实测) | • mAP@50 (%)<br>• mAP@50:95 (%)<br>• 极弱小目标召回率 | ⏳ **规划执行中**<br>(4 款模型同构重训后测试) |
| **消融 2** | **模型参数尺度**<br>Nano 级 vs Small 级 | 衡量模型参数翻倍在长焦微弱目标感知上的边际增益与伺服时延的权衡 | **DUT Anti-UAV 验证集**<br>(2,600 帧全量实测) | • GPU 纯推理耗时 (ms)<br>• 端到端总时延 (ms)<br>• 系统帧率 (FPS) | ⏳ **规划执行中**<br>(同构微调后测定) |
| **消融 3** | **领域训练策略增益**<br>通用预训练权重<br>vs DUT 单源微调<br>vs 多源联合训练 | 验证解决类别域漂移（Domain Gap）与小目标特征适配的飞跃幅度 | **DUT Anti-UAV 验证集**<br>+ **Det-Fly 验证集** | • mAP@50 (%)<br>• 强光逆光召回率<br>• 大姿态俯仰召回率 | ⏳ **规划执行中**<br>(验证从 19.7% 跨越到 90%+) |
| **消融 4** | **抗虚警校验增益**<br>单级 YOLO (τ=0.2)<br>vs YOLO + VaL_CNN | 评估条件式二级校验网络在低门限下对刺眼云层与飞鸟杂波的过滤能力 | **Drone-vs-Bird 挑战赛测试集**<br>(密集多目标，专攻飞鸟负样本) | • 每帧虚警数 (FP/frame)<br>• 真实目标召回率<br>• 综合 F1 分数 | ⏳ **规划执行中**<br>(验证虚警降低 2.6 倍) |
| **消融 5** | **目标尺度敏感度消融**<br>极弱小 (<32px)<br>中小 (32~64px)<br>中大 (>64px) | 验证网络对长焦镜头目标由远及近距离变化的尺度鲁棒性 | **DUT Anti-UAV 验证集**<br>(按像素面积三级切分) | • $AP_{tiny}$ (%)<br>• $AP_{small}$ (%)<br>• $AP_{med}$ (%) | ⏳ **伴随训练评估** |

---

## 五、 运行时免重启动态热切换（Hot-Swap）工程接入

在实际光电跟踪系统中，YOLO 检测模型支持在实时 1080P 视频流不中断、不黑屏的前提下，由操作员或系统自适应策略执行动态免重启热切换：

```mermaid
sequenceDiagram
    participant UI as 主界面 (MainWindow)
    participant Worker as InferenceWorkerThread
    participant Detector as OnnxDetector

    UI->>Worker: switchModelSignal("models/yolo11s_uav.onnx")
    Note over Worker: 获取 std::lock_guard(m_detectMutex)
    Worker->>Detector: loadModel("models/yolo11s_uav.onnx", config)
    Detector-->>Worker: DirectML GPU 会话重建成功 (耗时 < 30ms)
    Worker->>UI: modelSwitchedSignal(true, "YOLO11s_UAV")
    Note over Worker: 释放互斥锁，下一检测帧立即生效 (视频零掉帧)
```

### 5.1 线程安全模型重载接口
[OnnxDetector.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/onnxdetector.cpp) 内部所有读写操作均受 `m_detectMutex` 保护：

```cpp
bool OnnxDetector::loadModel(const QString& modelPath, const DetectorConfig& config)
{
    std::lock_guard<std::mutex> lock(m_detectMutex);
    release(); // 安全排空旧 Session 与 DirectML 显存资源
    
    // 初始化新模型的 DirectML GPU 硬件会话并绑定输入输出张量
    return initSession(modelPath, config);
}
```

### 5.2 界面控件与遥测状态
在 [MainWindow](file:///d:/PyProjects/RGB_PTZ_Integrated/mainwindow.cpp) 的 AI 感知设置面板中，通过 `comboAiModel` 下拉控件实时展示可用模型，切换后状态栏动态显示当前模型名称、GPU 单帧纯推理耗时（ms）、端到端时延与 FPS。

---

## 六、 实操推进路线图（Checklist）

- [x] **1. C++ OnnxDetector 互斥锁加固与动态热切换接入**：
  - 在 `OnnxDetector` 内部落实 `m_detectMutex` 读写安全互斥，支持界面即时免重启热重载。
- [ ] **2. DUT Anti-UAV 7,800 张检测样本 VOC ➔ YOLO 格式批量转换**：
  - 编写并运行 `scripts/convert_voc_to_yolo.py`，将 XML 标注批量转换为 YOLO 标准归一化 `.txt`，生成 `datasets/anti_uav.yaml`。
- [ ] **3. 编写受控训练脚本并启动 4 款候选模型同构微调**：
  - 编写 `scripts/train_yolo_uav.py`，配置单类别 `0: UAV` 与统一超参（80 Epochs, Mosaic 1.0, Copy-Paste 0.3）；
  - 分别对 `yolov8n`、`yolov8s`、`yolo11n`、`yolo11s` 四款模型执行同构训练；
  - 产生 4 个无人机专属检测权重：`yolov8n_uav.pt`、`yolov8s_uav.pt`、`yolo11n_uav.pt`、`yolo11s_uav.pt`。
- [ ] **4. 执行硕士论文 2×2 消融实验（微调后）**：
  - 对同构微调后的 4 款模型在 Anti-UAV 验证集上统一打点，生成学术正文所需的高精消融图表，确立最终的默认主力模型（YOLO11s_uav）与备选模型（YOLO11n_uav）。
- [ ] **5. 增补下载 Drone-vs-Bird 与 Det-Fly 可见光检测数据集**：
  - 编写自动化拉取脚本 `scripts/download_detection_benchmarks.py`；
  - 扩充多源鲁棒训练集与飞鸟抗虚警测试集。
- [ ] **6. 训练并接入 VaL_CNN 条件式二级校验网络**：
  - 训练 64×64 局部切片微型 CNN 分类器，在 Drone-vs-Bird 挑战赛基准上实测验证对飞鸟杂波的 2.6 倍虚警抑制效果。
'''

target_file = r'待执行方案/03_YOLO多模型(v8n_11n_11s)性能横评与动态切换实操方案.md'
with open(target_file, 'w', encoding='utf-8') as f:
    f.write(content)
print(f"Successfully updated {target_file}")
