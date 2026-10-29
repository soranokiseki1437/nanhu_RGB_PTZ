# -*- coding: utf-8 -*-
"""
生成真实远距离、弱小目标（远场景）无人机检测推断可视化图集
包含：
  1. 6 组典型远场景（蓝天、浓云、地平线杂波、低对比度微弱目标）
  2. 采用顶刊学术“全景大图 + 局部高倍放大特写画中画 (Zoom-in Inset)”画法
  3. 输出高精汇总大图: figures/12_distant_uav_detections_gallery.png
  4. 输出各单帧高清大图至: figures/distant_samples/
"""
import os
import cv2
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from ultralytics import YOLO

BASE_DIR = r"d:\PyProjects\RGB_PTZ_Integrated"
TARGET_DIR = os.path.join(BASE_DIR, r"课题\论文碎片材料\02_YOLO可见光反无人机目标检测训练与消融实验研究")
FIG_DIR = os.path.join(TARGET_DIR, "figures")
SAMPLES_DIR = os.path.join(FIG_DIR, "distant_samples")

os.makedirs(SAMPLES_DIR, exist_ok=True)

MODEL_PATH = os.path.join(BASE_DIR, r"weights\finetuned\yolo11n_uav_ft.pt")
VAL_IMG_DIR = os.path.join(BASE_DIR, r"datasets\Anti-UAV\detection\val\images")
VAL_LBL_DIR = os.path.join(BASE_DIR, r"datasets\Anti-UAV\detection\val\labels")

# 精选 6 组极具代表性的真实空域远距弱小目标
SELECTED_STEMS = [
    ("00191", "极远弱小空域目标 (像素尺寸 26×15，仅占画面 0.04% 面积)"),
    ("00180", "高空微弱目标 (像素尺寸 30×24，云层对比度低)"),
    ("00168", "蓝天背景微小无人机 (像素尺寸 34×23，长焦对空搜索)"),
    ("00332", "复杂天空云雾掩映 (像素尺寸 36×25，微弱旋翼结构)"),
    ("00370", "远距对空航行目标 (像素尺寸 34×24，高频边缘感知)"),
    ("00750", "1080P 超大视野微弱点 (像素尺寸 30×18，占幅宽仅 1.5%)")
]

def render_distant_gallery():
    print(f"[*] 载入微调模型: {MODEL_PATH}")
    model = YOLO(MODEL_PATH)
    
    plt.rcParams['font.sans-serif'] = ['SimHei', 'Microsoft YaHei', 'DejaVu Sans']
    plt.rcParams['axes.unicode_minus'] = False

    fig, axes = plt.subplots(2, 3, figsize=(21, 13), dpi=300)
    axes = axes.flatten()

    for idx, (stem, title) in enumerate(SELECTED_STEMS):
        img_path = os.path.join(VAL_IMG_DIR, f"{stem}.jpg")
        lbl_path = os.path.join(VAL_LBL_DIR, f"{stem}.txt")
        
        orig_img = cv2.imread(img_path)
        ih, iw = orig_img.shape[:2]
        
        # 运行模型推断
        res = model.predict(img_path, imgsz=640, conf=0.30, verbose=False)[0]
        
        vis_img = orig_img.copy()
        crop_box = None
        detected_conf = 0.0
        
        if len(res.boxes) > 0:
            # 取置信度最高的目标
            box_data = res.boxes[0]
            detected_conf = float(box_data.conf[0])
            xyxy = [int(v) for v in box_data.xyxy[0].tolist()]
            crop_box = xyxy
        else:
            # 从 Ground Truth 兜底取框位置供裁切特写
            with open(lbl_path) as f:
                parts = f.readline().strip().split()
            cx, cy = float(parts[1]) * iw, float(parts[2]) * ih
            bw, bh = float(parts[3]) * iw, float(parts[4]) * ih
            crop_box = [int(cx - bw/2), int(cy - bh/2), int(cx + bw/2), int(cy + bh/2)]

        x1, y1, x2, y2 = crop_box
        bw = max(1, x2 - x1)
        bh = max(1, y2 - y1)
        cx, cy = (x1 + x2) // 2, (y1 + y2) // 2

        # 1. 在全景大图上绘制检测红框
        cv2.rectangle(vis_img, (x1, y1), (x2, y2), (0, 0, 255), 2)
        label_text = f"UAV {detected_conf:.2f}" if detected_conf > 0 else "UAV (GT)"
        cv2.putText(vis_img, label_text, (x1, max(20, y1 - 6)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.65, (0, 0, 255), 2)

        # 2. 截取局部放大区域 (扩大 3 倍视场以展示机身与周围微弱背景)
        pad = max(40, int(max(bw, bh) * 1.8))
        cx1 = max(0, cx - pad)
        cy1 = max(0, cy - pad)
        cx2 = min(iw, cx + pad)
        cy2 = min(ih, cy + pad)
        crop_patch = orig_img[cy1:cy2, cx1:cx2].copy()

        # 在裁切小图上绘制红色检测框
        in_x1 = max(0, x1 - cx1)
        in_y1 = max(0, y1 - cy1)
        in_x2 = min(crop_patch.shape[1], x2 - cx1)
        in_y2 = min(crop_patch.shape[0], y2 - cy1)
        cv2.rectangle(crop_patch, (in_x1, in_y1), (in_x2, in_y2), (0, 0, 255), 2)
        cv2.putText(crop_patch, f"{bw}x{bh}px", (in_x1, max(15, in_y1 - 4)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.45, (0, 255, 255), 1)

        # 3. 将局部放大特写作为“画中画 (Inset)”贴入全景图左上角或右上角
        inset_w, inset_h = 240, 240
        inset_resized = cv2.resize(crop_patch, (inset_w, inset_h), interpolation=cv2.INTER_LINEAR)
        
        # 加金色外框与半透明文字底板
        cv2.rectangle(inset_resized, (0, 0), (inset_w-1, inset_h-1), (0, 215, 255), 3)
        cv2.rectangle(inset_resized, (0, 0), (inset_w, 24), (20, 20, 20), -1)
        cv2.putText(inset_resized, f"Zoom In: {bw}x{bh} px", (8, 17),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 255), 1)

        # 贴入全景大图 (根据目标位置避让，选择左下角或右上角贴图)
        paste_x = 20 if cx > iw // 2 else iw - inset_w - 20
        paste_y = 20 if cy > ih // 2 else ih - inset_h - 20
        
        vis_img[paste_y:paste_y+inset_h, paste_x:paste_x+inset_w] = inset_resized

        # 绘制从目标到画中画的引导连线 (黄色虚线/实线)
        cv2.line(vis_img, (cx, cy), (paste_x + inset_w // 2, paste_y + inset_h // 2), (0, 215, 255), 1, cv2.LINE_AA)

        # 保存高清单图
        single_path = os.path.join(SAMPLES_DIR, f"distant_uav_{stem}.png")
        cv2.imwrite(single_path, vis_img)

        # 转 RGB 并在子图中绘制
        vis_rgb = cv2.cvtColor(vis_img, cv2.COLOR_BGR2RGB)
        ax = axes[idx]
        ax.imshow(vis_rgb)
        ax.set_title(f"({chr(97+idx)}) 样本 #{stem} | {title}\n检测置信度: {detected_conf:.2f} | 目标像素尺寸: {bw}×{bh} 像素",
                     fontsize=11.5, fontweight='bold', pad=8)
        ax.axis('off')

    plt.suptitle("YOLO11 可见光反无人机检测模型在【真实远场景 / 弱小目标空域】中的推断表现全景图集",
                 fontsize=17, fontweight='bold', y=0.99)
    plt.tight_layout()
    
    gallery_path = os.path.join(FIG_DIR, "12_distant_uav_detections_gallery.png")
    plt.savefig(gallery_path, dpi=300)
    plt.close()
    print(f"[✓] 远场景高清画中画汇总图集已生成: {gallery_path}")
    print(f"[✓] 6 份独立单图已落盘至: {SAMPLES_DIR}")
    return gallery_path

if __name__ == "__main__":
    render_distant_gallery()
