# 基于 AMFT 自适应多特征融合的光电云台“跟踪为主、按需检测”协同实操方案

> **方案编号**：02（已提拔至核心前置推进优先级）  
> **归属工程**：[RGB_PTZ_Integrated](file:///d:/PyProjects/RGB_PTZ_Integrated)  
> **理论支撑**：海军工程大学 & 空军预警学院 2025 IEEE 顶会论文《*Adaptive Multi-Feature Fusion-Based UAV Detection and Tracking Collaboration Framework Trigger Mechanism*》(AMFT)  
> **前置依赖**：[01_Windows平台深度学习目标检测与跟踪集成实操方案.md](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/01_Windows平台深度学习目标检测与跟踪集成实操方案.md) 阶段一（OnnxDetector 3.06ms 推理底座已验收打通）  
> **当前状态**：🟢 **已闭环 (Completed - 闭环验收 4/4 项全部 PASS)**  
> **核心目标**：彻底告别“每隔数帧定期盲目检测”的算力浪费与低效模式，全面吸收 AMFT 论文精髓，构建**“三维可靠度评估 ($S_{conf}, S_{mot}, S_{bg}$) + 滑动窗口动态阈值 ($\tau_t$) + 零频待机单次唤醒 + 遮挡恢复自动重置”**的工业级闭环状态机，彻底替换过时的九宫格盲试。

---

## 一、 为什么必须以 AMFT 论文重构协同触发机制？

在光电云台跟踪实战中，传统单一阈值或固定帧率触发存在两大根本缺陷：
1. **单一置信度（如仅看 PSR）极易虚警**：光照瞬间阴暗、云层反光、目标微小形变会导致置信度骤降，但此时目标运动依然完全连续平滑，盲目唤醒全图检测造成严重算力浪费；
2. **固定阈值无法自适应复杂战场动态**：在目标超视距平稳巡航时阈值过低容易误触发，而在目标高速突防机动或进入树林遮挡时阈值过高又导致触发滞后脱靶。

### 论文给出的破局方案：
* **局部高频主循环**：[DsstCore](file:///d:/PyProjects/RGB_PTZ_Integrated/dsstcore.cpp) 满帧率（30/60 FPS）极速跟踪，单帧耗时 $< 1\text{ ms}$，CPU 占用 $< 3\%$；
* **全局大模型零频待机**：[OnnxDetector](file:///d:/PyProjects/RGB_PTZ_Integrated/onnxdetector.cpp) 常态处于挂起状态（0 FPS / 0% GPU占用）；
* **多特征融合精准把关**：当且仅当多维评估确认“航迹真正不可靠”时，才单次唤醒 1 帧全局扫描，找回后下一帧强制切回局部跟踪。

---

## 二、 AMFT 核心数学模型在本项目中的工程映射

```
                                  [ 输入当前视频帧 (30 FPS) ]
                                                │
                                                ▼
                                    【DsstCore 局部高频跟踪】
                                    (输出预测框与特征，耗时 < 1ms)
                                                │
                 ┌──────────────────────────────┼──────────────────────────────┐
                 ▼                              ▼                              ▼
      【维度 1: 跟踪置信度 S_conf】    【维度 2: 运动连续性 S_mot】    【维度 3: 背景干扰度 S_bg】
       • 取 DsstCore 归一化 PSR         • IoU_t (位置连续性)            • 目标区域与外围 3 倍背景
       • 映射为 [0.0, 1.0]              • D_t (位移突变率偏差)          • 复用已提取的 FHOG 计算余弦相似度
                                        • S_mot = 0.6*IoU + 0.4*(1-D)  • S_bg = 1 - 相似度 (区分度高则得分高)
                 └──────────────────────────────┼──────────────────────────────┘
                                                ▼
                             【加权计算综合可靠度得分 R_t】
                           R_t = 0.45*S_conf + 0.35*S_mot + 0.20*S_bg
                                                │
                                                ▼
                          【滑动窗口动态阈值模块 (K = 10)】
                           • 记录最近 10 帧的综合得分方差 σ_t
                           • 动态计算判定阈值: τ_t = τ_0 - α * σ_t (τ_0=0.65, α=0.5)
                                                │
                                   ┌────────────┴────────────┐
                                   ▼                         ▼
                           [ R_t ≥ τ_t 跟踪可靠 ]     [ R_t < τ_t 跟踪异常! ]
                                   │                         │
                                   ▼                         ▼
                           【维持局部跟踪模式】       【单次唤醒 OnnxDetector 全局扫描】
                           • YOLO 保持休眠 (0 FPS)    • 全图 640x640 检索目标 (3.06ms)
                           • 极速输出脱靶量驱动电机   • 命中: 立即重置 DsstCore 模板
                                                      • 铁律: 下一帧强制切回局部跟踪！
```

### 2.1 针对本工程的参数映射表：
1. **$S_{\text{conf}}$**：取 `DsstCore::psr()`，经验公式归一化 $S_{\text{conf}} = \text{qBound}(0.0f, (\text{psr} - 5.0f) / 20.0f, 1.0f)$；
2. **$S_{\text{mot}}$**：
   - 当前框与上一帧框计算 `computeIoU(box_t, box_{t-1})`；
   - 比较当前位移向量与前 3 帧平均位移向量的欧式距离偏差率 $D_t$；
   - 杜绝目标一闪光就误触发检测；
3. **$S_{\text{bg}}$**：
   - 直接复用 `DsstCore` 内部刚刚提取的 31 通道 FHOG 特征图，取目标中心与周围背景 patch 向量做内积，计算余弦距离。

---

## 三、 四态时序闭环状态机（彻底废除九宫格 reDetect）

在 [objecttracker.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/objecttracker.cpp) 中构建全新的 `TrackingState` 状态流转机：

1. **TRACK_NORMAL（正常跟踪态，占比 98%）**：
   - DsstCore 独占运行，YOLO 深度学习模型 0 FPS 完全挂起；
2. **TRACK_OCCLUDED（疑似遮挡/微失锁态）**：
   - 触发条件：$R_t < \tau_t$；
   - 动作：停止 DsstCore 模板更新（防止吸附错误背景），保持 KF 速度外推前馈，**异步单次投递 1 帧给 OnnxDetector 全图扫描**；
3. **TRACK_REACQUIRED（重捕恢复态）**：
   - 触发条件：OnnxDetector 成功检出目标（置信度 $> 0.4$ 且与前序预测位置临近）；
   - 动作：以新检测框**强制调用 `DsstCore::init()` 重置外观模板**，瞬间消除累积形变与漂移，下一帧立刻返回 `TRACK_NORMAL`；
4. **TRACK_LOST（彻底丢失态）**：
   - 触发条件：连续 15 帧未能重捕；
   - 动作：云台按最后速度匀速搜索或停稳，转入低频检索（每隔 10 帧检测 1 帧，即 3 FPS 待机检索）。

---

## 四、 实操落地推进路线图（Checklist）

- [x] **02-1.1 数据结构扩展**：在 `ObjectTracker` 中增加 `AMFTEvaluator` 辅助类，维护 10 帧滑动窗口与综合得分计算。
- [x] **02-1.2 废除九宫格盲试**：从 [objecttracker.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/objecttracker.cpp) 彻底删除原有旧代码 `reDetect()` 及九宫格偏移搜索逻辑。
- [x] **02-1.3 接入 IDetector 接口**：`ObjectTracker` 引入 `std::shared_ptr<IDetector>`，在 $R_t < \tau_t$ 时单次调用 `detect()`。
- [x] **02-1.4 模板自适应重置与强制回归**：检测返回高置信度目标后，自动执行 DsstCore 模板热更新，且下一帧严格强制切回局部跟踪（0 FPS 休眠）。
- [x] **02-1.5 变焦尺度动态校准**：联动 [LensManager](file:///d:/PyProjects/RGB_PTZ_Integrated/lensmanager.cpp) 变焦完成信号触发单次尺度纠偏校准。
- [x] **02-1.6 外场离线数据集验证**：在 `testdata/sequences/01` 上完成 4 大维度闭环验收实跑（通过 PASS，平均误差 2.22px，稳态 0 误唤醒）。

---

## 五、 预期验收成果基准

| 评估指标 | 传统固定帧率检测 | AMFT 方案实测预期 |
| :--- | :---: | :---: |
| **平均全局检测调用率** | 100% 逐帧 (30 FPS) 或 33% (10 FPS) | **$< 2\%$（仅异常时单次调用）** |
| **稳态 GPU / NPU 占用率** | 60% ~ 90% | **$\approx 0\%$（零频待机冷态）** |
| **瞬时遮挡误唤醒率** | 频繁误报唤醒 | **下降 $\ge 85\%$（运动平滑度压制）** |
| **严重遮挡重捕成功率** | 经常漂移吸附背景 | **$\ge 95\%$（YOLO 全局语义拉回）** |
