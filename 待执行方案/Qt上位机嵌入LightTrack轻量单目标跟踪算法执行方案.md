# Qt上位机嵌入LightTrack轻量单目标跟踪算法执行方案

> **文件性质**：工程实施规范（供执行代理逐包实施）  
> **归属工程**：[RGB_PTZ_Integrated](file:///d:/PyProjects/RGB_PTZ_Integrated)  
> **关联方案**：[04_轻量单目标跟踪基准模型交付与工程链路跑通方案.md](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/04_轻量单目标跟踪基准模型交付与工程链路跑通方案.md)、[05_光电跟踪系统RK3588边缘端量化与工程移植实操方案.md](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/05_光电跟踪系统RK3588边缘端量化与工程移植实操方案.md)  
> **算法事实源**：`D:\PyProjects\SOT_Tracking_Baselines_官方权重试跑\algorithms\LightTrack\lib\tracker\lighttrack.py`、`...\lib\utils\utils.py`（`get_subwindow_tracking`、`python2round`、`cxy_wh_2_rect`）、`...\experiments\test\OTB\LightTrack.yaml`；评测协议事实源 `...\evaluation\run_eval.py`、`...\evaluation\metrics.py`、`...\evaluation\trackers\lighttrack_adapter.py`

---

## 一、执行规程

1. 工作包（WP）按 WP0 → WP7 顺序执行，每个 WP 达到「完成判据」后执行 `git add` / `git commit`（语义化提交）/ `git push origin main`，并刷新 [checkpoint.md](file:///d:/PyProjects/RGB_PTZ_Integrated/checkpoint.md)（先将旧检查点归档至 `checkpoints/archive/`，仅保留最新一条）。
2. 命令行为 Windows PowerShell，语句分隔使用 `;`，不使用 `&&`。
3. 文档与代码注释仅陈述当前设计与客观数据，不写历史修订、不写主观评价。
4. 指标汇报必须同时给出 AUC、DP@20、Mean CLE、Lost Rate、SR@0.50、SR@0.75 与时延，未达标项原样汇报并归因。
5. 不修改 [amftevaluator.h](file:///d:/PyProjects/RGB_PTZ_Integrated/amftevaluator.h) / [.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/amftevaluator.cpp) 与 [dsstcore.h](file:///d:/PyProjects/RGB_PTZ_Integrated/dsstcore.h) / [.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/dsstcore.cpp)；DSST 内核既有行为保持不变。
6. 第十四节所列停止条件出现时，停止后续工作并向用户汇报，不自行放宽验收阈值。
7. `.gitignore` 已忽略 `models/*.onnx`、`weights/`、`*.pth`、`build/`、`*.exe`：模型与权重为本地资产，由脚本重建，不入库；`per_frame.csv` 类大体积产物不入库，汇总 JSON/CSV 入库。

---

## 二、固定路径与命令

| 项 | 值 |
| :--- | :--- |
| 工程根目录 | `D:\PyProjects\RGB_PTZ_Integrated` |
| Python | `D:\miniconda3\envs\anti_uav_yolo\python.exe`（torch 2.14.1+cu130，已含 `onnx`、`onnxruntime`（仅 CPU EP）、`easydict`、`yaml`、`scipy`、`cv2`；`onnxsim` 未安装） |
| LightTrack 源码根 | `D:\PyProjects\SOT_Tracking_Baselines_官方权重试跑\algorithms\LightTrack` |
| 官方权重 | `D:\PyProjects\SOT_Tracking_Baselines_官方权重试跑\checkpoints\lighttrack\LightTrackM.pth` |
| 工程内权重归档 | `weights/sot/lighttrack_m.pth` |
| 数据集 | `datasets/Anti-UAV/tracking/Anti-UAV-Tracking-V0/videoXX/*.jpg`，`datasets/Anti-UAV/tracking/Anti-UAV-Tracking-V0GT/videoXX_gt.txt`（XX = 01…20） |
| Qt / MinGW | `D:\Qt\6.12.0\mingw_64\bin`、`D:\Qt\Tools\mingw1310_64\bin` |
| 主工程构建 | 按 [README.md](file:///d:/PyProjects/RGB_PTZ_Integrated/README.md) 第七节：`qmake.exe RGB_PTZ_Integrated.pro "CONFIG+=release"` → `mingw32-make.exe -j8`，产物 `build/release/` |
| 运行环境 PATH | `$env:PATH = "D:\Qt\6.12.0\mingw_64\bin;D:\Qt\Tools\mingw1310_64\bin;D:\PyProjects\RGB_PTZ_Integrated\build\release;" + $env:PATH` |

Python 脚本统一在工程根目录执行；`SOT_Tracking_Baselines` 目录中的源码只读引用，不修改。

---

## 三、基准事实

### 3.1 网络张量形状（`LightTrackM_Subnet`，`stride=16`，权重加载 missing keys = 0，实测）

| 输入 | 输出 |
| :--- | :--- |
| 模板 `[1,3,127,127]` | `zf = [1,96,8,8]` |
| 搜索 `[1,3,255,255]` 或 `[1,3,256,256]` | `cls [1,1,16,16]`，`reg [1,4,16,16]` |
| 搜索 `[1,3,288,288]` | `cls [1,1,18,18]`，`reg [1,4,18,18]` |

### 3.2 评测所用超参数（`LightTrack.yaml` OTB 段 + `lighttrack_adapter.py` 覆写）

| 参数 | 值 |
| :--- | :--- |
| `exemplar_size` | 127 |
| `instance_size` | `目标面积 / 图像面积 < 0.004` → 288（`big_sz`），否则 256（`small_sz`） |
| `total_stride` / `score_size` | 16 / `round(instance_size / 16)`（256 → 16，288 → 18） |
| `context_amount` | 0.5 |
| `penalty_k` / `window_influence` / `lr` | 0.15 / 0.45 / 0.35 |
| 输入归一化 | RGB，`x/255`，减 `(0.485,0.456,0.406)`，除 `(0.229,0.224,0.225)` |
| 输出 | `cls` 为 logit（评测代码内做 sigmoid），`reg` 单位为搜索图像素 |

### 3.3 Anti-UAV 20 组序列的 `instance_size` 分布（首帧 GT 面积比，实测）

288：video02/04/05/06/09/10/12/13/14/15/16/17/18/19/20（15 组）；256：video01/03/07/08/11（5 组）。

### 3.4 已发布基准（LightTrackM 官方权重，20 组序列 OPE）

| 来源 | AUC | DP@20 | Mean CLE | Lost Rate | SR@0.50 | SR@0.75 | 说明 |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| 基准 A：`SOT_Tracking_Baselines_官方权重试跑\results\benchmark_summary.csv` | 52.24% | 66.06% | 131.49 px | 14.16% | 64.98% | 41.68% | Windows 本机，26.88 ms / 37.2 FPS（计时含 `cv2.imread` 与 Python 前后处理） |
| 基准 B：`SOT_Tracking_Baselines\baselines\results\benchmark_summary.csv` | 51.69% | 65.31% | 146.74 px | 15.57% | 64.29% | 41.18% | 16.66 ms 模型时延 / 50.9 E2E FPS |

两组基准使用同一权重、同一超参、同一数据，AUC 相差 0.55 点、DP@20 相差 0.75 点、Mean CLE 相差 15.25 px（11.6%）、Lost Rate 相差 1.41 点。该差异为同一算法在不同硬件/数值环境下的跟踪轨迹发散量，作为闭环指标对齐容差的依据（见 4.2 节）。

### 3.5 评测协议（`run_eval.py` + `metrics.py`）

| 项 | 规则 |
| :--- | :--- |
| 帧序 | 每序列按文件名排序，`n = min(帧数, GT 行数)` |
| 初始化 | 第 0 帧 + `GT[0]`；`preds[0] = GT[0]` |
| 跟踪 | 第 1…n-1 帧逐帧 `track`；图像读取失败时复用上一帧预测 |
| 有效帧 | GT 的 `w>0 且 h>0` 的帧（含第 0 帧）；全部序列的有效帧合并后统一统计 |
| 输出框 | `cxy_wh_2_rect`：`[max(0, cx - w/2), max(0, cy - h/2), w, h]`（左上角钳制 ≥ 0，宽高不钳制） |
| AUC | 阈值 `linspace(0,1,21)` 上成功率的均值 |
| DP@20 | CLE ≤ 20 px 的帧比例（阈值 `0…50` 整数的精度曲线第 20 项） |
| Mean CLE | 预测框与 GT 框中心欧氏距离均值 |
| Lost Rate | IoU == 0 的帧比例 |
| SR@0.50 / SR@0.75 | IoU ≥ 0.5 / 0.75 的帧比例 |
| 计时 | 区间 `imread + track`（E2E）；纯跟踪区间单独累计 |

`tools/test_amft.cpp` 的 AUC 采用 101 个阈值，与本协议不同；本方案所有 LightTrack 指标采用上表 21 阈值定义。

---

## 四、判据与取值依据

### 4.1 时延判据

时延按三个口径分别记录：

| 编号 | 口径 | 定义 |
| :---: | :--- | :--- |
| T1 | 搜索分支纯前向 | 仅 `OrtApi::Run`（搜索 Session）前后 `steady_clock` 差，输入输出为 CPU 内存张量，含 DirectML 主机↔设备拷贝；256、288 分别统计均值/P50/P99 |
| T2 | 单帧 `track()` 总耗时 | 含裁切、张量化、`Run`、解码；P99 统计 |
| T3 | 整机处理耗时 | `ObjectTracker::update()` 全过程（含 `QImageToMat`、AMFT 评估、KF），由飞行记录 `proc_ms` 列统计 |

判据取值：

| 项 | 硬门限 | 目标值（仅记录，未达成不判失败） |
| :--- | :---: | :---: |
| T1 均值 | ≤ 5.0 ms | ≤ 2.5 ms |
| T1 P99 | ≤ 8.0 ms | — |
| T2 P99 | ≤ 10.0 ms | — |
| T3 均值 | ≤ 16.0 ms（对应处理能力 ≥ 60 FPS） | — |
| 模板分支单次 | ≤ 10 ms（预热后） | — |

取值依据：云台伺服控制周期 ≤ 20 ms；既有检查点以 ≤ 5 ms 作为机电伺服硬实时要求；方案 05 的 RK3588 目标为 ≤ 5.0 ms。2.1 ms 的计时口径在工程内无对应脚本，以 T1 的 C++ 实测为准，并在汇报中同时给出 T1/T2/T3 与对应 EP（DirectML、CPU）。T1 超过 5.0 ms 时的处置顺序：① 确认 DirectML EP 实际生效且无 CPU 回退（`--strict-gpu`）；② 改用 `IoBinding` 将输入输出绑定到设备内存；③ 仍超限则按停止条件汇报。

### 4.2 精度判据（容差由 3.4 节两组基准的差异量标定）

| 层级 | 比较对象 | 通过标准 |
| :---: | :--- | :--- |
| L1 | PyTorch 与 ONNX Runtime CPU（随机输入，逐张量） | 最大绝对误差 / `max|ref|` ≤ 1e-5（实测 PyTorch 与 ORT CPU 相对误差 ≤ 2.6e-6） |
| L2 | Python 闭环：PyTorch 后端 vs ONNX 后端（同机，本地基准） | \|ΔAUC\| ≤ 0.5 点；\|ΔDP@20\| ≤ 0.75 点；\|ΔLost\| ≤ 1.0 点；Mean CLE 相对差 ≤ 7.5% |
| L3 | C++ 开环回放（以 Python 逐帧「跟踪前状态」为输入，比较单步输出） | 见 8.3 节 |
| L4 | C++ 闭环 vs 本地 PyTorch 基准 | \|ΔAUC\| ≤ 1.0 点；\|ΔDP@20\| ≤ 1.5 点；\|ΔLost\| ≤ 2.0 点；Mean CLE 相对差 ≤ 15%；SR@0.50 / SR@0.75 绝对差 ≤ 1.5 点 |

闭环跟踪对数值扰动敏感（3.4 节同算法不同环境即有表中差异），因此 L3 开环回放为数值正确性的判据，L4 闭环指标作为整体一致性判据。L3 通过而 L4 超限时，运行 `--cpu` 闭环交叉验证并向用户汇报，不放宽 L4。

### 4.3 搜索图规格

15/20 序列使用 288 搜索图，单一 255 输入无法复现 3.4 节基准。搜索分支导出为 256 与 288 两份纯静态计算图，初始化时按 3.2 节面积比规则选择其一，跟踪期间不切换。评测配置 `small_sz=256`、`big_sz=288` 覆盖 `Config` 类默认的 `instance_size=255`，因此小搜索图取 256，输出 16×16。

### 4.4 `cls` 输出形式

ONNX 搜索图输出 `cls_logit`（未经 sigmoid），`reg_dist`；sigmoid 在 C++ 与 Python 后处理中执行。Python 评测代码直接复用 `Lighttrack.update`（其内部含 sigmoid），保证参考实现零改动。

---

## 五、WP0：权重归档、Python 参考实现、ONNX 导出与校验

### 5.1 权重归档

```
Copy-Item "D:\PyProjects\SOT_Tracking_Baselines_官方权重试跑\checkpoints\lighttrack\LightTrackM.pth" weights\sot\lighttrack_m.pth
```

### 5.2 导出脚本 `scripts/export_lighttrack_onnx.py`

参数：`--lighttrack-root`（默认见第二节）、`--weights`（默认 `weights/sot/lighttrack_m.pth`）、`--out-dir`（默认 `models`）。

```python
import os, sys, argparse, torch, torch.nn as nn

PATH_NAME = "back_04502514044521042540+cls_211000022+reg_100000111_ops_32"
MEAN = torch.tensor([0.485, 0.456, 0.406]).view(1, 3, 1, 1)
STD  = torch.tensor([0.229, 0.224, 0.225]).view(1, 3, 1, 1)

class TemplateBranch(nn.Module):
    def __init__(self, net):
        super().__init__(); self.net = net
        self.register_buffer("mean", MEAN); self.register_buffer("std", STD)
    def forward(self, template_img):                       # RGB, [0,1]
        return self.net.features((template_img - self.mean) / self.std)

class SearchBranch(nn.Module):
    def __init__(self, net):
        super().__init__(); self.net = net
        self.register_buffer("mean", MEAN); self.register_buffer("std", STD)
    def forward(self, search_img, template_feat):
        xf = self.net.features((search_img - self.mean) / self.std)
        zf, xf = self.net.neck(template_feat, xf)
        oup = self.net.head(self.net.feature_fusor(zf, xf))
        return oup["cls"], oup["reg"]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lighttrack-root", default=r"D:\PyProjects\SOT_Tracking_Baselines_官方权重试跑\algorithms\LightTrack")
    ap.add_argument("--weights", default="weights/sot/lighttrack_m.pth")
    ap.add_argument("--out-dir", default="models")
    a = ap.parse_args()
    sys.path.insert(0, a.lighttrack_root)
    import lib.models.models as models
    from lib.utils.utils import load_pretrain
    net = models.__dict__["LightTrackM_Subnet"](PATH_NAME, stride=16)
    net = load_pretrain(net, a.weights, print_unuse=False).eval()
    os.makedirs(a.out_dir, exist_ok=True)
    kw = dict(opset_version=17, dynamo=False)               # TorchScript 导出器，opset 17，全静态
    torch.onnx.export(TemplateBranch(net).eval(), torch.rand(1, 3, 127, 127),
                      os.path.join(a.out_dir, "lighttrack_template.onnx"),
                      input_names=["template_img"], output_names=["template_feat"], **kw)
    for xs in (256, 288):
        torch.onnx.export(SearchBranch(net).eval(), (torch.rand(1, 3, xs, xs), torch.rand(1, 96, 8, 8)),
                          os.path.join(a.out_dir, f"lighttrack_search_{xs}.onnx"),
                          input_names=["search_img", "template_feat"],
                          output_names=["cls_logit", "reg_dist"], **kw)

if __name__ == "__main__":
    main()
```

该导出写法已在本机实测通过：opset_import 仅含 17；模板图算子集合 `{Conv, Sigmoid, Mul, Add, Sub, Div, Clip, Relu, GlobalAveragePool, Constant}`；搜索图另含 `{BatchNormalization, Reshape, Transpose, MatMul}`；输入输出无 `dim_param`。`dynamo=False` 参数不被接受（torch 版本差异）时，改用默认导出器并执行 `onnx.version_converter.convert_version(model, 17)`，再重复 5.3 的校验。

### 5.3 导出校验 `scripts/verify_lighttrack_onnx.py`

| 编号 | 检查项 | 通过标准 |
| :---: | :--- | :--- |
| E1 | 张量签名 | 输入输出名称/形状与 6.1 节表格完全一致，无动态维度 |
| E2 | 算子集合 | 不含 `Loop`/`Scan`/`If`/自定义域；`opset_import` 版本 = 17 |
| E3 | 数值对齐 | 3 组随机输入，PyTorch（`net.template` + `net.track`，外置归一化）与 ORT CPU 逐张量：绝对误差 / `max|ref|` ≤ 1e-5（`template_feat`、`cls_logit`、`reg_dist` 分别统计） |

### 5.4 Python 参考评测 `scripts/lighttrack_ref_eval.py`

参数：`--backend {torch,onnx}`、`--lighttrack-root`、`--weights`、`--onnx-dir models`、`--data-root datasets/Anti-UAV/tracking`、`--out results/lighttrack/<backend>`、`--seqs`（可选，逗号分隔，调试用）。

实现要求：

1. `from lib.tracker.lighttrack import Lighttrack`，`info` 使用 `types.SimpleNamespace(arch="LightTrackM_Subnet", dataset="OTB", epoch_test=False, stride=16)`，`tracker = Lighttrack(info, even=0)`；`tracker.init(...)` 后设置 `state['p'].window_influence = 0.45`、`penalty_k = 0.15`、`lr = 0.35`；图像在送入前 `cv2.cvtColor(BGR2RGB)`；`track` 次序与 `lighttrack_adapter.py` 一致。
2. `torch` 后端：`models.__dict__["LightTrackM_Subnet"](PATH_NAME, stride=16)`，`load_pretrain`，`.eval().cuda()`，与 `lighttrack_adapter.py` 相同。
3. `onnx` 后端：包装类提供 `template(z)`（`z.cpu().numpy()` → 模板 Session → 保存 `zf`）与 `track(x)`（按 `x.shape[-1]` 选择 256/288 Session，返回 `torch.from_numpy(cls_logit)`、`torch.from_numpy(reg_dist)`）；`Lighttrack` 子类重写 `normalize(self, x): return x / 255`（ImageNet 归一化已在计算图内）；ORT 使用 `CPUExecutionProvider`。
4. 对 `lib.tracker.lighttrack.get_subwindow_tracking` 设置包装函数，记录每次调用的 `pos`、`original_sz`，用于计算裁切参数：`crop_sz = original_sz`，`crop_xmin = round(pos[0] - (original_sz + 1) / 2)`，`crop_ymin` 同理（与 `get_subwindow_tracking` 内表达式一致）。
5. 按 3.5 节协议评测，指标函数按该节定义自行实现（不 import 外部 `metrics.py`）。
6. 输出 `per_frame.csv`，列：`seq,frame,bx,by,bw,bh,inst,crop_sz,crop_xmin,crop_ymin,ax,ay,aw,ah,cls_max,box_x,box_y,box_w,box_h,iou,cle,gt_valid`。其中 `bx,by,bw,bh` 为该帧 `track` 调用前的 `target_pos`/`target_sz`，`ax,ay,aw,ah` 为调用后（含边界钳制）的 `target_pos`/`target_sz`，`cls_max` 为 `state['cls_score'].max()`，`box_*` 为 `cxy_wh_2_rect` 输出，第 0 帧行的 `b*`/`a*` 为 GT 框中心与宽高、`crop_*` 置 0。
7. 输出 `summary.json`：帧数、AUC、DP@20、Mean CLE、Lost Rate、SR@0.50、SR@0.75、P_norm、纯跟踪耗时均值（ms）、E2E 耗时均值（ms）、逐序列同名指标。

### 5.5 WP0 运行顺序与完成判据

```
python scripts\export_lighttrack_onnx.py
python scripts\verify_lighttrack_onnx.py
python scripts\lighttrack_ref_eval.py --backend torch --out results/lighttrack/torch
python scripts\lighttrack_ref_eval.py --backend onnx  --out results/lighttrack/onnx
```

完成判据：E1–E3 通过；`results/lighttrack/torch/summary.json` 的 AUC / DP@20 / Lost Rate 各自落在 `[min(A,B) − 1.0, max(A,B) + 1.0]` 点区间内（否则按停止条件汇报）；L2 通过。`results/lighttrack/**/per_frame.csv` 加入 `.gitignore`，`summary.json` 入库。

---

## 六、ONNX 交付规格

### 6.1 文件与张量

| 文件 | 输入 | 输出 |
| :--- | :--- | :--- |
| `models/lighttrack_template.onnx` | `template_img` `[1,3,127,127]` float32，RGB，[0,1] | `template_feat` `[1,96,8,8]` float32 |
| `models/lighttrack_search_256.onnx` | `search_img` `[1,3,256,256]` float32，RGB，[0,1]；`template_feat` `[1,96,8,8]` | `cls_logit` `[1,1,16,16]`；`reg_dist` `[1,4,16,16]`（l,t,r,b，单位：搜索图像素） |
| `models/lighttrack_search_288.onnx` | `search_img` `[1,3,288,288]`；`template_feat` `[1,96,8,8]` | `cls_logit` `[1,1,18,18]`；`reg_dist` `[1,4,18,18]` |

输入为 RGB 平面（NCHW）、数值 [0,1]；ImageNet 均值方差已固化于计算图。

### 6.2 对方案 05 的交付约束

RKNN 转换时输入归一化配置为 `mean_values=[[0,0,0]]`、`std_values=[[255,255,255]]`（图内已含 ImageNet 归一化）；三份模型分别转换；`cls_logit` 的 sigmoid 与回归解码由 C++ 后处理完成。

---

## 七、WP1：`ISotTracker` 与 `OnnxSotTracker`

### 7.1 抽象接口 `ISotTracker.h`

```cpp
#ifndef ISOTTRACKER_H
#define ISOTTRACKER_H

#include <opencv2/core.hpp>
#include <QString>

struct SotDebugInfo {
    int instanceSize = 0;                 // 256 / 288
    int cropSz = 0, cropXmin = 0, cropYmin = 0;
    int argRow = -1, argCol = -1;         // pscore 峰值位置
    float clsMax = 0.0f;                  // sigmoid(cls) 全图最大值
};

struct SotResult {
    bool valid = false;
    cv::Point2f center;                   // 原图像素，已钳制 x∈[0,W], y∈[0,H]
    cv::Size2f size;                      // 原图像素，已钳制 w∈[10,W], h∈[10,H]
    cv::Rect2f bbox;                      // 评测输出框：(max(0,cx-w/2), max(0,cy-h/2), w, h)
    float score = 0.0f;                   // sigmoid(cls) 在峰值位置的值，[0,1]
    SotDebugInfo dbg;
};

class ISotTracker {
public:
    virtual ~ISotTracker() = default;
    virtual bool ensureLoaded() = 0;                                   // 幂等；首次调用加载模型
    virtual bool isLoaded() const = 0;
    virtual bool init(const cv::Mat& bgr, const cv::Rect2f& bbox) = 0; // BGR CV_8UC3
    virtual SotResult track(const cv::Mat& bgr) = 0;                   // 推进内部状态
    virtual SotResult peek(const cv::Mat& bgr, cv::Point2f center) = 0;// 以给定中心前向一次，不修改内部状态
    virtual void setState(cv::Point2f center, cv::Size2f size) = 0;    // 保留模板，重置位置与尺寸
    virtual bool isInitialized() const = 0;
    virtual float lastModelMs() const = 0;                             // T1
    virtual float lastTotalMs() const = 0;                             // T2
    virtual QString name() const = 0;
};

#endif
```

### 7.2 `onnxsottracker.h` 要点

```cpp
struct SotTrackerConfig {
    QString templateModel     = "models/lighttrack_template.onnx";
    QString searchModelSmall  = "models/lighttrack_search_256.onnx";
    QString searchModelBig    = "models/lighttrack_search_288.onnx";
    bool  useGpu = true;  int deviceId = 0;
    bool  strictGpu = false;                // true：禁止 CPU 回退（session.disable_cpu_ep_fallback=1）
    float penaltyK = 0.15f, windowInfluence = 0.45f, lr = 0.35f;
    float contextAmount = 0.5f, bigSzAreaRatio = 0.004f;
};

class OnnxSotTracker : public ISotTracker {
public:
    explicit OnnxSotTracker(const SotTrackerConfig& cfg = SotTrackerConfig());
    ~OnnxSotTracker() override;
    bool loadModel(const SotTrackerConfig& cfg);   // 显式加载（离线工具使用）
    bool isUsingGpu() const;
    // ISotTracker 全部方法 override
private:
    struct SearchNet {                              // 每个搜索 Session 一份
        OrtSession* session = nullptr;
        int inSize = 0, scoreSize = 0;
        char *inName0=nullptr, *inName1=nullptr, *outName0=nullptr, *outName1=nullptr;
        std::vector<float> gridX, gridY, window;    // scoreSize² 预计算
    };
    // OrtApi/OrtEnv/OrtSessionOptions/OrtMemoryInfo、模板 Session、m_small/m_big/m_active、
    // m_templateFeat(6144)、m_tmplInput/m_srchInput 缓冲、m_avgChans(floor 后的 RGB)、
    // m_pos(cv::Point2d)、m_sz(cv::Size2d)、m_imW/m_imH、std::mutex
};
```

内部状态使用 `double`；网络输入输出使用 `float`。

### 7.3 Session 生命周期

| 阶段 | 操作 |
| :--- | :--- |
| `loadModel` | `OrtGetApiBase()->GetApi(ORT_API_VERSION)` → `CreateEnv` → `CreateSessionOptions`：`SetIntraOpNumThreads(1)`、`SetSessionGraphOptimizationLevel(ORT_ENABLE_ALL)`、`SetSessionExecutionMode(ORT_SEQUENTIAL)`、`DisableMemPattern`（DirectML EP 要求）；`useGpu` 时 `OrtSessionOptionsAppendExecutionProvider_DML(opts, deviceId)`，失败则回退 CPU 并置 `m_usingGpu=false`；`strictGpu` 时 `AddSessionConfigEntry(opts, "session.disable_cpu_ep_fallback", "1")` → 依次创建模板、256、288 三个 Session（共用 Env 与 SessionOptions）→ 查询并缓存节点名（`_strdup` 拷贝）、校验形状与 6.1 节一致（不一致返回 `false`）→ `CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault)` → 预计算网格/余弦窗 → **各 Session 以全零输入预热 3 次**（消除 DirectML 首次编译耗时） |
| `ensureLoaded` | `m_loaded` 为真直接返回；否则以构造时的 `SotTrackerConfig` 调用 `loadModel`；持互斥锁 |
| `init` | 运行模板 Session，输出拷贝至 `m_templateFeat`；按面积比选定 `m_active` |
| `track` / `peek` | 运行 `m_active` 搜索 Session |
| `releaseAll`（析构/重载） | `free` 全部节点名 → `ReleaseSession`（288、256、模板）→ `ReleaseMemoryInfo` → `ReleaseSessionOptions` → `ReleaseEnv` → 指针置空 |

包含 `dml_provider_factory.h` 前定义 `_Maybenull_`、`_Maybenull_string_` 空宏（与 [onnxdetector.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/onnxdetector.cpp) 一致）。Session 创建与 API 调用方式以 `onnxdetector.cpp` 为模板；本模块日志使用 `qWarning` / `qDebug`，不依赖 `logging_categories`（保证离线工具独立链接）。

### 7.4 前处理（逐行对齐 `lighttrack.py` / `get_subwindow_tracking`）

**颜色**：入口 `cv::cvtColor(bgr, rgb, COLOR_BGR2RGB)`；全部裁切在 RGB 上进行。

**通道均值**：`init` 时 `m = cv::mean(rgb)`，`m_avgChans = (floor(m[0]), floor(m[1]), floor(m[2]))`（Python 侧 `avg_chans` 以浮点赋值给 `uint8` 数组，按截断取整）；跟踪期间不更新。

**取整函数**：
- `pyRound2(v)`：四舍五入、半数远离零（`python2round`，正数时等同 `std::round`）；
- `bankersRound(v)`：`std::nearbyint(v)`（默认舍入模式 FE_TONEAREST，对应 Python 3 内建 `round`）。

**尺寸**（`w,h` 为当前 `target_sz`，`ca = 0.5`）：

```
wc_z = w + ca*(w+h);  hc_z = h + ca*(w+h);  s_z = sqrt(wc_z*hc_z)
init  模板裁切边长 = bankersRound(s_z)                           // Python: round(np.sqrt(...))
track scale_z = 127 / s_z                                         // s_z 不取整
      d_search = (I - 127) / 2                                    // I = 256 → 64.5； I = 288 → 80.5
      pad = d_search / scale_z;  s_x = s_z + 2*pad
      搜索裁切边长 = pyRound2(s_x)
```

**裁切 `cropPatch(rgb, pos, sz, modelSz)`**：

```
c      = (sz + 1) / 2.0
xmin   = bankersRound(pos.x - c);   xmax = xmin + sz - 1             // Python: round(pos[0]-c)
ymin   = bankersRound(pos.y - c);   ymax = ymin + sz - 1
padL = max(0, -xmin); padT = max(0, -ymin)
padR = max(0, xmax - W + 1); padB = max(0, ymax - H + 1)
patch  = sz×sz 的 uint8 RGB 图，初值 m_avgChans；将 [xmin,xmax]×[ymin,ymax] 与 [0,W-1]×[0,H-1] 的交集从 rgb 拷入对应位置
if (sz != modelSz) cv::resize(patch, patch, Size(modelSz, modelSz), 0, 0, INTER_LINEAR)
```

仅对交集区域拷贝，不对整幅图像扩边。`sz == modelSz` 时不缩放。

**张量化**：`uint8` → `float / 255` → 平面 NCHW（R 平面、G 平面、B 平面连续）。

### 7.5 后处理（逐行对齐 `Lighttrack.update` / `track`）

符号：`S` = score_size，`I` = instance_size，`k = penaltyK`，`wi = windowInfluence`。

```
tw = w*scale_z;  th = h*scale_z                                      // target_sz * scale_z
cls[r][c] = sigmoid(cls_logit[0,0,r,c])
gx[r][c] = (c - floor(S/2))*16 + floor(I/2);  gy[r][c] = (r - floor(S/2))*16 + floor(I/2)
x1 = gx - reg[0,0,r,c];  y1 = gy - reg[0,1,r,c];  x2 = gx + reg[0,2,r,c];  y2 = gy + reg[0,3,r,c]
pw = x2 - x1;  ph = y2 - y1
szf(a,b)  = sqrt((a + (a+b)/2) * (b + (a+b)/2))
change(q) = max(q, 1/q)
s_c = change( szf(pw,ph) / szf(tw,th) )
r_c = change( (tw/th) / (pw/ph) )
penalty = exp(-(r_c*s_c - 1)*k)
pscore  = penalty*cls
pscore  = pscore*(1-wi) + window[r][c]*wi
(r*,c*) = argmax(pscore)        // 行优先首个最大值；若存在 NaN，取行优先首个 NaN 位置（与 numpy.argmax 一致）
window  = outer(hanning(S), hanning(S)),  hanning(n) = 0.5 - 0.5*cos(2π n/(S-1))   // 不归一化
pxs = (x1+x2)/2 [r*,c*];  pys = (y1+y2)/2 [r*,c*];  pw* = pw[r*,c*];  ph* = ph[r*,c*]
diff_x = (pxs - floor(I/2)) / scale_z;  diff_y = (pys - floor(I/2)) / scale_z
pw' = pw*/scale_z;  ph' = ph*/scale_z                                 // tw,th 除以 scale_z 还原为 w,h
lr_  = penalty[r*,c*] * cls[r*,c*] * 0.35
res_w = pw'*lr_ + (1-lr_)*w;      res_h = ph'*lr_ + (1-lr_)*h
pos_new.x = pos.x + diff_x;       pos_new.y = pos.y + diff_y
w_new = w*(1-lr_) + lr_*res_w;    h_new = h*(1-lr_) + lr_*res_h       // 两次混合，与 lighttrack.py 第 132–136 行一致
钳制：pos.x ∈ [0, W]，pos.y ∈ [0, H]，w ∈ [10, W]，h ∈ [10, H]
输出：score = cls[r*,c*]；bbox = (max(0, cx - w/2), max(0, cy - h/2), w, h)
```

`penalty`/`pscore` 计算含除法，`pw` 或 `ph` ≤ 0 时按 IEEE 浮点语义产生 inf/NaN，与 numpy 行为一致；仅当最终 `pos`/`sz` 为非有限值时本帧返回 `valid=false` 并保持上一帧状态。

### 7.6 `init` / `track` / `peek` / `setState`

- `init(bgr, bbox)`：`bbox` 宽高 < 4 或图像为空返回 `false`；`m_pos = (x+w/2, y+h/2)`，`m_sz = (w, h)`；计算 `m_avgChans`；裁切模板（7.4）→ 模板 Session → 缓存 `m_templateFeat`；按 `bbox.area / (W*H) < bigSzAreaRatio` 选择 `m_active`。YOLO 重捕触发的 `init` 重新判定搜索图规格。
- `track(bgr)`：7.4 搜索裁切 → 搜索 Session（输入 `search_img`、`template_feat`）→ 7.5 解码 → 更新 `m_pos`/`m_sz`；记录 T1（仅 `Run`）与 T2。
- `peek(bgr, center)`：与 `track` 相同，以 `center` 代替 `m_pos`、以当前 `m_sz` 计算 `s_z`，结果写入返回值，不修改成员状态。
- `setState(center, size)`：写入 `m_pos`、`m_sz`（按 7.5 钳制范围），不改变模板与搜索图规格。

### 7.7 异常与线程安全

| 项 | 规范 |
| :--- | :--- |
| `OrtStatus*` | 每次调用检查；非空时 `GetErrorMessage` 写 `qWarning`、`ReleaseStatus`，函数返回失败 |
| `OrtValue` | 每帧输入输出张量以 `std::unique_ptr<OrtValue, Deleter>` 持有，函数退出前释放 |
| 并发 | 全部公开方法持 `m_mutex`；运行期仅由 `ObjectTracker` 所在 worker 线程调用 |
| 输入 | 空图像、非 `CV_8UC3` 输入返回 `valid=false` |

---

## 八、WP2：离线验证工具 `tools/test_sot_tracker.cpp`

### 8.1 构建 `tools/test_sot_tracker.pro`

```pro
QT += core
CONFIG += console c++17 release
CONFIG -= app_bundle debug_and_release debug
TARGET = test_sot_tracker
DESTDIR = $$PWD/../build/release
DEFINES += USE_ONNXRUNTIME
SOURCES += test_sot_tracker.cpp ../onnxsottracker.cpp ../amftevaluator.cpp
HEADERS += ../onnxsottracker.h ../ISotTracker.h ../amftevaluator.h
INCLUDEPATH += $$PWD/.. $$PWD/../opencv/install/include $$PWD/../3rdparty/onnxruntime/include
LIBS += -L$$PWD/../opencv/install/x64/mingw/lib -lopencv_core4100 -lopencv_imgproc4100 -lopencv_imgcodecs4100
LIBS += -L$$PWD/../3rdparty/onnxruntime/lib -lonnxruntime -lDirectML -lpsapi
```

构建（影子构建目录，不干扰主工程 `build/`）：

```
New-Item -ItemType Directory -Force build\tools_sot | Out-Null
Push-Location build\tools_sot
D:\Qt\6.12.0\mingw_64\bin\qmake.exe ..\..\tools\test_sot_tracker.pro "CONFIG+=release"
D:\Qt\Tools\mingw1310_64\bin\mingw32-make.exe -j8
Pop-Location
```

运行脚本 `run_test_sot.ps1`（PATH 设置同 [run_test.ps1](file:///d:/PyProjects/RGB_PTZ_Integrated/run_test.ps1)），工作目录为工程根目录。

### 8.2 命令行与功能

| 参数 | 含义 |
| :--- | :--- |
| `--cpu` / `--gpu`（默认） | 执行提供者 |
| `--strict-gpu` | `strictGpu=true`，出现 CPU 回退时模型加载失败并返回非零 |
| `--seq videoXX[,videoYY]` | 仅评测指定序列 |
| `--out results/lighttrack/cpp` | 输出目录 |
| `--replay results/lighttrack/torch/per_frame.csv` | 开环回放（8.3） |
| `--amft` | 同步运行 `AMFTEvaluator`，输出 10.2 节标定所需列 |

主流程：加载模型（含预热）→ 逐序列按 3.5 节协议评测（`QElapsedTimer` 与 `std::chrono::steady_clock`）→ 输出：
1. 控制台：逐序列 `帧数 | instance_size | AUC | DP@20 | CLE | Lost | T1 均值 ms | E2E FPS`，及全局汇总（AUC、DP@20、Mean CLE、Lost Rate、SR@0.50、SR@0.75、P_norm）；
2. `<out>/per_frame.csv`：列 `seq,frame,bx,by,bw,bh,inst,crop_sz,crop_xmin,crop_ymin,ax,ay,aw,ah,cls_max,box_x,box_y,box_w,box_h,iou,cle,gt_valid,model_ms,total_ms`（`b*` = 调用前状态，`a*` = 调用后状态，与 5.4 节同义）；
3. `<out>/summary.json`：全局与逐序列指标；T1（256、288 分别统计：均值、P50、P99）、T2（P99）、模板分支均值；WorkingSet 与 PrivateUsage 在「加载后 / 第 1 序列后 / 全部序列后」三点采样；
4. 预热：正式计时前对 256、288 搜索 Session 各前向 20 次。

### 8.3 开环回放判据（L3）

`--replay` 读取 Python `per_frame.csv`，对每个序列的第 1…n-1 帧：用 `setState((bx,by),(bw,bh))` 置位 → `track()` → 与 CSV 的 `crop_*`、`ax..ah`、`cls_max`、`inst` 比较（第 0 帧 `init` 后用于确定 `inst`）。

| 编号 | 比较项 | 通过标准 |
| :---: | :--- | :--- |
| L3-1 | `inst`（256/288） | 全部序列一致 |
| L3-2 | `crop_sz`、`crop_xmin`、`crop_ymin` | 完全一致的帧占比 ≥ 99.9%，其余帧各分量偏差 ≤ 1 |
| L3-3 | 裁切参数一致的帧上 `ax,ay` | 最大偏差 ≤ 0.5 px（DirectML）；≤ 0.05 px（`--cpu`） |
| L3-4 | 同上帧 `aw,ah` | 最大偏差 ≤ 0.5 px（DirectML）；≤ 0.05 px（`--cpu`） |
| L3-5 | 同上帧 `cls_max` | 最大偏差 ≤ 1e-3（DirectML）；≤ 1e-4（`--cpu`） |

回放结果输出偏差分布（均值、最大值、超限帧数及前 20 个超限帧的 `seq,frame`）至 `<out>/replay_report.json`。L3-1…L3-5 先以 `--cpu` 验证（隔离 DirectML 数值差异），再以 DirectML 验证。

---

## 九、WP3–WP4：跟踪主干集成

### 9.1 `datatypes` 与 `TrackResult`

[objecttracker.h](file:///d:/PyProjects/RGB_PTZ_Integrated/objecttracker.h) 的 `TrackResult` 末尾新增 `float procMs = 0.0f;`（`update()` 全过程耗时，`QElapsedTimer::nsecsElapsed()/1e6f`，在 Mode A 返回点与末尾返回点赋值）；构造函数初始化为 0。

### 9.2 枚举与 ObjectTracker 接口

```cpp
enum class TrackingKernel { KERNEL_DSST = 0, KERNEL_LIGHTTRACK = 1 };
Q_DECLARE_METATYPE(TrackingKernel)

// ObjectTracker public
void setTrackingKernel(TrackingKernel k);                         // init 之前调用
TrackingKernel trackingKernel() const { return m_kernel; }
void setSotEngine(const std::shared_ptr<ISotTracker>& e) { m_sot = e; }

// ObjectTracker private
struct KernelOutput {
    bool  valid = false;
    cv::Point2f center;
    float rawScore = 0.0f;     // DSST: PSR；LightTrack: sigmoid cls 峰值
    float psrForAmft = 0.0f;   // 送入 AMFTEvaluator::evaluate 的 psr 参数
};
KernelOutput runKernel(const cv::Mat& bgr);
bool reinitKernel(const cv::Mat& bgr, const cv::Rect& roi);
TrackingKernel m_kernel = TrackingKernel::KERNEL_DSST;
std::shared_ptr<ISotTracker> m_sot;
```

`ObjectTracker.h` 包含 `ISotTracker.h`；`OnnxSotTracker` 的具体类型仅由 `TrackingController` 创建，`ObjectTracker` 只依赖抽象接口。

### 9.3 状态映射

LightTrack 内核每帧将 `SotResult.size` 写入 `m_targetW = size.width`、`m_targetH = size.height`，并令 `m_currentScale = 1.0f`；`m_posX/m_posY` 取 `SotResult.center`。`update()` 末尾输出框 `QRectF(m_posX - W*s/2, …)` 与 `getCurrentBBox()` 保持不变。

### 9.4 分派改造点（[objecttracker.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/objecttracker.cpp)）

| 位置（改造前行号） | 改造内容 |
| :--- | :--- |
| `init()` 第 103 行 `m_core.init(image, roi)` | `reinitKernel(image, roi)`；LightTrack 内核下 `ensureLoaded()` 失败返回 `false` |
| `tryDetectAndRecover()` 第 365 行 `m_core.init(image, newRoi)` | `reinitKernel(image, newRoi)` |
| Mode A 正常跟踪（第 540–555 行） | `runKernel(image)`；`psr`、`result.confidence`、`result.psr` 取 `rawScore`；LightTrack 内核下 `m_core.position()/currentScale()` 的读取替换为 9.3 节映射 |
| Mode A PSR 基线判丢（第 558–605 行） | 变量 `m_psrBaseline`/`m_baselineLocked`/`m_psrHistory` 在 LightTrack 内核下存放 `rawScore`（cls 峰值）基线，逻辑（前 30 帧均值、0.75 倍门限、连续 5 帧低于门限进入遮挡）不变 |
| Mode A 遮挡恢复 `reDetect()`（第 519 行调用处） | LightTrack 内核下改为：`r = m_sot->peek(image, KF 预测中心)`；当 `r.valid && r.score > 0.8*基线 && dist(r.center, KF 预测中心) ≤ 2*max(m_targetW, m_targetH)` 时 `m_sot->setState(r.center, r.size)` 并判定恢复；其余沿用原 `m_lostCount ≥ 10` 的触发节奏 |
| Mode B 正常跟踪（第 688–709 行） | `runKernel(image)`；`evalBox` 由 9.3 映射构造；`m_evaluator.evaluate(ko.psrForAmft, ko.valid, evalBox, image)` |
| `setLearningFrozen(...)` 调用点 | 仅 DSST 内核下调用 |
| `modelSize()` | LightTrack 内核返回 `"LightTrack-" + 实例尺寸` |

`runKernel` 内：DSST 分支沿用原 `m_core.update` + `computePSR`，`psrForAmft = rawScore = PSR`；LightTrack 分支 `psrForAmft = 5.0f + 20.0f * rawScore`（使 `computeSConf` 的 `(psr-5)/20` 映射输出恰为 `rawScore`）。

`QImageToMat` 的输出通道顺序在引擎入口约定为 BGR `CV_8UC3`：实施时阅读 [imagematconvert.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/imagematconvert.cpp) 确认；若输出为 RGB 或 4 通道，在 `runKernel`/`reinitKernel` 内先转换为 BGR 3 通道。

### 9.5 TrackingController

- 新增 `setTrackingKernel(TrackingKernel)` / `trackingKernel()`，成员 `m_kernel`、`std::shared_ptr<ISotTracker> m_sotEngine`；
- `setTarget()` 在新建 `ObjectTracker` 之后、`moveToThread` 之前执行：`m_tracker->setTrackingKernel(m_kernel)`；`m_kernel == KERNEL_LIGHTTRACK` 时，若 `m_sotEngine` 为空则 `std::make_shared<OnnxSotTracker>(cfg)`（`cfg` 的三个模型路径经 `resolveModelPath()` 的同一套候选目录规则解析为绝对路径），再 `m_tracker->setSotEngine(m_sotEngine)`；模型加载发生在 worker 线程的 `initSlot` 内（`ensureLoaded()`），不阻塞 UI；
- 内核切换仅影响下一次 `setTarget()`；跟踪进行中切换时，状态栏提示「新内核将在下次框选时生效」；
- `initDone(false, ...)` 的既有处理路径用于「LightTrack 模型加载失败」提示；
- 同一时刻仅一个 `ObjectTracker` 存活（`setTarget` 先销毁旧实例），共享引擎不存在并发调用。

### 9.6 飞行记录（CSV）

[trackingcontroller.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/trackingcontroller.cpp) 的 `startFlightRecord()` 表头与 `recordFlightFrame()` 行末尾追加两列：`proc_ms`（`TrackResult::procMs`）、`kernel`（`DSST` / `LT`）；既有列顺序与含义不变。`psr` 列记录 `TrackResult::psr`（LightTrack 内核下为 cls 峰值）。

### 9.7 UI 与配置

- [mainwindow.ui](file:///d:/PyProjects/RGB_PTZ_Integrated/mainwindow.ui)：在 `groupTrackMode` 的 `horizontalLayout_mode`（`radioModeAuto` 之后）增加 `QComboBox`，对象名 `comboTrackKernel`，选项「跟踪内核: DSST」「跟踪内核: LightTrack」；两种跟踪模式（单目标 / 多目标点击）共用；
- [mainwindow.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/mainwindow.cpp)：`on_comboTrackKernel_currentIndexChanged(int)` → `m_trackingController->setTrackingKernel(...)` 并写 `ConfigManager`；构造函数在 `m_trackingController` 创建后以 `blockSignals` 方式按配置恢复选项并同步控制器；
- `refreshModelList()` 的文件名过滤在 `sot_` 前缀基础上增加 `lighttrack_` 前缀；
- [configmanager.h](file:///d:/PyProjects/RGB_PTZ_Integrated/configmanager.h) / [.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/configmanager.cpp)：新增 `int getTrackKernel() const` / `void setTrackKernel(int)`，沿用既有 `QMutexLocker` + `QSettings` 模式，键 `tracking/kernel`，默认 0；
- 检查 `mainwindow.cpp` / `trackingcontroller.cpp` 中对 `psr`、`confidence` 的使用（状态栏文本、阈值判断）：DSST 的 PSR 量级约 5–30，LightTrack 的 `rawScore` 为 [0,1]；涉及阈值或量纲的位置按内核区分显示，状态栏文本标注「得分」。

### 9.8 工程文件

[RGB_PTZ_Integrated.pro](file:///d:/PyProjects/RGB_PTZ_Integrated/RGB_PTZ_Integrated.pro) 的 `SOURCES` 增加 `onnxsottracker.cpp`，`HEADERS` 增加 `onnxsottracker.h`、`ISotTracker.h`；无需新增库、宏或包含路径；post-link 已复制 `models/*.onnx` 至 `build/release/models/`。

---

## 十、WP5：AMFT 接线与阈值标定

### 10.1 归一化与状态机

`AMFTEvaluator` 代码不修改。置信度映射见 9.4 节。四态状态机（NORMAL / OCCLUDED / REACQUIRED / LOST）、连续 4 帧 `R_t < τ_t` 触发、YOLO 单次全局扫描、重捕后下一帧强制回归 NORMAL 的逻辑沿用，改造点仅为内核分派。

YOLO 重捕成功后的时序：`detect(image)` → `tryDetectAndRecover` 选出 `bestBox` → `reinitKernel(image, newRoi)`（LightTrack：重新选择 256/288、重算模板特征）→ 更新 `m_posX/Y`、`m_targetW/H`、`m_currentScale=1` → `kfUpdate` → `m_evaluator.init(newBox)` → 状态置 `TRACK_REACQUIRED`。变焦联动 `triggerScaleCalibration()` 经同一路径完成。

### 10.2 阈值标定

AMFT 默认 `τ0 = 0.65`、`α = 0.5` 以 DSST 的 PSR 分布标定。LightTrack 的 `S_conf = cls 峰值` 分布不同，按以下流程标定 `τ0`：

1. `test_sot_tracker --amft` 在 20 组序列上输出 `<out>/amft.csv`，列：`seq,frame,iou,cls,s_conf,s_mot,s_bg,r_t,tau_t`（`AMFTEvaluator` 以 `tau0=0.65` 运行，`evaluate(5+20*cls, true, box, image)`）。
2. `scripts/calib_amft_lighttrack.py`：按 `AMFTEvaluator` 公式（窗口 K=10、`σ` 为窗口内 `R_t` 总体标准差、`τ_t = clamp(τ0 − 0.5σ, 0.35, 0.75)`）对 `τ0 ∈ {0.40, 0.45, …, 0.75}` 离线重算 `τ_t`，统计：
   - **失败片段**：GT 有效且 IoU < 0.1 连续 ≥ 4 帧的片段；
   - **触发**：连续 4 帧 `R_t < τ_t`；
   - **误触发率**：好帧（IoU ≥ 0.5）段内触发次数 / 好帧数 × 1000；
   - **覆盖率**：含触发的失败片段 / 失败片段总数；
   - **触发延迟**：失败片段起点到首次触发的帧数中位数。
3. 取值规则：在误触发率 ≤ 1 次 / 1000 帧的 `τ0` 中选覆盖率最大者；若所有 `τ0` 的覆盖率均 < 50%，不自行选值，向用户汇报分布数据。选定值通过 `ObjectTracker` 在 LightTrack 内核下调用 `m_evaluator.setTau0(τ0)`；标定完成前沿用 0.65。
4. `calib_amft_lighttrack.py` 输出 `results/lighttrack/amft_calibration.json` 与逐 `τ0` 的统计表。

---

## 十一、WP6–WP7：回归、整机联调与文档同步

### 11.1 DSST 回归

新增 `tools/test_amft.pro`（`SOURCES`：`test_amft.cpp ../objecttracker.cpp ../dsstcore.cpp ../amftevaluator.cpp ../onnxdetector.cpp ../yolopostprocessor.cpp ../imagematconvert.cpp ../logging_categories.cpp ../onnxsottracker.cpp`；`QT += core gui`；包含路径、库同 8.1 节），构建 `test_amft.exe` 并运行，4 项闭环验收保持 100% PASS；`test_detector.exe` 行为不受影响。

### 11.2 整机联调

1. 主工程 Release 构建 0 错误 0 警告；
2. 内核选择 DSST：单目标框选、多目标点击、遮挡重捕行为与改造前一致；
3. 内核选择 LightTrack：单目标框选 → 实时跟踪 → 云台伺服闭环；多目标点击 → Mode B（AMFT + YOLO 重捕）链路；
4. 飞行记录 CSV 的 `proc_ms` 列统计 T3；`kernel` 列正确。

### 11.3 文档同步

- [04 方案](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/04_轻量单目标跟踪基准模型交付与工程链路跑通方案.md)、[05 方案](file:///d:/PyProjects/RGB_PTZ_Integrated/待执行方案/05_光电跟踪系统RK3588边缘端量化与工程移植实操方案.md) 的 SOT 张量规格已与本方案 6.1 节一致；实施后将实测指标（AUC、DP@20、Mean CLE、Lost Rate、SR、T1/T2/T3、各 EP）回填 04 方案「四、实施清单与验收准则」；
- [README.md](file:///d:/PyProjects/RGB_PTZ_Integrated/README.md) 工具清单增加 `tools/test_sot_tracker.cpp`；
- [checkpoint.md](file:///d:/PyProjects/RGB_PTZ_Integrated/checkpoint.md) 04-4.1 条目勾选并写入实测数据。

---

## 十二、文件清单

### 12.1 新增

| 文件 | 内容 |
| :--- | :--- |
| `weights/sot/lighttrack_m.pth` | 官方权重归档（本地，不入库） |
| `scripts/export_lighttrack_onnx.py` | 三文件导出 |
| `scripts/verify_lighttrack_onnx.py` | E1–E3 校验 |
| `scripts/lighttrack_ref_eval.py` | Python 参考评测（torch / onnx 后端） |
| `scripts/calib_amft_lighttrack.py` | AMFT 阈值标定 |
| `models/lighttrack_template.onnx`、`models/lighttrack_search_256.onnx`、`models/lighttrack_search_288.onnx` | 导出产物（本地，不入库） |
| `ISotTracker.h` | 跟踪引擎抽象接口 |
| `onnxsottracker.h` / `onnxsottracker.cpp` | ONNX Runtime + DirectML 实现 |
| `tools/test_sot_tracker.cpp` / `tools/test_sot_tracker.pro` | 离线验证工具 |
| `tools/test_amft.pro` | `test_amft` 回归构建 |
| `run_test_sot.ps1` | 工具启动脚本 |
| `results/lighttrack/{torch,onnx,cpp}/summary.json` | 汇总指标（入库） |

### 12.2 修改

| 文件 | 改动 |
| :--- | :--- |
| [objecttracker.h](file:///d:/PyProjects/RGB_PTZ_Integrated/objecttracker.h) / [.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/objecttracker.cpp) | `TrackingKernel`、`procMs`、`runKernel`/`reinitKernel`、9.4 节分派点 |
| [trackingcontroller.h](file:///d:/PyProjects/RGB_PTZ_Integrated/trackingcontroller.h) / [.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/trackingcontroller.cpp) | 内核成员与注入、飞行记录两列 |
| [mainwindow.ui](file:///d:/PyProjects/RGB_PTZ_Integrated/mainwindow.ui) / [.h](file:///d:/PyProjects/RGB_PTZ_Integrated/mainwindow.h) / [.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/mainwindow.cpp) | `comboTrackKernel`、模型列表过滤 |
| [configmanager.h](file:///d:/PyProjects/RGB_PTZ_Integrated/configmanager.h) / [.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/configmanager.cpp) | `tracking/kernel` |
| [RGB_PTZ_Integrated.pro](file:///d:/PyProjects/RGB_PTZ_Integrated/RGB_PTZ_Integrated.pro) | `onnxsottracker.cpp`、`onnxsottracker.h`、`ISotTracker.h` |
| `.gitignore` | `results/lighttrack/**/per_frame.csv`、`build/tools_sot/` 已被 `build/` 覆盖 |
| [README.md](file:///d:/PyProjects/RGB_PTZ_Integrated/README.md)、[checkpoint.md](file:///d:/PyProjects/RGB_PTZ_Integrated/checkpoint.md)、04 方案 | 实测回填 |

---

## 十三、工作包与验收矩阵

### 13.1 工作包

| WP | 内容 | 前置 | 完成判据 |
| :---: | :--- | :--- | :--- |
| WP0 | 5 节：权重归档、导出、E1–E3、Python 参考评测（torch/onnx）、L2 | — | E1–E3、L2 通过；torch 参考指标处于基准 A/B 外延 ±1.0 点内 |
| WP1 | 7 节：`ISotTracker.h`、`onnxsottracker.*` | WP0 | 工具工程编译 0 错误 0 警告 |
| WP2 | 8 节：`test_sot_tracker`、开环回放、闭环评测 | WP1 | L3（`--cpu` 与 DirectML）、L4、F1–F3、S1 通过 |
| WP3 | 9.1–9.6、9.8：ObjectTracker 分派、控制器注入、飞行记录 | WP2 | 主工程编译 0 错误 0 警告；DSST 回归 G1 通过 |
| WP4 | 9.7：UI、配置、模型列表过滤 | WP3 | U1–U3 通过 |
| WP5 | 10 节：`--amft` 输出、标定脚本、`τ0` 取值 | WP2、WP3 | 标定 JSON 产出；`τ0` 已写入或已向用户汇报分布 |
| WP6 | 11.1–11.2：回归与整机联调 | WP3–WP5 | F4、S2、S3、U4 通过 |
| WP7 | 11.3：文档与检查点同步 | WP6 | 04 方案回填、README、checkpoint 完成并推送 |

### 13.2 验收指标矩阵

| 编号 | 维度 | 指标 | 通过标准 | 验证方式 |
| :---: | :--- | :--- | :--- | :--- |
| E1 | 导出 | 张量签名 | 与 6.1 节一致，无动态维度 | `verify_lighttrack_onnx.py` |
| E2 | 导出 | 算子集合与 opset | 无控制流/自定义域；opset = 17 | 同上 |
| E3 | 导出 | 数值误差 | 相对 `max|ref|` ≤ 1e-5 | 同上 |
| R1 | 参考 | PyTorch 参考指标 | AUC/DP@20/Lost 在基准 A、B 外延 ±1.0 点内 | `lighttrack_ref_eval.py --backend torch` |
| L2 | 精度 | Python 闭环 torch vs onnx | 4.2 节 L2 容差 | 两份 `summary.json` 对比 |
| L3 | 精度 | C++ 开环回放 | 8.3 节 L3-1…L3-5（`--cpu` 与 DirectML 各一次） | `test_sot_tracker --replay` |
| L4 | 精度 | C++ 闭环 vs torch 参考 | 4.2 节 L4 容差 | `test_sot_tracker` |
| B1 | 编译 | 主工程、`test_sot_tracker`、`test_amft` | Release 0 错误 0 警告 | `qmake` + `mingw32-make` |
| G1 | 回归 | DSST 内核 | `test_amft.exe` 4 项 100% PASS；DSST 单目标/多目标行为与改造前一致 | 运行 + 手动 |
| F1 | 性能 | T1（256、288 分别） | 均值 ≤ 5.0 ms，P99 ≤ 8.0 ms；记录与 2.5 ms 目标的差值 | `test_sot_tracker --strict-gpu` |
| F2 | 性能 | T2、模板分支 | T2 P99 ≤ 10 ms；模板单次 ≤ 10 ms | 同上 |
| F3 | 性能 | EP 回退 | `--strict-gpu` 模型加载成功（零 CPU 回退） | 同上 |
| F4 | 性能 | T3 整机处理耗时 | LightTrack 内核下 Mode A、Mode B 的 `proc_ms` 均值 ≤ 16.0 ms（≥ 1000 帧）；25 fps 输入下无处理积压丢帧 | 飞行记录 CSV |
| S1 | 稳定 | 离线内存 | 20 序列评测期间，「第 1 序列后」到「全部序列后」的 WorkingSet 与 PrivateUsage 增量均 < 5 MB | `summary.json` |
| S2 | 稳定 | 长稳 | 连续跟踪 2 h，进程内存与 GPU 专用显存无单调增长 | 任务管理器 / `nvidia-smi` 采样 |
| S3 | 稳定 | 反复框选 | 200 次框选-停止循环无崩溃、无 Session 泄漏 | 手动 + 日志 |
| U1 | 功能 | 内核切换 | DSST ↔ LightTrack 切换后下一次框选使用新内核，`kernel` 列与 `modelInfo` 正确 | 手动 |
| U2 | 功能 | 模型列表 | `comboAiModel` 不含 `sot_*`、`lighttrack_*` | 手动 |
| U3 | 功能 | 配置持久化 | 重启后内核选择恢复 | 手动 |
| U4 | 功能 | AMFT 协同 | LightTrack 内核下人工遮挡 → OCCLUDED → YOLO 重捕 → REACQUIRED → 下一帧 NORMAL；飞行记录完整 | 实机 + CSV |
| C1 | 标定 | `τ0` | 10.2 节取值规则执行完毕，数据入库 | `calib_amft_lighttrack.py` |

### 13.3 已知约束

1. 精度上限由 LightTrackM 官方权重决定（基准 A/B：AUC 51.7–52.2%，Lost Rate 14.2–15.6%），本方案目标为 C++ 数值与协议对齐；精度提升归属方案 06。
2. OPE 评测（单次初始化、不重捕）与 AMFT + YOLO 协同闭环的指标分别统计，不混报。
3. RK3588 移植时，256、288 两个搜索模型分别量化为 RKNN，选择逻辑在 C++ 层复用 `ISotTracker` 抽象。

---

## 十四、停止条件与汇报格式

出现下列任一情况，停止后续工作包并向用户汇报：

1. WP0 的 R1 超出基准 A/B 外延 ±1.0 点（参考实现本身异常）；
2. E3 数值误差超限且 `dynamo=False` 与默认导出器均无法达标；
3. L3 在 `--cpu` 模式下仍有判据不通过（预处理/后处理实现与 Python 不一致）；
4. `--strict-gpu` 模型加载失败（存在 DirectML 不支持的节点）；
5. 4.1 节处置顺序执行完毕后 T1 仍超过 5.0 ms；
6. 10.2 节所有 `τ0` 的覆盖率均 < 50%；
7. G1 回归失败或 DSST 行为发生变化。

汇报内容：停止点编号、复现命令、实测数值（含 6 项精度指标与 T1/T2/T3）、已执行的排查步骤与归因。
