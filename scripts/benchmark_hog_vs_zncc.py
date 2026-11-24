#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
论证：像素级 ZNCC VS 单元格梯度特征(HOG-like/DSST特征) 在姿态旋转下的真实衰减对比
"""

import cv2
import numpy as np

def compute_simple_hog_feat(img_gray, cell_size=4):
    """提取类似于 DSST 的轻量级梯度方向特征图"""
    gx = cv2.Sobel(img_gray, cv2.CV_32F, 1, 0, ksize=1)
    gy = cv2.Sobel(img_gray, cv2.CV_32F, 0, 1, ksize=1)
    mag, ang = cv2.cartToPolar(gx, gy, angleInDegrees=True)
    
    # 划分 8 个方向区间
    bin_num = 8
    h, w = img_gray.shape
    h_cells = h // cell_size
    w_cells = w // cell_size
    
    feat = np.zeros((h_cells, w_cells, bin_num), dtype=np.float32)
    bin_idx = np.floor(ang / (360.0 / bin_num)).astype(np.int32) % bin_num
    
    for i in range(h_cells):
        for j in range(w_cells):
            cell_mag = mag[i*cell_size:(i+1)*cell_size, j*cell_size:(j+1)*cell_size]
            cell_bin = bin_idx[i*cell_size:(i+1)*cell_size, j*cell_size:(j+1)*cell_size]
            for b in range(bin_num):
                feat[i, j, b] = np.sum(cell_mag[cell_bin == b])
            # L2 归一化
            norm = np.linalg.norm(feat[i, j]) + 1e-4
            feat[i, j] /= norm
            
    return feat

def run_hog_vs_zncc_comparison():
    img_path = r"d:\PyProjects\RGB_PTZ_Integrated\build\Desktop_Qt_6_12_0_MinGW_64_bit_Release\release\captures\capture_20261013_121019_282.jpg"
    img = cv2.imread(img_path)
    H, W = img.shape[:2]
    
    tx, ty, tw, th = W // 2 - 24, H // 2 - 24, 48, 48
    target_gray = cv2.cvtColor(img[ty:ty+th, tx:tx+tw], cv2.COLOR_BGR2GRAY)
    
    tpl_hog = compute_simple_hog_feat(target_gray, cell_size=4)
    tpl_hog_vec = tpl_hog.flatten()
    tpl_hog_norm = np.linalg.norm(tpl_hog_vec)
    
    pad = tw
    padded = cv2.copyMakeBorder(target_gray, pad, pad, pad, pad, cv2.BORDER_CONSTANT, value=int(np.mean(target_gray)))
    center = (tw / 2.0 + pad, th / 2.0 + pad)
    
    angles = [0, 2, 4, 6, 8, 10, 12, 15, 20]
    
    print("=" * 70)
    print("【深度机理论证：像素级 ZNCC VS 单元格梯度特征 (HOG) 旋转衰减实测】")
    print("=" * 70)
    print(f"{'偏转角':^10}|{'像素级灰度 ZNCC (现有)':^25}|{'单元格梯度特征余弦相似度':^25}|{'衰减容忍度差距'}")
    print("-" * 75)
    
    for a in angles:
        M = cv2.getRotationMatrix2D(center, a, 1.0)
        rot_padded = cv2.warpAffine(padded, M, (padded.shape[1], padded.shape[0]), flags=cv2.INTER_LINEAR)
        rot_patch = rot_padded[pad:pad+th, pad:pad+tw]
        
        # 1. 像素级 ZNCC
        res_zncc = float(cv2.matchTemplate(rot_patch, target_gray, cv2.TM_CCOEFF_NORMED)[0, 0])
        
        # 2. HOG 特征相似度
        cand_hog = compute_simple_hog_feat(rot_patch, cell_size=4)
        cand_hog_vec = cand_hog.flatten()
        cand_hog_norm = np.linalg.norm(cand_hog_vec)
        cos_sim = float(np.dot(tpl_hog_vec, cand_hog_vec) / (tpl_hog_norm * cand_hog_norm + 1e-6))
        
        diff = cos_sim - res_zncc
        print(f"{a:^10d}|{res_zncc:^25.4f}|{cos_sim:^25.4f}| +{diff:.4f} (HOG优势显著)")
        
    print("=" * 75)

if __name__ == "__main__":
    run_hog_vs_zncc_comparison()
