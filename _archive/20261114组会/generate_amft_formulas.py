import matplotlib.pyplot as plt

plt.rcParams['font.sans-serif'] = ['Microsoft YaHei', 'SimHei', 'DejaVu Sans']
plt.rcParams['axes.unicode_minus'] = False
plt.rcParams['mathtext.fontset'] = 'cm'

fig, ax = plt.subplots(figsize=(9.2, 3.2), dpi=300)
ax.axis('off')

# Titles in Chinese, equations in LaTeX mathtext
items = [
    ("1. 运动连续性特征：", r"$S_{\mathrm{mot}} = 0.6 \times \mathrm{IoU}_t + 0.4 \times (1 - D_t)$"),
    ("2. 三维综合可靠性得分：", r"$R_t = \omega_1 S_{\mathrm{conf}} + \omega_2 S_{\mathrm{mot}} + \omega_3 S_{\mathrm{bg}}$"),
    ("3. 滑动窗口动态阈值：", r"$\tau_t = \tau_0 - \alpha \times \sigma_t \quad (\tau_0 = 0.7, \; \alpha = 0.5)$"),
    ("4. 重检测触发判据：", r"$R_t < \tau_t \quad \Rightarrow \quad \mathrm{Trigger \; Global \; Detection}$")
]

y_positions = [0.80, 0.55, 0.30, 0.05]
for (title, eq), y in zip(items, y_positions):
    ax.text(0.02, y, title, fontsize=12, fontweight='bold', color='#701624', va='center')
    ax.text(0.32, y, eq, fontsize=13, color='#111111', va='center')

out_path = r'D:\PyProjects\RGB_PTZ_Integrated\20261114组会\amft_formulas_box.png'
plt.savefig(out_path, transparent=True, bbox_inches='tight', dpi=300)
plt.close()
print('Generated AMFT formula box cleanly:', out_path)
