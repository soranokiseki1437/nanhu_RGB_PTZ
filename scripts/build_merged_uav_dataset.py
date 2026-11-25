# -*- coding: utf-8 -*-
"""Build a fixed, deterministic YOLO detection dataset fusing DUT Anti-UAV and CASIA-Anti-UAV300 keyframes."""

import argparse
import csv
import glob
import json
import os
import shutil
from PIL import Image

BASE_DIR = r"d:\PyProjects\RGB_PTZ_Integrated"
DUT_DET_DIR = os.path.join(BASE_DIR, "datasets", "Anti-UAV", "detection")
CASIA_DIR = os.path.join(BASE_DIR, "datasets", "CASIA-Anti-UAV300")
OUT_DIR = os.path.join(BASE_DIR, "datasets", "Anti-UAV-Merged")
OUT_YAML = os.path.join(BASE_DIR, "datasets", "anti_uav_merged.yaml")


def link_or_copy(src: str, dst: str) -> None:
    """Create an NTFS hardlink when possible to avoid duplicating gigabytes of images, else copy."""
    if os.path.exists(dst):
        os.remove(dst)
    try:
        os.link(src, dst)
    except OSError:
        shutil.copy2(src, dst)


def compute_iou(b1: list[float], b2: list[float]) -> float:
    """Compute IoU between two [x, y, w, h] boxes."""
    x1, y1, w1, h1 = b1
    x2, y2, w2, h2 = b2
    ix1 = max(x1, x2)
    iy1 = max(y1, y2)
    ix2 = min(x1 + w1, x2 + w2)
    iy2 = min(y1 + h1, y2 + h2)
    inter = max(0.0, ix2 - ix1) * max(0.0, iy2 - iy1)
    union = w1 * h1 + w2 * h2 - inter
    return inter / union if union > 0.0 else 0.0


def clamp_box(rect: list[float], width: int, height: int) -> list[float]:
    """Clamp [x, y, w, h] inside image bounds [0, width] x [0, height]."""
    x, y, w, h = rect
    x1 = max(0.0, min(float(x), float(width)))
    y1 = max(0.0, min(float(y), float(height)))
    x2 = max(0.0, min(float(x + w), float(width)))
    y2 = max(0.0, min(float(y + h), float(height)))
    return [x1, y1, x2 - x1, y2 - y1]


def uniform_subsample(indices: list[int], cap: int) -> list[int]:
    """Deterministically subsample indices evenly from start to end up to cap."""
    n = len(indices)
    if n <= cap:
        return indices
    if cap == 1:
        return [indices[0]]
    return [indices[round(k * (n - 1) / (cap - 1))] for k in range(cap)]


def copy_dut_split(split: str, out_img_dir: str, out_lbl_dir: str, manifest_rows: list[list[str]]) -> dict[str, int]:
    """Copy/link DUT Anti-UAV detection split into the merged directory with 'dut_' prefix."""
    src_img_dir = os.path.join(DUT_DET_DIR, split, "images")
    src_lbl_dir = os.path.join(DUT_DET_DIR, split, "labels")
    img_paths = sorted(glob.glob(os.path.join(src_img_dir, "*.jpg")))

    pos_count = 0
    neg_count = 0
    for img_path in img_paths:
        stem = os.path.splitext(os.path.basename(img_path))[0]
        lbl_path = os.path.join(src_lbl_dir, f"{stem}.txt")
        if not os.path.exists(lbl_path):
            raise FileNotFoundError(f"Missing DUT label file: {lbl_path}")

        dst_stem = f"dut_{stem}"
        dst_img = os.path.join(out_img_dir, f"{dst_stem}.jpg")
        dst_lbl = os.path.join(out_lbl_dir, f"{dst_stem}.txt")
        link_or_copy(img_path, dst_img)
        shutil.copy2(lbl_path, dst_lbl)

        with open(lbl_path, "r", encoding="utf-8") as f:
            lines = [ln.strip() for ln in f if ln.strip()]
        is_pos = len(lines) > 0
        if is_pos:
            pos_count += 1
        else:
            neg_count += 1
        manifest_rows.append([split, "DUT", stem, stem, "pos" if is_pos else "neg", dst_stem])

    return {"total": len(img_paths), "pos": pos_count, "neg": neg_count}


def select_casia_frames(
    seq_dir: str,
    iou_thresh: float = 0.4,
    cap_pos: int = 45,
    cap_neg: int = 4,
    neg_stride: int = 30,
    neg_margin: int = 5,
    min_box_px: float = 4.0,
) -> tuple[list[tuple[int, list[float] | None]], int, int]:
    """Select deterministic positive and negative frame indices for one CASIA sequence."""
    vis_path = os.path.join(seq_dir, "visible.json")
    with open(vis_path, "r", encoding="utf-8") as f:
        data = json.load(f)

    exist = data["exist"]
    gt_rect = data["gt_rect"]
    total_len = len(exist)

    first_img = os.path.join(seq_dir, "000000.jpg")
    if not os.path.exists(first_img):
        jpgs = sorted(glob.glob(os.path.join(seq_dir, "*.jpg")))
        if not jpgs:
            raise FileNotFoundError(f"No jpg frames found in {seq_dir}")
        first_img = jpgs[0]
    with Image.open(first_img) as im:
        width, height = im.size

    valid_boxes: dict[int, list[float]] = {}
    for idx, (ex, rect) in enumerate(zip(exist, gt_rect)):
        if ex == 1 and isinstance(rect, list) and len(rect) == 4 and rect[2] > 0 and rect[3] > 0:
            clamped = clamp_box(rect, width, height)
            if clamped[2] >= min_box_px and clamped[3] >= min_box_px:
                valid_boxes[idx] = clamped

    # Adaptive stride bounds based on sequence length
    min_stride = 20 if total_len >= 150 else 8
    max_stride = 50 if total_len >= 150 else 25

    picked_pos: list[int] = []
    last_idx: int | None = None
    for idx in sorted(valid_boxes.keys()):
        if last_idx is None:
            picked_pos.append(idx)
            last_idx = idx
            continue
        gap = idx - last_idx
        if gap >= max_stride or (gap >= min_stride and compute_iou(valid_boxes[idx], valid_boxes[last_idx]) < iou_thresh):
            picked_pos.append(idx)
            last_idx = idx
    picked_pos = uniform_subsample(picked_pos, cap_pos)

    picked_neg: list[int] = []
    i = 0
    min_seg_len = max(15, neg_margin * 2 + 1)
    while i < total_len:
        if exist[i] == 0:
            j = i
            while j < total_len and exist[j] == 0:
                j += 1
            if j - i >= min_seg_len:
                for k in range(i + neg_margin, j - neg_margin, neg_stride):
                    picked_neg.append(k)
            i = j
        else:
            i += 1
    picked_neg = uniform_subsample(picked_neg, cap_neg)

    selected: list[tuple[int, list[float] | None]] = [(idx, valid_boxes[idx]) for idx in picked_pos]
    selected.extend((idx, None) for idx in picked_neg)
    selected.sort(key=lambda item: item[0])
    return selected, width, height


def extract_casia_split(split: str, out_img_dir: str, out_lbl_dir: str, manifest_rows: list[list[str]]) -> dict[str, int]:
    """Extract deterministic CASIA keyframes into the merged YOLO detection directory."""
    seq_dirs = sorted(glob.glob(os.path.join(CASIA_DIR, split, "*")))
    pos_count = 0
    neg_count = 0

    for seq_dir in seq_dirs:
        seq_name = os.path.basename(seq_dir)
        selected, width, height = select_casia_frames(seq_dir)
        for frame_idx, box in selected:
            src_img = os.path.join(seq_dir, f"{frame_idx:06d}.jpg")
            if not os.path.exists(src_img):
                raise FileNotFoundError(f"Missing CASIA frame: {src_img}")

            dst_stem = f"casia_{seq_name}_{frame_idx:06d}"
            dst_img = os.path.join(out_img_dir, f"{dst_stem}.jpg")
            dst_lbl = os.path.join(out_lbl_dir, f"{dst_stem}.txt")
            link_or_copy(src_img, dst_img)

            if box is not None:
                x, y, w, h = box
                xc = (x + w / 2.0) / width
                yc = (y + h / 2.0) / height
                wn = w / width
                hn = h / height
                with open(dst_lbl, "w", encoding="utf-8") as f_lbl:
                    f_lbl.write(f"0 {xc:.6f} {yc:.6f} {wn:.6f} {hn:.6f}\n")
                pos_count += 1
                lbl_type = "pos"
            else:
                with open(dst_lbl, "w", encoding="utf-8") as f_lbl:
                    f_lbl.write("")
                neg_count += 1
                lbl_type = "neg"

            manifest_rows.append([split, "CASIA", seq_name, f"{frame_idx:06d}", lbl_type, dst_stem])

    return {"total": pos_count + neg_count, "pos": pos_count, "neg": neg_count, "seqs": len(seq_dirs)}


def write_dataset_yaml(out_dir: str, yaml_path: str) -> None:
    """Write Ultralytics dataset YAML pointing to the merged detection dataset."""
    posix_dir = out_dir.replace("\\", "/")
    content = (
        "# Merged UAV Detection Dataset (DUT Anti-UAV + Deterministic CASIA-Anti-UAV300 Keyframes)\n"
        f"path: {posix_dir}\n"
        "train: train/images\n"
        "val: val/images\n"
        "test: val/images\n\n"
        "nc: 1\n"
        "names: ['UAV']\n"
    )
    with open(yaml_path, "w", encoding="utf-8") as f:
        f.write(content)


def build_dataset(overwrite: bool = False) -> None:
    """Build the complete merged detection dataset and manifest."""
    if os.path.exists(OUT_DIR):
        if not overwrite:
            raise FileExistsError(f"Target directory already exists: {OUT_DIR}. Pass --overwrite to rebuild.")
        shutil.rmtree(OUT_DIR)

    manifest_rows: list[list[str]] = []
    summary: dict[str, dict[str, dict[str, int]]] = {}

    for split in ["train", "val"]:
        out_img_dir = os.path.join(OUT_DIR, split, "images")
        out_lbl_dir = os.path.join(OUT_DIR, split, "labels")
        os.makedirs(out_img_dir, exist_ok=True)
        os.makedirs(out_lbl_dir, exist_ok=True)

        dut_stats = copy_dut_split(split, out_img_dir, out_lbl_dir, manifest_rows)
        casia_stats = extract_casia_split(split, out_img_dir, out_lbl_dir, manifest_rows)
        summary[split] = {"dut": dut_stats, "casia": casia_stats}

    manifest_path = os.path.join(OUT_DIR, "manifest.csv")
    with open(manifest_path, "w", newline="", encoding="utf-8") as f_csv:
        writer = csv.writer(f_csv)
        writer.writerow(["split", "source", "sequence_or_id", "frame_id", "label_type", "merged_stem"])
        writer.writerows(manifest_rows)

    write_dataset_yaml(OUT_DIR, OUT_YAML)

    print("=" * 72)
    print(f"[+] Merged detection dataset created at: {OUT_DIR}")
    print(f"[+] YOLO dataset config written to:      {OUT_YAML}")
    for split in ["train", "val"]:
        d = summary[split]["dut"]
        c = summary[split]["casia"]
        tot = d["total"] + c["total"]
        print(
            f"  - {split:5s}: total={tot:5d} | "
            f"DUT={d['total']} (pos={d['pos']}, neg={d['neg']}) | "
            f"CASIA={c['total']} from {c['seqs']} seqs (pos={c['pos']}, neg={c['neg']})"
        )
    print("=" * 72)


def main() -> None:
    parser = argparse.ArgumentParser(description="Build merged DUT + CASIA YOLO detection dataset.")
    parser.add_argument("--overwrite", action="store_true", help="Remove and rebuild target dataset directory if it exists.")
    args = parser.parse_args()
    build_dataset(overwrite=args.overwrite)


if __name__ == "__main__":
    main()
