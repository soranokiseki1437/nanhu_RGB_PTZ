#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Class-Agnostic Backup Re-Detection Benchmark: Route A vs Route B
Evaluation on Anti-UAV real tracking sequences.

Route A: Multi-Scale Pyramid ZNCC (s=0.25/0.50, 3 scales) + HSV Color Anchor Verification
         (Strictly aligned with 待执行方案/archive/临时方案_基于金字塔多尺度ZNCC模板匹配的无类别保底重捕方案.md)
Route B: Deep SOT (LightTrack ONNX) 6-Tile Full-Frame Tiling (3x2 grid of 768x768 tiles)

Metrics aligned with AGENTS.md Rule III.1 (CVPR/ICCV tracking standards):
- Success AUC (IoU curve [0, 1])
- DP@20 (CLE <= 20px)
- Mean CLE (Center Location Error in pixels on valid frames)
- Mean IoU (on valid frames)
- Detection Rate / Recovery Success Rate
- False Positive Rate on Target-Absent Frames (Rejecting background clutter)
- Latency (ms per full 1080P frame)
"""

import os
import sys
import glob
import time
import json
import numpy as np
import cv2
import onnxruntime as ort

def compute_iou(b1, b2):
    if b1 is None or b2 is None:
        return 0.0
    x1 = max(b1[0], b2[0])
    y1 = max(b1[1], b2[1])
    x2 = min(b1[0] + b1[2], b2[0] + b2[2])
    y2 = min(b1[1] + b1[3], b2[1] + b2[3])
    iw = max(0.0, x2 - x1)
    ih = max(0.0, y2 - y1)
    ia = iw * ih
    ua = b1[2] * b1[3] + b2[2] * b2[3] - ia
    if ua <= 0.0:
        return 0.0
    return float(ia / ua)

def compute_cle(b1, b2):
    if b1 is None or b2 is None:
        return 9999.0
    c1 = (b1[0] + b1[2] / 2.0, b1[1] + b1[3] / 2.0)
    c2 = (b2[0] + b2[2] / 2.0, b2[1] + b2[3] / 2.0)
    return float(np.sqrt((c1[0] - c2[0]) ** 2 + (c1[1] - c2[1]) ** 2))

class RouteAPyramidZNCC:
    """Route A: Pyramid ZNCC + Color Anchor Verification"""
    def __init__(self, zncc_thresh=0.50, color_thresh=0.45):
        self.zncc_thresh = zncc_thresh
        self.color_thresh = color_thresh
        self.init_box = None
        self.tpl_gray = None
        self.hist_hs = None
        self.gw = 0.0
        self.gh = 0.0
        self.s = 0.25

    def init_template(self, img_bgr, box):
        self.init_box = list(box)
        gx, gy, gw, gh = [float(v) for v in box]
        self.gw, self.gh = gw, gh
        
        x1 = max(0, min(img_bgr.shape[1] - 1, int(round(gx))))
        y1 = max(0, min(img_bgr.shape[0] - 1, int(round(gy))))
        x2 = max(x1 + 1, min(img_bgr.shape[1], int(round(gx + gw))))
        y2 = max(y1 + 1, min(img_bgr.shape[0], int(round(gy + gh))))
        
        tpl_bgr = img_bgr[y1:y2, x1:x2].copy()
        self.tpl_gray = cv2.cvtColor(tpl_bgr, cv2.COLOR_BGR2GRAY)
        
        tpl_hsv = cv2.cvtColor(tpl_bgr, cv2.COLOR_BGR2HSV)
        hist = cv2.calcHist([tpl_hsv], [0, 1], None, [16, 8], [0, 180, 0, 256])
        cv2.normalize(hist, hist, 1.0, 0.0, cv2.NORM_L1)
        self.hist_hs = hist
        
        min_dim = min(gw, gh)
        self.s = 0.25 if min_dim >= 32 else 0.50

    def redetect(self, img_bgr):
        t0 = time.perf_counter()
        H, W = img_bgr.shape[:2]
        gray_full = cv2.cvtColor(img_bgr, cv2.COLOR_BGR2GRAY)
        gray_small = cv2.resize(gray_full, (0, 0), fx=self.s, fy=self.s, interpolation=cv2.INTER_AREA)
        
        best_score = -1.0
        best_loc = (0, 0)
        best_scale = 1.0
        best_wt, best_ht = 0, 0
        
        for sc in [0.85, 1.00, 1.15]:
            wt = max(8, min(gray_small.shape[1] - 2, int(round(self.gw * self.s * sc))))
            ht = max(8, min(gray_small.shape[0] - 2, int(round(self.gh * self.s * sc))))
            tpl_sc = cv2.resize(self.tpl_gray, (wt, ht), interpolation=cv2.INTER_AREA)
            
            res = cv2.matchTemplate(gray_small, tpl_sc, cv2.TM_CCOEFF_NORMED)
            min_v, max_v, min_l, max_l = cv2.minMaxLoc(res)
            if max_v > best_score:
                best_score = max_v
                best_loc = max_l
                best_scale = sc
                best_wt, best_ht = wt, ht
                
        t_elap = (time.perf_counter() - t0) * 1000.0
        
        if best_score < self.zncc_thresh:
            return None, float(best_score), False, t_elap # Rejected by ZNCC
            
        cx = (best_loc[0] + best_wt / 2.0) / self.s
        cy = (best_loc[1] + best_ht / 2.0) / self.s
        w_recov = self.gw * best_scale
        h_recov = self.gh * best_scale
        cand_box = [cx - w_recov / 2.0, cy - h_recov / 2.0, w_recov, h_recov]
        
        # Color Anchor Verification
        bx1 = max(0, min(W - 1, int(round(cand_box[0]))))
        by1 = max(0, min(H - 1, int(round(cand_box[1]))))
        bx2 = max(bx1 + 1, min(W, int(round(cand_box[0] + cand_box[2]))))
        by2 = max(by1 + 1, min(H, int(round(cand_box[1] + cand_box[3]))))
        
        cand_patch = img_bgr[by1:by2, bx1:bx2]
        cand_hsv = cv2.cvtColor(cand_patch, cv2.COLOR_BGR2HSV)
        cand_hist = cv2.calcHist([cand_hsv], [0, 1], None, [16, 8], [0, 180, 0, 256])
        cv2.normalize(cand_hist, cand_hist, 1.0, 0.0, cv2.NORM_L1)
        
        d = cv2.compareHist(self.hist_hs, cand_hist, cv2.HISTCMP_BHATTACHARYYA)
        color_sim = max(0.0, 1.0 - d * d)
        
        t_elap = (time.perf_counter() - t0) * 1000.0
        
        if color_sim < self.color_thresh:
            return None, float(best_score), False, t_elap # Rejected by Color Anchor
            
        final_score = 0.5 * best_score + 0.5 * color_sim
        return cand_box, float(final_score), True, t_elap

class RouteBLightTrackTiling:
    """Route B: Deep SOT (LightTrack) 6-Tile Full-Frame Tiling"""
    def __init__(self, models_dir="models", score_thresh=0.60):
        self.score_thresh = score_thresh
        opts = ort.SessionOptions()
        opts.intra_op_num_threads = 4
        opts.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
        
        tmpl_path = os.path.join(models_dir, "lighttrack_template.onnx")
        search_path = os.path.join(models_dir, "lighttrack_search_288.onnx")
        
        self.sess_tmpl = ort.InferenceSession(tmpl_path, opts, providers=["CPUExecutionProvider"])
        self.sess_search = ort.InferenceSession(search_path, opts, providers=["CPUExecutionProvider"])
        self.tmpl_feat = None
        
        sz = 18
        sz_x = sz // 2
        sz_y = sz // 2
        x, y = np.meshgrid(np.arange(0, sz) - np.floor(float(sz_x)),
                           np.arange(0, sz) - np.floor(float(sz_y)))
        self.grid_x = x * 16 + 288 // 2
        self.grid_y = y * 16 + 288 // 2
        
        # 6-Tile Layout for 1920x1080 (3x2 grid of 768x768 tiles)
        self.tiles = [
            (0, 0, 768, 768),
            (576, 0, 768, 768),
            (1152, 0, 768, 768),
            (0, 312, 768, 768),
            (576, 312, 768, 768),
            (1152, 312, 768, 768),
        ]

    def init_template(self, img_bgr, box):
        gx, gy, gw, gh = [float(v) for v in box]
        cx, cy = gx + gw / 2.0, gy + gh / 2.0
        
        wc_z = gw + 0.5 * (gw + gh)
        hc_z = gh + 0.5 * (gw + gh)
        s_z = np.sqrt(wc_z * hc_z)
        
        x1 = int(round(cx - s_z / 2.0))
        y1 = int(round(cy - s_z / 2.0))
        x2 = x1 + int(round(s_z))
        y2 = y1 + int(round(s_z))
        
        pad_l = max(0, -x1)
        pad_t = max(0, -y1)
        pad_r = max(0, x2 - img_bgr.shape[1])
        pad_b = max(0, y2 - img_bgr.shape[0])
        
        if pad_l > 0 or pad_t > 0 or pad_r > 0 or pad_b > 0:
            avg_bgr = np.mean(img_bgr, axis=(0, 1)).astype(np.uint8)
            im_pad = cv2.copyMakeBorder(img_bgr, pad_t, pad_b, pad_l, pad_r, cv2.BORDER_CONSTANT, value=avg_bgr.tolist())
            patch = im_pad[y1+pad_t:y2+pad_t, x1+pad_l:x2+pad_l]
        else:
            patch = img_bgr[y1:y2, x1:x2]
            
        tmpl_resized = cv2.resize(patch, (127, 127), interpolation=cv2.INTER_LINEAR)
        tmpl_rgb = cv2.cvtColor(tmpl_resized, cv2.COLOR_BGR2RGB).astype(np.float32) / 255.0
        tmpl_tensor = np.transpose(tmpl_rgb, (2, 0, 1))[np.newaxis, ...]
        
        self.tmpl_feat = self.sess_tmpl.run(["template_feat"], {"template_img": tmpl_tensor})[0]

    def redetect(self, img_bgr):
        t0 = time.perf_counter()
        best_score = -1.0
        best_box = None
        
        for (tx, ty, tw, th) in self.tiles:
            tile_bgr = img_bgr[ty:ty+th, tx:tx+tw]
            tile_resized = cv2.resize(tile_bgr, (288, 288), interpolation=cv2.INTER_LINEAR)
            tile_rgb = cv2.cvtColor(tile_resized, cv2.COLOR_BGR2RGB).astype(np.float32) / 255.0
            search_tensor = np.transpose(tile_rgb, (2, 0, 1))[np.newaxis, ...]
            
            cls_logit, reg_dist = self.sess_search.run(["cls_logit", "reg_dist"], {
                "search_img": search_tensor,
                "template_feat": self.tmpl_feat
            })
            
            cls_score = 1.0 / (1.0 + np.exp(-cls_logit[0, 0]))
            r_max, c_max = np.unravel_index(cls_score.argmax(), cls_score.shape)
            tile_max = float(cls_score[r_max, c_max])
            
            if tile_max > best_score:
                best_score = tile_max
                
                bbox_pred = reg_dist[0]
                l = bbox_pred[0, r_max, c_max]
                t = bbox_pred[1, r_max, c_max]
                r = bbox_pred[2, r_max, c_max]
                b = bbox_pred[3, r_max, c_max]
                
                px1 = self.grid_x[r_max, c_max] - l
                py1 = self.grid_y[r_max, c_max] - t
                px2 = self.grid_x[r_max, c_max] + r
                py2 = self.grid_y[r_max, c_max] + b
                
                scale = tw / 288.0
                bx1 = tx + px1 * scale
                by1 = ty + py1 * scale
                bx2 = tx + px2 * scale
                by2 = ty + py2 * scale
                
                best_box = [float(bx1), float(by1), float(bx2 - bx1), float(by2 - by1)]
                
        t_elap = (time.perf_counter() - t0) * 1000.0
        
        if best_score < self.score_thresh:
            return None, float(best_score), False, t_elap # Rejected
            
        return best_box, float(best_score), True, t_elap

def run_benchmark():
    sequences = [
        {"name": "video01", "step": 5, "desc": "Daylight medium UAV, smooth flight"},
        {"name": "video03", "step": 2, "desc": "Distant small UAV (42x40)"},
        {"name": "video04", "step": 3, "desc": "UAV with tree/building clutter"},
        {"name": "video05", "step": 4, "desc": "UAV maneuvering and turning"},
        {"name": "video07", "step": 10, "desc": "Long sequence with absent frames"},
        {"name": "video11", "step": 8, "desc": "Medium UAV attitude change"},
        {"name": "video16", "step": 10, "desc": "Small UAV, low contrast"},
    ]
    
    print("=" * 80)
    print("CLASS-AGNOSTIC BACKUP RE-DETECTION BENCHMARK: ROUTE A VS ROUTE B")
    print("Dataset: Anti-UAV 1080P Sequences | Pure Full-Frame Re-detection (Zero Prior)")
    print("=" * 80)
    
    r_a = RouteAPyramidZNCC()
    r_b = RouteBLightTrackTiling()
    
    overall_results = {"RouteA": {}, "RouteB": {}}
    for m in ["RouteA", "RouteB"]:
        overall_results[m] = {
            "valid_frames": 0,
            "ious": [],
            "cles": [],
            "latencies": [],
            "accepted_count": 0,
            "absent_frames": 0,
            "absent_false_positives": 0,
        }
        
    per_seq_table = []
    
    for seq_info in sequences:
        sname = seq_info["name"]
        step = seq_info["step"]
        desc = seq_info["desc"]
        
        gt_path = f"datasets/Anti-UAV/tracking/Anti-UAV-Tracking-V0GT/{sname}_gt.txt"
        img_dir = f"datasets/Anti-UAV/tracking/Anti-UAV-Tracking-V0/{sname}"
        
        if not os.path.exists(gt_path) or not os.path.isdir(img_dir):
            print(f"Skipping {sname}: files not found.")
            continue
            
        with open(gt_path) as f:
            gt_lines = [l.strip() for l in f if l.strip()]
            
        gts = []
        for l in gt_lines:
            pts = [float(v) for v in l.replace(",", " ").split() if v]
            if len(pts) >= 4:
                gts.append(pts[:4])
                
        img_files = sorted(glob.glob(os.path.join(img_dir, "*.jpg")) + glob.glob(os.path.join(img_dir, "*.png")))
        n_frames = min(len(img_files), len(gts))
        
        if n_frames < 2:
            continue
            
        # Init both trackers on Frame 0
        img0 = cv2.imread(img_files[0])
        gt0 = gts[0]
        r_a.init_template(img0, gt0)
        r_b.init_template(img0, gt0)
        
        seq_stats = {
            "name": sname,
            "desc": desc,
            "total_frames_sampled": 0,
            "valid_sampled": 0,
            "absent_sampled": 0,
            "RouteA": {"ious": [], "cles": [], "times": [], "acc": 0, "absent_fp": 0},
            "RouteB": {"ious": [], "cles": [], "times": [], "acc": 0, "absent_fp": 0},
        }
        
        # Test frames
        for f_idx in range(1, n_frames, step):
            gt = gts[f_idx]
            is_valid = (gt[2] > 0 and gt[3] > 0)
            
            img_curr = cv2.imread(img_files[f_idx])
            if img_curr is None:
                continue
                
            seq_stats["total_frames_sampled"] += 1
            if is_valid:
                seq_stats["valid_sampled"] += 1
            else:
                seq_stats["absent_sampled"] += 1
                
            # 1. Run Route A
            box_a, score_a, acc_a, t_a = r_a.redetect(img_curr)
            seq_stats["RouteA"]["times"].append(t_a)
            overall_results["RouteA"]["latencies"].append(t_a)
            
            if is_valid:
                overall_results["RouteA"]["valid_frames"] += 1
                if acc_a:
                    seq_stats["RouteA"]["acc"] += 1
                    overall_results["RouteA"]["accepted_count"] += 1
                    iou_a = compute_iou(box_a, gt)
                    cle_a = compute_cle(box_a, gt)
                else:
                    iou_a = 0.0
                    cle_a = 9999.0
                seq_stats["RouteA"]["ious"].append(iou_a)
                seq_stats["RouteA"]["cles"].append(cle_a)
                overall_results["RouteA"]["ious"].append(iou_a)
                overall_results["RouteA"]["cles"].append(cle_a)
            else:
                overall_results["RouteA"]["absent_frames"] += 1
                if acc_a:
                    seq_stats["RouteA"]["absent_fp"] += 1
                    overall_results["RouteA"]["absent_false_positives"] += 1
                    
            # 2. Run Route B
            box_b, score_b, acc_b, t_b = r_b.redetect(img_curr)
            seq_stats["RouteB"]["times"].append(t_b)
            overall_results["RouteB"]["latencies"].append(t_b)
            
            if is_valid:
                overall_results["RouteB"]["valid_frames"] += 1
                if acc_b:
                    seq_stats["RouteB"]["acc"] += 1
                    overall_results["RouteB"]["accepted_count"] += 1
                    iou_b = compute_iou(box_b, gt)
                    cle_b = compute_cle(box_b, gt)
                else:
                    iou_b = 0.0
                    cle_b = 9999.0
                seq_stats["RouteB"]["ious"].append(iou_b)
                seq_stats["RouteB"]["cles"].append(cle_b)
                overall_results["RouteB"]["ious"].append(iou_b)
                overall_results["RouteB"]["cles"].append(cle_b)
            else:
                overall_results["RouteB"]["absent_frames"] += 1
                if acc_b:
                    seq_stats["RouteB"]["absent_fp"] += 1
                    overall_results["RouteB"]["absent_false_positives"] += 1
                    
        # Summary for this sequence
        def calc_seq_metrics(d, n_valid):
            if n_valid == 0:
                return 0.0, 0.0, 0.0, 0.0
            ious = np.array(d["ious"])
            cles = np.array(d["cles"])
            auc = float(np.mean([np.mean(ious >= th) for th in np.linspace(0, 1, 21)]))
            dp20 = float(np.mean(cles <= 20.0))
            mean_cle = float(np.mean(cles[cles < 9000])) if np.sum(cles < 9000) > 0 else 9999.0
            avg_t = float(np.mean(d["times"]))
            return auc, dp20, mean_cle, avg_t
            
        nv = seq_stats["valid_sampled"]
        auc_a, dp_a, cle_a, t_a = calc_seq_metrics(seq_stats["RouteA"], nv)
        auc_b, dp_b, cle_b, t_b = calc_seq_metrics(seq_stats["RouteB"], nv)
        
        per_seq_table.append({
            "seq": sname,
            "desc": desc,
            "n_valid": nv,
            "n_absent": seq_stats["absent_sampled"],
            "A_AUC": auc_a, "A_DP20": dp_a, "A_CLE": cle_a, "A_ms": t_a, "A_FP": seq_stats["RouteA"]["absent_fp"],
            "B_AUC": auc_b, "B_DP20": dp_b, "B_CLE": cle_b, "B_ms": t_b, "B_FP": seq_stats["RouteB"]["absent_fp"],
        })
        print(f"[{sname:8s}] Valid: {nv:3d} | A: AUC={auc_a:.3f}, DP20={dp_a:.3f}, CLE={cle_a:5.1f}px, {t_a:4.1f}ms | B: AUC={auc_b:.3f}, DP20={dp_b:.3f}, CLE={cle_b:5.1f}px, {t_b:4.1f}ms")

    # Overall Summary
    print("\n" + "=" * 80)
    print("OVERALL SUMMARY EVALUATION (ACROSS ALL SEQUENCES)")
    print("=" * 80)
    
    for route in ["RouteA", "RouteB"]:
        res = overall_results[route]
        nv = res["valid_frames"]
        ious = np.array(res["ious"])
        cles = np.array(res["cles"])
        
        auc = float(np.mean([np.mean(ious >= th) for th in np.linspace(0, 1, 21)]))
        dp20 = float(np.mean(cles <= 20.0))
        mean_cle_all = float(np.mean(np.minimum(cles, 1000.0)))
        hit_mask = (cles < 9000)
        mean_cle_hit = float(np.mean(cles[hit_mask])) if np.sum(hit_mask) > 0 else 9999.0
        sr_50 = float(np.mean(ious >= 0.50))
        mean_iou = float(np.mean(ious))
        lost_rate = float(np.mean(ious == 0.0))
        avg_ms = float(np.mean(res["latencies"]))
        
        absent_tot = res["absent_frames"]
        fp_rate = (res["absent_false_positives"] / absent_tot) if absent_tot > 0 else 0.0
        
        print(f"\n--- {route} Full Metrics ---")
        print(f"  Total Valid Frames Tested: {nv}")
        print(f"  Success AUC:               {auc:.4f} ({auc*100:.2f}%)")
        print(f"  Distance Precision (DP@20):{dp20:.4f} ({dp20*100:.2f}%)")
        print(f"  Mean IoU:                  {mean_iou:.4f}")
        print(f"  Success Rate (IoU >= 0.50):{sr_50:.4f} ({sr_50*100:.2f}%)")
        print(f"  Mean CLE (Hit frames):     {mean_cle_hit:.2f} px")
        print(f"  Lost / Miss Rate:          {lost_rate:.4f} ({lost_rate*100:.2f}%)")
        print(f"  Absent Frames Tested:      {absent_tot}")
        print(f"  Absent False Alarm Rate:   {fp_rate:.4f} ({res['absent_false_positives']}/{absent_tot})")
        print(f"  Average Latency:           {avg_ms:.2f} ms/frame")
        
        overall_results[route]["summary"] = {
            "auc": auc, "dp20": dp20, "mean_iou": mean_iou,
            "sr_50": sr_50, "mean_cle_hit": mean_cle_hit,
            "lost_rate": lost_rate, "fp_rate": fp_rate,
            "avg_ms": avg_ms
        }
        
    out_json = {
        "per_sequence": per_seq_table,
        "overall": {
            "RouteA": overall_results["RouteA"]["summary"],
            "RouteB": overall_results["RouteB"]["summary"],
        }
    }
    with open("scripts/benchmark_results.json", "w") as f:
        json.dump(out_json, f, indent=2)
    print("\nBenchmark results successfully saved to scripts/benchmark_results.json")

if __name__ == "__main__":
    run_benchmark()
