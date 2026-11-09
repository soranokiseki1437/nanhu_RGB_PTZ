"""
DAC-Head: Dynamic Attention Correlation Head and Prediction Heads
for Long-Range Visible-Light Anti-UAV Single Object Tracking.

Key Problem Solved:
Standard SOT networks use unweighted cross-correlation, causing severe false peaks
and drift when drones fly near tree canopies, ground clutter, or cloud boundaries.
DAC-Head dynamically estimates spatial-channel saliency from the template,
suppresses background perimeter interference within the template window,
and outputs high-confidence classification and sub-pixel bounding box regressions.
"""

import torch
import torch.nn as nn
import torch.nn.functional as F
from typing import Tuple
from models.sot.rep_backbone import RepBlock, conv_bn


def pixel_corr_mat(z: torch.Tensor, x: torch.Tensor) -> torch.Tensor:
    """
    Vectorized Point-wise Correlation via Batch Matrix Multiplication.
    Inputs:
      z: [B, C, Hz, Wz] (Template, e.g. [B, 96, 8, 8])
      x: [B, C, Hx, Wx] (Search, e.g. [B, 96, 16, 16])
    Output:
      corr: [B, Hz*Wz, Hx, Wx] (e.g. [B, 64, 16, 16])
    """
    b, c, hx, wx = x.size()
    _, _, hz, wz = z.size()
    # Reshape z to [B, Hz*Wz, C]
    z_mat = z.view(b, c, -1).transpose(1, 2)
    # Reshape x to [B, C, Hx*Wx]
    x_mat = x.view(b, c, -1)
    # Batch matmul: [B, Hz*Wz, C] @ [B, C, Hx*Wx] -> [B, Hz*Wz, Hx*Wx]
    corr = torch.matmul(z_mat, x_mat)
    return corr.view(b, hz * wz, hx, wx)


class DynamicAttentionCorrelation(nn.Module):
    """
    Dynamic Attention Correlation (DAC) Module.
    Computes spatial-channel saliency on the template to suppress false background peaks.
    """
    def __init__(self, in_channels: int = 96, out_channels: int = 128, template_sz: int = 8, use_dac: bool = True):
        super(DynamicAttentionCorrelation, self).__init__()
        self.in_channels = in_channels
        self.out_channels = out_channels
        self.template_points = template_sz * template_sz  # 8*8 = 64
        self.use_dac = use_dac

        # Template spatial saliency gate: highlights drone, suppresses background sky/trees
        self.spatial_gate = nn.Sequential(
            nn.Conv2d(in_channels, in_channels // 2, kernel_size=3, padding=1),
            nn.ReLU(inplace=True),
            nn.Conv2d(in_channels // 2, 1, kernel_size=1),
            nn.Sigmoid()
        )

        # Template channel attention gate
        self.channel_gate = nn.Sequential(
            nn.AdaptiveAvgPool2d(1),
            nn.Conv2d(in_channels, in_channels // 4, kernel_size=1),
            nn.ReLU(inplace=True),
            nn.Conv2d(in_channels // 4, in_channels, kernel_size=1),
            nn.Sigmoid()
        )

        # Post-correlation channel projection to out_channels (128)
        self.corr_proj = nn.Sequential(
            RepBlock(in_channels=self.template_points, out_channels=out_channels, stride=1),
            RepBlock(in_channels=out_channels, out_channels=out_channels, stride=1)
        )

    def forward(self, z: torch.Tensor, x: torch.Tensor) -> torch.Tensor:
        if not self.use_dac:
            # Baseline/Ablation: unweighted cross-correlation
            corr = pixel_corr_mat(z, x)
            return self.corr_proj(corr)

        # Step 1: Spatial saliency on template
        s_weight = self.spatial_gate(z)  # [B, 1, Hz, Wz]
        z_sal = z * s_weight

        # Step 2: Channel importance modulation on both template and search
        c_weight = self.channel_gate(z_sal)  # [B, C, 1, 1]
        z_mod = z_sal * c_weight
        x_mod = x * c_weight

        # Step 3: Vectorized correlation
        corr = pixel_corr_mat(z_mod, x_mod)  # [B, 64, 16, 16]

        # Step 4: Projection to target feature dimension
        feat = self.corr_proj(corr)  # [B, 128, 16, 16]
        return feat

    def switch_to_deploy(self):
        for m in self.modules():
            if isinstance(m, RepBlock):
                m.switch_to_deploy()


class SOTPredictionHeads(nn.Module):
    """
    Dual-branch Anchor-free Prediction Heads:
      - Cls Head: Outputs target center probability map [B, 1, H, W]
      - Reg Head: Outputs bounding box distances (left, top, right, bottom) [B, 4, H, W]
    """
    def __init__(self, in_channels: int = 128, stride: int = 16):
        super(SOTPredictionHeads, self).__init__()
        self.stride = stride

        # Classification tower (Depthwise separable structure for low latency)
        self.cls_tower = nn.Sequential(
            RepBlock(in_channels=in_channels, out_channels=in_channels, stride=1),
            RepBlock(in_channels=in_channels, out_channels=in_channels, stride=1)
        )
        self.cls_pred = nn.Conv2d(in_channels, 1, kernel_size=3, padding=1, bias=True)

        # Regression tower
        self.reg_tower = nn.Sequential(
            RepBlock(in_channels=in_channels, out_channels=in_channels, stride=1),
            RepBlock(in_channels=in_channels, out_channels=in_channels, stride=1)
        )
        self.reg_pred = nn.Conv2d(in_channels, 4, kernel_size=3, padding=1, bias=True)

        # Prior initialization for classification head to avoid loss explosion
        nn.init.constant_(self.cls_pred.bias, -4.59)  # sigmoid(-4.59) ≈ 0.01

    def forward(self, feat: torch.Tensor) -> Tuple[torch.Tensor, torch.Tensor]:
        # Cls logits & score
        cls_feat = self.cls_tower(feat)
        cls_logits = self.cls_pred(cls_feat)
        cls_score = torch.sigmoid(cls_logits)

        # Reg distances (ltrb >= 0, scaled by stride)
        reg_feat = self.reg_tower(feat)
        reg_dist = F.relu(self.reg_pred(reg_feat)) * float(self.stride)

        return cls_score, reg_dist

    def switch_to_deploy(self):
        for m in self.modules():
            if isinstance(m, RepBlock):
                m.switch_to_deploy()

