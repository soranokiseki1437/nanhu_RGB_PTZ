# 基于 AMFT 自适应多特征融合的光电云台“跟踪为主、按需检测”协同实操方案

> **方案编号**：02（核心前置推进方案）  
> **归属工程**：[RGB_PTZ_Integrated](file:///d:/PyProjects/RGB_PTZ_Integrated)  
> **理论支撑**：海军工程大学 & 空军预警学院 2025 IEEE 顶会论文《*Adaptive Multi-Feature Fusion-Based UAV Detection and Tracking Collaboration Framework Trigger Mechanism*》(AMFT)  
> **前置依赖**：[01_Windows平台深度学习目标检测与跟踪集成实操方案.md](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/01_Windows平台深度学习目标检测与跟踪集成实操方案.md) 阶段一（OnnxDetector 3.06ms 推理底座已验收打通）  
> **当前状态**：🟢 **已闭环 (Completed - 闭环验收 4/4 项全部 PASS)**  
> **核心目标**：吸收 AMFT 论文精髓，构建**“三维可靠度评估 ($S_{conf}, S_{mot}, S_{bg}$) + 滑动窗口动态阈值 ($\tau_t$) + 零频待机单次唤醒 + 遮挡恢复自动重置”**的工业级闭环状态机，实现高能效、高精度的自适应检测-跟踪协同。

---

## 一、 协同触发机制设计原则

在光电云台跟踪任务中，跟踪器与大模型检测器采用分工协同架构：
1. **局部高频主循环**：[DsstCore](file:///d:/PyProjects/RGB_PTZ_Integrated/dsstcore.cpp) 满帧率（30/60 FPS）极速跟踪，单帧耗时 $< 2\text{ ms}$，计算资源开销极低；
2. **全局大模型零频待机**：[OnnxDetector](file:///d:/PyProjects/RGB_PTZ_Integrated/onnxdetector.cpp) 常态处于挂起状态（0 FPS / 0% GPU占用）；
3. **多特征融合精准把关**：由 [AMFTEvaluator](file:///d:/PyProjects/RGB_PTZ_Integrated/amftevaluator.cpp) 评估器担任协同判决仲裁，当且仅当多维评估确认“航迹真正不可靠”时，才单次唤醒 1 帧全局扫描，找回后下一帧强制切回局部跟踪。

---

## 二、 AMFT 核心数学模型在本项目中的工程映射

```
                                  [ 输入当前视频帧 (30 FPS) ]
                                                │
                                                ▼
                                    【DsstCore 局部高频跟踪】
                                    (输出预测框与特征，耗时 < 2ms)
                                                │
                 ┌──────────────────────────────┼──────────────────────────────┐
                 ▼                              ▼                              ▼
      【维度 1: 跟踪置信度 S_conf】    【维度 2: 运动连续性 S_mot】    【维度 3: 背景可区分度 S_bg】
       • 取 DsstCore 归一化 PSR         • IoU_t (位置重叠连续性)        • 目标与外围 2.5 倍环形背景
       • 映射为 [0.0, 1.0]              • D_t (位移突变率偏差)          • 可见光: HSV 空间 2D 颜色直方图
                                        • S_mot = 0.6*IoU + 0.4*(1-D)   • 红外光: 16-bin 灰度直方图
                                                                        • 巴氏距离衡量上下文可区分度
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
   - 比较当前位移向量与历史平均位移向量的欧式距离偏差率 $D_t$；
   - 抑制目标因瞬时反光或光照变化导致的误触发；
3. **$S_{\text{bg}}$（双模自适应特征）**：
   - **可见光 RGB 通道**：转换至 HSV 空间，提取目标区域与外围 2.5 倍环形背景的 $16 \times 8$ bin（色调 H 与饱和度 S）二维色彩直方图，利用巴氏距离衡量目标与地表地貌的颜色区分度；
   - **红外热成像通道**：提取单通道 16-bin 灰度直方图，利用巴氏距离衡量目标与空域背景的辐射亮度反差。

---

## 三、 四态时序闭环状态机

在 [objecttracker.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/objecttracker.cpp) 中构建 `TrackingState` 状态流转机：

1. **TRACK_NORMAL（正常跟踪态，占比 98%）**：
   - DsstCore 独占运行，YOLO 深度学习模型 0 FPS 完全挂起；
2. **TRACK_OCCLUDED（疑似遮挡/微失锁态）**：
   - 触发条件：$R_t < \tau_t$；
   - 动作：停止 DsstCore 模板更新（防止吸附错误背景），保持 KF 速度外推前馈，**异步单次投递 1 帧给 OnnxDetector 全图扫描**；
3. **TRACK_REACQUIRED（重捕恢复态）**：
   - 触发条件：OnnxDetector 成功检出目标（置信度 $> 0.4$ 且与前序预测位置临近）；
   - 动作：以新检测框**调用 `DsstCore::init()` 重置外观模板**，消除累积形变与漂移，下一帧立刻返回 `TRACK_NORMAL`；
4. **TRACK_LOST（彻底丢失态）**：
   - 触发条件：连续 15 帧未能重捕；
   - 动作：云台按最后速度匀速搜索或停稳，转入低频检索（每隔 10 帧检测 1 帧，即 3 FPS 待机检索）。

---

## 四、 实操落地推进路线图（Checklist）

- [x] **02-1.1 数据结构扩展**：在 `ObjectTracker` 中增加 `AMFTEvaluator` 辅助类，维护 10 帧滑动窗口与综合得分计算。
- [x] **02-1.2 异常状态调度解耦**：重构目标丢失异常处理，接入单次深度检测唤醒链路。
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
