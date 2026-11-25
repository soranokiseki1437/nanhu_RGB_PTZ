#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
论证：级联微角度展开与 HSV 颜色锚点结合后，能否真正解决 4°~15° 偏转时的漏检与背景假峰劫持？
"""

import cv2
import numpy as np

def run_cascade_verification():
    img_path = r"d:\PyProjects\RGB_PTZ_Integrated\build\Desktop_Qt_6_12_0_MinGW_64_bit_Release\release\captures\capture_20261013_121019_282.jpg"
    img = cv2.imread(img_path)
    H, W = img.shape[:2]
    
    # 提取 48x48 目标 Patch
    tx, ty, tw, th = W // 2 - 24, H // 2 - 24, 48, 48
    target_bgr = img[ty:ty+th, tx:tx+tw].copy()
    target_gray = cv2.cvtColor(target_bgr, cv2.COLOR_BGR2GRAY)
    
    # 提取初始模板 HSV 色彩直方图 (完全对齐 C++ ObjectTracker::extractPatchAppearance)
    target_hsv = cv2.cvtColor(target_bgr, cv2.COLOR_BGR2HSV)
    hist_hs = cv2.calcHist([target_hsv], [0, 1], None, [16, 8], [0, 180, 0, 256])
    cv2.normalize(hist_hs, hist_hs, 1.0, 0.0, cv2.NORM_L1)
    
    print("=" * 70)
    print("【深层对比论证：单一 ZNCC 方案 VS 增强级联方案 (Angle + HSV 锚点)】")
    print("=" * 70)
    
    angles = [0, 2, 4, 6, 8, 10, 12, 15]
    
    # 构造旋转目标
    pad = tw
    padded = cv2.copyMakeBorder(target_bgr, pad, pad, pad, pad, cv2.BORDER_CONSTANT, value=[128, 128, 128])
    center = (tw / 2.0 + pad, th / 2.0 + pad)
    
    print(f"{'偏转角':^8}|{'现有方案(裸ZNCC)':^20}|{'增强方案(带±10°展开)':^24}|{'HSV颜色一致性(巴氏)':^20}|{'最终恢复判定':^14}")
    print("-" * 88)
    
    for a in angles:
        M = cv2.getRotationMatrix2D(center, a, 1.0)
        rot_bgr_padded = cv2.warpAffine(padded, M, (padded.shape[1], padded.shape[0]), flags=cv2.INTER_LINEAR)
        rot_bgr = rot_bgr_padded[pad:pad+th, pad:pad+tw]
        rot_gray = cv2.cvtColor(rot_bgr, cv2.COLOR_BGR2GRAY)
        
        # 1. 现有方案：单模板（0°）ZNCC
        r_single = float(cv2.matchTemplate(rot_gray, target_gray, cv2.TM_CCOEFF_NORMED)[0, 0])
        
        # 2. 增强方案：3 角度 [-10°, 0°, +10°] 模板取最大响应
        best_angle_score = -1.0
        c_tpl = (tw / 2.0, th / 2.0)
        for ang_tpl in [-10.0, 0.0, 10.0]:
            if ang_tpl == 0.0:
                tpl = target_gray
            else:
                M_tpl = cv2.getRotationMatrix2D(c_tpl, ang_tpl, 1.0)
                tpl = cv2.warpAffine(target_gray, M_tpl, (tw, th))
            res = float(cv2.matchTemplate(rot_gray, tpl, cv2.TM_CCOEFF_NORMED)[0, 0])
            best_angle_score = max(best_angle_score, res)
            
        # 3. 颜色直方图巴氏相似度 (1 - d)
        rot_hsv = cv2.cvtColor(rot_bgr, cv2.COLOR_BGR2HSV)
        cand_hist = cv2.calcHist([rot_hsv], [0, 1], None, [16, 8], [0, 180, 0, 256])
        cv2.normalize(cand_hist, cand_hist, 1.0, 0.0, cv2.NORM_L1)
        d = cv2.compareHist(hist_hs, cand_hist, cv2.HISTCMP_BHATTACHARYYA)
        color_sim = max(0.0, 1.0 - d * d)
        
        # 判定
        single_ok = (r_single >= 0.50)
        cascade_ok = (best_angle_score >= 0.50 and color_sim >= 0.40)
        
        status = "✅ 增强方案成功" if cascade_ok else ("⚠️ 仅微小角可重捕" if single_ok else "❌ 全部漏检")
        print(f"{a:^8d}|{r_single:^20.4f}|{best_angle_score:^24.4f}|{color_sim:^20.4f}|{status:^14}")
        
    print("=" * 88)

if __name__ == "__main__":
    run_cascade_verification()
