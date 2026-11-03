"""
MFFN: Multi-scale Feature Fusion Network
for Anti-UAV micro-target spatial detail recovery.

Key Problem Solved:
Tiny UAVs (10x10 ~ 20x20 px) lose spatial contours after 16x downsampling in standard SOT networks.
MFFN extracts high-resolution P2 (stride 4) spatial details, aligns the scale to P4 (stride 16),
and dynamically fuses them via Channel-Spatial Attention gating.
"""

import torch
import torch.nn as nn
import torch.nn.functional as F
from models.sot.rep_backbone import RepBlock, conv_bn


class ChannelAttention(nn.Module):
    """Squeeze-and-Excitation Channel Gate for multi-scale feature weighting."""
    def __init__(self, channels: int, reduction: int = 4):
        super(ChannelAttention, self).__init__()
        reduced = max(channels // reduction, 16)
        self.fc = nn.Sequential(
            nn.AdaptiveAvgPool2d(1),
            nn.Conv2d(channels, reduced, kernel_size=1, bias=True),
            nn.ReLU(inplace=True),
            nn.Conv2d(reduced, channels, kernel_size=1, bias=True),
            nn.Sigmoid()
        )

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return x * self.fc(x)


class MFFN(nn.Module):
    """
    Multi-scale Feature Fusion Network.
    Inputs:
      - p2: [B, C_p2, 4*H, 4*W] (Stride 4 shallow details, default C_p2 = 64)
      - p4: [B, C_p4, H, W]     (Stride 16 deep semantics, default C_p4 = 96)
    Output:
      - fused: [B, out_channels, H, W] (Fused representation, default out_channels = 96)
    """
    def __init__(self, in_channels_p2: int = 64, in_channels_p4: int = 96, out_channels: int = 96):
        super(MFFN, self).__init__()
        self.in_channels_p2 = in_channels_p2
        self.in_channels_p4 = in_channels_p4
        self.out_channels = out_channels

        # 4x Spatial Downsampling for P2: Stride 2 -> Stride 2
        # Using RepBlocks for deployable zero-overhead folding
        self.p2_downsample = nn.Sequential(
            RepBlock(in_channels=in_channels_p2, out_channels=in_channels_p2, stride=2),
            RepBlock(in_channels=in_channels_p2, out_channels=out_channels, stride=2)
        )

        # Semantic projection for P4
        self.p4_proj = RepBlock(in_channels=in_channels_p4, out_channels=out_channels, stride=1)

        # Channel Attention Gating on concatenated representations
        self.fusion_conv = nn.Sequential(
            conv_bn(in_channels=out_channels * 2, out_channels=out_channels, kernel_size=1, stride=1),
            nn.ReLU(inplace=True)
        )
        self.channel_gate = ChannelAttention(channels=out_channels)

    def forward(self, p2: torch.Tensor, p4: torch.Tensor) -> torch.Tensor:
        # Downsample P2 to match P4's spatial grid [H, W]
        p2_down = self.p2_downsample(p2)
        p4_proj = self.p4_proj(p4)

        # Ensure exact spatial match if rounding occurs on odd dimensions
        if p2_down.shape[-2:] != p4_proj.shape[-2:]:
            p2_down = F.interpolate(p2_down, size=p4_proj.shape[-2:], mode='bilinear', align_corners=False)

        # Concatenate shallow contour and deep semantic features
        cat = torch.cat([p4_proj, p2_down], dim=1)
        fused = self.fusion_conv(cat)
        fused = self.channel_gate(fused) + p4_proj  # Residual shortcut
        return fused

    def switch_to_deploy(self):
        """Recursively fold RepBlocks inside MFFN."""
        for m in self.modules():
            if isinstance(m, RepBlock):
                m.switch_to_deploy()

