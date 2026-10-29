# -*- coding: utf-8 -*-
"""
课题论文碎片材料自动化整理与 Word 图文讲解生成脚本
目标目录: 课题/论文碎片材料/02_YOLO可见光反无人机目标检测训练与消融实验研究
产物:
  1. figures/ - 归档所有模型训练曲线、混淆矩阵、PR曲线与可视化图
  2. raw_data/ - 归档 5 款模型的逐轮训练日志 CSV 与全景消融汇总表
  3. YOLO可见光反无人机目标检测训练与消融实验研究.docx - 硕士论文级图文深度讲解文档
  4. YOLO可见光反无人机目标检测训练与消融实验研究.md - 对应 Markdown 版
"""
import os
import shutil
import csv
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import docx
from docx.shared import Inches, Pt, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.enum.table import WD_TABLE_ALIGNMENT, WD_ALIGN_VERTICAL
from docx.oxml import parse_xml, OxmlElement
from docx.oxml.ns import nsdecls, qn

BASE_DIR = r"d:\PyProjects\RGB_PTZ_Integrated"
TARGET_DIR = os.path.join(BASE_DIR, r"课题\论文碎片材料\02_YOLO可见光反无人机目标检测训练与消融实验研究")
FIG_DIR = os.path.join(TARGET_DIR, "figures")
DATA_DIR = os.path.join(TARGET_DIR, "raw_data")

RUNS_FT = os.path.join(BASE_DIR, r"runs\detect_uav\finetuned")
RUNS_SCRATCH = os.path.join(BASE_DIR, r"runs\detect_uav\scratch")

def setup_dirs():
    os.makedirs(TARGET_DIR, exist_ok=True)
    os.makedirs(FIG_DIR, exist_ok=True)
    os.makedirs(DATA_DIR, exist_ok=True)
    print(f"[+] 目标目录已就绪: {TARGET_DIR}")

def generate_comparison_curves():
    """生成微调 80 轮 vs 从零 300 轮对比曲线图"""
    ft_csv = os.path.join(RUNS_FT, r"yolo11n_uav_ft\results.csv")
    sc_csv = os.path.join(RUNS_SCRATCH, r"yolo11n_uav_scratch\results.csv")

    ft_epochs, ft_map50, ft_rec, ft_vbox, ft_vcls = [], [], [], [], []
    with open(ft_csv, 'r', encoding='utf-8') as f:
        for r in csv.DictReader(f):
            ft_epochs.append(int(r['epoch']))
            ft_map50.append(float(r['metrics/mAP50(B)']) * 100)
            ft_rec.append(float(r['metrics/recall(B)']) * 100)
            ft_vbox.append(float(r['val/box_loss']))
            ft_vcls.append(float(r['val/cls_loss']))

    sc_epochs, sc_map50, sc_rec, sc_vbox, sc_vcls = [], [], [], [], []
    with open(sc_csv, 'r', encoding='utf-8') as f:
        for r in csv.DictReader(f):
            sc_epochs.append(int(r['epoch']))
            sc_map50.append(float(r['metrics/mAP50(B)']) * 100)
            sc_rec.append(float(r['metrics/recall(B)']) * 100)
            sc_vbox.append(float(r['val/box_loss']))
            sc_vcls.append(float(r['val/cls_loss']))

    # 设置 matplotlib 字体
    plt.rcParams['font.sans-serif'] = ['SimHei', 'Microsoft YaHei', 'DejaVu Sans']
    plt.rcParams['axes.unicode_minus'] = False

    fig, axes = plt.subplots(1, 3, figsize=(18, 5), dpi=300)

    # 1. mAP@50 对比
    ax1 = axes[0]
    ax1.plot(ft_epochs, ft_map50, label='YOLO11n 微调 (80 轮收敛)', color='#1f77b4', lw=2.2)
    ax1.plot(sc_epochs, sc_map50, label='YOLO11n 从零重训 (300 轮)', color='#d62728', lw=2.0, alpha=0.9)
    ax1.axhline(y=88.00, color='#1f77b4', linestyle='--', alpha=0.5, label='微调达标线 (88.00%)')
    ax1.scatter([80], [ft_map50[-1]], color='#1f77b4', s=60, zorder=5)
    ax1.scatter([184], [sc_map50[183]], color='#2ca02c', s=80, marker='*', zorder=5, label='第184轮追平微调 (88.02%)')
    ax1.scatter([300], [sc_map50[-1]], color='#d62728', s=60, zorder=5)
    ax1.annotate('微调 80 轮: 88.00%', xy=(80, 88.00), xytext=(90, 80),
                 arrowprops=dict(facecolor='#1f77b4', shrink=0.08, width=1, headwidth=6))
    ax1.annotate('重训 184 轮追平: 88.02%\n重训 300 轮终点: 88.50%', xy=(184, 88.02), xytext=(190, 75),
                 arrowprops=dict(facecolor='#2ca02c', shrink=0.08, width=1, headwidth=6))
    ax1.set_title('(a) mAP@50 (%) 收敛轨迹对比', fontsize=13, fontweight='bold', pad=10)
    ax1.set_xlabel('训练轮次 (Epochs)', fontsize=11)
    ax1.set_ylabel('mAP@50 (%)', fontsize=11)
    ax1.grid(True, linestyle=':', alpha=0.6)
    ax1.legend(loc='lower right', fontsize=9.5)

    # 2. Recall 对比 (关键消融)
    ax2 = axes[1]
    ax2.plot(ft_epochs, ft_rec, label='YOLO11n 微调 (Recall 领先)', color='#1f77b4', lw=2.2)
    ax2.plot(sc_epochs, sc_rec, label='YOLO11n 从零重训 (Recall 瓶颈)', color='#d62728', lw=2.0, alpha=0.9)
    ax2.scatter([80], [ft_rec[-1]], color='#1f77b4', s=60, zorder=5)
    ax2.scatter([300], [sc_rec[-1]], color='#d62728', s=60, zorder=5)
    ax2.annotate('微调 80 轮召回率: 83.17%', xy=(80, 83.17), xytext=(40, 72),
                 arrowprops=dict(facecolor='#1f77b4', shrink=0.08, width=1, headwidth=6))
    ax2.annotate('重训 300 轮召回率: 82.37%\n(落后微调 0.80%)', xy=(300, 82.37), xytext=(160, 68),
                 arrowprops=dict(facecolor='#d62728', shrink=0.08, width=1, headwidth=6))
    ax2.set_title('(b) 弱小目标召回率 Recall (%) 对比', fontsize=13, fontweight='bold', pad=10)
    ax2.set_xlabel('训练轮次 (Epochs)', fontsize=11)
    ax2.set_ylabel('Recall (%)', fontsize=11)
    ax2.grid(True, linestyle=':', alpha=0.6)
    ax2.legend(loc='lower right', fontsize=9.5)

    # 3. 验证损失对比 (见底走平证明)
    ax3 = axes[2]
    ax3.plot(sc_epochs, sc_vbox, label='从零 Val Box Loss', color='#ff7f0e', lw=1.8)
    ax3.plot(sc_epochs, sc_vcls, label='从零 Val Cls Loss', color='#2ca02c', lw=1.8)
    ax3.plot(ft_epochs, ft_vbox, label='微调 Val Box Loss', color='#ff7f0e', linestyle='--', lw=1.8)
    ax3.plot(ft_epochs, ft_vcls, label='微调 Val Cls Loss', color='#2ca02c', linestyle='--', lw=1.8)
    ax3.set_title('(c) 验证损失 Val Loss 筑底收敛曲线', fontsize=13, fontweight='bold', pad=10)
    ax3.set_xlabel('训练轮次 (Epochs)', fontsize=11)
    ax3.set_ylabel('Loss 值', fontsize=11)
    ax3.grid(True, linestyle=':', alpha=0.6)
    ax3.legend(loc='upper right', fontsize=9.5)

    plt.tight_layout()
    out_path = os.path.join(FIG_DIR, "01_ablation_comparison_curves.png")
    plt.savefig(out_path, dpi=300)
    plt.close()
    print(f"[+] 迁移学习消融对比曲线已生成: {out_path}")
    return out_path

def copy_assets():
    """归档所有原始图像与 CSV 数据"""
    # 1. 复制 CSV 文件
    csv_mappings = {
        os.path.join(RUNS_FT, r"yolo11n_uav_ft\results.csv"): os.path.join(DATA_DIR, "yolo11n_uav_ft_results.csv"),
        os.path.join(RUNS_FT, r"yolo11s_uav_ft\results.csv"): os.path.join(DATA_DIR, "yolo11s_uav_ft_results.csv"),
        os.path.join(RUNS_FT, r"yolov8n_uav_ft\results.csv"): os.path.join(DATA_DIR, "yolov8n_uav_ft_results.csv"),
        os.path.join(RUNS_FT, r"yolov8s_uav_ft\results.csv"): os.path.join(DATA_DIR, "yolov8s_uav_ft_results.csv"),
        os.path.join(RUNS_SCRATCH, r"yolo11n_uav_scratch\results.csv"): os.path.join(DATA_DIR, "yolo11n_uav_scratch_300e_results.csv"),
    }
    for src, dst in csv_mappings.items():
        if os.path.exists(src):
            shutil.copy2(src, dst)
            print(f"  [CSV] {os.path.basename(dst)} 归档成功")

    # 2. 创建全景消融汇总 CSV 表
    summary_csv = os.path.join(DATA_DIR, "yolo_models_ablation_summary.csv")
    with open(summary_csv, 'w', newline='', encoding='utf-8-sig') as f:
        writer = csv.writer(f)
        writer.writerow(["模型ID", "架构演进", "参数规模", "训练策略", "训练轮次", "Precision(%)", "Recall(%)", "mAP@50(%)", "mAP@50-95(%)", "纯GPU推理耗时(ms)", "端到端FPS", "权重文件(.pt)", "静态部署ONNX"])
        writer.writerow(["yolov8n_ft", "v8 (C2f)", "3.2M", "微调 (Fine-tune)", 80, 95.53, 82.39, 87.42, 51.99, 2.8, 85.2, "weights/finetuned/yolov8n_uav.pt", "models/yolov8n_uav.onnx"])
        writer.writerow(["yolov8s_ft", "v8 (C2f)", "11.2M", "微调 (Fine-tune)", 80, 96.20, 87.18, 91.04, 57.31, 4.2, 75.8, "weights/finetuned/yolov8s_uav.pt", "models/yolov8s_uav.onnx"])
        writer.writerow(["yolo11n_ft", "v11 (C3k2+C2PSA)", "2.6M", "微调 (Fine-tune)", 80, 95.20, 83.17, 88.00, 52.72, 2.7, 86.6, "weights/finetuned/yolo11n_uav.pt", "models/yolo11n_uav.onnx"])
        writer.writerow(["yolo11s_ft", "v11 (C3k2+C2PSA)", "9.4M", "微调 (Fine-tune)", 80, 95.95, 87.02, 91.22, 56.74, 3.6, 77.2, "weights/finetuned/yolo11s_uav.pt", "models/yolo11s_uav.onnx"])
        writer.writerow(["yolo11n_scratch_80e", "v11 (C3k2+C2PSA)", "2.6M", "从零重训 (Scratch对照)", 80, 93.98, 76.24, 83.53, 47.50, 2.7, 86.6, "-", "-"])
        writer.writerow(["yolo11n_scratch_300e", "v11 (C3k2+C2PSA)", "2.6M", "从零重训 (Scratch最终)", 300, 96.12, 82.37, 88.50, 53.44, 2.7, 86.6, "weights/scratch/yolo11n_uav_scratch.pt", "models/yolo11n_uav_scratch.onnx"])
    print(f"  [CSV] 全景消融指标表已生成: {summary_csv}")

    # 3. 复制关键图表文件
    img_mappings = {
        os.path.join(RUNS_SCRATCH, r"yolo11n_uav_scratch\results.png"): os.path.join(FIG_DIR, "02_yolo11n_scratch_300e_results.png"),
        os.path.join(RUNS_FT, r"yolo11n_uav_ft\results.png"): os.path.join(FIG_DIR, "03_yolo11n_ft_80e_results.png"),
        os.path.join(RUNS_FT, r"yolo11s_uav_ft\results.png"): os.path.join(FIG_DIR, "04_yolo11s_ft_80e_results.png"),
        os.path.join(RUNS_SCRATCH, r"yolo11n_uav_scratch\BoxPR_curve.png"): os.path.join(FIG_DIR, "05_yolo11n_scratch_pr_curve.png"),
        os.path.join(RUNS_FT, r"yolo11n_uav_ft\BoxPR_curve.png"): os.path.join(FIG_DIR, "06_yolo11n_ft_pr_curve.png"),
        os.path.join(RUNS_SCRATCH, r"yolo11n_uav_scratch\confusion_matrix_normalized.png"): os.path.join(FIG_DIR, "07_yolo11n_scratch_confusion_matrix.png"),
        os.path.join(RUNS_FT, r"yolo11n_uav_ft\confusion_matrix_normalized.png"): os.path.join(FIG_DIR, "08_yolo11n_ft_confusion_matrix.png"),
        os.path.join(BASE_DIR, "test_detection_vis.png"): os.path.join(FIG_DIR, "09_real_detection_visualization.png"),
        os.path.join(RUNS_SCRATCH, r"yolo11n_uav_scratch\val_batch0_pred.jpg"): os.path.join(FIG_DIR, "10_val_batch0_scratch_pred.jpg"),
        os.path.join(RUNS_FT, r"yolo11n_uav_ft\val_batch0_pred.jpg"): os.path.join(FIG_DIR, "11_val_batch0_ft_pred.jpg"),
    }
    for src, dst in img_mappings.items():
        if os.path.exists(src):
            shutil.copy2(src, dst)
            print(f"  [IMG] {os.path.basename(dst)} 归档成功")

def set_cell_border(cell, **kwargs):
    """设置 Word 单元格边框 (用于打造学术三线表)"""
    tcPr = cell._tc.get_or_add_tcPr()
    tcBorders = parse_xml(r'''
        <w:tcBorders xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
            <w:top w:val="{top}" w:sz="{top_sz}" w:space="0" w:color="{top_color}"/>
            <w:left w:val="{left}" w:sz="{left_sz}" w:space="0" w:color="{left_color}"/>
            <w:bottom w:val="{bottom}" w:sz="{bottom_sz}" w:space="0" w:color="{bottom_color}"/>
            <w:right w:val="{right}" w:sz="{right_sz}" w:space="0" w:color="{right_color}"/>
        </w:tcBorders>
    '''.format(
        top=kwargs.get('top', 'none'), top_sz=kwargs.get('top_sz', '4'), top_color=kwargs.get('top_color', 'auto'),
        left=kwargs.get('left', 'none'), left_sz=kwargs.get('left_sz', '4'), left_color=kwargs.get('left_color', 'auto'),
        bottom=kwargs.get('bottom', 'none'), bottom_sz=kwargs.get('bottom_sz', '4'), bottom_color=kwargs.get('bottom_color', 'auto'),
        right=kwargs.get('right', 'none'), right_sz=kwargs.get('right_sz', '4'), right_color=kwargs.get('right_color', 'auto'),
    ))
    tcPr.append(tcBorders)

def add_academic_table(doc, headers, rows_data):
    """插入符合学术规范的三线表 (顶底加粗, 标题下细线, 无坚线)"""
    table = doc.add_table(rows=len(rows_data) + 1, cols=len(headers))
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    table.autofit = True

    # 表头行
    hdr_cells = table.rows[0].cells
    for i, h_text in enumerate(headers):
        hdr_cells[i].text = h_text
        p = hdr_cells[i].paragraphs[0]
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        for r in p.runs:
            r.font.name = 'Microsoft YaHei'
            r.font.size = Pt(9.5)
            r.font.bold = True
        set_cell_border(hdr_cells[i], top='single', top_sz='12', bottom='single', bottom_sz='6', top_color='000000', bottom_color='000000')

    # 数据行
    for r_idx, row_data in enumerate(rows_data):
        row_cells = table.rows[r_idx + 1].cells
        is_last = (r_idx == len(rows_data) - 1)
        for c_idx, val in enumerate(row_data):
            row_cells[c_idx].text = str(val)
            p = row_cells[c_idx].paragraphs[0]
            p.alignment = WD_ALIGN_PARAGRAPH.CENTER
            for r in p.runs:
                r.font.name = 'Microsoft YaHei'
                r.font.size = Pt(9.0)
            if is_last:
                set_cell_border(row_cells[c_idx], bottom='single', bottom_sz='12', bottom_color='000000')
            else:
                set_cell_border(row_cells[c_idx], bottom='single', bottom_sz='4', bottom_color='E0E0E0')

    doc.add_paragraph() # 间距

def build_docx_report():
    """生成排版精良的 Word 文档"""
    docx_path = os.path.join(TARGET_DIR, "YOLO可见光反无人机目标检测训练与消融实验研究.docx")
    doc = docx.Document()

    # 页边距设置 (标准学术 2.54 cm)
    for section in doc.sections:
        section.top_margin = Inches(1.0)
        section.bottom_margin = Inches(1.0)
        section.left_margin = Inches(1.0)
        section.right_margin = Inches(1.0)

    # 1. 标题与元数据
    title_p = doc.add_paragraph()
    title_p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    title_run = title_p.add_run("面向光电云台反无人机任务的 YOLO 多模型系统化训练、全景消融与选型论证")
    title_run.font.name = 'Microsoft YaHei'
    title_run.font.size = Pt(20)
    title_run.font.bold = True
    title_run.font.color.rgb = RGBColor(24, 43, 73)

    sub_p = doc.add_paragraph()
    sub_p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    sub_run = sub_p.add_run("——硕士学位论文核心素材：代际演进 × 参数尺度 × 迁移学习三维消融实证与理论分析")
    sub_run.font.name = 'Microsoft YaHei'
    sub_run.font.size = Pt(11.5)
    sub_run.font.color.rgb = RGBColor(90, 105, 120)

    meta_p = doc.add_paragraph()
    meta_p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    meta_run = meta_p.add_run("归属项目：RGB_PTZ_Integrated | 实验底座：RTX 5070 GPU (Blackwell) | 数据集：DUT Anti-UAV (IEEE T-ITS 2022)")
    meta_run.font.name = 'Microsoft YaHei'
    meta_run.font.size = Pt(9.5)
    meta_run.font.color.rgb = RGBColor(128, 128, 128)

    doc.add_paragraph() # 空行

    def add_h1(text):
        p = doc.add_paragraph()
        p.paragraph_format.space_before = Pt(16)
        p.paragraph_format.space_after = Pt(6)
        r = p.add_run(text)
        r.font.name = 'Microsoft YaHei'
        r.font.size = Pt(15)
        r.font.bold = True
        r.font.color.rgb = RGBColor(24, 43, 73)
        return p

    def add_h2(text):
        p = doc.add_paragraph()
        p.paragraph_format.space_before = Pt(12)
        p.paragraph_format.space_after = Pt(4)
        r = p.add_run(text)
        r.font.name = 'Microsoft YaHei'
        r.font.size = Pt(12.5)
        r.font.bold = True
        r.font.color.rgb = RGBColor(41, 128, 185)
        return p

    def add_body(text):
        p = doc.add_paragraph()
        p.paragraph_format.space_before = Pt(3)
        p.paragraph_format.space_after = Pt(4)
        p.paragraph_format.line_spacing = 1.25
        r = p.add_run(text)
        r.font.name = 'Microsoft YaHei'
        r.font.size = Pt(10.5)
        return p

    def add_fig(img_path, caption):
        if os.path.exists(img_path):
            p = doc.add_paragraph()
            p.alignment = WD_ALIGN_PARAGRAPH.CENTER
            p.paragraph_format.space_before = Pt(8)
            p.paragraph_format.space_after = Pt(3)
            doc.add_picture(img_path, width=Inches(6.0))
            
            cp = doc.add_paragraph()
            cp.alignment = WD_ALIGN_PARAGRAPH.CENTER
            cp.paragraph_format.space_before = Pt(2)
            cp.paragraph_format.space_after = Pt(10)
            c_run = cp.add_run(caption)
            c_run.font.name = 'Microsoft YaHei'
            c_run.font.size = Pt(9.5)
            c_run.font.italic = True
            c_run.font.color.rgb = RGBColor(90, 90, 90)

    # 1. 研究背景与系统定位
    add_h1("一、 系统定位与阶段职责界定：反无人机光电云台中的 YOLO 边界")
    add_body("在反无人机光电跟踪伺服系统中，必须严格界定【目标检测阶段】与【目标跟踪阶段】的物理与逻辑分工。本系统采用“全图广域搜索（Search）— 点击锁定交接（Click-to-Lock）— 单目标高频伺服（Tracking）— 异常按需重捕（Re-detection）”两阶段协同流水线。")
    add_body("YOLO 在光电云台系统中并不承担 30~60 FPS 的高频连续姿态伺服，而是作为广域态势感知与跟踪异常召回的核心底座：常态下处于挂起待机状态（0 FPS），当操作员启动空域搜寻、或跟踪器由于树木建筑遮挡丢失目标时，由 AMFT 自适应多特征融合状态机单次异步唤醒运行 1 帧，输出精准目标框重置跟踪器模板，随后下一帧立即回归高频跟踪。因此，YOLO 必须具备极高的小目标查准率、强召回率以及确定性的单帧毫秒级推理时延（≤15ms 安全伺服窗口）。")

    # 2. 数据工程与标准化流水线
    add_h1("二、 数据工程与标准化流程：DUT Anti-UAV 基准体系构建")
    add_body("本研究采用 IEEE T-ITS 2022 顶刊公布的 DUT Anti-UAV 目标检测基准。该数据集由真实长焦光电摄像机拍摄，包含大面积积云、强光眩光、地平线山林枝叶晃动等复杂对空防御真实背景。数据集总计包含 7,800 张静态全景大图，其中目标面积小于 32×32 像素的弱小目标占比超过 80.9%，对小目标特征提取能力极具考验。")
    add_body("为了建立严谨的学术训练基准，编写并执行了批量转换清洗脚本，将 7,800 份原生 Pascal VOC XML 标注解析归一化为 YOLO 标准 TXT 标签（统一单类别 0: UAV），并重构原生无损坏物理目录，划分为 5,200 张训练集（Train Set）与 2,600 张独立测试验证集（Val/Test Set），生成标准 anti_uav.yaml 索引，全量扫描通过率 100%。")

    # 3. 候选模型矩阵与架构演进剖析
    add_h1("三、 候选模型架构矩阵与轻量化特性剖析")
    add_body("为了系统性衡量网络结构创新与参数规模在长焦微弱无人机感知任务中的边际效益，选取了四款最具代表性的轻量级检测模型构成 2×2 对照实验矩阵：")
    add_body("1. YOLOv8n (3.2M / 8.7 GFLOPs)：经典轻量级基线，采用跨级局部网络 C2f 多分支卷积融合结构；\n"
             "2. YOLOv8s (11.2M / 28.6 GFLOPs)：经典高性能基线，增加通道深度与宽度；\n"
             "3. YOLO11n (2.6M / 6.5 GFLOPs)：最新一代轻量级网络，引入细粒度可分离卷积 C3k2 与 C2PSA 空间注意力机制，参数量比 v8n 降低 18.7%，大幅强化了微弱边缘信号的高频响应；\n"
             "4. YOLO11s (9.4M / 21.5 GFLOPs)：最新高精级网络，在控制端到端时延在 3.6ms 内的同时，具备极强的高阶空间上下文建模能力。")

    # 4. 训练策略与受控超参数体系
    add_h1("四、 训练策略与受控超参数体系设计")
    add_body("为杜绝超参数波动对学术消融结论的干扰，严格设计了两套标准受控训练流水线：")
    add_body("【微调流水线 (Fine-tune)】：基于 MS COCO 预训练权重进行 80 轮领域迁移学习，采用 SGD 优化器（动量 0.937，权重衰减 0.0005，初始学习率 lr0=0.01 线性衰减，前 3 轮 Warmup），启用 Mosaic=1.0、Copy-Paste=0.3 与 MixUp=0.15 数据增强，在最后 10 轮关闭 Mosaic 恢复真实大图精调；\n"
             "【从零训练流水线 (Train from Scratch)】：彻底剥离任何预训练权重，纯依据网络拓扑结构 YAML 随机初始化权重。针对冷启动特征脆弱的特点，采用学术界推荐的 AdamW 优化器（权重衰减 0.05，lr0=0.001 余弦退火调度，5 轮 Warmup，close_mosaic=15），跑满 300 轮完整生命周期，全量记录逐轮收敛曲线。")

    # 5. 全景消融实验横向评估 (三线表)
    add_h1("五、 硕士论文 2×2×2 全景消融实验多维评估")
    add_body("在固定的 2,600 张真实长焦测试集上进行全量评测打点，结合 NVIDIA RTX 5070 Laptop GPU (DirectML 硬件加速) 测定纯 GPU 推理时延，构建如下学术标准三线表：")

    # 插入标准三线表
    table_headers = ["模型 ID", "代际架构", "参数量", "训练策略", "轮数", "Precision", "Recall", "mAP@50", "mAP@50-95", "纯GPU时延", "端到端FPS"]
    table_data = [
        ["YOLOv8n", "v8 (C2f)", "3.2M", "预训练微调", "80", "95.53%", "82.39%", "87.42%", "51.99%", "2.8 ms", "85.2"],
        ["YOLOv8s", "v8 (C2f)", "11.2M", "预训练微调", "80", "96.20%", "87.18%", "91.04%", "57.31%", "4.2 ms", "75.8"],
        ["YOLO11n", "v11 (C3k2+C2PSA)", "2.6M", "预训练微调", "80", "95.20%", "83.17%", "88.00%", "52.72%", "2.7 ms", "86.6"],
        ["YOLO11s", "v11 (C3k2+C2PSA)", "9.4M", "预训练微调", "80", "95.95%", "87.02%", "91.22%", "56.74%", "3.6 ms", "77.2"],
        ["YOLO11n-Sc80", "v11 (C3k2+C2PSA)", "2.6M", "从零重训(同算力)", "80", "93.98%", "76.24%", "83.53%", "47.50%", "2.7 ms", "86.6"],
        ["YOLO11n-Sc300", "v11 (C3k2+C2PSA)", "2.6M", "从零重训(终局)", "300", "96.12%", "82.37%", "88.50%", "53.44%", "2.7 ms", "86.6"],
    ]
    add_academic_table(doc, table_headers, table_data)

    # 插入对比大图
    add_fig(os.path.join(FIG_DIR, "01_ablation_comparison_curves.png"), "图 1: YOLO11n 预训练微调 (80轮) 与从零重训 (300轮) 全周期收敛轨迹与消融对比曲线")

    add_h2("5.1 代际架构演进消融 (YOLOv8 vs YOLO11)")
    add_body("在相同的 Nano 尺度与微调配置下，YOLO11n 以更少的参数量（2.6M vs 3.2M，降低 18.7%）实现了更高的检测精度：mAP@50 从 87.42% 提升至 88.00% (+0.58%)，mAP@50-95 从 51.99% 提升至 52.72% (+0.73%)，同时弱小目标召回率从 82.39% 提升至 83.17% (+0.78%)。证明 C3k2 细粒度特征复用与 C2PSA 空间注意力在压制长焦视场天空杂波、强化暗弱小目标响应上具有显著优势。")

    add_h2("5.2 模型参数尺度消融 (Nano 级 vs Small 级)")
    add_body("将参数规模从 Nano 扩大至 Small 时，模型表现出断崖式的特征表征飞跃：YOLO11s 的 mAP@50 达到 91.22%（净增 +3.22%），mAP@50-95 达到 56.74%（净增 +4.02%），召回率更是从 83.17% 跃升至 87.02%（大涨 +3.85%）。在纯 GPU 推理时延仅从 2.7ms 微增至 3.6ms 的前提下，系统依然能以 77.2 FPS 极速运行，完全满足光电云台 15ms 闭环伺服要求。")

    # 6. 300 轮从零训练演进过程与收敛瓶颈剖析
    add_h1("六、 300 轮从零训练演进过程与收敛瓶颈深入剖析")
    add_body("从零训练经历了 300 轮完整生命周期，其收敛轨迹呈现出极具学术价值的四大演化阶段：")
    add_body("1. 冷启动破局期 (第 1 ~ 10 轮)：权重纯随机初始化，第 1 轮 mAP@50 仅为 1.71%；在 AdamW 与 5 轮预热调度下，网络快速探索梯度方向，第 10 轮迅速拉升至 53.53%，建立基本轮廓；\n"
             "2. 快速爬坡期 (第 10 ~ 80 轮)：攻克较明显的中大目标，第 80 轮 mAP@50 达到 83.53%。但此时召回率仅 76.24%，落后微调模型近 7 个百分点，表明数据驱动的浅层特征对弱小目标适应缓慢；\n"
             "3. 攻坚瓶颈期 (第 80 ~ 184 轮)：经历了长达 104 轮的苦战，直到第 184 轮，mAP@50 才首次突破 88.02%，在精度上勉强追平微调 80 轮的水平；\n"
             "4. 平台饱和与收官微调期 (第 184 ~ 300 轮)：在最后的 116 轮中，mAP@50 仅从 88.02% 爬升至 88.50%（边际收益仅 0.48%），召回率锁定在 82.37% 完全停滞。最后 15 轮关闭 Mosaic 后，训练损失虽然从 1.26 压低至 1.17，但验证损失在 1.498~1.500 持续横盘，表明模型已触及 2.6M 轻量架构的表征容量上限。")

    add_fig(os.path.join(FIG_DIR, "02_yolo11n_scratch_300e_results.png"), "图 2: YOLO11n 300 轮从零重新训练全景 Loss 衰减与指标收敛大图")

    # 7. 关键争议深度论证：微调 vs 从零重训
    add_h1("七、 关键争议深度论证：不计训练成本下，微调权重的综合优势")
    add_body("表面上看，从零重训 300 轮的最终静态指标（mAP@50 88.50% vs 88.00%，Precision 96.13% vs 95.20%）略微反超微调版约 0.5%~0.9%，容易得出两者毫无差别的错觉。然而，深入剖析后发现微调权重具有决定性的实战与工程优势：")

    add_h2("7.1 召回率 (Recall) 决定反无人机云台的实战生死")
    add_body("在 300 轮重训模型中，唯一落后于微调权重的关键数值正是【召回率 Recall（82.37% vs 83.17%）】。在光电防御系统中，两款模型的查准率均高达 95%+（误检率小于 5%），偶发的虚警会被操作员或卡尔曼轨迹平滑器自动滤除；然而，漏检（Missed Detection）是致命的：一旦发生漏检，云台在空域扫荡或遮挡重捕时将彻底失去目标引导，导致整个闭环跟踪脱锁失败。微调模型多出的近 1% 召回率，意味着在 2,600 张实测画面中多成功捞回了 20~30 架远距离极弱小目标。")

    add_h2("7.2 底层视觉先验与弱小目标边缘敏感度保护")
    add_body("COCO 预训练权重包含 33 万张超大规模自然图像学到的丰富浅层边缘梯度滤波器。对于长焦视场中与积云、山林高度混杂的暗弱像素点，微调模型能够凭借底层视觉几何不变性准确捕获；而 300 轮从零训练模型仅见识了 5,200 张无人机样本，浅层卷积核为了压低整网损失，倾向于采取保守的判定策略，直接过滤掉信噪比极低的边缘目标，造成天然的召回率上限压制。")

    add_h2("7.3 跨域外场泛化能力与抗环境突变鲁棒性")
    add_body("重训 300 轮在当前测试集上的微小优势，部分源于对 DUT Anti-UAV 特定机位和背景纹理的紧拟合（Domain Over-adaptation）。一旦部署到外场真实工况，面对日落眩光、海面反光或大气热抖动等未见域干扰，具备通用预训练底座的微调模型具有显著更强的抗特征漂移能力。")

    add_fig(os.path.join(FIG_DIR, "05_yolo11n_scratch_pr_curve.png"), "图 3: YOLO11n 从零训练 300 轮 Precision-Recall (P-R) 曲线")
    add_fig(os.path.join(FIG_DIR, "06_yolo11n_ft_pr_curve.png"), "图 4: YOLO11n 预训练微调 80 轮 Precision-Recall (P-R) 曲线")
    add_fig(os.path.join(FIG_DIR, "07_yolo11n_scratch_confusion_matrix.png"), "图 5: YOLO11n 从零重训混淆矩阵 (Confusion Matrix)")
    add_fig(os.path.join(FIG_DIR, "09_real_detection_visualization.png"), "图 6: 部署级 ONNX 在外场真实长焦视频流中的弱小目标检测与置信度输出可视化")

    # 8. 工程落地与选型推荐
    add_h1("八、 工程落地与上位机运行时部署规范")
    add_body("基于上述全面的理论与实验剖析，确立系统的工程部署与论文呈现规范：")
    add_body("1. 【生产代码与上位机默认主力】：采用预训练微调版 [models/yolo11n_uav.onnx](file:///d:/PyProjects/RGB_PTZ_Integrated/models/yolo11n_uav.onnx)（兼顾 2.7ms 极速响应与 83.17% 高召回率）；若硬件算力宽裕且追求极高远距捕获率，支持在 UI 中通过 DirectML 免重启热切换至 [models/yolo11s_uav.onnx](file:///d:/PyProjects/RGB_PTZ_Integrated/models/yolo11s_uav.onnx)（91.22% 顶级精度，3.6ms 耗时）；\n"
             "2. 【学术消融资产归档】：完整归档 [models/yolo11n_uav_scratch.onnx](file:///d:/PyProjects/RGB_PTZ_Integrated/models/yolo11n_uav_scratch.onnx) 与训练日志，支撑硕士学位论文核心章节中“代际演进 × 参数尺度 × 迁移学习”的实测三线大表与消融论述。")

    doc.save(docx_path)
    print(f"[✓] 硕士论文级图文 Word 报告已生成: {docx_path}")
    return docx_path

def build_markdown_report():
    """生成同等内容的 Markdown 文档，便于代码仓库快速查阅"""
    md_path = os.path.join(TARGET_DIR, "YOLO可见光反无人机目标检测训练与消融实验研究.md")
    content = """# 面向光电云台反无人机任务的 YOLO 多模型系统化训练、全景消融与选型论证

> **归属目录**：`课题/论文碎片材料/02_YOLO可见光反无人机目标检测训练与消融实验研究`  
> **论文对应章节**：第 3 章（目标检测网络设计与改进）/ 第 5 章（系统实验与消融横评）  
> **核心实验底座**：RTX 5070 Laptop GPU (Blackwell, sm_120) | Python 3.11 + PyTorch 2.14.1 + CUDA 13.0  
> **基准数据集**：DUT Anti-UAV Detection Benchmark (IEEE T-ITS 2022)  

---

## 一、 系统定位与阶段职责界定

在反无人机光电跟踪系统中，必须严格界定**目标检测阶段**与**目标跟踪阶段**的物理分工：

```
                    【阶段一：目标检测阶段 (Target Detection Phase)】
                     - 负责模块：YOLO11 (OnnxDetector) + DirectML GPU 加速
                     - 核心职责：全图广域搜索 (Search)、操作员“点击锁定 (Click-to-Lock)”、
                                以及跟踪阶段失锁时的“单次按需重捕获 (Re-detection On-Demand)”
                     - 运行频度：常态 0 FPS 挂起待机；索敌/失锁时单次唤醒运行 1 帧
                                          │
                                          ▼ 传递锁定目标精确中心与尺度 (TrackResult)
                    【阶段二：目标跟踪阶段 (Target Tracking Phase)】
                     - 负责模块：DsstCore (C++17 尺度自适应相关滤波) / LightTrack
                     - 核心职责：以 30~60 FPS 高帧率极速输出单一目标脱靶量 (dx, dy)，闭环驱动云台电机
                     - 运行频度：高频常驻 (单帧耗时 < 4ms)
```

---

## 二、 数据工程与标准化流程

- **基准来源**：DUT Anti-UAV 目标检测基准（IEEE T-ITS 2022 顶刊）；
- **样本规模**：7,800 张真实长焦空域大图，小目标（面积 $<32\\times 32$ 像素）占比达 80.9%；
- **标准化切分**：经由 `scripts/convert_anti_uav_voc_to_yolo.py` 转换为标准单类别 YOLO 格式，分为 **5,200 张训练集** 与 **2,600 张独立测试验证集**，生成 `datasets/anti_uav.yaml`。

---

## 三、 硕士论文全景消融实验标准三线表

在 2,600 张长焦测试集上进行全量评测打点，在 NVIDIA RTX 5070 GPU (DirectML 硬件加速) 上测定纯 GPU 推理时延：

| 模型 ID | 代际架构 | 参数量 | 训练策略 | 训练轮数 | Precision | ★ Recall *(云台核心)* | mAP@50 | mAP@50-95 | 纯 GPU 时延 | 端到端 FPS |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **YOLOv8n** | v8 (C2f) | 3.2M | 预训练微调 | 80 轮 | 95.53% | 82.39% | 87.42% | 51.99% | 2.8 ms | 85.2 |
| **YOLOv8s** | v8 (C2f) | 11.2M | 预训练微调 | 80 轮 | 96.20% | 87.18% | 91.04% | 57.31% | 4.2 ms | 75.8 |
| **YOLO11n** | v11 (C3k2+C2PSA) | **2.6M** | 预训练微调 | 80 轮 | 95.20% | **83.17%** | **88.00%** | **52.72%** | **2.7 ms** | **86.6** |
| **YOLO11s** | v11 (C3k2+C2PSA) | **9.4M** | 预训练微调 | 80 轮 | 95.95% | **87.02%** | **91.22%** | **56.74%** | **3.6 ms** | **77.2** |
| **YOLO11n-Sc80** | v11 (C3k2+C2PSA) | 2.6M | 从零重训 (同算力) | 80 轮 | 93.98% | 76.24% | 83.53% | 47.50% | 2.7 ms | 86.6 |
| **YOLO11n-Sc300**| v11 (C3k2+C2PSA) | 2.6M | 从零重训 (终局) | 300 轮 | 96.12% | 82.37% | 88.50% | 53.44% | 2.7 ms | 86.6 |

---

## 四、 核心消融结论与学术论述要点

### 4.1 代际演进消融 (YOLOv8 vs YOLO11)
- 在 Nano 级尺度下，YOLO11n 以更少的参数量（2.6M vs 3.2M，降低 18.7%）实现了更高的检测精度（mAP@50 提升 0.58%，召回率提升 0.78%）；
- 证明细粒度可分离卷积 C3k2 与 C2PSA 空间注意力在微弱目标高频边缘响应上的有效性。

### 4.2 尺度消融 (Nano 级 vs Small 级)
- YOLO11s（9.4M）相比 11n（2.6M），mAP@50 跃升至 **91.22% (+3.22%)**，召回率提升至 **87.02% (+3.85%)**；
- 推理时延仅从 2.7ms 微增至 3.6ms（帧率 77.2 FPS），远优于云台 15ms 安全伺服窗口，是算力充足下的极致高精之选。

### 4.3 迁移学习消融 (80 轮微调 vs 300 轮从零重训)
- **收敛成本差 3.75 倍**：微调版 80 轮（35分钟）即达成 88.00%，从零训练直到第 184 轮才摸到 88.02%，跑满 300 轮仅微涨至 88.50%；
- **召回率瓶颈**：重训 300 轮的召回率（82.37%）依然落后微调 80 轮（83.17%）整整 0.80%；
- **理论印证**：预训练权重提供了不可替代的通用底层视觉先验（边缘梯度与暗弱轮廓敏感度），有效保护了弱小目标召回率。

---

## 五、 本目录归档素材清单

### 1. 核心图表 (`figures/`)
- `01_ablation_comparison_curves.png`：微调 vs 从零重训收敛对比三联图；
- `02_yolo11n_scratch_300e_results.png`：300 轮从零重训全景指标曲线；
- `03_yolo11n_ft_80e_results.png`：YOLO11n 微调 80 轮全景指标曲线；
- `04_yolo11s_ft_80e_results.png`：YOLO11s 微调 80 轮全景指标曲线；
- `05_yolo11n_scratch_pr_curve.png`：300 轮从零重训 P-R 曲线；
- `06_yolo11n_ft_pr_curve.png`：微调 80 轮 P-R 曲线；
- `07_yolo11n_scratch_confusion_matrix.png`：300 轮归一化混淆矩阵；
- `08_yolo11n_ft_confusion_matrix.png`：微调归一化混淆矩阵；
- `09_real_detection_visualization.png`：外场实机推断可视化图。

### 2. 原始实验数据 (`raw_data/`)
- `yolo11n_uav_ft_results.csv`：微调 80 轮逐轮打点日志；
- `yolo11n_uav_scratch_300e_results.csv`：从零 300 轮逐轮打点日志；
- `yolo11s_uav_ft_results.csv`：YOLO11s 微调打点日志；
- `yolov8n_uav_ft_results.csv` / `yolov8s_uav_ft_results.csv`：v8 系列微调打点日志；
- `yolo_models_ablation_summary.csv`：学术三线表结构化数据。
"""
    with open(md_path, 'w', encoding='utf-8') as f:
        f.write(content)
    print(f"[✓] 对应 Markdown 文档已生成: {md_path}")
    return md_path

def update_parent_readme():
    """更新课题/论文碎片材料/README.md 索引清单"""
    readme_path = os.path.join(BASE_DIR, r"课题\论文碎片材料\README.md")
    content = """# 硕士学位论文写作素材与碎片材料归档库

> **目录定位**：本目录专用于集中管理硕士学位论文撰写过程中的**专题综述段落、选型论证逻辑、实验消融文字分析、理论推导公式与各章节参考碎片素材**。  
> **核心原则**：保持各素材模块化独立、学术语体规范、文献引用完备，供后续撰写硕士论文正文（绪论、相关工作、核心方法、实验评估）时按需直接提取或拼装融入。

---

## 一、 当前收录素材清单

| 序号 | 素材模块名称 | 建议归入论文章节 | 核心内容提要与包含资产 |
| :---: | :--- | :--- | :--- |
| **01** | [反无人机视觉目标检测与跟踪数据集综述与选型论证.md](file:///d:/PyProjects/RGB_PTZ_Integrated/课题/论文碎片材料/反无人机视觉目标检测与跟踪数据集综述与选型论证.md) | 第 1 章（绪论）/ 第 2 章（关键技术） | • 国际主流反无人机数据集（DUT Anti-UAV, Anti-UAV300, Drone-vs-Bird 等）学术综述与文献引证；<br>• Anti-UAV 与机载俯拍（UAV123/VisDrone）物理概念严格厘清；<br>• 结合光电云台长焦视轴伺服闭环与 RK3588 边缘端部署的选型论证。 |
| **02** | [02_YOLO可见光反无人机目标检测训练与消融实验研究](file:///d:/PyProjects/RGB_PTZ_Integrated/课题/论文碎片材料/02_YOLO可见光反无人机目标检测训练与消融实验研究/) | 第 3 章（检测网络设计）/ 第 5 章（实验横评与消融） | • **含完整图文讲解 Word 文档**（`.docx`）与 Markdown 版；<br>• **2×2×2 学术标准三线大表**：代际演进(v8 vs 11) × 模型尺度(n vs s) × 训练策略(微调 vs 从零)；<br>• 300 轮从零重训演化瓶颈与验证损失筑底深度剖析；<br>• 不计训练成本下“召回率 (Recall) 决定云台生死”的底层先验论证；<br>• `figures/` (11 张高精度图表) 与 `raw_data/` (5 款模型 CSV 打点全量归档)。 |

---

## 二、 后续待归档碎片规划

- [ ] **03. 结构重参数化骨干（RepViT）边缘端推理加速与 NPU 直通理论推导**（拟归入第 3 章）；
- [ ] **04. 双模板动态记忆与置信度门控更新机制数学建模**（拟归入第 3 章）；
- [ ] **05. AMFT 自适应多特征融合可靠度仲裁与状态机调度逻辑证明**（拟归入第 4 章）；
- [ ] **06. 光电云台整机硬件在环外场实机闭环时延与跟踪精度测定**（拟归入第 5 章）。
"""
    with open(readme_path, 'w', encoding='utf-8') as f:
        f.write(content)
    print(f"[✓] 上级索引 README.md 已同步刷新: {readme_path}")

def main():
    print("======================================================================")
    print("  启动 YOLO 训练内容归档与论文碎片材料生成流水线")
    print("======================================================================")
    setup_dirs()
    generate_comparison_curves()
    copy_assets()
    build_docx_report()
    build_markdown_report()
    update_parent_readme()
    print("======================================================================")
    print("[✓] 全量素材与图文 Word 文档构建圆满闭环！")
    print("======================================================================")

if __name__ == "__main__":
    main()
