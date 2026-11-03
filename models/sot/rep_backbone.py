"""
Rep-LightBackbone: Structural Re-parameterization Lightweight Backbone
for Long-Range Visible-Light Anti-UAV Single Object Tracking.

Key Design:
- Training phase: Multi-branch topology (Conv3x3 + Conv1x1 + Identity) for rich feature representation.
- Inference/Deployment phase: Algebraically folded into a single single-path Conv3x3 with zero branching overhead.
- Dual-scale output: P2 (shallow high-resolution spatial details for tiny drones) & P4 (deep discriminative semantics).
"""

import torch
import torch.nn as nn
import torch.nn.functional as F
from typing import Tuple, Optional


def conv_bn(in_channels: int, out_channels: int, kernel_size: int, stride: int = 1, padding: int = 0, groups: int = 1):
    cell = nn.Sequential()
    cell.add_module('conv', nn.Conv2d(in_channels, out_channels, kernel_size=kernel_size,
                                      stride=stride, padding=padding, groups=groups, bias=False))
    cell.add_module('bn', nn.BatchNorm2d(out_channels))
    return cell


class RepBlock(nn.Module):
    """
    Structural Re-parameterization Convolution Block.
    Training: 3x3 Conv-BN + 1x1 Conv-BN + Identity-BN (if shape matches).
    Inference: Fused into a single 3x3 Conv2d with bias.
    """
    def __init__(self, in_channels: int, out_channels: int, stride: int = 1, groups: int = 1, act: bool = True):
        super(RepBlock, self).__init__()
        self.in_channels = in_channels
        self.out_channels = out_channels
        self.stride = stride
        self.groups = groups
        self.act_fn = nn.ReLU(inplace=True) if act else nn.Identity()
        self.is_deployed = False

        # Multi-branch during training
        self.conv3x3 = conv_bn(in_channels, out_channels, kernel_size=3, stride=stride, padding=1, groups=groups)
        self.conv1x1 = conv_bn(in_channels, out_channels, kernel_size=1, stride=stride, padding=0, groups=groups)
        
        if in_channels == out_channels and stride == 1:
            self.identity = nn.BatchNorm2d(in_channels)
        else:
            self.identity = None

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        if self.is_deployed:
            return self.act_fn(self.fused_conv(x))

        out = self.conv3x3(x) + self.conv1x1(x)
        if self.identity is not None:
            out = out + self.identity(x)
        return self.act_fn(out)

    def _fuse_conv_bn(self, conv_bn_block) -> Tuple[torch.Tensor, torch.Tensor]:
        conv = conv_bn_block.conv
        bn = conv_bn_block.bn
        kernel = conv.weight
        running_mean = bn.running_mean
        running_var = bn.running_var
        gamma = bn.weight
        beta = bn.bias
        eps = bn.eps

        std = (running_var + eps).sqrt()
        t = (gamma / std).reshape(-1, 1, 1, 1)
        fused_weight = kernel * t
        fused_bias = beta - running_mean * gamma / std
        return fused_weight, fused_bias

    def _fuse_identity_bn(self, bn) -> Tuple[torch.Tensor, torch.Tensor]:
        running_mean = bn.running_mean
        running_var = bn.running_var
        gamma = bn.weight
        beta = bn.bias
        eps = bn.eps

        std = (running_var + eps).sqrt()
        # Identity 3x3 kernel
        in_c = self.in_channels
        out_c = self.out_channels
        groups = self.groups
        input_dim = in_c // groups
        
        kernel = torch.zeros((out_c, input_dim, 3, 3), dtype=gamma.dtype, device=gamma.device)
        for i in range(out_c):
            kernel[i, i % input_dim, 1, 1] = 1.0

        t = (gamma / std).reshape(-1, 1, 1, 1)
        fused_weight = kernel * t
        fused_bias = beta - running_mean * gamma / std
        return fused_weight, fused_bias

    def switch_to_deploy(self):
        """Converts multi-branch training structure into a single 3x3 Conv."""
        if self.is_deployed:
            return

        w3, b3 = self._fuse_conv_bn(self.conv3x3)
        w1, b1 = self._fuse_conv_bn(self.conv1x1)
        # Pad 1x1 kernel to 3x3
        w1_padded = F.pad(w1, (1, 1, 1, 1))

        fused_w = w3 + w1_padded
        fused_b = b3 + b1

        if self.identity is not None:
            wid, bid = self._fuse_identity_bn(self.identity)
            fused_w = fused_w + wid
            fused_b = fused_b + bid

        self.fused_conv = nn.Conv2d(
            self.in_channels, self.out_channels, kernel_size=3,
            stride=self.stride, padding=1, groups=self.groups, bias=True
        )
        self.fused_conv.weight.data = fused_w
        self.fused_conv.bias.data = fused_b

        # Remove training branches to save parameters and VRAM
        self.__delattr__('conv3x3')
        self.__delattr__('conv1x1')
        if hasattr(self, 'identity'):
            self.__delattr__('identity')
        self.is_deployed = True


class RepLightBackbone(nn.Module):
    """
    Rep-LightBackbone: Optimized for Anti-UAV micro-targets.
    Outputs:
      - feat_shallow (P2): Stride 4, channel 64 (spatial details, high-frequency contours)
      - feat_deep (P4): Stride 16, channel 96 (discriminative semantic representation)
    Total parameters (deploy): ~ 0.52 M.
    """
    def __init__(self, out_channels_p2: int = 64, out_channels_p4: int = 96):
        super(RepLightBackbone, self).__init__()
        self.out_channels_p2 = out_channels_p2
        self.out_channels_p4 = out_channels_p4

        # Stem: Input 3 -> 24, Stride 2
        self.stem = RepBlock(in_channels=3, out_channels=24, stride=2)

        # Stage 1: Stride 2 -> Total Stride 4, Channels 24 -> 48
        self.stage1 = nn.Sequential(
            RepBlock(in_channels=24, out_channels=48, stride=2),
            RepBlock(in_channels=48, out_channels=48, stride=1)
        )

        # Stage 2 (P2: Shallow High-Res Detail): Stride 1 -> Total Stride 4, Channels 48 -> 64
        self.stage2 = nn.Sequential(
            RepBlock(in_channels=48, out_channels=out_channels_p2, stride=1),
            RepBlock(in_channels=out_channels_p2, out_channels=out_channels_p2, stride=1)
        )

        # Stage 3: Stride 2 -> Total Stride 8, Channels 64 -> 80
        self.stage3 = nn.Sequential(
            RepBlock(in_channels=out_channels_p2, out_channels=80, stride=2),
            RepBlock(in_channels=80, out_channels=80, stride=1)
        )

        # Stage 4 (P4: Deep Semantic): Stride 2 -> Total Stride 16, Channels 80 -> 96
        self.stage4 = nn.Sequential(
            RepBlock(in_channels=80, out_channels=out_channels_p4, stride=2),
            RepBlock(in_channels=out_channels_p4, out_channels=out_channels_p4, stride=1),
            RepBlock(in_channels=out_channels_p4, out_channels=out_channels_p4, stride=1)
        )

    def forward(self, x: torch.Tensor) -> Tuple[torch.Tensor, torch.Tensor]:
        x0 = self.stem(x)       # Stride 2
        x1 = self.stage1(x0)    # Stride 4
        p2 = self.stage2(x1)    # Stride 4, P2 (Shallow detail feature)
        x3 = self.stage3(p2)    # Stride 8
        p4 = self.stage4(x3)    # Stride 16, P4 (Deep semantic feature)
        return p2, p4

    def switch_to_deploy(self):
        """Recursively convert all RepBlocks in backbone to single-path 3x3 conv."""
        for m in self.modules():
            if isinstance(m, RepBlock):
                m.switch_to_deploy()

