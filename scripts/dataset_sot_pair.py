"""
Anti-UAV SOT Pair Dataset & Augmentation Pipeline.
Generates (template, search) patch pairs with ground truth regression distances and classification heatmaps.
"""

import os
import cv2
import random
import glob
import numpy as np
import torch
from torch.utils.data import Dataset
from typing import Tuple, List, Dict, Optional


def crop_hwc(image: np.ndarray, bbox: list, out_sz: int, padding_val=(128, 128, 128)) -> Tuple[np.ndarray, float]:
    """
    Crops and resizes an image patch centered at bbox with context padding.
    bbox: [x, y, w, h]
    out_sz: target square size (127 or 255)
    Returns:
      cropped_patch: [out_sz, out_sz, 3] (RGB uint8)
      scale: ratio of target size to crop crop_size
    """
    x, y, w, h = bbox
    cx, cy = x + w / 2.0, y + h / 2.0

    # Siamese context padding formula
    context = 0.5 * (w + h)
    crop_sz = np.sqrt((w + context) * (h + context))
    crop_sz = max(float(crop_sz), 10.0)

    # Coordinates in original image
    x1 = int(np.floor(cx - crop_sz / 2.0))
    y1 = int(np.floor(cy - crop_sz / 2.0))
    x2 = int(np.ceil(cx + crop_sz / 2.0))
    y2 = int(np.ceil(cy + crop_sz / 2.0))

    img_h, img_w = image.shape[:2]

    # Padding needed
    pad_left = max(0, -x1)
    pad_top = max(0, -y1)
    pad_right = max(0, x2 - img_w)
    pad_bottom = max(0, y2 - img_h)

    # Clamp coordinates
    x1_c = max(0, x1)
    y1_c = max(0, y1)
    x2_c = min(img_w, x2)
    y2_c = min(img_h, y2)

    cropped = image[y1_c:y2_c, x1_c:x2_c]
    if pad_left > 0 or pad_top > 0 or pad_right > 0 or pad_bottom > 0:
        cropped = cv2.copyMakeBorder(
            cropped, pad_top, pad_bottom, pad_left, pad_right,
            cv2.BORDER_CONSTANT, value=padding_val
        )

    scale = out_sz / float(crop_sz)
    resized = cv2.resize(cropped, (out_sz, out_sz), interpolation=cv2.INTER_LINEAR)
    return resized, scale


class AntiUAVPairDataset(Dataset):
    """
    Pairwise SOT Dataset over Anti-UAV tracking sequences.
    """
    def __init__(self, data_root: str, max_frame_gap: int = 80,
                 template_sz: int = 127, search_sz: int = 255, stride: int = 16,
                 is_train: bool = True, preload_ram: bool = True, pairs_per_seq: int = 150):
        super(AntiUAVPairDataset, self).__init__()
        self.data_root = data_root
        self.max_frame_gap = max_frame_gap
        self.template_sz = template_sz
        self.search_sz = search_sz
        self.stride = stride
        self.grid_sz = search_sz // stride  # 255 // 16 = 15 or 16 (16 for 256)
        # Note: If 255, grid is 16x16 with padding/stride alignment
        self.is_train = is_train
        self.preload_ram = preload_ram
        self.pairs_per_seq = pairs_per_seq
        self.ram_cache: Dict[str, bytes] = {}
        self.template_cache: Dict[str, np.ndarray] = {}

        self.seqs = []
        video_dir = os.path.join(data_root, 'Anti-UAV-Tracking-V0')
        gt_dir = os.path.join(data_root, 'Anti-UAV-Tracking-V0GT')

        gt_files = sorted(glob.glob(os.path.join(gt_dir, '*_gt.txt')))
        for gtf in gt_files:
            video_name = os.path.basename(gtf).replace('_gt.txt', '')
            img_folder = os.path.join(video_dir, video_name)
            if not os.path.isdir(img_folder):
                continue

            # Load ground truth
            boxes = []
            with open(gtf, 'r') as fp:
                for line in fp:
                    line = line.strip()
                    if not line:
                        continue
                    parts = [float(v) for v in line.replace(',', ' ').split()]
                    if len(parts) >= 4:
                        boxes.append(parts[:4])

            # Get frame paths
            img_names = sorted(os.listdir(img_folder))
            img_paths = [os.path.join(img_folder, name) for name in img_names if name.endswith(('.jpg', '.png'))]

            num_frames = min(len(boxes), len(img_paths))
            if num_frames < 5:
                continue

            # Filter valid (w > 0, h > 0)
            valid_indices = [i for i in range(num_frames) if boxes[i][2] > 2 and boxes[i][3] > 2]
            if len(valid_indices) >= 5:
                self.seqs.append({
                    'name': video_name,
                    'img_paths': img_paths[:num_frames],
                    'boxes': boxes[:num_frames],
                    'valid_indices': valid_indices
                })

        print(f'Loaded AntiUAVPairDataset: {len(self.seqs)} sequences available.')

        if self.preload_ram:
            total_frames = sum(len(s['valid_indices']) for s in self.seqs)
            print(f'Preloading {total_frames} valid frames into RAM cache...', flush=True)
            for s in self.seqs:
                for idx in s['valid_indices']:
                    p = s['img_paths'][idx]
                    box = s['boxes'][idx]
                    if p not in self.ram_cache:
                        with open(p, 'rb') as f:
                            raw = f.read()
                        self.ram_cache[p] = raw
                        arr = np.frombuffer(raw, dtype=np.uint8)
                        im_bgr = cv2.imdecode(arr, cv2.IMREAD_COLOR)
                        if im_bgr is not None:
                            im_rgb = cv2.cvtColor(im_bgr, cv2.COLOR_BGR2RGB)
                            patch_z, _ = crop_hwc(im_rgb, box, self.template_sz)
                            self.template_cache[p] = patch_z
            total_ram = sum(len(v) for v in self.ram_cache.values()) + sum(v.nbytes for v in self.template_cache.values())
            print(f'RAM Cache Ready: {len(self.ram_cache)} raw frames, {len(self.template_cache)} templates ({total_ram / 1e9:.2f} GB).', flush=True)

    def __len__(self) -> int:
        # Approximate epoch size (e.g. 20 seqs * 150 = 3000 pairs / epoch)
        return max(len(self.seqs) * self.pairs_per_seq, 1000) if self.is_train else len(self.seqs) * 50

    def __getitem__(self, idx: int) -> Dict[str, torch.Tensor]:
        # Pick random sequence
        seq = random.choice(self.seqs)
        valid_indices = seq['valid_indices']

        # Pick template frame
        idx_z = random.choice(valid_indices)

        # Pick search frame within max_frame_gap
        low = max(0, idx_z - self.max_frame_gap)
        high = min(len(seq['img_paths']) - 1, idx_z + self.max_frame_gap)
        candidate_indices = [i for i in valid_indices if low <= i <= high and i != idx_z]
        if not candidate_indices:
            idx_x = idx_z
        else:
            idx_x = random.choice(candidate_indices)

        # 1. Fetch template patch
        p_z = seq['img_paths'][idx_z]
        if self.preload_ram and p_z in self.template_cache:
            patch_z = self.template_cache[p_z]
        else:
            im_z = cv2.imread(p_z)
            if im_z is None:
                im_z = np.zeros((720, 1280, 3), dtype=np.uint8)
            im_z = cv2.cvtColor(im_z, cv2.COLOR_BGR2RGB)
            patch_z, _ = crop_hwc(im_z, seq['boxes'][idx_z], self.template_sz)

        # 2. Fetch search image
        p_x = seq['img_paths'][idx_x]
        if self.preload_ram and p_x in self.ram_cache:
            arr_x = np.frombuffer(self.ram_cache[p_x], dtype=np.uint8)
            im_x = cv2.imdecode(arr_x, cv2.IMREAD_COLOR)
            if im_x is None:
                im_x = np.zeros((720, 1280, 3), dtype=np.uint8)
            im_x = cv2.cvtColor(im_x, cv2.COLOR_BGR2RGB)
        else:
            im_x = cv2.imread(p_x)
            if im_x is None:
                im_x = np.zeros((720, 1280, 3), dtype=np.uint8)
            im_x = cv2.cvtColor(im_x, cv2.COLOR_BGR2RGB)

        box_x = seq['boxes'][idx_x]

        # Crop search with random scale and translation jitter during training
        if self.is_train:
            jitter_scale = random.uniform(0.9, 1.1)
            tx = random.uniform(-0.1, 0.1) * box_x[2]
            ty = random.uniform(-0.1, 0.1) * box_x[3]
            jittered_box = [box_x[0] + tx, box_x[1] + ty, box_x[2] * jitter_scale, box_x[3] * jitter_scale]
        else:
            jittered_box = list(box_x)

        # In SOT, search region is centered at the previous target position with search context
        # Center at jittered_box, but evaluate ground truth target position inside the cropped search patch
        cx_x, cy_x = jittered_box[0] + jittered_box[2] / 2.0, jittered_box[1] + jittered_box[3] / 2.0
        context_x = 0.5 * (jittered_box[2] + jittered_box[3])
        s_z = np.sqrt((jittered_box[2] + context_x) * (jittered_box[3] + context_x))
        s_x = s_z * (self.search_sz / float(self.template_sz))
        s_x = max(float(s_x), 20.0)

        # Search patch crop
        x1 = cx_x - s_x / 2.0
        y1 = cy_x - s_x / 2.0
        scale_x = self.search_sz / s_x

        # Ground truth target box in search patch coordinates [0, search_sz]
        gt_x = (box_x[0] - x1) * scale_x
        gt_y = (box_x[1] - y1) * scale_x
        gt_w = box_x[2] * scale_x
        gt_h = box_x[3] * scale_x

        # Crop search patch
        patch_x, _ = crop_hwc(im_x, [x1, y1, s_x, s_x], self.search_sz)

        # Data augmentation on patches
        if self.is_train:
            # Random brightness/contrast
            if random.random() < 0.5:
                alpha = random.uniform(0.8, 1.2)
                beta = random.uniform(-20, 20)
                patch_x = np.clip(patch_x * alpha + beta, 0, 255).astype(np.uint8)

            # Random horizontal flip
            if random.random() < 0.5:
                patch_z = cv2.flip(patch_z, 1)
                patch_x = cv2.flip(patch_x, 1)
                gt_x = self.search_sz - (gt_x + gt_w)

        # Generate Ground Truth Labels on 16x16 grid
        grid_h = 16
        grid_w = 16
        cls_label = np.zeros((1, grid_h, grid_w), dtype=np.float32)
        reg_label = np.zeros((4, grid_h, grid_w), dtype=np.float32)
        reg_mask = np.zeros((1, grid_h, grid_w), dtype=np.float32)

        # Box corners in patch
        x_min, y_min = gt_x, gt_y
        x_max, y_max = gt_x + gt_w, gt_y + gt_h

        # Positive assignment on grid points
        for r in range(grid_h):
            for c in range(grid_w):
                # Grid point coordinate in patch
                px = (c + 0.5) * self.stride
                py = (r + 0.5) * self.stride

                # Point inside bounding box
                if x_min <= px <= x_max and y_min <= py <= y_max:
                    cls_label[0, r, c] = 1.0
                    reg_mask[0, r, c] = 1.0
                    # l, t, r, b
                    l = px - x_min
                    t = py - y_min
                    r_dist = x_max - px
                    b_dist = y_max - py
                    reg_label[:, r, c] = [l, t, r_dist, b_dist]

        # Convert to float tensors normalized to [0, 1]
        t_z = torch.from_numpy(patch_z.transpose(2, 0, 1)).float() / 255.0
        t_x = torch.from_numpy(patch_x.transpose(2, 0, 1)).float() / 255.0

        return {
            'template': t_z,
            'search': t_x,
            'cls_label': torch.from_numpy(cls_label),
            'reg_label': torch.from_numpy(reg_label),
            'reg_mask': torch.from_numpy(reg_mask)
        }

