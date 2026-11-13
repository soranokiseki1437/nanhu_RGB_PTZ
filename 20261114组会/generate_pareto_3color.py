import os
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

# Load data
csv_path = r'D:\PyProjects\SOT_Tracking_Baselines\results\benchmark_summary.csv'
df = pd.read_csv(csv_path)

# Filter out KCF (1240 FPS off chart)
df = df[df['Tracker'] != 'KCF'].copy()

# Categorize into 3 groups
def categorize(row):
    tracker = row['Tracker']
    if tracker in ['DSST']:
        return '传统相关滤波算法'
    elif tracker in ['NanoTrack-V1', 'NanoTrack-V2', 'NanoTrack-V3', 'LightTrackM', 'FEAR-XS']:
        return '轻型深度模型 (<2M)'
    else:
        return '重型高精度模型 (>10M)'

df['Category'] = df.apply(categorize, axis=1)

# Color mapping (3 distinct colors, no colorbar)
color_map = {
    '重型高精度模型 (>10M)': '#C0392B',   # Crimson Red
    '轻型深度模型 (<2M)': '#1E8449',     # Deep Emerald Green
    '传统相关滤波算法': '#2874A6'       # Steel Blue
}

# Params parsing
def parse_params(p):
    try:
        return float(p)
    except:
        return 0.1

df['params_val'] = df['Params (M)'].apply(parse_params)

# Plot setup
plt.figure(figsize=(10.5, 6.8), dpi=300)
plt.rcParams['font.sans-serif'] = ['SimHei', 'Microsoft YaHei', 'DejaVu Sans']
plt.rcParams['axes.unicode_minus'] = False

# Plot each category for clean legend
categories = [
    '重型高精度模型 (>10M)',
    '轻型深度模型 (<2M)',
    '传统相关滤波算法'
]

for cat in categories:
    sub = df[df['Category'] == cat]
    plt.scatter(
        sub['Throughput (FPS)'], sub['Success (AUC)'],
        s=[max(130, np.sqrt(max(p, 0.2)) * 65) for p in sub['params_val']],
        color=color_map[cat],
        label=cat,
        edgecolors='#222222',
        linewidth=1.4,
        alpha=0.90,
        zorder=4
    )

# Real-time baseline 30 FPS
plt.axvline(x=30.0, color='#E67E22', linestyle='--', linewidth=1.6, alpha=0.9, zorder=2, label='实时基准线 (30 FPS)')

# Annotations with custom offsets
offsets = {
    'OSTrack-Base':   {'ox': -2.5, 'oy': 0.020, 'ha': 'right'},
    'SiamCAR':        {'ox': -1.2, 'oy': 0.018, 'ha': 'right'},
    'TransT':         {'ox': -1.2, 'oy': -0.018, 'ha': 'right'},
    'SiamRPN++':      {'ox': 1.4,  'oy': 0.022, 'ha': 'left'},
    'NanoTrack-V3':   {'ox': 0.8,  'oy': 0.014, 'ha': 'left'},
    'MixFormer':      {'ox': -1.2, 'oy': -0.028, 'ha': 'right'},
    'LightTrackM':    {'ox': 1.2,  'oy': -0.018, 'ha': 'left'},
    'NanoTrack-V2':   {'ox': 1.2,  'oy': 0.014, 'ha': 'left'},
    'FEAR-XS':        {'ox': -1.2, 'oy': 0.016, 'ha': 'right'},
    'NanoTrack-V1':   {'ox': -1.2, 'oy': -0.018, 'ha': 'right'},
    'SiamGAT':        {'ox': 1.2,  'oy': -0.018, 'ha': 'left'},
    'DSST':           {'ox': 1.2,  'oy': 0.014, 'ha': 'left'},
    'DaSiamRPN':      {'ox': -1.2, 'oy': 0.016, 'ha': 'right'}
}

for _, r in df.iterrows():
    name = r['Tracker']
    cfg = offsets.get(name, {'ox': 1.0, 'oy': 0.01, 'ha': 'left'})
    p_label = f"{r['params_val']:.1f}M" if r['params_val'] >= 0.2 else 'CPU'
    txt = f"{name}\n({p_label})"
    c = color_map[r['Category']]

    plt.annotate(
        txt,
        xy=(r['Throughput (FPS)'], r['Success (AUC)']),
        xytext=(r['Throughput (FPS)'] + cfg['ox'], r['Success (AUC)'] + cfg['oy']),
        ha=cfg['ha'],
        va='center',
        fontsize=8.5,
        fontweight='bold',
        color='#111111',
        bbox=dict(boxstyle='round,pad=0.25', facecolor='white', edgecolor=c, alpha=0.92, lw=1.3),
        arrowprops=dict(arrowstyle='->', connectionstyle='arc3,rad=0.0', color='#666666', lw=0.8, alpha=0.8),
        zorder=5
    )

plt.xlim(29.0, 86.0)
plt.ylim(0.20, 0.72)

plt.title('Anti-UAV 基准：跟踪成功率 (AUC) vs 推理帧率 (FPS) 前沿分布', fontsize=13, fontweight='bold', pad=14)
plt.xlabel('推理帧率 Throughput (FPS) [NVIDIA RTX 5070 Laptop GPU]', fontsize=11, fontweight='semibold')
plt.ylabel('跟踪成功率 Tracking Success Rate (AUC)', fontsize=11, fontweight='semibold')
plt.grid(True, linestyle='--', alpha=0.4)

plt.legend(loc='upper right', fontsize=10, framealpha=0.95, edgecolor='#cccccc')
plt.tight_layout()

out_path = r'D:\PyProjects\RGB_PTZ_Integrated\20261114组会\pareto_speed_vs_auc_3color.png'
plt.savefig(out_path, dpi=300)
plt.close()
print('Generated 3-color pareto plot successfully:', out_path)

