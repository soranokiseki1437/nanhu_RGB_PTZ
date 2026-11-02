# -*- coding: utf-8 -*-
import docx

doc = docx.Document(r"d:\PyProjects\RGB_PTZ_Integrated\课题\反无人机云台视觉跟踪与抗遮挡重检测课题规划指南.docx")
lines = []

lines.append("# 反无人机云台视觉跟踪与抗遮挡重检测课题规划指南\n")
lines.append("> **版本定义**：2026 结构重参数化、动态双模板与条件式二级抗虚警校验版  ")
lines.append("> **归属工程**：[RGB_PTZ_Integrated](file:///d:/PyProjects/RGB_PTZ_Integrated)  \n")
lines.append("---\n")

for p in doc.paragraphs:
    t = p.text.strip()
    if not t:
        continue
    if t.startswith("反无人机云台视觉跟踪"):
        continue
    elif any(t.startswith(x) for x in ["一、", "二、", "三、", "四、", "五、"]):
        lines.append(f"\n## {t}\n")
    elif any(t.startswith(f"{i}.") for i in range(1, 25)):
        lines.append(f"\n### {t}\n")
    elif t.startswith("•") or t.startswith("-"):
        clean_t = t.lstrip("•- ").strip()
        lines.append(f"* {clean_t}")
    else:
        lines.append(f"{t}\n")

lines.append("\n## 五、 课题规划数据总表与多维横评矩阵\n")
table_titles = [
    "硕士课题论文章节规划与工作量平衡总表",
    "主流跟踪算法多维对比规划基准表（毕业论文实验大表模板）",
    "面向真实低空工况的多维实际指标对比规划表（UAV123 / Anti-UAV 实测）",
    "第 3 章消融实验设计矩阵（Ablation Study Matrix）"
]

for t_idx, t in enumerate(doc.tables):
    title = table_titles[t_idx] if t_idx < len(table_titles) else f"表格 {t_idx+1}"
    lines.append(f"\n### {title}\n")
    headers = [c.text.replace("\n", "<br>") for c in t.rows[0].cells]
    lines.append("| " + " | ".join(headers) + " |")
    lines.append("| " + " | ".join([":---" for _ in headers]) + " |")
    for r in t.rows[1:]:
        row_vals = [c.text.replace("\n", "<br>") for c in r.cells]
        lines.append("| " + " | ".join(row_vals) + " |")

with open(r"d:\PyProjects\RGB_PTZ_Integrated\课题\反无人机云台视觉跟踪与抗遮挡重检测课题规划指南.md", "w", encoding="utf-8") as f:
    f.write("\n".join(lines))

print("Markdown generated successfully")
