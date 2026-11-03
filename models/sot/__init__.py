"""
LightUAV-SOT: Lightweight Single Object Tracking Architecture for Anti-UAV Systems.
Incorporates:
- RepLightBackbone: Structural re-parameterization backbone
- MFFN: Multi-scale Feature Fusion Network
- DynamicAttentionCorrelation: Dynamic Attention Correlation matching head
- SOTPredictionHeads: Anchor-free classification & regression prediction heads
- LightUAV_SOT: Integrated end-to-end model
"""

from .rep_backbone import RepLightBackbone, RepBlock
from .mffn import MFFN
from .dac_head import DynamicAttentionCorrelation, SOTPredictionHeads
from .lightuav_sot import LightUAV_SOT

__all__ = [
    'RepLightBackbone',
    'RepBlock',
    'MFFN',
    'DynamicAttentionCorrelation',
    'SOTPredictionHeads',
    'LightUAV_SOT'
]
