# LightUAV-SOT 创新架构消融实验与基线性能大表

### 一、学术三线表（Markdown 格式）

| 算法 / 变体架构 | Success (AUC) | Precision (DP@20) | Mean CLE (px) | Model FPS (GPU) | E2E FPS | 参数量 (Params) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **DSST (C++ OpenCV Handcrafted)** | 45.20% | 51.00% | 142.5 | 550.0 | 120.0 | 0.00M |
| **NanoTrack-V3 (ICCV 2021)** | 55.56% | 71.22% | 150.3 | 420.0 | 35.7 | 0.55M |
| **LightTrack-M (ICCV 2021)** | 52.24% | 66.06% | 131.5 | 450.0 | 37.2 | 1.60M |
| **Baseline (No MFFN, No DAC)** | 0.50% | 0.79% | 997.0 | 112.4 | 71.3 | 1.50M |
| **LightUAV + MFFN (No DAC)** | 0.23% | 0.18% | 1111.3 | 101.4 | 66.0 | 1.50M |
| **LightUAV Full Model (+ DAC-Head)** | 22.99% | 75.32% | 167.2 | 224.5 | 107.0 | 1.66M |
| **LightUAV Deploy (Folded Conv3x3)** | 22.99% | 75.32% | 167.2 | 231.8 | 109.5 | 1.50M |
| **Milestone Ep 30 (Early)** | 8.37% | 17.03% | 894.7 | 230.4 | 107.6 | 1.50M |
| **Milestone Ep 50 (Mid)** | 8.66% | 22.24% | 829.2 | 205.6 | 100.1 | 1.50M |
| **Milestone Ep 80 (Deep)** | 7.90% | 21.80% | 782.9 | 123.1 | 75.2 | 1.50M |

### 二、学术三线表（LaTeX 格式供论文直接收录）

```latex
\begin{table*}[t]
\centering
\caption{Ablation Study and Benchmark Evaluation on Anti-UAV Dataset}
\label{tab:sot_ablation}
\begin{tabular}{lcccccc}
\toprule
Method / Variant & Success (\%) & Precision (\%) & CLE (px) & Model FPS & E2E FPS & Params (M) \\
\midrule
DSST (C++ OpenCV Handcrafted) & 45.20 & 51.00 & 142.5 & 550.0 & 120.0 & 0.00 \\
NanoTrack-V3 (ICCV 2021) & 55.56 & 71.22 & 150.3 & 420.0 & 35.7 & 0.55 \\
LightTrack-M (ICCV 2021) & 52.24 & 66.06 & 131.5 & 450.0 & 37.2 & 1.60 \\
Baseline (No MFFN, No DAC) & 0.50 & 0.79 & 997.0 & 112.4 & 71.3 & 1.50 \\
LightUAV + MFFN (No DAC) & 0.23 & 0.18 & 1111.3 & 101.4 & 66.0 & 1.50 \\
LightUAV Full Model (+ DAC-Head) & 22.99 & 75.32 & 167.2 & 224.5 & 107.0 & 1.66 \\
LightUAV Deploy (Folded Conv3x3) & 22.99 & 75.32 & 167.2 & 231.8 & 109.5 & 1.50 \\
Milestone Ep 30 (Early) & 8.37 & 17.03 & 894.7 & 230.4 & 107.6 & 1.50 \\
Milestone Ep 50 (Mid) & 8.66 & 22.24 & 829.2 & 205.6 & 100.1 & 1.50 \\
Milestone Ep 80 (Deep) & 7.90 & 21.80 & 782.9 & 123.1 & 75.2 & 1.50 \\
\bottomrule
\end{tabular}
\end{table*}
```
