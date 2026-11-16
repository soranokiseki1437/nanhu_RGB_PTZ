import os
import sys
import win32com.client

# Win32 RGB helper: R + (G << 8) + (B << 16)
def rgb(r, g, b):
    return int(r + (g << 8) + (b << 16))

COLOR_BURGUNDY   = rgb(112, 22, 36)   # #701624
COLOR_WHITE       = rgb(255, 255, 255)
COLOR_CARD_BG     = rgb(248, 248, 250)
COLOR_CARD_HEADER = rgb(238, 238, 242)
COLOR_BORDER      = rgb(215, 215, 222)
COLOR_TEXT_MAIN   = rgb(25, 25, 25)
COLOR_TEXT_MUTED  = rgb(80, 80, 80)
COLOR_ACCENT_RED  = rgb(192, 57, 43)
COLOR_ACCENT_GRN  = rgb(30, 132, 73)
COLOR_ACCENT_BLU  = rgb(40, 116, 166)

BASE_DIR = r"D:\PyProjects\RGB_PTZ_Integrated\20261114组会"
SRC_PPTX = os.path.join(BASE_DIR, "李行—组会汇报20260523.pptx")
DST_PPTX = os.path.join(BASE_DIR, "李行—组会汇报20261114.pptx")

def setup_text(tr, text, size=13, bold=False, color=COLOR_TEXT_MAIN, font="Microsoft YaHei"):
    tr.Text = text
    tr.Font.Name = font
    tr.Font.Size = size
    tr.Font.Bold = -1 if bold else 0
    tr.Font.Color.RGB = color

def add_card(slide, left, top, width, height, bg_color=COLOR_CARD_BG, border_color=COLOR_BORDER):
    card = slide.Shapes.AddShape(1, left, top, width, height)
    card.Fill.Solid()
    card.Fill.ForeColor.RGB = bg_color
    if border_color is not None:
        card.Line.Visible = -1
        card.Line.ForeColor.RGB = border_color
        card.Line.Weight = 1.0
    else:
        card.Line.Visible = 0
    return card

def add_header_card(slide, left, top, width, height, title, content_lines, title_color=COLOR_BURGUNDY, font_size=11):
    # Base card
    card = add_card(slide, left, top, width, height)
    # Header bar
    h_bar = slide.Shapes.AddShape(1, left, top, width, 30)
    h_bar.Fill.Solid()
    h_bar.Fill.ForeColor.RGB = COLOR_CARD_HEADER
    h_bar.Line.Visible = -1
    h_bar.Line.ForeColor.RGB = COLOR_BORDER
    h_bar.Line.Weight = 1.0
    setup_text(h_bar.TextFrame.TextRange, f"▍ {title}", size=12.5, bold=True, color=title_color)
    h_bar.TextFrame.MarginLeft = 10
    h_bar.TextFrame.MarginTop = 5
    # Body
    body_tb = slide.Shapes.AddTextbox(1, left + 8, top + 34, width - 16, height - 38)
    body_tb.TextFrame.WordWrap = -1
    body_tb.TextFrame.MarginLeft = 4
    body_tb.TextFrame.MarginRight = 4
    body_tb.TextFrame.MarginTop = 4
    body_tb.TextFrame.MarginBottom = 4
    body_text = "\n".join(content_lines)
    setup_text(body_tb.TextFrame.TextRange, body_text, size=font_size, bold=False, color=COLOR_TEXT_MAIN)
    return card

def add_header(slide, section_title, sub_title, page_num):
    for sh in slide.Shapes:
        if sh.Name == "Rectangle 76" or (sh.Top < 50 and sh.Left > 200 and sh.Type == 1):
            if sh.HasTextFrame:
                setup_text(sh.TextFrame.TextRange, section_title, size=18, bold=True, color=COLOR_WHITE)
                break

    for sh in slide.Shapes:
        if sh.Name == "TextBox 24" or (sh.HasTextFrame and 70 < sh.Top < 125 and sh.Left < 100):
            sh.Width = 850
            setup_text(sh.TextFrame.TextRange, sub_title, size=16, bold=True, color=COLOR_TEXT_MAIN)
            break

    for sh in slide.Shapes:
        if "Slide Number" in sh.Name or (sh.Top > 480 and sh.Left > 700):
            if sh.HasTextFrame:
                setup_text(sh.TextFrame.TextRange, str(page_num), size=12, bold=False, color=COLOR_TEXT_MUTED)
                break

def clean_body(slide):
    to_delete = []
    for sh in slide.Shapes:
        if sh.Top >= 125 and "Slide Number" not in sh.Name:
            to_delete.append(sh)
    for sh in to_delete:
        try:
            sh.Delete()
        except:
            pass

def insert_media(slide, vpath, ppath, left, top, width, height):
    try:
        med = slide.Shapes.AddMediaObject2(vpath, False, True, left, top, width, height)
        return med
    except Exception as e:
        print(f"Fallback to poster for {vpath}: {e}")
        pic = slide.Shapes.AddPicture(ppath, False, True, left, top, width, height)
        return pic

def build_all():
    print(f"Opening template {SRC_PPTX}...")
    ppt = win32com.client.Dispatch("PowerPoint.Application")
    ppt.Visible = -1
    pres = ppt.Presentations.Open(SRC_PPTX)

    # 1. Update Cover (Slide 1)
    print("Formatting Slide 1 (Cover)...")
    s1 = pres.Slides(1)
    for sh in s1.Shapes:
        if sh.HasTextFrame:
            t = sh.TextFrame.TextRange.Text.strip()
            if "汇报人" in t:
                setup_text(sh.TextFrame.TextRange, "汇报人：李行  2026年11月14日", size=16, color=COLOR_TEXT_MAIN)
            elif "组会" in t:
                setup_text(sh.TextFrame.TextRange, "组会汇报", size=14, bold=True, color=COLOR_BURGUNDY)

    # 2. Update Contents (Slide 2)
    print("Formatting Slide 2 (Contents)...")
    s2 = pres.Slides(2)
    to_del = []
    for sh in s2.Shapes:
        if sh.HasTextFrame:
            t = sh.TextFrame.TextRange.Text.strip()
            if t in ["03", "算法嵌入规划"]:
                to_del.append(sh)
    for sh in to_del:
        sh.Delete()

    for sh in s2.Shapes:
        if sh.Name == "Flowchart: Connector 2":
            sh.Top = 145
        elif sh.Name == "TextBox 3":
            sh.Top = 140
            sh.Width = 560
            sh.Height = 130
            t_content = "01  Qt上位机与系统搭建\n  • 可见光上位机：检测模型对比与选型、跟踪 AMFT 机制、室内与外场实测归因\n  • 单红外上位机：一体化云台控制上位机开发与功能实现"
            setup_text(sh.TextFrame.TextRange, t_content, size=14, bold=False, color=COLOR_TEXT_MAIN)
        elif sh.Name == "Flowchart: Connector 8":
            sh.Top = 295
        elif sh.Name == "TextBox 12":
            sh.Top = 290
            sh.Width = 560
            sh.Height = 130
            t_content = "02  课题规划与算法演进路线\n  • 硕士学位论文整体研究框架与章节划分\n  • 目标检测演进规划（YOLOv11 小目标定制 + Val-CNN 二次校验）\n  • 目标跟踪演进规划（LightTrack 融合 RepViT 重参数化骨干）\n  • SOT 基准 Pareto 前沿与工程保底策略"
            setup_text(sh.TextFrame.TextRange, t_content, size=14, bold=False, color=COLOR_TEXT_MAIN)

    # Reorder slides setup:
    # We will create 13 content slides by duplicating Slide 3 to the end.
    base_slide = pres.Slides(3)
    
    def create_content_slide(section_title, sub_title, page_num):
        dup = base_slide.Duplicate()
        dup.MoveTo(pres.Slides.Count)
        clean_body(dup)
        add_header(dup, section_title, sub_title, page_num)
        return dup

    # P03: 可见光检测训练与选型
    print("Building P03: 可见光检测训练与选型...")
    s3 = create_content_slide("一、Qt上位机与系统搭建", "可见光检测：YOLOv11 与 YOLOv8 训练对比与选型", 3)
    ui_img = os.path.join(BASE_DIR, "可见光qt上位机_yolo检测.png")
    s3.Shapes.AddPicture(ui_img, False, True, 45, 140, 420, 335)
    tbl_sh = s3.Shapes.AddTable(5, 4, 485, 140, 430, 185)
    tbl = tbl_sh.Table
    headers = ["模型 / 尺度", "训练策略", "验证集指标", "选型结论"]
    rows = [
        ["YOLOv8s / n", "预训练微调", "s > n (基线参照)", "基础对比"],
        ["YOLOv11s", "预训练微调", "指标优于 v8s", "特征表征强"],
        ["YOLOv11n", "预训练微调", "满足实时性约束", "边缘选型候选"],
        ["YOLOv11n", "从头训练 300轮", "与微调指标相当", "选定部署主干"]
    ]
    for c, h in enumerate(headers):
        cell = tbl.Cell(1, c + 1)
        setup_text(cell.Shape.TextFrame.TextRange, h, size=11, bold=True, color=COLOR_WHITE)
        cell.Shape.Fill.Solid()
        cell.Shape.Fill.ForeColor.RGB = COLOR_BURGUNDY
    for r, row_data in enumerate(rows):
        for c, val in enumerate(row_data):
            cell = tbl.Cell(r + 2, c + 1)
            setup_text(cell.Shape.TextFrame.TextRange, val, size=10.5, color=COLOR_TEXT_MAIN)
            cell.Shape.Fill.Solid()
            cell.Shape.Fill.ForeColor.RGB = COLOR_WHITE if r % 2 == 0 else COLOR_CARD_BG

    add_header_card(s3, 485, 340, 430, 135, "实验设置与选型决策", [
        "• 训练数据集：DUT-AntiUAV 检测数据集（5,200 张无人机标注图像）。",
        "• 核心结论：模型尺度维度 s 优于 n，版本维度 11 优于 8；从头训练与微调模型在验证集指标上无显著优劣。",
        "• 选型结果：综合考虑边缘部署性能要求，主干模型选定 YOLOv11n。"
    ])

    # P04: 跟踪 AMFT 原理与公式
    print("Building P04: AMFT 机制与公式...")
    s4 = create_content_slide("一、Qt上位机与系统搭建", "目标跟踪：AMFT 自适应重检测触发机制与公式", 4)
    top_c = add_card(s4, 45, 135, 870, 65)
    setup_text(top_c.TextFrame.TextRange, "▍ 机制目标：在“跟踪为主、按需检测”协同框架中，克服固定阈值缺陷，自适应决定触发重检测的最佳时机，保障跟踪连续性并抑制冗余检测计算。（依据论文 AMFT_IMCEC2025）", size=12.5, bold=True, color=COLOR_BURGUNDY)
    top_c.TextFrame.MarginLeft = 15
    top_c.TextFrame.MarginRight = 15
    top_c.TextFrame.MarginTop = 10

    formula_img = os.path.join(BASE_DIR, "amft_formulas_box.png")
    s4.Shapes.AddPicture(formula_img, False, True, 45, 215, 520, 260)

    add_header_card(s4, 580, 215, 335, 260, "核心参数与决策逻辑", [
        "• S_conf：跟踪器响应峰值 / 分类置信度",
        "• IoU_t：相邻前后两帧跟踪框交并比（位置连续性）",
        "• D_t：当前帧位移与前3帧平均位移偏差率（运动合理性）",
        "• S_bg：目标区域与3倍背景区域的 HOG 余弦相似度",
        "• σ_t：滑动窗口（K=10）得分标准差（表征突变复杂度）",
        "• 动态阈值：τ_t = 0.7 - 0.5 × σ_t",
        "• 触发判据：当综合得分 R_t < τ_t 时判定跟踪不可靠，立即触发全局重检测并更新模板。"
    ], font_size=10.5)

    # P05: 室内跟椅子归因
    print("Building P05: 室内多目标实测与归因...")
    s5 = create_content_slide("一、Qt上位机与系统搭建", "目标跟踪：室内多目标实测与跟踪帧率异常归因", 5)
    v_chair = os.path.join(BASE_DIR, "yolov11通用权重跟椅子.mp4")
    p_chair = os.path.join(BASE_DIR, "video_chair_poster.png")
    insert_media(s5, v_chair, p_chair, 45, 140, 420, 335)
    add_header_card(s5, 485, 140, 430, 95, "实测异常现象", [
        "• 在室内多目标场景下使用 DSST 跟踪指定目标（椅子）；",
        "• 实测跟踪帧率出现肉眼可见的严重下跌。"
    ])
    add_header_card(s5, 485, 245, 430, 130, "系统性原因归因", [
        "• 归因 1：DSST 算法在相邻两跟踪帧间对静止目标做了错误的运动预测，且两跟踪帧时间间隔较长，放大了预测偏差；",
        "• 排除误判：实测排查日志记录显示当时并未触发重检测（排除此前“重检测后多目标误选”的推测）。"
    ])
    add_header_card(s5, 485, 385, 430, 90, "工程调试规范改进", [
        "• 上位机日志系统增加强制打点：实时输出逐帧耗时与 FPS；",
        "• 每帧输出脱靶量与运动预测值，闭环支撑原因分析。"
    ])

    # P06: 外场实测 1+2
    print("Building P06: 外场实测（视频 1+2）...")
    s6 = create_content_slide("一、Qt上位机与系统搭建", "无人机外场实测：目标尺度与相机自动变焦异常", 6)
    v_zoom = os.path.join(BASE_DIR, "跟上自动变焦跟丢.mp4")
    p_zoom = os.path.join(BASE_DIR, "video_zoom_poster.png")
    insert_media(s6, v_zoom, p_zoom, 120, 135, 720, 275)
    add_header_card(s6, 120, 420, 720, 75, "实测问题现象与精准归因（视频 1+2）", [
        "1、只在尺度变化小以及运动速度不快时可以跟上（跟踪算法性能问题）",
        "2、单一天空背景下，相机会触发自动变焦，此时容易丢失目标。（相机问题）"
    ], title_color=COLOR_ACCENT_RED, font_size=12)

    # P07: 外场实测 3
    print("Building P07: 外场实测（视频 3）...")
    s7 = create_content_slide("一、Qt上位机与系统搭建", "无人机外场实测：跟踪框尺度滞后", 7)
    v_scale = os.path.join(BASE_DIR, "框选目标尺度变化.mp4")
    p_scale = os.path.join(BASE_DIR, "video_scale_poster.png")
    insert_media(s7, v_scale, p_scale, 120, 135, 720, 280)
    add_header_card(s7, 120, 425, 720, 65, "实测问题现象与精准归因（视频 3）", [
        "3、跟踪框大小没有跟随物体尺度变化，跟踪框小于目标时丢失目标（软件设计问题）"
    ], title_color=COLOR_ACCENT_RED, font_size=12)

    # P08: 外场实测 4
    print("Building P08: 外场实测（视频 4）...")
    s8 = create_content_slide("一、Qt上位机与系统搭建", "无人机外场实测：复杂背景重检测异常", 8)
    v_complex = os.path.join(BASE_DIR, "背景复杂跟丢.mp4")
    p_complex = os.path.join(BASE_DIR, "video_complex_poster.png")
    insert_media(s8, v_complex, p_complex, 120, 135, 720, 280)
    add_header_card(s8, 120, 425, 720, 65, "实测问题现象与精准归因（视频 4）", [
        "4、跟丢和遮挡后，重检测未触发或未成功（软件设计问题）"
    ], title_color=COLOR_ACCENT_RED, font_size=12)

    # P09: 单红外上位机
    print("Building P09: 单红外上位机...")
    s9 = create_content_slide("一、Qt上位机与系统搭建", "单红外上位机开发与功能实现", 9)
    img_photo = os.path.join(BASE_DIR, "单红外上位机照片.jpg")
    img_snap  = os.path.join(BASE_DIR, "单红外上位机截屏.png")
    s9.Shapes.AddPicture(img_photo, False, True, 45, 140, 420, 160)
    s9.Shapes.AddPicture(img_snap,  False, True, 45, 310, 420, 165)
    add_header_card(s9, 485, 140, 430, 155, "软件架构与功能实现", [
        "• 基于 Qt 6.12 (MinGW 64-bit, C++17) 开发单红外一体化上位机；",
        "• 基于厂商网络 SDK（SV_NET_SDK）实现云台水平/俯仰转动与镜头变焦/调焦控制；",
        "• 支持预置位巡航扫描与通信看门狗保护机制；",
        "• 界面实时统计并显示抓流与渲染帧率。"
    ])
    add_header_card(s9, 485, 305, 430, 170, "视频链路与实测现状", [
        "• 视频链路：基于第三方库 FFmpeg 抓取 RTSP 视频流并多线程解码渲染，未调用相机原生 SDK；",
        "• 存储功能：支持音视频录制与高清图片抓拍落盘；",
        "• 实测现状：实测目前未实现单红外跟踪功能。"
    ])

    # P10: 毕业论文整体章节规划
    print("Building P10: 硕士学位论文章节规划...")
    s10 = create_content_slide("二、课题规划与算法演进路线", "硕士学位论文整体研究框架与章节划分", 10)
    col_w = 162
    gap = 15
    l_base = 45
    chapters = [
        ("第一章 绪论", [
            "• 低空安防背景与意义",
            "• Anti-UAV 探测现状",
            "• 光电伺服追踪瓶颈",
            "• 论文主要研究内容"
        ]),
        ("第二章 目标检测", [
            "• 面向低空弱小目标",
            "• YOLOv11 结构改进",
            "• P2 特征层与注意力",
            "• Val-CNN 二次校验"
        ]),
        ("第三章 目标跟踪", [
            "• 超轻量 SOT 跟踪算法",
            "• LightTrack 架构重构",
            "• RepViT 重参数化骨干",
            "• 抗形变与尺度适配"
        ]),
        ("第四章 系统集成", [
            "• 光电云台伺服协同",
            "• 像素脱靶量闭环解算",
            "• 多模态 Qt 上位机集成",
            "• 地面外场闭环验证"
        ]),
        ("第五章 总结展望", [
            "• 全文研究成果总结",
            "• 关键技术指标提炼",
            "• 系统工程局限归纳",
            "• 后续研究方向展望"
        ])
    ]
    for idx, (ch_title, ch_items) in enumerate(chapters):
        col_x = l_base + idx * (col_w + gap)
        add_header_card(s10, col_x, 140, col_w, 335, ch_title, ch_items, title_color=COLOR_BURGUNDY, font_size=10.5)

    # P11: 目标检测演进规划
    print("Building P11: 目标检测演进规划...")
    s11 = create_content_slide("二、课题规划与算法演进路线", "目标检测演进规划：小目标模块定制与集成校验", 11)
    add_header_card(s11, 45, 140, 420, 335, "方案一：YOLOv11 小目标网络模块定制", [
        "▍ 设计动因：",
        "• 针对 Anti-UAV 远距离、低信噪比弱小目标特性，传统检测头深层特征易丢失微弱几何结构。",
        "",
        "▍ 核心改进方案：",
        "• 增设 P2 超高分辨率浅层检测头：引入 160×160 特征层，保留微小纹理与空间轮廓信息；",
        "• 引入轻量空间/通道注意力机制：引入 ECA-Net / CoordAttention 模块，自适应抑制复杂地杂波背景噪声；",
        "• 优化小目标尺度损失函数：采用 Normalized Wasserstein Distance (NWD) 提升小目标回归敏感度。"
    ], font_size=10.5)
    add_header_card(s11, 495, 140, 420, 335, "方案二：集成校验网络（Val-CNN 二次判决）", [
        "▍ 设计动因：",
        "• 地面与低空存在鸟类、树丛、建筑物等大量干扰物，单检测器极易产生虚警（假阳性）。",
        "",
        "▍ 核心改进方案（DeTrAck 范式）：",
        "• 引入 2026 顶刊论文《DeTrAck: UAV detection and tracking...》集成架构；",
        "• 在主检测器输出的高置信度候选框后级，串联轻量级分类校验网络（Val-CNN）；",
        "• 专门对切片候选区域进行二次精细化二分类滤波判决；",
        "• 彻底压制地杂波虚警，提升系统整体误检容忍度。"
    ], font_size=10.5)

    # P12: 目标跟踪模块重构
    print("Building P12: LightTrack + RepViT 跟踪重构...")
    s12 = create_content_slide("二、课题规划与算法演进路线", "超轻量跟踪器设计：LightTrack 融合 RepViT 骨干网络", 12)
    add_header_card(s12, 45, 140, 870, 95, "基准算法选择 —— LightTrack (ICCV 2021)", [
        "• 经典超轻量单目标跟踪网络，采用高效孪生网络（Siamese）架构与逐层互相关头；",
        "• 参数量仅 1.6M，计算复杂度极低，天生适配嵌入式边缘计算平台（如 RK3588 NPU）。"
    ])
    add_header_card(s12, 45, 245, 870, 130, "骨干网络重构 —— 引入 RepViT (CVPR 2024) 重参数化网络", [
        "• 替代原始 MobileNetV3 骨干，利用 ViT 空间注意力机制提升远距离弱小目标的特征表征力；",
        "• 结构重参数化（Structural Re-parameterization）：",
        "  - 训练阶段：多分支结构提供丰富特征提取与梯度多样性，保证模型强拟合能力；",
        "  - 推理阶段：通过数学等价变换融合为纯单路标准卷积，零多分支内存搬移开销，最大化边缘 NPU 吞吐。"
    ])
    add_header_card(s12, 45, 385, 870, 90, "部署契约与边缘适配", [
        "• 导出纯静态 ONNX 模型（opset 17，模板 127×127，搜索 255×255）；",
        "• 对齐 100% 硬件直通与零 CPU 回退契约，保障板载实时运行。"
    ])

    # P13: 跟踪模型三阶段训练规划
    print("Building P13: 跟踪三阶段训练体系...")
    s13 = create_content_slide("二、课题规划与算法演进路线", "跟踪模型三阶段训练规划", 13)
    step_w = 270
    step_gap = 30
    steps = [
        ("阶段 1：骨干预训练", [
            "▍ 权重来源：",
            "• ImageNet 官方预训练权重；",
            "",
            "▍ 核心任务：",
            "• 初始化底层空间特征提取能力；",
            "",
            "▍ 关键目标：",
            "• 提供稳固的底层边缘纹理捕捉基底，加速后续跟踪匹配任务收敛。"
        ]),
        ("阶段 2：通用大规模预训练", [
            "▍ 训练数据集：",
            "• GOT-10k、TrackingNet 大规模数据集；",
            "",
            "▍ 核心任务：",
            "• 全量充分训练孪生互相关与尺度回归分支；",
            "",
            "▍ 关键目标：",
            "• 学习物体的通用外观不变性与位移时序相关性，提升泛化表征。"
        ]),
        ("阶段 3：Anti-UAV 领域微调", [
            "▍ 训练数据集：",
            "• Anti-UAV300 专用无人机多模态数据集；",
            "",
            "▍ 核心任务：",
            "• 面向无人机弱小目标与红外低对比度场景微调；",
            "",
            "▍ 关键目标：",
            "• 强化空对空、地对空高速运动下的尺度回归与抗形变鲁棒性。"
        ])
    ]
    for idx, (st_title, st_items) in enumerate(steps):
        st_x = 45 + idx * (step_w + step_gap)
        add_header_card(s13, st_x, 140, step_w, 335, st_title, st_items, title_color=COLOR_BURGUNDY, font_size=10.5)

    # P14: SOT 基准 Pareto 前沿
    print("Building P14: SOT 基准 Pareto 前沿...")
    s14 = create_content_slide("二、课题规划与算法演进路线", "SOT 基准算法评测：成功率 (AUC) 与推理帧率 (FPS) 前沿分布", 14)
    pareto_img = os.path.join(BASE_DIR, "pareto_speed_vs_auc_3color.png")
    s14.Shapes.AddPicture(pareto_img, False, True, 45, 140, 510, 335)
    add_header_card(s14, 575, 140, 340, 195, "算法分级对比（3色离散标注）", [
        "• 重型高精度模型（红色）：",
        "  OSTrack (AUC 0.656)、SiamCAR (0.609)，精度高但参数大 (20~100M)，边缘实时性吃紧；",
        "• 轻型深度模型（绿色）：",
        "  LightTrackM (AUC 0.522, 37.2 FPS, 丢跟率最低 14.16%)、NanoTrack 系列，处于速度-精度均衡最优前沿；",
        "• 传统算法（蓝色）：",
        "  DSST (63.9 FPS) 速度快，但 AUC 仅 0.245，跟丢率高达 64.35%。"
    ], font_size=10.5)
    add_header_card(s14, 575, 345, 340, 130, "选型与演进结论", [
        "• 传统算法帧率充裕但表征容量严重不足，极易跟丢；",
        "• 轻量深度模型处于 Pareto 最优前沿，兼顾实时性与泛化性，为最终工程选型方向。"
    ], font_size=10.5)

    # P15: 实验执行规划与工程退路
    print("Building P15: 实验执行规划与工程退路...")
    s15 = create_content_slide("二、课题规划与算法演进路线", "基准推进计划与工程保底机制（退路）", 15)
    add_header_card(s15, 45, 140, 420, 335, "基线实验两批次推进计划", [
        "▍ 依据 LightTrack 实验执行手册规范：",
        "",
        "• 批次一：",
        "  - 完成 RepViT 骨干网络权重移植与前向特征对齐校验；",
        "  - 跑通 GOT-10k 通用跟踪全量训练基线。",
        "",
        "• 批次二：",
        "  - 完成 Anti-UAV300 领域自适应微调；",
        "  - 导出纯静态 ONNX 模型并在边缘计算平台测试推理时延。"
    ], font_size=10.5)
    add_header_card(s15, 495, 140, 420, 335, "工程保底机制（退路预案）", [
        "▍ 核心原则：工程落地与论文实验闭环优先",
        "",
        "▍ 退路策略：",
        "• 若自主研发的 LightTrack-RepViT 在 Anti-UAV 上的综合指标打不过其他轻型网络；",
        "• 直接选用基准测试中综合表现最优的轻量网络（如 Pareto 前沿上的最优轻量分支），确保上位机工程联调进度不受单点算法探索影响。"
    ], font_size=10.5)

    # Reorganize Ending Slide:
    # Move Slide 18 (original ending) to the end of the presentation
    print("Moving and formatting ending slide...")
    s_end = pres.Slides(18)
    s_end.MoveTo(pres.Slides.Count)
    for sh in s_end.Shapes:
        if sh.HasTextFrame:
            t = sh.TextFrame.TextRange.Text.strip()
            if "汇报人" in t:
                setup_text(sh.TextFrame.TextRange, "汇报人：李行  2026年11月14日", size=16, color=COLOR_TEXT_MAIN)
            elif "组会" in t:
                setup_text(sh.TextFrame.TextRange, "组会汇报", size=14, bold=True, color=COLOR_BURGUNDY)

    # Delete the 15 original template slides (slides 3 to 17)
    print("Deleting unused original template slides...")
    for _ in range(15):
        pres.Slides(3).Delete()

    print(f"Final presentation total slides: {pres.Slides.Count}")

    # Save to DST_PPTX
    print(f"Saving presentation to {DST_PPTX}...")
    pres.SaveAs(DST_PPTX)
    pres.Close()
    ppt.Quit()
    print(">>> Generation Completed Successfully! <<<")

if __name__ == "__main__":
    build_all()
