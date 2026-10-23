# 基于光电跟踪场景的 YOLO 架构改良、领域微调与量化适配方案

> **方案编号**：04  
> **归属工程**：[RGB_PTZ_Integrated](file:///d:/PyProjects/RGB_PTZ_Integrated)  
> **前置依赖**：[01_Windows平台深度学习目标检测与跟踪集成实操方案.md](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/01_Windows平台深度学习目标检测与跟踪集成实操方案.md)（已打通推理底座）、[02_YOLO多模型(v8n_11n_11s)性能横评与动态切换实操方案.md](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/02_YOLO多模型(v8n_11n_11s)性能横评与动态切换实操方案.md)  
> **当前状态**：⚪ **已纳编 (Planned / Ready for Experiment)**  
> **核心目标**：针对光电吊舱/长焦云台外场实测中的“远距弱小目标漏检（<20像素）”、“复杂背景杂波虚警”及“边缘端量化掉点”三大痛点，制定网络微结构改良、专用数据集迁移微调、标准导出与免重启热评测的工程化闭环规范。

---

## 一、 为什么要在本项目中改良 YOLO？

通用 YOLO（在 COCO 80 类预训练）主要面向日常生活近景场景（人、汽车、猫狗等），在光电云台实际追踪任务中面临如下结构性短板：

| 实际挑战 | 通用 YOLO 表现 | 本方案改良策略 | 预期增益 |
| :--- | :--- | :--- | :--- |
| **远距离弱小目标** | 目标仅 10~25 像素，下采样 8 倍后特征完全消失，频繁漏检 | 引入 **P2 极小目标微尺度检测头**（$160 \times 160$ 特征图） | 小目标召回率提升 $\ge 25\%$ |
| **高空云层/树林杂波** | 容易将边缘云彩、摇晃树叶误报为无人机或鸟类 | 引入 **EMA / BiFPN 注意力机制**，过滤高频背景干扰 | 虚警率下降 $\ge 40\%$ |
| **长焦微抖尺度变化** | 尺度快速变化时，普通 CIoU 损失对微小抖动过分敏感 | 引入 **Wise-IoU / Inner-IoU 动态聚焦损失函数** | 边框预测更平滑，降低 PID 抖动 |
| **RK3588 INT8 部署** | 任意私有算子会导致 NPU 工具链量化失败或严重掉点 | 实施 **算子中立约束 + 量化感知训练 (QAT)** | NPU 转换 100% 成功且精度无损 |

---

## 二、 YOLO 改进全流程流转规范（五步闭环法）

为确保改进的模型与上位机 C++ 生产代码零冲突，全体开发者必须严格遵守以下五步流程：

```
 [步骤 1: 极速复用与微创微调] ──► [步骤 2: 算子中立导出] ──► [步骤 3: 上位机一键热测试]
  (模板提取+手术改动+冻结快训)    (严格标准 ONNX Opset 12)    (test_detector 实机打点)
                                                                 │
 [步骤 5: 纳入 RKNN INT8] ◄─── [步骤 4: 伺服闭环对拍] ◄──────────┘
  (量化转换并同步 checkpoint)   (云台追踪实测与指标验收)
```

### 2.1 步骤 1：极速复用与微创手术式模型制作指南（四步提速 95%）
严禁从零编写模型层或从零随机初始化训练，全体开发者必须遵循以下“四步极速复用流水线”：

#### 2.1.1 一键提取官方原生结构蓝图
工程提供了自动化提取脚本 [scripts/extract_yolo_cfg.py](file:///d:/PyProjects/RGB_PTZ_Integrated/scripts/extract_yolo_cfg.py)，5 秒钟即可将官方经过数万次调优的原生 YAML 蓝图提取为可编辑文件：
```bash
python scripts/extract_yolo_cfg.py --model yolo11 --out yolo11_custom.yaml
```

#### 2.1.2 结构微创手术（复用 95% 的官方既有模块）
无需手写底层卷积和反向传播，仅需在 YAML 中针对性修改 2~4 行：
```yaml
# yolo11_custom.yaml 片段：微创增加 P2 极小目标检测头 (160x160)
head:
  - [-1, 1, nn.Upsample, [None, 2, "nearest"]]
  - [[-1, 4], 1, Concat, [1]]  # 融合 P3 特征
  - [-1, 2, C3k2, [128, False]] # 12 (80x80 分辨率)

  # >>> 新增：微创追加 P2 特征融合层 <<<
  - [-1, 1, nn.Upsample, [None, 2, "nearest"]]
  - [[-1, 2], 1, Concat, [1]]  # 融合骨干网第 2 层 P2 特征 (160x160 超大分辨率)
  - [-1, 2, C3k2, [64, False]]  # 15 (新增 P2 检测层)

  # 检测头：从 3 头扩展为 4 头
  - [[15, 12, 18, 21], 1, Detect, [nc]]
```

#### 2.1.3 状态字典模糊匹配与权重精准嫁接（`model.load()`）
利用 PyTorch 状态字典的自动模糊匹配，未改动的层 100% 瞬间继承官方数十亿预训练参数：
```python
from ultralytics import YOLO

# 1. 载入改好的结构蓝图
model = YOLO("yolo11_custom.yaml")
# 2. 状态字典自动匹配：未修改的 95% 网络层权重全部直接继承，新层随机初始化
model.load("yolo11s.pt")
```

#### 2.1.4 冻结骨干网快速收敛训练（训练耗时从“几天”压至“30分钟”）
骨干网（Backbone）提取通用边缘、纹理特征已趋完美，通过 `freeze=10` 锁死前 10 层：
```python
model.train(
    data="uav_data.yaml",
    epochs=50,
    imgsz=640,
    batch=16,
    freeze=10,  # 核心大招：锁定前 10 层，显存下降 50%，训练提速 2 倍，杜绝过拟合
    device=0    # 使用宿主机 NVIDIA RTX 5070 GPU
)
```

---

### 2.2 步骤 2：中立算子导出网关 (Export Gateway)
导出 ONNX 时必须严格遵守 **三大零冲突铁律**：
```python
from ultralytics import YOLO

model = YOLO("runs/train/weights/best.pt")
# 严格遵守：静态尺寸 640x640、标准 opset 12、禁止在内部打包 NMS、启用 onnxslim
model.export(
    format="onnx",
    imgsz=640,
    opset=12,
    dynamic=False,
    simplify=True,
    nms=False  # 严禁设为 True！必须保持纯净矩阵输出，由纯 C++ 后处理运行 NMS
)
```
- **关键技术契约**：输出格式必须维持为 $1 \times (4 + N) \times \text{Anchors}$。
  - 原生 YOLOv8/11 锚点数：$8400$；
  - 增加 P2 检测头后锚点数：$8400 + 160 \times 160 = 34000$。
  - **我们实现的 [OnnxDetector](file:///d:/PyProjects/RGB_PTZ_Integrated/onnxdetector.cpp) 会自动动态自适应锚点数**，上位机 C++ 运行时完全兼容！

---

### 2.3 步骤 3：上位机离线测试工具动态热评测（一键免重启验证）
- 将导出的 `yolo11_custom.onnx` 放入 `models/` 目录；
- 直接使用我们已建立的 [test_detector.exe](file:///d:/PyProjects/RGB_PTZ_Integrated/tools/test_detector.cpp) 进行一键对拍测试：
  ```bash
  .\build\release\test_detector.exe models/yolo11_custom.onnx
  ```
- 验证指标：
  1. DirectML (RTX 5070) 推理耗时是否依然稳定在 $\le 8\text{ ms}$（$\ge 120\text{ FPS}$）；
  2. 在真实数据集上，原先漏检的微小目标是否被成功检出；
  3. 内存与张量分配零泄露。

---

### 2.4 步骤 4：云台伺服闭环对拍与消融记录
- 将新模型载入上位机，开启 ByteTrack 跟踪与 PTZ 伺服；
- 统计目标在进出遮挡、长焦变焦时的锁定稳定性；
- 产出消融对比报告（Baseline vs Improved）。

---

### 2.5 步骤 5：成果沉淀与 RK3588 量化转化适配
- 达标模型正式提交至 `models/` 归档并在 [checkpoint.md](file:///d:/PyProjects/RGB_PTZ_Integrated/checkpoint.md) 中追加记录；
- 针对未来 RK3588 边缘端，使用 `rknn-toolkit2` 执行量化转化：
  ```python
  from rknn.api import RKNN
  rknn = RKNN()
  rknn.config(mean_values=[[0,0,0]], std_values=[[255,255,255]], target_platform="rk3588")
  rknn.load_onnx("models/yolo11_custom.onnx")
  rknn.build(do_quantization=True, dataset="./dataset_calibration.txt")
  rknn.export_rknn("models/yolo11_custom.rknn")
  ```


---

## 三、 候选改进方案池（Backlog）

| 改进编号 | 改进主题 | 适合场景 | 预期难度 | 优先级 |
| :---: | :--- | :--- | :---: | :---: |
| **04-A** | **P2 极小目标微尺度头集成** | 50m~200m 极远距离低空微小目标（无人机、雀鸟）识别 | 🟡 中 | **P1 (首选)** |
| **04-B** | **Anti-UAV / VisDrone 领域微调** | 增强特定飞行器几何特征识别与抗天空杂波能力 | 🟢 低 | **P1** |
| **04-C** | **EMA 多尺度空间注意力融入** | 复杂地面杂草、水面反光、低照度环境下的目标增强 | 🟡 中 | **P2** |
| **04-D** | **RKNN INT8 量化感知微调 (QAT)** | 未来迁移 RK3588 边缘端前消除 INT8 精度劣化 | 🔴 高 | **P3 (硬件就绪后)** |

---

## 四、 成果验收判定基准

1. **实时性指标**：改进后的模型在 RTX 5070 DirectML 上的单帧纯推理耗时不得超过 **$8\text{ ms}$**（保证 $\ge 120\text{ FPS}$ 吞吐）；
2. **小目标检出率**：在面积 $< 20 \times 20$ 像素的测试集中，目标召回率提升不低于 **20%**；
3. **架构中立性**：导出的 ONNX 模型不得包含任何平台绑定算子，确保 `rknn-toolkit2` 的 `config` 和 `build` 无报错。
