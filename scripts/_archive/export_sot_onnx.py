"""
LightUAV-SOT Pure Static ONNX Export Tool.
Exports two decoupled static subgraphs with opset 17 for DirectML / RK3588 deployment:
1. sot_template.onnx: template_img [1, 3, 127, 127] -> z_feat [1, 96, 8, 8]
2. sot_search.onnx:   search_img [1, 3, 255, 255], z_feat [1, 96, 8, 8] -> cls_score [1, 1, 16, 16], reg_dist [1, 4, 16, 16]
"""

import os
import sys
import argparse
import torch
import torch.nn as nn

WORKSPACE_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if WORKSPACE_ROOT not in sys.path:
    sys.path.insert(0, WORKSPACE_ROOT)

from models.sot.lightuav_sot import LightUAV_SOT


class TemplateWrapper(nn.Module):
    def __init__(self, model: LightUAV_SOT):
        super(TemplateWrapper, self).__init__()
        self.model = model

    def forward(self, template: torch.Tensor) -> torch.Tensor:
        return self.model.extract_template(template)


class SearchWrapper(nn.Module):
    def __init__(self, model: LightUAV_SOT):
        super(SearchWrapper, self).__init__()
        self.model = model

    def forward(self, search: torch.Tensor, z_feat: torch.Tensor):
        return self.model.track_search(search, z_feat)


def export_onnx(model_path: str, out_dir: str = "models", opset: int = 17):
    os.makedirs(out_dir, exist_ok=True)
    device = torch.device('cpu')  # Export on CPU for ONNX canonicalization

    model = LightUAV_SOT()
    if os.path.isfile(model_path):
        print(f"Loading weights from {model_path}...")
        state_dict = torch.load(model_path, map_location='cpu')
        model.load_state_dict(state_dict, strict=False)
    else:
        print(f"Warning: {model_path} not found. Exporting architectural skeleton with random weights.")

    model.eval()
    print("Folding structural re-parameterization branches for deployment...")
    model.switch_to_deploy()

    # 1. Export sot_template.onnx
    template_model = TemplateWrapper(model)
    template_input = torch.randn(1, 3, 127, 127, dtype=torch.float32)
    template_onnx_path = os.path.join(out_dir, "sot_template.onnx")

    print(f"Exporting template subgraph to {template_onnx_path}...")
    torch.onnx.export(
        template_model,
        template_input,
        template_onnx_path,
        export_params=True,
        opset_version=opset,
        do_constant_folding=True,
        dynamo=False,
        input_names=['template_img'],
        output_names=['template_feat']
    )
    print(f"Template ONNX size: {os.path.getsize(template_onnx_path) / 1024 / 1024:.2f} MB")

    # 2. Export sot_search.onnx
    search_model = SearchWrapper(model)
    search_input = torch.randn(1, 3, 255, 255, dtype=torch.float32)
    z_feat_input = torch.randn(1, 96, 8, 8, dtype=torch.float32)
    search_onnx_path = os.path.join(out_dir, "sot_search.onnx")

    print(f"Exporting search subgraph to {search_onnx_path}...")
    torch.onnx.export(
        search_model,
        (search_input, z_feat_input),
        search_onnx_path,
        export_params=True,
        opset_version=opset,
        do_constant_folding=True,
        dynamo=False,
        input_names=['search_img', 'template_feat'],
        output_names=['cls_score', 'reg_dist']
    )
    print(f"Search ONNX size: {os.path.getsize(search_onnx_path) / 1024 / 1024:.2f} MB")
    print("Static ONNX export completed successfully!")


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="Export LightUAV-SOT to Static ONNX")
    parser.add_argument('--weights', type=str, default='weights/sot/lightuav_sot_best.pth')
    parser.add_argument('--out-dir', type=str, default='models')
    parser.add_argument('--opset', type=int, default=17)
    args = parser.parse_args()

    export_onnx(model_path=args.weights, out_dir=args.out_dir, opset=args.opset)
