"""
LightUAV-SOT: Lightweight Anti-UAV Single Object Tracking Architecture.
Integrated System:
  - RepLightBackbone (Structural Re-parameterization CNN)
  - MFFN (Multi-scale Feature Fusion Network)
  - DynamicAttentionCorrelation (DAC-Head)
  - SOTPredictionHeads (Anchor-free Dual-branch Cls & Reg Heads)
"""

import torch
import torch.nn as nn
from typing import Dict, Tuple
from models.sot.rep_backbone import RepLightBackbone
from models.sot.mffn import MFFN
from models.sot.dac_head import DynamicAttentionCorrelation, SOTPredictionHeads


class LightUAV_SOT(nn.Module):
    """
    LightUAV-SOT Full Model.
    Designed for long-range electro-optical anti-UAV tracking with <= 2.0M parameters
    and <= 3.0ms inference latency on RTX 5070 / RK3588.
    """
    def __init__(self, channels_p2: int = 64, channels_p4: int = 96,
                 fused_channels: int = 96, corr_channels: int = 128, stride: int = 16):
        super(LightUAV_SOT, self).__init__()
        self.stride = stride
        self.fused_channels = fused_channels
        self.corr_channels = corr_channels

        # 1. Structural Re-parameterization Backbone (Shared weights for template and search)
        self.backbone = RepLightBackbone(out_channels_p2=channels_p2, out_channels_p4=channels_p4)

        # 2. Multi-scale Feature Fusion Network (Shared weights)
        self.mffn = MFFN(in_channels_p2=channels_p2, in_channels_p4=channels_p4, out_channels=fused_channels)

        # 3. Dynamic Attention Correlation
        self.dac = DynamicAttentionCorrelation(in_channels=fused_channels, out_channels=corr_channels, template_sz=8)

        # 4. Dual-branch Anchor-free Heads
        self.heads = SOTPredictionHeads(in_channels=corr_channels, stride=stride)

    def extract_template(self, template: torch.Tensor) -> torch.Tensor:
        """
        Extract and fuse template feature z.
        Called once during target initialization (initSlot).
        Input:  [B, 3, 127, 127]
        Output: [B, 96, 8, 8] cached in GPU VRAM
        """
        p2_z, p4_z = self.backbone(template)
        z_feat = self.mffn(p2_z, p4_z)
        return z_feat

    def track_search(self, search: torch.Tensor, z_feat: torch.Tensor) -> Tuple[torch.Tensor, torch.Tensor]:
        """
        High-frequency tracking loop execution.
        Input:
          search: [B, 3, 255, 255]
          z_feat: [B, 96, 8, 8] (Cached template feature)
        Output:
          cls_score: [B, 1, 16, 16] (Confidence heatmap in [0, 1])
          reg_dist:  [B, 4, 16, 16] (Distance offsets: l, t, r, b in pixels)
        """
        p2_x, p4_x = self.backbone(search)
        x_feat = self.mffn(p2_x, p4_x)
        corr_feat = self.dac(z_feat, x_feat)
        cls_score, reg_dist = self.heads(corr_feat)
        return cls_score, reg_dist

    def forward(self, template: torch.Tensor, search: torch.Tensor) -> Dict[str, torch.Tensor]:
        """
        End-to-end training forward pass.
        Inputs:
          template: [B, 3, 127, 127]
          search:   [B, 3, 255, 255]
        Outputs:
          dict with 'cls_score' and 'reg_dist'
        """
        z_feat = self.extract_template(template)
        cls_score, reg_dist = self.track_search(search, z_feat)
        return {
            'cls_score': cls_score,
            'reg_dist': reg_dist
        }

    def switch_to_deploy(self):
        """
        Convert all multi-branch RepBlocks into single-path Conv3x3s across the entire network.
        Significantly reduces memory bandwidth and cuts latency.
        """
        self.backbone.switch_to_deploy()
        self.mffn.switch_to_deploy()
        self.dac.switch_to_deploy()
        self.heads.switch_to_deploy()

    def get_num_params(self) -> Tuple[int, int]:
        """Returns (total_params, trainable_params)."""
        total = sum(p.numel() for p in self.parameters())
        trainable = sum(p.numel() for p in self.parameters() if p.requires_grad)
        return total, trainable

