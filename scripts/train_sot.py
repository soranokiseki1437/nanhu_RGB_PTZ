"""
LightUAV-SOT Two-Stage Training Pipeline.
Stage 1: Warmup Fusion & Prediction Heads (Backbone frozen)
Stage 2: Joint End-to-End Fine-Tuning (All layers active with Cosine Annealing)
Hardware Acceleration: RTX 5070 Laptop GPU (CUDA 13.0)
"""

import os
import sys
import time
import argparse
import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F
from torch.utils.data import DataLoader
from typing import Dict

# Add workspace root to sys.path
WORKSPACE_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if WORKSPACE_ROOT not in sys.path:
    sys.path.insert(0, WORKSPACE_ROOT)

from models.sot.lightuav_sot import LightUAV_SOT
from scripts.dataset_sot_pair import AntiUAVPairDataset


class SOTLoss(nn.Module):
    """
    Combined Focal Loss (Cls) + IoU/Smooth-L1 Loss (Reg) on Anchor-free Grid.
    """
    def __init__(self, alpha: float = 0.75, gamma: float = 2.0, reg_weight: float = 1.2):
        super(SOTLoss, self).__init__()
        self.alpha = alpha
        self.gamma = gamma
        self.reg_weight = reg_weight

    def forward(self, pred: Dict[str, torch.Tensor], target: Dict[str, torch.Tensor]) -> Dict[str, torch.Tensor]:
        cls_pred = pred['cls_score'].clamp(1e-6, 1.0 - 1e-6)  # [B, 1, 16, 16]
        reg_pred = pred['reg_dist']                           # [B, 4, 16, 16]

        cls_gt = target['cls_label']                          # [B, 1, 16, 16]
        reg_gt = target['reg_label']                          # [B, 4, 16, 16]
        reg_mask = target['reg_mask']                        # [B, 1, 16, 16]

        # 1. Focal Loss for Classification
        p_t = cls_pred * cls_gt + (1.0 - cls_pred) * (1.0 - cls_gt)
        alpha_t = self.alpha * cls_gt + (1.0 - self.alpha) * (1.0 - cls_gt)
        focal_weight = alpha_t * torch.pow(1.0 - p_t, self.gamma)
        bce_loss = -(cls_gt * torch.log(cls_pred) + (1.0 - cls_gt) * torch.log(1.0 - cls_pred))
        cls_loss = (focal_weight * bce_loss).mean()

        # 2. Smooth L1 Loss on Positive Grid Points
        num_pos = reg_mask.sum().clamp(min=1.0)
        reg_diff = F.smooth_l1_loss(reg_pred, reg_gt, reduction='none')  # [B, 4, 16, 16]
        reg_loss = (reg_diff * reg_mask).sum() / (num_pos * 4.0)

        total_loss = cls_loss + self.reg_weight * reg_loss
        return {
            'total_loss': total_loss,
            'cls_loss': cls_loss,
            'reg_loss': reg_loss
        }


def train_one_epoch(model: nn.Module, loader: DataLoader, optimizer: torch.optim.Optimizer,
                    criterion: nn.Module, device: torch.device, epoch: int) -> Dict[str, float]:
    model.train()
    total_loss_accum = 0.0
    cls_loss_accum = 0.0
    reg_loss_accum = 0.0
    steps = 0

    t0 = time.time()
    for batch_idx, batch in enumerate(loader):
        template = batch['template'].to(device, non_blocking=True)
        search = batch['search'].to(device, non_blocking=True)
        target = {
            'cls_label': batch['cls_label'].to(device, non_blocking=True),
            'reg_label': batch['reg_label'].to(device, non_blocking=True),
            'reg_mask': batch['reg_mask'].to(device, non_blocking=True)
        }

        optimizer.zero_grad()
        out = model(template, search)
        loss_dict = criterion(out, target)
        loss = loss_dict['total_loss']
        loss.backward()
        torch.nn.utils.clip_grad_norm_(model.parameters(), max_norm=5.0)
        optimizer.step()

        total_loss_accum += loss.item()
        cls_loss_accum += loss_dict['cls_loss'].item()
        reg_loss_accum += loss_dict['reg_loss'].item()
        steps += 1

    elapsed = time.time() - t0
    return {
        'total_loss': total_loss_accum / max(steps, 1),
        'cls_loss': cls_loss_accum / max(steps, 1),
        'reg_loss': reg_loss_accum / max(steps, 1),
        'time_s': elapsed
    }


def main():
    parser = argparse.ArgumentParser(description="LightUAV-SOT Training Pipeline")
    parser.add_argument('--data-root', type=str, default='datasets/Anti-UAV/tracking')
    parser.add_argument('--save-dir', type=str, default='weights/sot')
    parser.add_argument('--epochs-warmup', type=int, default=10, help='Epochs to warmup heads (backbone frozen)')
    parser.add_argument('--epochs-joint', type=int, default=40, help='Epochs for joint end-to-end training')
    parser.add_argument('--batch-size', type=int, default=16)
    parser.add_argument('--lr-warmup', type=float, default=1e-3)
    parser.add_argument('--lr-joint', type=float, default=2e-4)
    parser.add_argument('--test-run', action='store_true', help='Execute 1 epoch quick test')
    args = parser.parse_args()

    os.makedirs(args.save_dir, exist_ok=True)
    device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
    print(f'Starting LightUAV-SOT Training on device: {device}')

    # Model & Loss
    model = LightUAV_SOT().to(device)
    total_p, trainable_p = model.get_num_params()
    print(f'Model initialized. Total params: {total_p / 1e6:.3f}M, Trainable: {trainable_p / 1e6:.3f}M')

    criterion = SOTLoss()
    dataset = AntiUAVPairDataset(data_root=args.data_root, is_train=True)
    loader = DataLoader(dataset, batch_size=args.batch_size, shuffle=True, num_workers=0, pin_memory=True)

    if args.test_run:
        print('=== Executing Test Run (1 Epoch) ===')
        opt = torch.optim.AdamW(model.parameters(), lr=args.lr_warmup)
        metrics = train_one_epoch(model, loader, opt, criterion, device, epoch=1)
        print(f"Test Run Success: Loss={metrics['total_loss']:.4f} (Cls={metrics['cls_loss']:.4f}, Reg={metrics['reg_loss']:.4f}) in {metrics['time_s']:.1f}s")
        # Save test checkpoint
        torch.save(model.state_dict(), os.path.join(args.save_dir, 'lightuav_sot_test.pth'))
        return

    # ================= Stage 1: Warmup (Backbone Frozen) =================
    print(f"\n=================== Stage 1: Warmup Heads ({args.epochs_warmup} Epochs) ===================")
    for p in model.backbone.parameters():
        p.requires_grad = False

    warmup_params = [p for p in model.parameters() if p.requires_grad]
    optimizer_w = torch.optim.AdamW(warmup_params, lr=args.lr_warmup, weight_decay=1e-4)

    best_loss = float('inf')
    for ep in range(1, args.epochs_warmup + 1):
        metrics = train_one_epoch(model, loader, optimizer_w, criterion, device, epoch=ep)
        print(f"[Stage 1 - Epoch {ep:02d}/{args.epochs_warmup:02d}] "
              f"Loss: {metrics['total_loss']:.4f} (Cls: {metrics['cls_loss']:.4f}, Reg: {metrics['reg_loss']:.4f}) "
              f"Time: {metrics['time_s']:.1f}s")

    # ================= Stage 2: Joint End-to-End Fine-Tuning =================
    print(f"\n=================== Stage 2: Joint Fine-Tuning ({args.epochs_joint} Epochs) ===================")
    for p in model.backbone.parameters():
        p.requires_grad = True

    optimizer_j = torch.optim.AdamW(model.parameters(), lr=args.lr_joint, weight_decay=1e-4)
    scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(optimizer_j, T_max=args.epochs_joint, eta_min=1e-6)

    for ep in range(1, args.epochs_joint + 1):
        metrics = train_one_epoch(model, loader, optimizer_j, criterion, device, epoch=ep)
        scheduler.step()
        cur_lr = scheduler.get_last_lr()[0]

        print(f"[Stage 2 - Epoch {ep:02d}/{args.epochs_joint:02d} | lr: {cur_lr:.2e}] "
              f"Loss: {metrics['total_loss']:.4f} (Cls: {metrics['cls_loss']:.4f}, Reg: {metrics['reg_loss']:.4f}) "
              f"Time: {metrics['time_s']:.1f}s")

        if metrics['total_loss'] < best_loss:
            best_loss = metrics['total_loss']
            best_ckpt = os.path.join(args.save_dir, 'lightuav_sot_best.pth')
            torch.save(model.state_dict(), best_ckpt)
            print(f" -> Best checkpoint saved: {best_ckpt} (Loss: {best_loss:.4f})")

    # Export deploy model with fused Conv3x3s
    print("\nFolding multi-branch weights for deployment...")
    model.switch_to_deploy()
    deploy_ckpt = os.path.join(args.save_dir, 'lightuav_sot_deploy.pth')
    torch.save(model.state_dict(), deploy_ckpt)
    print(f"Deploy-folded model saved: {deploy_ckpt}")


if __name__ == '__main__':
    main()

