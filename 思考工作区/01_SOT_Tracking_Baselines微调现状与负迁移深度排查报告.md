# SOT_Tracking_Baselines 异机微调现状与负迁移深度排查报告

> **排查时间**：2026-11-16  
> **排查对象**：[D:\PyProjects\SOT_Tracking_Baselines](file:///D:/PyProjects/SOT_Tracking_Baselines)（异机 Git 同步仓库）及对照库 [D:\PyProjects\SOT_Tracking_Baselines_官方权重试跑](file:///D:/PyProjects/SOT_Tracking_Baselines_官方权重试跑)  
> **对标规划**：`实验执行手册.md`、`docs/06_后续创新规划.md`、`课题/轻量级跟踪基线实验分析与Rep-LightBackbone立项论证.md`

---

## 一、 异机实际执行进度与原定规划的全局对比

### 1.1 原定规划（`实验执行手册.md` 与 `docs/06_后续创新规划.md`）
原定规划在异机（Ubuntu 20.04 + RTX 4080 SUPER）上分为两条主线：
1. **基线评测主线**：14 款开源基线在 `DUT-Anti-UAV-Val`（20 组真机航迹 / 24,804 帧）上完成零样本 OPE 评测（Step 3）；
2. **自研 Rep-LightTrack V2 主线**：
   - **Stage 2**：从 `repvgg_a0_rvgg_in1k` 预训练骨干出发，在 `GOT-10k/train`（9,335 序列，每轮 60,000 对）训练 50 轮（Step 5）；
   - **Stage 3**：从 Stage 2 最优权重出发，在 `CASIA-Anti-UAV300/train`（160 序列，混合 15% GOT-10k）微调 30 轮，并完成 `ciou`、`inner_ciou`、`hybrid`、`cov_nwd` 四组损失消融（Step 6）。

### 1.2 异机实际执行情况
异机完成了 **第一批（14 款官方权重零样本评测）**，随后将重心转向了 **第二批（14 款开源基线在 CASIA Anti-UAV300 上的微调）** 以及针对 `LightTrackM` 和 `FEAR-XS` 微调崩溃后的 **P0/P1/P2 重训抢救实验（累计 47 个 run）**：

| 任务模块 | 原定规划 | 异机实际执行状态 | 关键差异说明 |
| :--- | :--- | :---: | :--- |
| **数据集准备** | GOT-10k 全量 (9,335 seq) + CASIA (160/67 seq) + DUT (20 seq) | ⚠️ 部分执行 | CASIA (160/67) 与 DUT (20) 已全量就绪；GOT-10k 仅解压了 `split01/02`（1,000 序列 / 98,523 帧）用于回放 |
| **第一批：14 款基线零样本评测** | 14 款官方权重在 DUT 上 OPE 盲测 | ✅ 14/14 完成 | 产出 `baselines/results/benchmark_summary.csv` |
| **第二批：14 款基线 CASIA 微调** | 规划外新增（全量基线域微调对比） | ✅ 14/14 完成 | 11 款深度模型微调 + KCF/DSST 网格搜参 + DaSiamRPN 沿用官方权重，结果普遍出现严重负迁移 |
| **LightTrackM / FEAR-XS 重训排查** | 规划外新增（R1~R6 共 6 套抢救配方） | 🔄 30+ 次实验 | 尝试冻结骨干、低学习率、GOT-10k 回放、Zoom-out、从零重训头（30/120 ep）、LoRA、UAV-only 100 ep（至 ep84） |
| **自研 Rep-LightTrack V2 训练** | Stage 2 (GOT-10k 50ep) + Stage 3 (CASIA 30ep × 4 损失) | ❌ **未启动 (0%)** | `scripts/train_sot_v2.py` 与 `models/sot/rep_lighttrack_v2.py` 全程未运行，`weights/` 与 `results_v2/` 未生成 |

---

## 二、 14 款基线两批 DUT 全维度指标对比（第一批零样本 vs 第二批微调）

数据来源：[baselines/results/benchmark_summary.csv](file:///D:/PyProjects/SOT_Tracking_Baselines/baselines/results/benchmark_summary.csv) 与 [baselines/results_finetuned/benchmark_summary.csv](file:///D:/PyProjects/SOT_Tracking_Baselines/baselines/results_finetuned/benchmark_summary.csv)。

| 跟踪算法 | 参数量 (M) | B1 AUC | B2 AUC | $\Delta\text{AUC}$ | B1 DP@20 | B2 DP@20 | B1 CLE (px) | B2 CLE (px) | B1 Lost (%) | B2 Lost (%) | B1 FPS | B2 FPS |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **OSTrack-Base** | 86.00 | 0.6388 | **0.6568** | **+0.0180** | 0.8253 | 0.8346 | 126.92 | 90.30 | 17.31 | 16.25 | 99.5 | 35.2 |
| **TransT** | 23.02 | 0.5393 | **0.5474** | **+0.0081** | 0.7111 | 0.7151 | 162.58 | 151.25 | 28.22 | 27.82 | 150.7 | 68.3 |
| **SiamGAT** | 14.23 | 0.4370 | **0.4407** | **+0.0037** | 0.6172 | 0.5941 | 214.70 | 211.46 | 36.62 | 35.57 | 227.8 | 153.5 |
| **KCF** | - | 0.0501 | 0.0502 | +0.0001 | 0.0026 | 0.0029 | 563.37 | 593.27 | 99.11 | 99.24 | 721.2 | 535.0 |
| **DaSiamRPN** | 90.44 | 0.3373 | 0.3373 | 0.0000 | 0.4702 | 0.4702 | 283.58 | 283.58 | 47.53 | 47.53 | 233.4 | 135.2 |
| **SiamCAR** | 51.38 | 0.5529 | 0.5377 | -0.0152 | 0.7133 | 0.7137 | 152.28 | 199.03 | 24.81 | 27.95 | 183.3 | 178.0 |
| **MixFormer** | 104.31 | 0.5493 | 0.5328 | -0.0165 | 0.7164 | 0.6907 | 177.03 | 186.73 | 28.04 | 30.71 | 105.2 | 35.6 |
| **SiamRPN++** | 54.00 | 0.5693 | 0.5500 | -0.0193 | 0.7391 | 0.7128 | 110.92 | 139.08 | 25.24 | 27.82 | 186.9 | 178.5 |
| **NanoTrack-V2** | 0.33 | 0.5360 | 0.4706 | **-0.0654** | 0.6833 | 0.5828 | 188.11 | 141.65 | 26.00 | 23.37 | 182.6 | 89.1 |
| **DSST** | - | 0.2448 | 0.1741 | **-0.0707** | 0.3111 | 0.2097 | 445.41 | 485.81 | 64.35 | 75.90 | 262.6 | 222.3 |
| **NanoTrack-V1** | 0.29 | 0.4062 | 0.3351 | **-0.0711** | 0.5346 | 0.4324 | 224.20 | 286.70 | 38.68 | 43.77 | 212.0 | 160.5 |
| **NanoTrack-V3** | 0.55 | 0.5448 | 0.4711 | **-0.0737** | 0.7003 | 0.6002 | 159.69 | 249.08 | 22.66 | 35.49 | 119.8 | 188.1 |
| **FEAR-XS** | 1.20 | 0.5128 | 0.3676 | **-0.1452** | 0.7381 | 0.5409 | 145.75 | 243.33 | 25.82 | 43.51 | 287.1 | 155.4 |
| **LightTrackM** | 1.60 | 0.5169 | 0.3013 | **-0.2156** | 0.6531 | 0.3682 | 146.74 | 236.20 | 15.57 | 24.19 | 60.0 | 38.7 |

---

## 三、 深度代码级归因：异机《负迁移分析》未发现的 5 个关键工程与代码缺陷

异机文档 [反无人机微调负迁移分析与重训方案.md](file:///D:/PyProjects/SOT_Tracking_Baselines/反无人机微调负迁移分析与重训方案.md) 将轻量模型的断崖式下跌全部归因于“小模型容量不足 + 160 条 CASIA 序列过少（差 2 个数量级）”。经逐行审查训练脚本、数据集加载器与训练日志，发现**除域分布差异外，存在 5 个直接导致训练退化或结论偏差的代码/配置级缺陷**：

### 缺陷 1（致命）：`LightTrackM` 自建训练管线中 `SHIFTs=0.0, SCALEs=0.0` + `FLIP=0.5` 未同步更新坐标，导致训练陷入恒定中心退化解
* **代码与日志证据**：
  1. 在 [lt_uav100.log](file:///D:/PyProjects/SOT_Tracking_Baselines/baselines/logs/finetune/lt_uav100.log#L5-L33)、[lighttrack_ft.log](file:///D:/PyProjects/SOT_Tracking_Baselines/baselines/logs/finetune/lighttrack_ft.log#L5-L28)、[lt_backbone30.log](file:///D:/PyProjects/SOT_Tracking_Baselines/baselines/logs/finetune/lt_backbone30.log#L5-L33)、[lt_lora20.log](file:///D:/PyProjects/SOT_Tracking_Baselines/baselines/logs/finetune/lt_lora20.log#L9-L37) 中，`config_ocean` 的实际生效参数均为：
     ```yaml
     FLIP: 0.5
     SCALE: 0.1
     SCALEs: 0.0
     SHIFT: 0.1
     SHIFTs: 0.0
     ```
  2. 查看 [dataset_base.py](file:///D:/PyProjects/SOT_Tracking_Baselines_官方权重试跑/algorithms/LightTrack/lib/dataset/dataset_base.py#L49-L51) 与 `_augmentation`（第 326~355 行）：
     - 搜索分支（`search=True`）使用的位移与尺度参数是 `self.shift_s = cfg.OCEAN.DATASET.SHIFTs` 与 `self.scale_s = cfg.OCEAN.DATASET.SCALEs`；
     - 当 `SHIFTs = 0.0` 且 `SCALEs = 0.0` 时，每个搜索帧裁剪时的随机位移 `param.shift = (0.0, 0.0)`，随机缩放 `param.scale = (1.0, 1.0)`！
     - 因为 `crop511` 离线切片本身以 GT 中心为图像正中心 `(255, 255)`，当 `shift = (0, 0)` 时，**所有训练样本的搜索图像中，目标永远100%固定在正中心 `(128, 128)`**！
     - 更严重的是，分类标签 `out_label = self._dynamic_label([16, 16], dag_param.shift=(0, 0), rPos=1)` **在所有样本上完全恒定不变**（永远只有中心 `(8,8)` 十字 5 个点为 1，其余全为 0）！
  3. 同时，[dataset_base.py](file:///D:/PyProjects/SOT_Tracking_Baselines_官方权重试跑/algorithms/LightTrack/lib/dataset/dataset_base.py#L56-L63) 中 `self.transform_extra` 在 `__init__` 时按 `self.flip > random.random()`（50% 概率）静态固化了 `RandomHorizontalFlip()`，且 `transform_extra` 只翻转了 `image` 像素而**根本没有翻转 `bbox` 坐标和 `dag_param.shift`**！
* **直接后果**：
  - 在所有 `LightTrackM` 微调日志中，分类损失 `cls` 在第 1 轮前 50 步就降到 `0.0019~0.0093`，第 2 轮起直接变为 **`0.0000`**（如 `lt_uav100.log` 第 81 轮 `cls 0.0000`）；
  - 网络学会了**完全忽略模板与搜索图的互相关匹配特征**，直接在分类头偏置项输出固定中心单峰、在回归头输出恒定均值尺寸；
  - 这完整解释了为什么异机做 R4（从零重训头 30 轮 / 120 轮 / 关 EMA 8 轮）时，连 5 条训练序列自测 AUC 都只有 `0.1000~0.1324` 且预测框近似恒定在中心（CLE > 660 px）。**根本原因是 `SHIFTs=0, SCALEs=0` 导致训练目标退化为常量，而非“160 条序列不够学出模板匹配”**。

### 缺陷 2（严重）：`NanoTrack-V1/V2/V3`、`SiamCAR`、`SiamRPN++`、`SiamGAT` 微调学习率与 BN 策略激进，破坏预训练特征统计量
* **代码与日志证据**：
  1. 在 [configv3_casia_ft.yaml](file:///D:/PyProjects/SOT_Tracking_Baselines/baselines/finetune/configs/nanotrack/configv3_casia_ft.yaml#L8-L10)、[siamcar_r50_casia_ft.yaml](file:///D:/PyProjects/SOT_Tracking_Baselines/baselines/finetune/configs/siamcar/siamcar_r50_casia_ft.yaml#L8-L10)、[siamrpn_r50_casia_ft.yaml](file:///D:/PyProjects/SOT_Tracking_Baselines/baselines/finetune/configs/siamrpnpp/siamrpn_r50_casia_ft.yaml#L8-L10) 中，全部设置了 `TRAIN_EPOCH: 999`；
  2. 在 `pysot` / `NanoTrack` / `SiamCAR` 的 `train.py` 逻辑中，只有当 `epoch >= cfg.BACKBONE.TRAIN_EPOCH` 时才会调用 `model.train()` 的骨干解冻逻辑；但 `model.train()` 默认把骨干所有 `BatchNorm2d` 置于 `train()` 模式（除非显式遍历冻结 BN running_mean/running_var），而单源 CASIA（天空/树林单调背景）直接冲刷了 ImageNet/GOT-10k 预训练的 BN 统计量；
  3. 同时，`SiamCAR` 和 `SiamRPN++` 配置了 `NEG: 0.0`（0% 负样本对），导致分类器在微调期间从未见过异源负样本对，抗背景杂波判别力下降。

### 缺陷 3（严重）：`FEAR-XS` 自建训练脚本 `train_fear.py` 的 Albumentations 2.x 兼容性缺陷与双模板分支失配
* **代码与日志证据**：
  1. 在 [fear_ft.log](file:///D:/PyProjects/SOT_Tracking_Baselines/baselines/logs/finetune/fear_ft.log#L11-L18) 等所有 FEAR 训练日志中，每个 worker 均持续抛出警告：
     `UserWarning: Got processor for bboxes, but no transform to process it.`
     原因在于 [tracking_dataset.py](file:///D:/PyProjects/SOT_Tracking_Baselines_官方权重试跑/algorithms/FEARTracker/model_training/dataset/tracking_dataset.py#L170-L173) 中 `self.common_transforms` 包含 `A.Downscale(scale_min=0.5, scale_max=0.5, p=0.2)` 等旧版 Albumentations 算子，在 Albumentations 2.x 下 `bbox_params` 处理器签名变化；
  2. 在 [train_fear.py](file:///D:/PyProjects/SOT_Tracking_Baselines/baselines/finetune/train_fear.py#L128) 中，显式设置了 `"negative_ratio": 0.0`，关闭了负样本采样；
  3. 在 [train_fear.py](file:///D:/PyProjects/SOT_Tracking_Baselines/baselines/finetune/train_fear.py#L166) 中，`CosineAnnealingLR(optimizer, T_max=args.epochs)` 按 **epoch** 步进（每轮只调 1 次学习率），而非按 iteration 步进，且无 warmup，导致首轮全量 500~1875 步恒定以最高学习率冲击预训练头部。

### 缺陷 4（口径混淆）：`CASIA val` 评测脚本存在两套截然不同的帧数截断口径，导致异机报告误判 `lowlr` 增益
* **代码与日志证据**：
  1. 在 Batch 2 初始选优阶段，[lt_val_official.log](file:///D:/PyProjects/SOT_Tracking_Baselines/baselines/logs/finetune/lt_val_official.log) 与 [lt_lowlr_val_ep2.log](file:///D:/PyProjects/SOT_Tracking_Baselines/baselines/logs/finetune/lt_lowlr_val_ep2.log) 使用了 `--seq-stride 4 --max-frames 300`（17 序列，**4,957 帧**）：
     - 官方权重 AUC = **`0.5410`**；
     - `lighttrack_lowlr/lighttrack_ep2.pth` AUC = **`0.5293`**（实际比同口径官方权重**低 0.0117**）。
  2. 在 P0/P1/P2 重训阶段，[lt_off_s4.json](file:///D:/PyProjects/SOT_Tracking_Baselines/baselines/logs/p0_eval/lt_off_s4.json) 使用了 `--seq-stride 4 --max-frames 0`（17 序列全帧，**15,805 帧**）：
     - 官方权重 AUC = **`0.4709`**。
  3. 异机报告 [反无人机微调负迁移分析与重训方案.md](file:///D:/PyProjects/SOT_Tracking_Baselines/反无人机微调负迁移分析与重训方案.md#L161-L170) 将 `max-frames=300` 测出的 `0.5293` 与 `max-frames=0` 测出的官方 `0.4709` 直接相减，误以为 `lowlr` 取得了 `+0.058` 的全项目最高增益并写下“尚未盲测”；实际上该权重早已作为第二批 `checkpoints_finetuned/lighttrack/best.pth` 在 DUT 上测过（[batch2/lighttrack.log](file:///D:/PyProjects/SOT_Tracking_Baselines/baselines/logs/batch2/lighttrack.log#L50)，DUT AUC = **`0.3013`**，暴跌 `-0.2156`）。

### 缺陷 5（长训浪费）：正在运行的 `R6 UAV-only 100 ep` (`lt_uav100.log`) 仍在沿用错误的 `SHIFTs=0.0` 配置空转
* 截至 `2026-11-16 16:00` Git 提交时，异机后台仍在运行 `lt_uav100`（已跑至第 84 轮，每轮约 140 秒，耗时逾 3 小时），日志显示 `cls 0.0000 | reg 0.0401`，且 `WHICH_USE: ['GOT10K']`（指向 `CASIA-Anti-UAV300/ocean` 下的软链，但 `SHIFTs: 0.0, SCALEs: 0.0`），训出的权重必然同样是中心常值退化解。

---

## 四、 客观真实的领域规律总结（剥离代码 Bug 后的真实物理事实）

在剔除上述代码级 Bug（特别是 `LightTrackM` 的 `SHIFTs=0` 退化与 `FEAR` 的无 warmup/无负样本问题）后，从正常跑通官方训练管线的模型（`OSTrack`、`TransT`、`SiamGAT`、`SiamCAR`、`SiamRPN++`、`MixFormer`、`NanoTrack`）中，依然可以提取出 3 条真实的客观事实：

1. **CASIA-Anti-UAV300 与 DUT-Anti-UAV-Val 存在显著跨数据集域差（Target Scale & Sequence Length Shift）**：
   - `CASIA train/val` 目标中位尺寸 $\sqrt{w\cdot h} \approx 80\sim 85\text{ px}$（偏中近景无人机，序列长 $\le 1000$ 帧）；
   - `DUT-Anti-UAV-Val` 目标中位尺寸 $\sqrt{w\cdot h} \approx 56.3\text{ px}$（面积小约 50%，p10 仅 $36.7\text{ px}$，且平均序列长 $1240$ 帧、最长 $2635$ 帧）；
   - 因此，单纯在 `CASIA`（160 条序列）上做全参数微调并以 `CASIA val` 选优，会让模型向“80px 中等尺度目标 + CASIA 背景”收缩，进而在 `DUT`（56px 弱小目标 + 长航迹）上产生域偏移。
2. **微调抗遗忘能力与模型特征容量强相关**：
   - `OSTrack-Base`（86M，ViT-B 全局注意力）在 CASIA 微调后，CASIA val 从 `0.7808` 升至 `0.8148`，DUT 盲测 AUC 从 `0.6388` 提升至 **`0.6568`**（`+0.0180`），Mean CLE 从 `126.92 px` 降至 **`90.30 px`**，Lost Rate 从 `17.31%` 降至 **`16.25%`**；
   - `TransT`（23M）DUT 盲测 AUC 从 `0.5393` 提升至 **`0.5474`**（`+0.0081`）；
   - 50M 级 CNN（`SiamRPN++`、`SiamCAR`）微降 `1.5%~1.9%`；
   - 0.3M~0.55M 超轻量 `NanoTrack` 微调后在 CASIA val 上上涨 `+2.1%~+5.2%`，但在 DUT 上下跌 `-6.5%~-7.4%`，证明超小容量 CNN 在单一小数据集（160 序列）上微调极易发生源域过拟合与通用特征遗忘。
3. **本机工程主线（方案 04 / 05）完全不受负面影响，反而获得了更有力的论文实证支撑**：
   - 本机 `RGB_PTZ_Integrated` 的 **方案 04（WP0~WP2 已闭环，LightTrackM 官方权重导出 ONNX + C++ DirectML 1.86ms 落地）** 采用的正是官方预训练权重（AUC 52.24%，Lost Rate 14.16%），工程决策完全正确；
   - 异机的第一批 14 款基线零样本评测已 100% 完成，图表与三线表已齐备；
   - 异机的第二批微调实验（修正 LightTrack Bug 前后）揭示了一个极具学术价值的论文切入点：**为什么在反无人机任务中，直接拿百余段小规模无人机视频对轻量级 SOT 做常规微调会导致严重的“源域过拟合与跨域负迁移”？**

---

## 五、 核心待决策议题（供讨论）

1. **议题一：异机后台正在跑的 `lt_uav100`（100 轮错误配置长训）是否立即叫停，并修复 `train_lighttrack.py` 的 `SHIFTs=64, SCALEs=0.18` Bug？**
   - 修复只需在 `config_ocean.py` / `train_lighttrack.py` 中补齐 `SHIFTs=64.0, SCALEs=0.18` 并修正 `FLIP` 逻辑，即可验证 LightTrackM 在正确的数据增强下微调与从头训头的真实表现（消除因配置 Bug 导致的“假性退化解”）。
2. **议题二：异机上的核心主线任务——自研模型 `Rep-LightTrack V2`（`scripts/train_sot_v2.py`）尚未启动，下一步如何安排？**
   - 异机具备 RTX 4080 SUPER，且我们已经有了全套写好的 `models/sot/rep_lighttrack_v2.py` 与 `scripts/train_sot_v2.py`；
   - 鉴于第二批基线实验已经证明“仅靠 CASIA 160 条序列单源训练/高比例微调会因为尺度差异（85px vs 56px）和场景单一在 DUT 上过拟合”，我们在启动 `Rep-LightTrack V2`（或对 LightTrack 进行改进）时，是否直接结合此次经验调整训练配方（例如：**提高 GOT-10k 通用样本混合比例、在训练中加入针对 DUT 小目标尺度的缩放增强、或引入 `OSTrack-Base` (AUC 0.6568) 作为教师网络进行响应图/特征蒸馏**）？
3. **议题三：硕士论文实验叙事的定位調整**
   - 异机完成的“14 款基线官方权重 vs CASIA 微调权重对比”是否直接作为论文第 4 章的先导实验（Motivation Experiment），用来论证“常规单源微调在超轻量跟踪器上的灾难性遗忘与尺度域偏移现象”，从而引出我们的改进方法（结构重参数化 + Stride 8 密集网格 + 小目标损失/混合域正则 + AMFT 检测协同兜底）？

