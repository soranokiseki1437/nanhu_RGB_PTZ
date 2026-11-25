# -*- coding: utf-8 -*-
"""
反无人机目标检测模型训练流水线 (面向消融实验、域内微调与全量从零重训)
支持两种训练模式：
  1. finetune: 基于官方 COCO 预训练权重 (.pt) 进行领域微调 (默认 80 轮)
  2. scratch:  不加载任何预训练权重，纯依据网络结构 (.yaml) 随机初始化从零训练 (默认 300 轮)
环境底座: Python 3.11 + PyTorch 2.14.1 + CUDA 13.0 (RTX 5070 Blackwell sm_120)
默认模型矩阵: yolo11n / yolo11s / yolov8n / yolov8s
"""
import argparse
import json
import os
import shutil
from ultralytics import YOLO

BASE_DIR = r"d:\PyProjects\RGB_PTZ_Integrated"
DEFAULT_DATASET_YAML = os.path.join(BASE_DIR, r"datasets\anti_uav.yaml")
RUNS_ROOT = os.path.join(BASE_DIR, r"runs\detect_uav")
WEIGHTS_PRETRAINED_DIR = os.path.join(BASE_DIR, r"weights\pretrained")
WEIGHTS_FT_DIR = os.path.join(BASE_DIR, r"weights\finetuned")
WEIGHTS_SCRATCH_DIR = os.path.join(BASE_DIR, r"weights\scratch")
MODELS_DEPLOY_DIR = os.path.join(BASE_DIR, "models")

MODEL_LIST = ["yolo11n", "yolo11s", "yolov8n", "yolov8s"]


def ensure_dirs():
    for d in [WEIGHTS_PRETRAINED_DIR, WEIGHTS_FT_DIR, WEIGHTS_SCRATCH_DIR, MODELS_DEPLOY_DIR]:
        os.makedirs(d, exist_ok=True)


def extract_val_metrics(val_res) -> dict:
    """Extract standard detection metrics from Ultralytics validator output."""
    box = getattr(val_res, "box", None)
    if box is None:
        return {}
    return {
        "precision": round(float(box.mp), 5),
        "recall": round(float(box.mr), 5),
        "mAP50": round(float(box.map50), 5),
        "mAP50_95": round(float(box.map), 5),
    }


def build_train_config(mode: str, scratch_optim: str = "sgd") -> dict:
    """Build scientifically aligned training hyperparameters per mode."""
    if mode == "scratch":
        if scratch_optim == "sgd":
            opt_cfg = dict(
                optimizer="SGD",
                lr0=0.01,
                lrf=0.01,
                momentum=0.937,
                weight_decay=0.0005,
                cos_lr=True,
                warmup_epochs=5.0,
                close_mosaic=15,
                patience=0,
            )
        elif scratch_optim == "adamw":
            opt_cfg = dict(
                optimizer="AdamW",
                lr0=0.001,
                lrf=0.01,
                momentum=0.9,
                weight_decay=0.05,
                cos_lr=True,
                warmup_epochs=5.0,
                close_mosaic=15,
                patience=0,
            )
        else:
            raise ValueError(f"Unsupported scratch optimizer: {scratch_optim}")

        # Small-object from-scratch augmentation: preserve crisp tiny-target edges and tight AABBs
        aug_cfg = dict(
            mosaic=1.0,
            copy_paste=0.0,
            mixup=0.0,
            degrees=0.0,
            scale=0.5,
            translate=0.1,
            fliplr=0.5,
            flipud=0.0,
            erasing=0.0,
        )
        return {**opt_cfg, **aug_cfg}

    if mode == "finetune":
        opt_cfg = dict(
            optimizer="SGD",
            lr0=0.01,
            lrf=0.01,
            momentum=0.937,
            weight_decay=0.0005,
            cos_lr=False,
            warmup_epochs=3.0,
            close_mosaic=10,
            patience=50,
        )
        aug_cfg = dict(
            mosaic=1.0,
            copy_paste=0.3,
            mixup=0.15,
            degrees=10.0,
            scale=0.5,
            fliplr=0.5,
            flipud=0.0,
        )
        return {**opt_cfg, **aug_cfg}

    raise ValueError(f"未知训练模式: {mode}")


def train_single_model(
    model_name: str,
    mode: str = "finetune",
    epochs: int | None = None,
    batch_size: int = 16,
    imgsz: int = 640,
    device: str | int = 0,
    data_yaml: str = DEFAULT_DATASET_YAML,
    extra_val_yamls: list[str] | None = None,
    tag: str = "uav",
    scratch_optim: str = "sgd",
    workers: int = 8,
):
    ensure_dirs()
    clean_name = model_name.replace(".pt", "").replace(".yaml", "")
    resolved_epochs = epochs if epochs is not None else (300 if mode == "scratch" else 80)

    if mode == "finetune":
        weight_path = os.path.join(WEIGHTS_PRETRAINED_DIR, f"{clean_name}.pt")
        if not os.path.exists(weight_path):
            weight_path = f"{clean_name}.pt"
        run_name = f"{clean_name}_{tag}_ft"
        project_dir = os.path.join(RUNS_ROOT, "finetuned" if tag == "uav" else f"finetuned_{tag}")
        out_weights_dir = WEIGHTS_FT_DIR
        out_pt_name = f"{run_name}.pt"
        out_onnx_name = f"{run_name}.onnx"
        init_src = weight_path
        stage_label = "领域微调 Fine-tune"
    elif mode == "scratch":
        run_name = f"{clean_name}_{tag}_scratch"
        project_dir = os.path.join(RUNS_ROOT, "scratch" if tag == "uav" else f"scratch_{tag}")
        out_weights_dir = WEIGHTS_SCRATCH_DIR
        out_pt_name = f"{run_name}.pt"
        out_onnx_name = f"{run_name}.onnx"
        init_src = f"{clean_name}.yaml"
        stage_label = f"从零重新训练 Train from Scratch ({scratch_optim.upper()})"
    else:
        raise ValueError(f"未知训练模式: {mode}")

    print("=" * 78)
    print(f"[*] 【阶段：{stage_label}】模型={clean_name} | 初始化={init_src}")
    print(f"[*] 数据集={data_yaml} | 轮数={resolved_epochs} | 批大小={batch_size} | 设备=GPU {device}")
    print("=" * 78)

    save_dir = os.path.join(project_dir, run_name)
    dest_pt = os.path.join(out_weights_dir, out_pt_name)
    dest_onnx = os.path.join(MODELS_DEPLOY_DIR, out_onnx_name)
    last_pt = os.path.join(save_dir, "weights", "last.pt")

    if os.path.exists(dest_pt) and os.path.exists(dest_onnx):
        print(f"[✓] 模型 {run_name} 已完成训练并导出 ONNX，跳过本模型。")
        return None, {}

    if os.path.exists(last_pt):
        print(f"[*] 检测到中断断点权重: {last_pt}，正在执行断点续训 (resume=True)...")
        model = YOLO(last_pt)
        results = model.train(resume=True)
    else:
        model = YOLO(init_src)
        train_cfg = build_train_config(mode=mode, scratch_optim=scratch_optim)
        results = model.train(
            data=data_yaml,
            epochs=resolved_epochs,
            batch=batch_size,
            imgsz=imgsz,
            device=device,
            workers=workers,
            project=project_dir,
            name=run_name,
            exist_ok=True,
            **train_cfg,
            save=True,
            save_period=10,
            plots=True,
            verbose=True,
        )

    best_pt = os.path.join(save_dir, "weights", "best.pt")
    eval_summary = {}
    if os.path.exists(best_pt):
        best_model = YOLO(best_pt)
        val_targets = [data_yaml] + [y for y in (extra_val_yamls or []) if os.path.abspath(y) != os.path.abspath(data_yaml)]
        for vy in val_targets:
            print(f"\n[+] 正在使用最优权重 {best_pt} 在 {vy} 上执行全量评测...")
            val_res = best_model.val(data=vy, imgsz=imgsz, device=device, workers=workers)
            eval_summary[os.path.basename(vy)] = extract_val_metrics(val_res)

        shutil.copy2(best_pt, dest_pt)
        print(f"[+] 最优权重已归档至: {dest_pt}")

        print(f"[*] 正在导出纯静态 ONNX 模型 (imgsz={imgsz}, opset=17)...")
        onnx_temp = best_model.export(
            format="onnx",
            imgsz=imgsz,
            dynamic=False,
            opset=17,
            simplify=True,
        )
        shutil.copy2(onnx_temp, dest_onnx)
        print(f"[+] 部署级静态 ONNX 已发布至: {dest_onnx}")

    return results, eval_summary


def main():
    parser = argparse.ArgumentParser(description="Anti-UAV YOLO 训练流水线 (支持微调与从零训练)")
    parser.add_argument(
        "--model",
        type=str,
        default="yolo11n",
        help="模型名称，支持单个、逗号分隔列表 (如 yolov8s,yolo11n,yolo11s) 或 'all'",
    )
    parser.add_argument(
        "--mode",
        type=str,
        default="finetune",
        choices=["finetune", "scratch", "both"],
        help="训练模式: finetune(微调), scratch(从零重训), both(先微调后从零)",
    )
    parser.add_argument("--epochs", type=int, default=None, help="训练轮数 (不传时 finetune=80, scratch=300)")
    parser.add_argument("--batch", type=int, default=16, help="批处理大小 (默认 16)")
    parser.add_argument("--imgsz", type=int, default=640, help="输入分辨率 (默认 640)")
    parser.add_argument("--workers", type=int, default=8, help="DataLoader 工作线程数 (默认 8)")
    parser.add_argument("--device", type=str, default="0", help="CUDA 设备 ID")
    parser.add_argument("--data", type=str, default=DEFAULT_DATASET_YAML, help="主训练与验证数据集 YAML")
    parser.add_argument(
        "--extra-val-data",
        type=str,
        nargs="*",
        default=[],
        help="附加交叉验证数据集 YAML 列表 (例如同时在原 DUT val 评测)",
    )
    parser.add_argument("--tag", type=str, default="uav", help="实验标签，用于区分产出目录与权重文件名")
    parser.add_argument(
        "--scratch-optim",
        type=str,
        default="sgd",
        choices=["sgd", "adamw"],
        help="从零训练优化器 (默认 sgd)",
    )
    args = parser.parse_args()

    if args.model == "all":
        models = MODEL_LIST
    else:
        models = [m.strip() for m in args.model.split(",") if m.strip()]

    modes = ["finetune", "scratch"] if args.mode == "both" else [args.mode]
    all_summaries = {}

    for mode in modes:
        print("\n######################################################################")
        print(f"  启动模式批次: {mode.upper()} | 目标模型: {models} | tag={args.tag}")
        print("######################################################################\n")
        for m in models:
            _, eval_summary = train_single_model(
                m,
                mode=mode,
                epochs=args.epochs,
                batch_size=args.batch,
                imgsz=args.imgsz,
                device=args.device,
                data_yaml=args.data,
                extra_val_yamls=args.extra_val_data,
                tag=args.tag,
                scratch_optim=args.scratch_optim,
                workers=args.workers,
            )
            if eval_summary:
                all_summaries[f"{m}_{mode}"] = eval_summary

    if all_summaries:
        summary_dir = os.path.join(RUNS_ROOT, f"summary_{args.tag}")
        os.makedirs(summary_dir, exist_ok=True)
        summary_path = os.path.join(summary_dir, "eval_summary.json")
        with open(summary_path, "w", encoding="utf-8") as f:
            json.dump(all_summaries, f, indent=2, ensure_ascii=False)
        print(f"\n[+] 全部模型汇总指标已写入: {summary_path}")


if __name__ == "__main__":
    main()
