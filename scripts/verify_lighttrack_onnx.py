#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
LightTrackM 导出模型验证脚本。
执行 E1（张量签名）、E2（算子集合与 opset 版本）、E3（PyTorch vs ORT CPU 数值对齐）验证。
"""

import os
import sys
import argparse
import numpy as np
import onnx
import onnxruntime as ort
import torch

from export_lighttrack_onnx import PATH_NAME, TemplateBranch, SearchBranch

EXPECTED_SPECS = {
    "lighttrack_template.onnx": {
        "inputs": {"template_img": [1, 3, 127, 127]},
        "outputs": {"template_feat": [1, 96, 8, 8]},
    },
    "lighttrack_search_256.onnx": {
        "inputs": {
            "search_img": [1, 3, 256, 256],
            "template_feat": [1, 96, 8, 8],
        },
        "outputs": {
            "cls_logit": [1, 1, 16, 16],
            "reg_dist": [1, 4, 16, 16],
        },
    },
    "lighttrack_search_288.onnx": {
        "inputs": {
            "search_img": [1, 3, 288, 288],
            "template_feat": [1, 96, 8, 8],
        },
        "outputs": {
            "cls_logit": [1, 1, 18, 18],
            "reg_dist": [1, 4, 18, 18],
        },
    },
}

FORBIDDEN_OP_TYPES = {"Loop", "Scan", "If"}


def check_e1_and_e2(model_path, spec):
    print(f"\n--- Checking E1 & E2: {os.path.basename(model_path)} ---")
    model = onnx.load(model_path)

    # E2: Opset check
    opset_versions = {entry.domain: entry.version for entry in model.opset_import}
    default_opset = opset_versions.get("", None)
    print(f"  Opset imports: {opset_versions}")
    if default_opset != 17:
        raise AssertionError(f"E2 FAILED: Expected opset 17, got {default_opset}")

    # E2: Forbidden ops & domain check
    all_ops = set()
    for node in model.graph.node:
        all_ops.add(node.op_type)
        if node.op_type in FORBIDDEN_OP_TYPES:
            raise AssertionError(f"E2 FAILED: Forbidden op '{node.op_type}' found.")
        if node.domain not in ("", "ai.onnx"):
            raise AssertionError(f"E2 FAILED: Non-standard domain '{node.domain}' found.")
    print(f"  E2 PASS: Op types present ({len(all_ops)}): {sorted(list(all_ops))}")

    # E1: Input/Output Signature check
    def parse_tensor_shape(type_proto):
        shape = []
        for d in type_proto.tensor_type.shape.dim:
            if d.HasField("dim_value"):
                shape.append(d.dim_value)
            elif d.HasField("dim_param"):
                raise AssertionError(f"E1 FAILED: Dynamic dimension '{d.dim_param}' found.")
            else:
                raise AssertionError("E1 FAILED: Unknown dimension type.")
        return shape

    actual_inputs = {inp.name: parse_tensor_shape(inp.type) for inp in model.graph.input}
    actual_outputs = {out.name: parse_tensor_shape(out.type) for out in model.graph.output}

    print(f"  Inputs:  {actual_inputs}")
    print(f"  Outputs: {actual_outputs}")

    if actual_inputs != spec["inputs"]:
        raise AssertionError(f"E1 FAILED: Input mismatch. Expected {spec['inputs']}, got {actual_inputs}")
    if actual_outputs != spec["outputs"]:
        raise AssertionError(f"E1 FAILED: Output mismatch. Expected {spec['outputs']}, got {actual_outputs}")

    print("  E1 PASS: Tensor signature exactly matches specification.")


def check_e3_numerical(models_dir, lighttrack_root, weights_path):
    print("\n--- Checking E3: Numerical Alignment (PyTorch vs ORT CPU) ---")
    sys.path.insert(0, lighttrack_root)
    import lib.models.models as models
    from lib.utils.utils import load_pretrain

    net = models.__dict__["LightTrackM_Subnet"](PATH_NAME, stride=16)
    net = load_pretrain(net, weights_path, print_unuse=False).eval()

    torch_tmpl = TemplateBranch(net).eval()
    torch_search = SearchBranch(net).eval()

    sess_tmpl = ort.InferenceSession(os.path.join(models_dir, "lighttrack_template.onnx"), providers=["CPUExecutionProvider"])
    sess_256 = ort.InferenceSession(os.path.join(models_dir, "lighttrack_search_256.onnx"), providers=["CPUExecutionProvider"])
    sess_288 = ort.InferenceSession(os.path.join(models_dir, "lighttrack_search_288.onnx"), providers=["CPUExecutionProvider"])

    for seed in [42, 100, 2024]:
        torch.manual_seed(seed)
        np.random.seed(seed)
        print(f"\n[Seed {seed}]")

        # 1. Template
        tmpl_in = torch.rand(1, 3, 127, 127, dtype=torch.float32)
        with torch.no_grad():
            ref_zf = torch_tmpl(tmpl_in).numpy()
        ort_zf = sess_tmpl.run(["template_feat"], {"template_img": tmpl_in.numpy()})[0]

        rel_err_zf = np.max(np.abs(ref_zf - ort_zf)) / np.max(np.abs(ref_zf))
        print(f"  Template zf rel_err: {rel_err_zf:.3e} (max_diff={np.max(np.abs(ref_zf - ort_zf)):.3e})")
        if rel_err_zf > 1e-5:
            raise AssertionError(f"E3 FAILED on template_feat: rel_err={rel_err_zf} > 1e-5")

        # 2. Search 256
        s256_in = torch.rand(1, 3, 256, 256, dtype=torch.float32)
        zf_t = torch.from_numpy(ref_zf)
        with torch.no_grad():
            ref_cls256, ref_reg256 = torch_search(s256_in, zf_t)
            ref_cls256 = ref_cls256.numpy()
            ref_reg256 = ref_reg256.numpy()

        ort_cls256, ort_reg256 = sess_256.run(["cls_logit", "reg_dist"], {"search_img": s256_in.numpy(), "template_feat": ort_zf})
        rel_err_cls256 = np.max(np.abs(ref_cls256 - ort_cls256)) / np.max(np.abs(ref_cls256))
        rel_err_reg256 = np.max(np.abs(ref_reg256 - ort_reg256)) / np.max(np.abs(ref_reg256))
        print(f"  Search 256 cls rel_err: {rel_err_cls256:.3e}, reg rel_err: {rel_err_reg256:.3e}")
        if rel_err_cls256 > 1e-5 or rel_err_reg256 > 1e-5:
            raise AssertionError(f"E3 FAILED on search 256: cls={rel_err_cls256}, reg={rel_err_reg256}")

        # 3. Search 288
        s288_in = torch.rand(1, 3, 288, 288, dtype=torch.float32)
        with torch.no_grad():
            ref_cls288, ref_reg288 = torch_search(s288_in, zf_t)
            ref_cls288 = ref_cls288.numpy()
            ref_reg288 = ref_reg288.numpy()

        ort_cls288, ort_reg288 = sess_288.run(["cls_logit", "reg_dist"], {"search_img": s288_in.numpy(), "template_feat": ort_zf})
        rel_err_cls288 = np.max(np.abs(ref_cls288 - ort_cls288)) / np.max(np.abs(ref_cls288))
        rel_err_reg288 = np.max(np.abs(ref_reg288 - ort_reg288)) / np.max(np.abs(ref_reg288))
        print(f"  Search 288 cls rel_err: {rel_err_cls288:.3e}, reg rel_err: {rel_err_reg288:.3e}")
        if rel_err_cls288 > 1e-5 or rel_err_reg288 > 1e-5:
            raise AssertionError(f"E3 FAILED on search 288: cls={rel_err_cls288}, reg={rel_err_reg288}")

    print("\n  E3 PASS: All relative errors <= 1e-5 across 3 random seeds.")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--models-dir", default="models")
    ap.add_argument(
        "--lighttrack-root",
        default=r"D:\PyProjects\SOT_Tracking_Baselines_官方权重试跑\algorithms\LightTrack",
    )
    ap.add_argument("--weights", default="weights/sot/lighttrack_m.pth")
    a = ap.parse_args()

    for filename, spec in EXPECTED_SPECS.items():
        model_path = os.path.join(a.models_dir, filename)
        if not os.path.exists(model_path):
            raise FileNotFoundError(f"Model not found: {model_path}")
        check_e1_and_e2(model_path, spec)

    check_e3_numerical(a.models_dir, a.lighttrack_root, a.weights)
    print("\n==========================================")
    print("ALL VERIFICATIONS PASSED: E1, E2, E3 [OK]")
    print("==========================================")


if __name__ == "__main__":
    main()

