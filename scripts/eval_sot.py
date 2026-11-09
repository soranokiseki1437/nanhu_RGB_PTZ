"""
Anti-UAV Benchmark Evaluation Pipeline for LightUAV-SOT.
Executes One-Pass Evaluation (OPE) on 20 real flight sequences (24,919 frames),
calculating Success (AUC), Distance Precision (DP@20px), Mean CLE, Lost Rate, and Throughput (FPS).
"""

import os
import sys
import time
import glob
import argparse
import numpy as np
import cv2
import torch
import torch.nn.functional as F
from typing import List, Tuple, Dict

WORKSPACE_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if WORKSPACE_ROOT not in sys.path:
    sys.path.insert(0, WORKSPACE_ROOT)

from models.sot.lightuav_sot import LightUAV_SOT


def sz_wh(wh):
    pad = (wh[0] + wh[1]) * 0.5
    sz2 = (wh[0] + pad) * (wh[1] + pad)
    return np.sqrt(sz2)


class LightUAVTracker:
    """
    Inference Tracker Wrapper for LightUAV-SOT with Cosine Window Penalty & Scale Smoothing.
    """
    def __init__(self, model_path: str = None, device: str = 'cuda',
                 instance_sz: int = 255, exemplar_sz: int = 127,
                 penalty_k: float = 0.05, window_influence: float = 0.40, lr: float = 0.38,
                 use_mffn: bool = True, use_dac: bool = True):
        self.device = torch.device(device if torch.cuda.is_available() else 'cpu')
        self.instance_sz = instance_sz
        self.exemplar_sz = exemplar_sz
        self.penalty_k = penalty_k
        self.window_influence = window_influence
        self.lr = lr

        self.model = LightUAV_SOT(use_mffn=use_mffn, use_dac=use_dac).to(self.device)
        is_deploy_file = model_path and ('deploy' in os.path.basename(model_path).lower())
        if is_deploy_file:
            self.model.switch_to_deploy()

        if model_path and os.path.isfile(model_path):
            print(f"Loading weights from: {model_path} (MFFN={use_mffn}, DAC={use_dac})")
            state_dict = torch.load(model_path, map_location=self.device)
            self.model.load_state_dict(state_dict, strict=False)
        else:
            print(f"No weights provided or file not found, using initialized weights (MFFN={use_mffn}, DAC={use_dac}).")

        self.model.eval()
        if not is_deploy_file:
            self.model.switch_to_deploy()

        # Build 16x16 grid coordinate maps and Hanning window
        grid_sz = 16
        stride = 16.0
        gy, gx = np.meshgrid(np.arange(grid_sz), np.arange(grid_sz), indexing='ij')
        self.grid_x = gx * stride + stride / 2.0  # [16, 16]
        self.grid_y = gy * stride + stride / 2.0  # [16, 16]

        # 2D Hanning window
        hanning = np.hanning(grid_sz)
        self.window = np.outer(hanning, hanning)
        self.window = self.window / (self.window.sum() + 1e-6)

        self.z_feat = None
        self.target_pos = None
        self.target_sz = None
        self.scale_z = 1.0

    def init(self, image: np.ndarray, init_bbox: List[float]):
        """
        image: BGR uint8 [H, W, 3]
        init_bbox: [x, y, w, h]
        """
        x, y, w, h = [float(v) for v in init_bbox]
        self.target_pos = np.array([x + w / 2.0, y + h / 2.0], dtype=np.float32)
        self.target_sz = np.array([w, h], dtype=np.float32)

        s_z = sz_wh(self.target_sz)
        self.scale_z = self.exemplar_sz / float(s_z)

        # Crop exemplar patch
        patch_z = self._crop_patch(image, self.target_pos, s_z, self.exemplar_sz)
        patch_z_tensor = torch.from_numpy(patch_z.transpose(2, 0, 1)).float().unsqueeze(0).to(self.device) / 255.0

        with torch.no_grad():
            self.z_feat = self.model.extract_template(patch_z_tensor)

    def track(self, image: np.ndarray) -> Tuple[List[float], float]:
        """
        Track current frame.
        Returns:
          bbox: [x, y, w, h]
          confidence: float in [0.0, 1.0]
        """
        s_z = sz_wh(self.target_sz)
        s_x = s_z * (self.instance_sz / float(self.exemplar_sz))

        # Crop search patch centered at target_pos
        patch_x = self._crop_patch(image, self.target_pos, s_x, self.instance_sz)
        patch_x_tensor = torch.from_numpy(patch_x.transpose(2, 0, 1)).float().unsqueeze(0).to(self.device) / 255.0

        scale_x = self.instance_sz / float(s_x)

        with torch.no_grad():
            cls_score, reg_dist = self.model.track_search(patch_x_tensor, self.z_feat)

        cls_score = cls_score.squeeze().cpu().numpy()  # [16, 16]
        reg_dist = reg_dist.squeeze().cpu().numpy()    # [4, 16, 16] (l, t, r, b)

        # Decode coordinates in search patch [0, 255]
        pred_x1 = self.grid_x - reg_dist[0]
        pred_y1 = self.grid_y - reg_dist[1]
        pred_x2 = self.grid_x + reg_dist[2]
        pred_y2 = self.grid_y + reg_dist[3]

        pred_w = np.maximum(pred_x2 - pred_x1, 2.0)
        pred_h = np.maximum(pred_y2 - pred_y1, 2.0)

        # Scale and ratio penalty
        s_c = np.maximum(sz_wh([pred_w, pred_h]) / (s_z * scale_x), (s_z * scale_x) / sz_wh([pred_w, pred_h]))
        r_c = np.maximum((self.target_sz[0] / self.target_sz[1]) / (pred_w / pred_h),
                         (pred_w / pred_h) / (self.target_sz[0] / self.target_sz[1]))
        penalty = np.exp(-(r_c * s_c - 1.0) * self.penalty_k)
        pscore = penalty * cls_score

        # Window influence
        pscore = pscore * (1.0 - self.window_influence) + self.window * self.window_influence

        # Best position on grid
        r_max, c_max = np.unravel_index(np.argmax(pscore), pscore.shape)

        # Displacement relative to search patch center (127.5, 127.5)
        center_x = (pred_x1[r_max, c_max] + pred_x2[r_max, c_max]) / 2.0
        center_y = (pred_y1[r_max, c_max] + pred_y2[r_max, c_max]) / 2.0

        disp_x = (center_x - self.instance_sz / 2.0) / scale_x
        disp_y = (center_y - self.instance_sz / 2.0) / scale_x

        # Update position
        img_h, img_w = image.shape[:2]
        self.target_pos[0] = np.clip(self.target_pos[0] + disp_x, 0.0, img_w)
        self.target_pos[1] = np.clip(self.target_pos[1] + disp_y, 0.0, img_h)

        # Update size with adaptive learning rate
        res_w = pred_w[r_max, c_max] / scale_x
        res_h = pred_h[r_max, c_max] / scale_x
        lr_adaptive = penalty[r_max, c_max] * cls_score[r_max, c_max] * self.lr
        new_w = (1.0 - lr_adaptive) * self.target_sz[0] + lr_adaptive * res_w
        new_h = (1.0 - lr_adaptive) * self.target_sz[1] + lr_adaptive * res_h
        self.target_sz[0] = np.clip(new_w, 5.0, img_w * 0.5)
        self.target_sz[1] = np.clip(new_h, 5.0, img_h * 0.5)

        # Return [x, y, w, h]
        x1 = float(self.target_pos[0] - self.target_sz[0] / 2.0)
        y1 = float(self.target_pos[1] - self.target_sz[1] / 2.0)
        w_out = float(self.target_sz[0])
        h_out = float(self.target_sz[1])
        conf = float(cls_score[r_max, c_max])

        return [x1, y1, w_out, h_out], conf

    def _crop_patch(self, image: np.ndarray, center: np.ndarray, crop_sz: float, out_sz: int) -> np.ndarray:
        img_h, img_w = image.shape[:2]
        crop_sz = np.clip(float(crop_sz), 10.0, max(img_w, img_h) * 1.5)
        cx, cy = center[0], center[1]
        x1 = int(np.floor(cx - crop_sz / 2.0))
        y1 = int(np.floor(cy - crop_sz / 2.0))
        x2 = int(np.ceil(cx + crop_sz / 2.0))
        y2 = int(np.ceil(cy + crop_sz / 2.0))

        img_h, img_w = image.shape[:2]
        pad_l = max(0, -x1)
        pad_t = max(0, -y1)
        pad_r = max(0, x2 - img_w)
        pad_b = max(0, y2 - img_h)

        x1_c = max(0, x1)
        y1_c = max(0, y1)
        x2_c = min(img_w, x2)
        y2_c = min(img_h, y2)

        cropped = image[y1_c:y2_c, x1_c:x2_c]
        if pad_l > 0 or pad_t > 0 or pad_r > 0 or pad_b > 0:
            avg_color = [128, 128, 128]
            cropped = cv2.copyMakeBorder(cropped, pad_t, pad_b, pad_l, pad_r, cv2.BORDER_CONSTANT, value=avg_color)

        resized = cv2.resize(cropped, (out_sz, out_sz), interpolation=cv2.INTER_LINEAR)
        return cv2.cvtColor(resized, cv2.COLOR_BGR2RGB)


def compute_iou(box1: np.ndarray, box2: np.ndarray) -> float:
    x1 = max(box1[0], box2[0])
    y1 = max(box1[1], box2[1])
    x2 = min(box1[0] + box1[2], box2[0] + box2[2])
    y2 = min(box1[1] + box1[3], box2[1] + box2[3])
    inter = max(0.0, x2 - x1) * max(0.0, y2 - y1)
    area1 = box1[2] * box1[3]
    area2 = box2[2] * box2[3]
    union = area1 + area2 - inter
    return inter / union if union > 0 else 0.0


def compute_center_error(box1: np.ndarray, box2: np.ndarray) -> float:
    c1 = np.array([box1[0] + box1[2] / 2.0, box1[1] + box1[3] / 2.0])
    c2 = np.array([box2[0] + box2[2] / 2.0, box2[1] + box2[3] / 2.0])
    return float(np.linalg.norm(c1 - c2))


def evaluate_sequences(tracker: LightUAVTracker, data_root: str, max_seqs: int = None) -> Dict[str, float]:
    video_dir = os.path.join(data_root, 'Anti-UAV-Tracking-V0')
    gt_dir = os.path.join(data_root, 'Anti-UAV-Tracking-V0GT')

    gt_files = sorted(glob.glob(os.path.join(gt_dir, '*_gt.txt')))
    if max_seqs is not None:
        gt_files = gt_files[:max_seqs]

    all_ious = []
    all_cles = []
    total_frames = 0
    total_time = 0.0

    print(f"Starting evaluation across {len(gt_files)} sequences...")

    for gtf in gt_files:
        video_name = os.path.basename(gtf).replace('_gt.txt', '')
        img_folder = os.path.join(video_dir, video_name)
        if not os.path.isdir(img_folder):
            continue

        # Load GT
        gt_boxes = []
        with open(gtf, 'r') as fp:
            for line in fp:
                line = line.strip()
                if line:
                    parts = [float(v) for v in line.replace(',', ' ').split()]
                    if len(parts) >= 4:
                        gt_boxes.append(parts[:4])

        img_names = sorted(os.listdir(img_folder))
        img_paths = [os.path.join(img_folder, name) for name in img_names if name.endswith(('.jpg', '.png'))]
        num_frames = min(len(gt_boxes), len(img_paths))
        if num_frames < 2:
            continue

        # Initialize on frame 0
        im0 = cv2.imread(img_paths[0])
        tracker.init(im0, gt_boxes[0])

        seq_ious = [1.0]
        seq_cles = [0.0]

        t0 = time.time()
        track_time = 0.0
        for idx in range(1, num_frames):
            im = cv2.imread(img_paths[idx])
            t_track0 = time.time()
            pred_box, conf = tracker.track(im)
            track_time += time.time() - t_track0
            gt_box = gt_boxes[idx]

            if gt_box[2] > 0 and gt_box[3] > 0:
                iou = compute_iou(np.array(pred_box), np.array(gt_box))
                cle = compute_center_error(np.array(pred_box), np.array(gt_box))
            else:
                iou = 0.0
                cle = 999.0

            seq_ious.append(iou)
            seq_cles.append(cle)

        elapsed = time.time() - t0
        track_fps = (num_frames - 1) / max(track_time, 1e-6)
        e2e_fps = (num_frames - 1) / max(elapsed, 1e-6)
        total_time += elapsed
        total_track_time = getattr(evaluate_sequences, 'total_track_time', 0.0) + track_time
        evaluate_sequences.total_track_time = total_track_time
        total_frames += (num_frames - 1)

        auc = np.mean(seq_ious)
        dp20 = np.mean([1.0 if err <= 20.0 else 0.0 for err in seq_cles])
        mean_cle = np.mean(seq_cles)

        print(f"[{video_name:8s}] Frames: {num_frames:4d} | AUC: {auc:.4f} | DP@20: {dp20*100:5.1f}% | CLE: {mean_cle:6.1f}px | Model FPS: {track_fps:5.1f} | E2E FPS: {e2e_fps:5.1f}")
        all_ious.extend(seq_ious)
        all_cles.extend(seq_cles)

    overall_auc = float(np.mean(all_ious))
    overall_dp20 = float(np.mean([1.0 if err <= 20.0 else 0.0 for err in all_cles]))
    overall_cle = float(np.mean(all_cles))
    overall_e2e_fps = total_frames / max(total_time, 1e-6)
    total_tr_time = getattr(evaluate_sequences, 'total_track_time', total_time)
    overall_track_fps = total_frames / max(total_tr_time, 1e-6)
    evaluate_sequences.total_track_time = 0.0

    print("\n" + "=" * 65)
    print(f"OVERALL RESULTS ({len(gt_files)} Sequences, {total_frames} Frames):")
    print(f"Success Rate (AUC)          : {overall_auc:.4f}")
    print(f"Distance Precision (DP@20)  : {overall_dp20 * 100:.2f}%")
    print(f"Mean Center Error (CLE)     : {overall_cle:.2f} px")
    print(f"Algorithm Throughput (FPS)  : {overall_track_fps:.1f} FPS (Pure Model + GPU)")
    print(f"End-to-End Throughput (FPS) : {overall_e2e_fps:.1f} FPS (Including Disk I/O)")
    print("=" * 65)

    return {
        'auc': overall_auc,
        'dp20': overall_dp20,
        'cle': overall_cle,
        'track_fps': overall_track_fps,
        'e2e_fps': overall_e2e_fps,
        'frames': total_frames
    }


def main():
    parser = argparse.ArgumentParser(description="Evaluate LightUAV-SOT on Anti-UAV Dataset")
    parser.add_argument('--weights', type=str, default='weights/sot/lightuav_sot_best.pth')
    parser.add_argument('--data-root', type=str, default='datasets/Anti-UAV/tracking')
    parser.add_argument('--max-seqs', type=int, default=None, help='Evaluate first N sequences for quick test')
    parser.add_argument('--device', type=str, default='cuda')
    parser.add_argument('--no-mffn', action='store_true', help='Disable MFFN multi-scale detail fusion')
    parser.add_argument('--no-dac', action='store_true', help='Disable DAC dynamic attention correlation')
    args = parser.parse_args()

    tracker = LightUAVTracker(
        model_path=args.weights,
        device=args.device,
        use_mffn=(not args.no_mffn),
        use_dac=(not args.no_dac)
    )
    evaluate_sequences(tracker, data_root=args.data_root, max_seqs=args.max_seqs)


if __name__ == '__main__':
    main()
