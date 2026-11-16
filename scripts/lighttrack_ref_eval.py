#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
LightTrack Python 参考评测与对齐验证流水线。
支持 PyTorch (CUDA) 与 ONNX Runtime (CPU) 双后端。
严格按照方案 3.5 节协议在 Anti-UAV 20 组真机序列上评测，生成 per_frame.csv 与 summary.json。
"""

import os
import sys
import glob
import time
import json
import csv
import argparse
import types
import numpy as np
import cv2
import torch

PATH_NAME = "back_04502514044521042540+cls_211000022+reg_100000111_ops_32"


def compute_iou(box1, box2):
    """
    Compute IoU between two boxes [x, y, w, h].
    """
    x1 = max(box1[0], box2[0])
    y1 = max(box1[1], box2[1])
    x2 = min(box1[0] + box1[2], box2[0] + box2[2])
    y2 = min(box1[1] + box1[3], box2[1] + box2[3])

    inter_w = max(0.0, x2 - x1)
    inter_h = max(0.0, y2 - y1)
    inter_area = inter_w * inter_h

    area1 = max(0.0, box1[2]) * max(0.0, box1[3])
    area2 = max(0.0, box2[2]) * max(0.0, box2[3])
    union_area = area1 + area2 - inter_area

    if union_area <= 0:
        return 0.0
    return float(inter_area / union_area)


def compute_cle(box1, box2):
    """
    Compute Center Location Error (CLE) Euclidean distance in pixels.
    """
    c1 = (box1[0] + box1[2] / 2.0, box1[1] + box1[3] / 2.0)
    c2 = (box2[0] + box2[2] / 2.0, box2[1] + box2[3] / 2.0)
    return float(np.sqrt((c1[0] - c2[0]) ** 2 + (c1[1] - c2[1]) ** 2))


def compute_norm_cle(box1, box2):
    """
    Compute Normalized Center Location Error (normalized by GT bbox size).
    """
    c1 = (box1[0] + box1[2] / 2.0, box1[1] + box1[3] / 2.0)
    c2 = (box2[0] + box2[2] / 2.0, box2[1] + box2[3] / 2.0)
    w_gt = max(1.0, float(box2[2]))
    h_gt = max(1.0, float(box2[3]))
    return float(np.sqrt(((c1[0] - c2[0]) / w_gt) ** 2 + ((c1[1] - c2[1]) / h_gt) ** 2))


def cxy_wh_2_rect(pos, sz):
    """
    Convert center and size to top-left bounding box [x, y, w, h].
    Top-left clamped to >= 0, width and height unclamped.
    """
    return [
        float(max(0.0, pos[0] - sz[0] / 2.0)),
        float(max(0.0, pos[1] - sz[1] / 2.0)),
        float(sz[0]),
        float(sz[1]),
    ]


def load_groundtruth(gt_path):
    gts = []
    with open(gt_path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            cleaned = line.replace(",", " ").replace("\t", " ")
            parts = [float(v) for v in cleaned.split() if v]
            if len(parts) >= 4:
                gts.append(parts[:4])
    return gts


def discover_sequences(dataset_dir, gt_dir, seq_filter=None):
    video_dirs = sorted([d for d in glob.glob(os.path.join(dataset_dir, "*")) if os.path.isdir(d)])
    sequences = []

    for vdir in video_dirs:
        seq_name = os.path.basename(vdir)
        if seq_filter and seq_name not in seq_filter:
            continue
        gt_file = os.path.join(gt_dir, f"{seq_name}_gt.txt")
        if not os.path.exists(gt_file):
            print(f"[-] Warning: GT file not found for {seq_name} at {gt_file}, skipping.")
            continue

        gts = load_groundtruth(gt_file)
        img_files = sorted(glob.glob(os.path.join(vdir, "*.jpg")) + glob.glob(os.path.join(vdir, "*.png")))
        if not img_files:
            print(f"[-] Warning: No images found in {vdir}, skipping.")
            continue

        sequences.append({
            "name": seq_name,
            "img_files": img_files,
            "gts": gts,
        })

    return sequences


class OnnxModelWrapper:
    def __init__(self, models_dir):
        import onnxruntime as ort
        opts = ort.SessionOptions()
        opts.intra_op_num_threads = 4
        opts.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL

        tmpl_path = os.path.join(models_dir, "lighttrack_template.onnx")
        s256_path = os.path.join(models_dir, "lighttrack_search_256.onnx")
        s288_path = os.path.join(models_dir, "lighttrack_search_288.onnx")

        print(f"Loading ONNX template session from {tmpl_path}...")
        self.sess_template = ort.InferenceSession(tmpl_path, opts, providers=["CPUExecutionProvider"])
        print(f"Loading ONNX search 256 session from {s256_path}...")
        self.sess_search_256 = ort.InferenceSession(s256_path, opts, providers=["CPUExecutionProvider"])
        print(f"Loading ONNX search 288 session from {s288_path}...")
        self.sess_search_288 = ort.InferenceSession(s288_path, opts, providers=["CPUExecutionProvider"])
        self.zf = None

    def template(self, z):
        z_np = z.detach().cpu().numpy()
        self.zf = self.sess_template.run(["template_feat"], {"template_img": z_np})[0]

    def track(self, x):
        x_np = x.detach().cpu().numpy()
        sz = x_np.shape[-1]
        sess = self.sess_search_256 if sz == 256 else self.sess_search_288
        cls_logit, reg_dist = sess.run(["cls_logit", "reg_dist"], {"search_img": x_np, "template_feat": self.zf})
        return torch.from_numpy(cls_logit), torch.from_numpy(reg_dist)


def calculate_metrics_from_arrays(all_ious, all_cles, all_norm_cles):
    if len(all_ious) == 0:
        return {
            "auc": 0.0,
            "dp20": 0.0,
            "mean_cle": 0.0,
            "lost_rate": 1.0,
            "sr_50": 0.0,
            "sr_75": 0.0,
            "p_norm": 0.0,
        }

    # 1. AUC (21 bins: linspace(0, 1, 21))
    iou_th = np.linspace(0.0, 1.0, 21)
    succ_curve = np.array([np.mean(all_ious >= th) for th in iou_th])
    auc = float(np.mean(succ_curve))

    # 2. DP@20 (CLE thresholds 0 to 50)
    cle_th = np.arange(0, 51)
    prec_curve = np.array([np.mean(all_cles <= th) for th in cle_th])
    dp20 = float(prec_curve[20])
    mean_cle = float(np.mean(all_cles))

    # 3. P_norm (51 bins in [0.0, 0.5])
    norm_th = np.linspace(0.0, 0.5, 51)
    norm_prec_curve = np.array([np.mean(all_norm_cles <= th) for th in norm_th])
    p_norm = float(np.mean(norm_prec_curve))

    # 4. Robustness
    lost_rate = float(np.mean(all_ious == 0.0))
    sr_50 = float(np.mean(all_ious >= 0.50))
    sr_75 = float(np.mean(all_ious >= 0.75))

    return {
        "auc": auc,
        "dp20": dp20,
        "mean_cle": mean_cle,
        "lost_rate": lost_rate,
        "sr_50": sr_50,
        "sr_75": sr_75,
        "p_norm": p_norm,
    }


def main():
    ap = argparse.ArgumentParser(description="Evaluate LightTrack baseline on Anti-UAV")
    ap.add_argument("--backend", choices=["torch", "onnx"], required=True, help="Inference backend")
    ap.add_argument(
        "--lighttrack-root",
        default=r"D:\PyProjects\SOT_Tracking_Baselines_官方权重试跑\algorithms\LightTrack",
        help="Path to LightTrack algorithm root",
    )
    ap.add_argument("--weights", default="weights/sot/lighttrack_m.pth", help="Path to weights (.pth)")
    ap.add_argument("--onnx-dir", default="models", help="Directory of exported ONNX models")
    ap.add_argument("--data-root", default="datasets/Anti-UAV/tracking", help="Root directory of tracking dataset")
    ap.add_argument("--out", default=None, help="Output directory")
    ap.add_argument("--seqs", default=None, help="Comma-separated sequence names filter")
    a = ap.parse_args()

    if a.out is None:
        a.out = os.path.join("results", "lighttrack", a.backend)
    os.makedirs(a.out, exist_ok=True)

    sys.path.insert(0, a.lighttrack_root)
    import lib.models.models as models
    from lib.utils.utils import load_pretrain
    import lib.tracker.lighttrack as tracker_module
    from lib.tracker.lighttrack import Lighttrack

    # Hook get_subwindow_tracking to record cropping parameters
    latest_crop_info = {}
    orig_subwindow_func = tracker_module.get_subwindow_tracking

    def hooked_get_subwindow_tracking(im, pos, model_sz, original_sz, avg_chans, out_mode="torch"):
        latest_crop_info["pos"] = (float(pos[0]), float(pos[1]))
        latest_crop_info["original_sz"] = int(original_sz)
        return orig_subwindow_func(im, pos, model_sz, original_sz, avg_chans, out_mode=out_mode)

    tracker_module.get_subwindow_tracking = hooked_get_subwindow_tracking

    # Define ONNX Lighttrack subclass if needed
    class OnnxLighttrack(Lighttrack):
        def normalize(self, x):
            return x / 255.0

    # Build model and tracker factory
    if a.backend == "torch":
        print(f"Building PyTorch model from {a.weights}...")
        net = models.__dict__["LightTrackM_Subnet"](PATH_NAME, stride=16)
        net = load_pretrain(net, a.weights, print_unuse=False).eval().cuda()
        tracker_cls = Lighttrack
    else:
        print(f"Building ONNX Runtime model from {a.onnx_dir}...")
        net = OnnxModelWrapper(a.onnx_dir)
        tracker_cls = OnnxLighttrack

    # Discover sequences
    dataset_dir = os.path.join(a.data_root, "Anti-UAV-Tracking-V0")
    gt_dir = os.path.join(a.data_root, "Anti-UAV-Tracking-V0GT")
    seq_filter = [s.strip() for s in a.seqs.split(",")] if a.seqs else None
    sequences = discover_sequences(dataset_dir, gt_dir, seq_filter)
    print(f"Discovered {len(sequences)} sequence(s) to evaluate.")

    per_frame_rows = []
    per_seq_summaries = {}

    all_valid_ious = []
    all_valid_cles = []
    all_valid_norm_cles = []

    total_track_time_ms = 0.0
    total_e2e_time_ms = 0.0
    total_evaluated_frames = 0

    csv_path = os.path.join(a.out, "per_frame.csv")
    csv_file = open(csv_path, "w", newline="", encoding="utf-8")
    csv_writer = csv.writer(csv_file)
    csv_writer.writerow([
        "seq", "frame", "bx", "by", "bw", "bh", "inst",
        "crop_sz", "crop_xmin", "crop_ymin",
        "ax", "ay", "aw", "ah", "cls_max",
        "box_x", "box_y", "box_w", "box_h",
        "iou", "cle", "gt_valid"
    ])

    for s_idx, seq in enumerate(sequences, 1):
        seq_name = seq["name"]
        img_files = seq["img_files"]
        gts = seq["gts"]
        n_frames = min(len(img_files), len(gts))

        print(f"[{s_idx}/{len(sequences)}] Evaluating {seq_name} ({n_frames} frames)...")

        # Instantiate tracker
        info = types.SimpleNamespace(arch="LightTrackM_Subnet", dataset="OTB", epoch_test=False, stride=16)
        tracker = tracker_cls(info, even=0)

        # First frame initialization
        first_img = cv2.imread(img_files[0])
        init_box = gts[0]
        x, y, w, h = [float(v) for v in init_box]
        cx = x + w / 2.0
        cy = y + h / 2.0
        target_pos = np.array([cx, cy], dtype=np.float32)
        target_sz = np.array([w, h], dtype=np.float32)

        rgb_im0 = cv2.cvtColor(first_img, cv2.COLOR_BGR2RGB)
        with torch.no_grad():
            state = tracker.init(rgb_im0, target_pos, target_sz, net)
            state["p"].window_influence = 0.45
            state["p"].penalty_k = 0.15
            state["p"].lr = 0.35

        inst_sz = int(state["p"].instance_size)

        seq_ious = []
        seq_cles = []
        seq_norm_cles = []
        seq_track_ms = []
        seq_e2e_ms = []

        # Record frame 0
        gt0_valid = 1 if (init_box[2] > 0 and init_box[3] > 0) else 0
        iou0 = 1.0 if gt0_valid else 0.0
        cle0 = 0.0 if gt0_valid else 0.0
        norm_cle0 = 0.0 if gt0_valid else 0.0

        if gt0_valid:
            seq_ious.append(iou0)
            seq_cles.append(cle0)
            seq_norm_cles.append(norm_cle0)
            all_valid_ious.append(iou0)
            all_valid_cles.append(cle0)
            all_valid_norm_cles.append(norm_cle0)

        csv_writer.writerow([
            seq_name, 0,
            f"{cx:.4f}", f"{cy:.4f}", f"{w:.4f}", f"{h:.4f}",
            inst_sz, 0, 0, 0,
            f"{cx:.4f}", f"{cy:.4f}", f"{w:.4f}", f"{h:.4f}",
            "1.0000",
            f"{init_box[0]:.4f}", f"{init_box[1]:.4f}", f"{init_box[2]:.4f}", f"{init_box[3]:.4f}",
            f"{iou0:.4f}", f"{cle0:.4f}", gt0_valid
        ])

        prev_box = list(init_box)

        # Track subsequent frames
        for f_idx in range(1, n_frames):
            t_e2e_start = time.perf_counter()
            img = cv2.imread(img_files[f_idx])
            if img is None:
                box = prev_box
                t_track = 0.0
                bx, by = float(state["target_pos"][0]), float(state["target_pos"][1])
                bw, bh = float(state["target_sz"][0]), float(state["target_sz"][1])
                ax, ay, aw, ah = bx, by, bw, bh
                cls_max = 0.0
                crop_sz, crop_xmin, crop_ymin = 0, 0, 0
            else:
                rgb_im = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
                bx = float(state["target_pos"][0])
                by = float(state["target_pos"][1])
                bw = float(state["target_sz"][0])
                bh = float(state["target_sz"][1])

                t_tr_start = time.perf_counter()
                with torch.no_grad():
                    state = tracker.track(state, rgb_im)
                t_tr_end = time.perf_counter()
                t_track = (t_tr_end - t_tr_start) * 1000.0

                ax = float(state["target_pos"][0])
                ay = float(state["target_pos"][1])
                aw = float(state["target_sz"][0])
                ah = float(state["target_sz"][1])

                crop_sz = latest_crop_info.get("original_sz", 0)
                pos = latest_crop_info.get("pos", (0.0, 0.0))
                crop_xmin = round(pos[0] - (crop_sz + 1) / 2.0)
                crop_ymin = round(pos[1] - (crop_sz + 1) / 2.0)

                cls_score_arr = state.get("cls_score", 1.0)
                if isinstance(cls_score_arr, np.ndarray):
                    cls_max = float(cls_score_arr.max())
                elif hasattr(cls_score_arr, "max"):
                    cls_max = float(cls_score_arr.max())
                else:
                    cls_max = float(cls_score_arr)

                box = cxy_wh_2_rect(state["target_pos"], state["target_sz"])
                prev_box = box

            t_e2e_end = time.perf_counter()
            t_e2e = (t_e2e_end - t_e2e_start) * 1000.0

            seq_track_ms.append(t_track)
            seq_e2e_ms.append(t_e2e)
            total_track_time_ms += t_track
            total_e2e_time_ms += t_e2e
            total_evaluated_frames += 1

            gt = gts[f_idx]
            gt_valid = 1 if (gt[2] > 0 and gt[3] > 0) else 0

            if gt_valid:
                iou = compute_iou(box, gt)
                cle = compute_cle(box, gt)
                norm_cle = compute_norm_cle(box, gt)

                seq_ious.append(iou)
                seq_cles.append(cle)
                seq_norm_cles.append(norm_cle)
                all_valid_ious.append(iou)
                all_valid_cles.append(cle)
                all_valid_norm_cles.append(norm_cle)
            else:
                iou = 0.0
                cle = 0.0

            csv_writer.writerow([
                seq_name, f_idx,
                f"{bx:.4f}", f"{by:.4f}", f"{bw:.4f}", f"{bh:.4f}",
                inst_sz, crop_sz, crop_xmin, crop_ymin,
                f"{ax:.4f}", f"{ay:.4f}", f"{aw:.4f}", f"{ah:.4f}",
                f"{cls_max:.4f}",
                f"{box[0]:.4f}", f"{box[1]:.4f}", f"{box[2]:.4f}", f"{box[3]:.4f}",
                f"{iou:.4f}", f"{cle:.4f}", gt_valid
            ])

        seq_metrics = calculate_metrics_from_arrays(
            np.array(seq_ious, dtype=np.float32),
            np.array(seq_cles, dtype=np.float32),
            np.array(seq_norm_cles, dtype=np.float32),
        )
        seq_metrics["frames"] = n_frames
        seq_metrics["valid_frames"] = len(seq_ious)
        seq_metrics["track_ms_avg"] = float(np.mean(seq_track_ms)) if seq_track_ms else 0.0
        seq_metrics["e2e_ms_avg"] = float(np.mean(seq_e2e_ms)) if seq_e2e_ms else 0.0
        per_seq_summaries[seq_name] = seq_metrics

    csv_file.close()
    print(f"Saved per-frame log to {csv_path}")

    # Overall metrics
    overall_metrics = calculate_metrics_from_arrays(
        np.array(all_valid_ious, dtype=np.float32),
        np.array(all_valid_cles, dtype=np.float32),
        np.array(all_valid_norm_cles, dtype=np.float32),
    )
    overall_metrics["total_sequences"] = len(sequences)
    overall_metrics["total_frames"] = total_evaluated_frames + len(sequences)
    overall_metrics["total_valid_frames"] = len(all_valid_ious)
    overall_metrics["track_ms_avg"] = float(total_track_time_ms / total_evaluated_frames) if total_evaluated_frames > 0 else 0.0
    overall_metrics["e2e_ms_avg"] = float(total_e2e_time_ms / total_evaluated_frames) if total_evaluated_frames > 0 else 0.0

    summary_data = {
        "overall": overall_metrics,
        "per_sequence": per_seq_summaries,
    }

    summary_path = os.path.join(a.out, "summary.json")
    with open(summary_path, "w", encoding="utf-8") as f:
        json.dump(summary_data, f, indent=2, ensure_ascii=False)
    print(f"Saved summary to {summary_path}")

    # Print overall scorecard
    print("\n" + "=" * 60)
    print(f" LIGHTTRACK EVALUATION SCORECARD [{a.backend.upper()}]")
    print("=" * 60)
    print(f" Total Sequences   : {overall_metrics['total_sequences']}")
    print(f" Total Valid Frames: {overall_metrics['total_valid_frames']}")
    print(f" Success AUC       : {overall_metrics['auc'] * 100:.2f}%")
    print(f" Precision DP@20   : {overall_metrics['dp20'] * 100:.2f}%")
    print(f" Mean CLE          : {overall_metrics['mean_cle']:.2f} px")
    print(f" Lost Rate         : {overall_metrics['lost_rate'] * 100:.2f}%")
    print(f" SR@0.50           : {overall_metrics['sr_50'] * 100:.2f}%")
    print(f" SR@0.75           : {overall_metrics['sr_75'] * 100:.2f}%")
    print(f" Norm Precision    : {overall_metrics['p_norm'] * 100:.2f}%")
    print(f" Track Latency Avg : {overall_metrics['track_ms_avg']:.2f} ms")
    print(f" E2E Latency Avg   : {overall_metrics['e2e_ms_avg']:.2f} ms")
    print("=" * 60 + "\n")


if __name__ == "__main__":
    main()
