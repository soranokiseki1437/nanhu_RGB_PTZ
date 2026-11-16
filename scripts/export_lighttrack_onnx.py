#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
LightTrackM 纯静态 ONNX 模型导出脚本。
将模板分支 (127x127) 与搜索分支 (256x256, 288x288) 分别导出为纯静态 ONNX 模型 (opset 17)。
输入为 [0, 1] 范围的 RGB 浮点张量 (NCHW)，ImageNet 均值方差归一化内嵌于模型计算图。
"""

import os
import sys
import argparse
import torch
import torch.nn as nn

PATH_NAME = "back_04502514044521042540+cls_211000022+reg_100000111_ops_32"
MEAN = torch.tensor([0.485, 0.456, 0.406]).view(1, 3, 1, 1)
STD = torch.tensor([0.229, 0.224, 0.225]).view(1, 3, 1, 1)


class TemplateBranch(nn.Module):
    def __init__(self, net):
        super().__init__()
        self.net = net
        self.register_buffer("mean", MEAN)
        self.register_buffer("std", STD)

    def forward(self, template_img):  # RGB, [0,1]
        return self.net.features((template_img - self.mean) / self.std)


class SearchBranch(nn.Module):
    def __init__(self, net):
        super().__init__()
        self.net = net
        self.register_buffer("mean", MEAN)
        self.register_buffer("std", STD)

    def forward(self, search_img, template_feat):
        xf = self.net.features((search_img - self.mean) / self.std)
        zf, xf = self.net.neck(template_feat, xf)
        oup = self.net.head(self.net.feature_fusor(zf, xf))
        return oup["cls"], oup["reg"]


def main():
    ap = argparse.ArgumentParser(description="Export LightTrackM to static ONNX models")
    ap.add_argument(
        "--lighttrack-root",
        default=r"D:\PyProjects\SOT_Tracking_Baselines_官方权重试跑\algorithms\LightTrack",
        help="Path to LightTrack algorithm root",
    )
    ap.add_argument(
        "--weights",
        default="weights/sot/lighttrack_m.pth",
        help="Path to LightTrackM weights (.pth)",
    )
    ap.add_argument(
        "--out-dir",
        default="models",
        help="Output directory for exported ONNX models",
    )
    a = ap.parse_args()

    if not os.path.exists(a.weights):
        raise FileNotFoundError(f"Weights file not found: {a.weights}")

    sys.path.insert(0, a.lighttrack_root)
    import lib.models.models as models
    from lib.utils.utils import load_pretrain

    print(f"Loading LightTrackM model from {a.weights}...")
    net = models.__dict__["LightTrackM_Subnet"](PATH_NAME, stride=16)
    net = load_pretrain(net, a.weights, print_unuse=False).eval()

    os.makedirs(a.out_dir, exist_ok=True)
    kw = dict(opset_version=17, dynamo=False)

    tmpl_path = os.path.join(a.out_dir, "lighttrack_template.onnx")
    print(f"Exporting template branch (1x3x127x127) -> {tmpl_path}...")
    torch.onnx.export(
        TemplateBranch(net).eval(),
        torch.rand(1, 3, 127, 127),
        tmpl_path,
        input_names=["template_img"],
        output_names=["template_feat"],
        **kw,
    )

    for xs in (256, 288):
        search_path = os.path.join(a.out_dir, f"lighttrack_search_{xs}.onnx")
        print(f"Exporting search branch (1x3x{xs}x{xs}) -> {search_path}...")
        torch.onnx.export(
            SearchBranch(net).eval(),
            (torch.rand(1, 3, xs, xs), torch.rand(1, 96, 8, 8)),
            search_path,
            input_names=["search_img", "template_feat"],
            output_names=["cls_logit", "reg_dist"],
            **kw,
        )

    print("All ONNX models exported successfully.")


if __name__ == "__main__":
    main()
