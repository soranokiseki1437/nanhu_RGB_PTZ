# -*- coding: utf-8 -*-
"""
反无人机目标检测模型训练流水线 (面向硕士论文 2×2 消融实验与迁移学习对比)
支持两种训练模式：
  1. finetune: 基于官方 COCO 预训练权重 (.pt) 进行 80 轮领域微调
  2. scratch:  不加载任何预训练权重，纯依据网络结构 (.yaml) 随机初始化从零重新训练
环境底座: Python 3.11 + PyTorch 2.14.1 + CUDA 13.0 (RTX 5070 Blackwell sm_120)
数据集: DUT Anti-UAV Detection (7,800 张真实空域长焦图像，80.9% 弱小目标)
模型矩阵:
  - yolo11n (现代 C2PSA 空间注意力轻量级，2.6M)
  - yolo11s (现代 C2PSA 空间注意力高精级，9.4M)
  - yolov8n (经典轻量级基线，3.2M)
  - yolov8s (经典高精级基线，11.2M)
"""
import os
import sys
import shutil
import argparse
from ultralytics import YOLO

BASE_DIR = r"d:\PyProjects\RGB_PTZ_Integrated"
DATASET_YAML = os.path.join(BASE_DIR, r"datasets\anti_uav.yaml")
RUNS_ROOT = os.path.join(BASE_DIR, r"runs\detect_uav")
WEIGHTS_PRETRAINED_DIR = os.path.join(BASE_DIR, r"weights\pretrained")
WEIGHTS_FT_DIR = os.path.join(BASE_DIR, r"weights\finetuned")
WEIGHTS_SCRATCH_DIR = os.path.join(BASE_DIR, r"weights\scratch")
MODELS_DEPLOY_DIR = os.path.join(BASE_DIR, "models")

MODEL_LIST = ["yolo11n", "yolo11s", "yolov8n", "yolov8s"]

def ensure_dirs():
    for d in [WEIGHTS_PRETRAINED_DIR, WEIGHTS_FT_DIR, WEIGHTS_SCRATCH_DIR, MODELS_DEPLOY_DIR]:
        os.makedirs(d, exist_ok=True)

def train_single_model(model_name, mode="finetune", epochs=80, batch_size=16, imgsz=640, device=0):
    ensure_dirs()
    clean_name = model_name.replace(".pt", "").replace(".yaml", "")
    
    if mode == "finetune":
        # 1. 微调模式：载入预训练权重
        weight_path = os.path.join(WEIGHTS_PRETRAINED_DIR, f"{clean_name}.pt")
        if not os.path.exists(weight_path):
            weight_path = f"{clean_name}.pt"
        print("=" * 75)
        print(f"[*] 【阶段：领域微调 Fine-tune】模型={clean_name} | 预训练权重={weight_path}")
        print(f"[*] 训练轮数={epochs} | 批大小={batch_size} | 设备=GPU {device}")
        print("=" * 75)
        model = YOLO(weight_path)
        run_name = f"{clean_name}_uav_ft"
        project_dir = os.path.join(RUNS_ROOT, "finetuned")
        out_weights_dir = WEIGHTS_FT_DIR
        out_pt_name = f"{clean_name}_uav_ft.pt"
        out_onnx_name = f"{clean_name}_uav_ft.onnx"
        
    elif mode == "scratch":
        # 2. 从零训练模式：仅载入网络结构配置，随机初始化权重
        yaml_name = f"{clean_name}.yaml"
        print("=" * 75)
        print(f"[*] 【阶段：从零重新训练 Train from Scratch】模型={clean_name} | 结构配置={yaml_name}")
        print(f"[*] 训练轮数={epochs} | 批大小={batch_size} | 设备=GPU {device}")
        print("=" * 75)
        model = YOLO(yaml_name)
        run_name = f"{clean_name}_uav_scratch"
        project_dir = os.path.join(RUNS_ROOT, "scratch")
        out_weights_dir = WEIGHTS_SCRATCH_DIR
        out_pt_name = f"{clean_name}_uav_scratch.pt"
        out_onnx_name = f"{clean_name}_uav_scratch.onnx"
    else:
        raise ValueError(f"未知训练模式: {mode}")

    save_dir = os.path.join(project_dir, run_name)
    dest_pt = os.path.join(out_weights_dir, out_pt_name)
    dest_onnx = os.path.join(MODELS_DEPLOY_DIR, out_onnx_name)
    last_pt = os.path.join(save_dir, "weights", "last.pt")

    # 1. 检查是否已经完全训练且导出完成
    if os.path.exists(dest_pt) and os.path.exists(dest_onnx):
        print(f"[✓] 模型 {clean_name} ({mode}) 已经完成训练并成功导出 ONNX，跳过本模型。")
        return None, None

    # 2. 检查是否存在中断的断点权重 last.pt，实现智能断点续训
    if os.path.exists(last_pt):
        print(f"[*] 检测到中断断点权重: {last_pt}，正在执行自动断点续训 (resume=True)...")
        model = YOLO(last_pt)
        results = model.train(resume=True)
    else:
        # 针对微调与从零训练采用官方/社区推荐的最优超参体系
        if mode == "scratch":
            # 官方/学术界从零训练推荐配置 (AdamW + 余弦退火 + 充分预热 + 关闭早停跑满曲线)
            opt_cfg = dict(
                optimizer="AdamW",
                lr0=0.001,
                lrf=0.01,
                weight_decay=0.05,
                cos_lr=True,
                warmup_epochs=5.0,
                close_mosaic=15,
                patience=0,  # 完整记录 300 轮训练收敛曲线供消融对比
            )
        else:
            # 官方预训练微调推荐配置 (SGD + 线性衰减)
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

        results = model.train(
            data=DATASET_YAML,
            epochs=epochs,
            batch=batch_size,
            imgsz=imgsz,
            device=device,
            project=project_dir,
            name=run_name,
            exist_ok=True,
            **opt_cfg,
            # 针对空域弱小无人机的小目标专属增强策略
            mosaic=1.0,         # 增加微小无人机密度与背景组合多样性
            copy_paste=0.3,     # 强化无人机小目标在各类复杂云层背景中的鲁棒性
            mixup=0.15,
            degrees=10.0,
            scale=0.5,
            fliplr=0.5,
            flipud=0.0,         # 贴合真实飞行物理姿态
            # 工程化配置
            save=True,
            save_period=10,
            plots=True,
            verbose=True
        )
    
    # 验证集全量评测打点
    print(f"\n[+] {clean_name} ({mode}) 训练完成，正在进行验证集全量评测打点...")
    val_metrics = model.val(data=DATASET_YAML, imgsz=imgsz, device=device)
    
    # 归档最优权重并导出纯静态 ONNX
    best_pt = os.path.join(save_dir, "weights", "best.pt")
    if os.path.exists(best_pt):
        # 1. 归档到 weights 目录
        dest_pt = os.path.join(out_weights_dir, out_pt_name)
        shutil.copy2(best_pt, dest_pt)
        print(f"[+] 最优权重已成功归档至: {dest_pt}")
        
        # 2. 导出纯静态 ONNX
        print(f"[*] 正在将最优权重 {best_pt} 导出为纯静态 ONNX 模型...")
        best_model = YOLO(best_pt)
        onnx_temp = best_model.export(
            format="onnx",
            imgsz=imgsz,
            dynamic=False,
            opset=17,
            simplify=True
        )
        # 3. 部署到 models/ 目录
        dest_onnx = os.path.join(MODELS_DEPLOY_DIR, out_onnx_name)
        shutil.copy2(onnx_temp, dest_onnx)
        print(f"[+] 部署级静态 ONNX 已发布至: {dest_onnx}")
        
    return results, val_metrics

def main():
    parser = argparse.ArgumentParser(description="DUT Anti-UAV YOLO 训练流水线 (支持微调与从零训练)")
    parser.add_argument("--model", type=str, default="yolo11n", help="指定模型名称 (yolo11n/yolo11s/yolov8n/yolov8s) 或 'all'")
    parser.add_argument("--mode", type=str, default="finetune", choices=["finetune", "scratch", "both"], help="训练模式: finetune(微调), scratch(从零重训), both(先微调后从零)")
    parser.add_argument("--epochs", type=int, default=80, help="训练轮数 (默认 80)")
    parser.add_argument("--batch", type=int, default=16, help="批处理大小 (默认 16)")
    parser.add_argument("--device", type=str, default="0", help="CUDA 设备 ID")
    args = parser.parse_args()
    
    models = MODEL_LIST if args.model == "all" else [args.model]
    modes = ["finetune", "scratch"] if args.mode == "both" else [args.mode]
    
    for mode in modes:
        print(f"\n######################################################################")
        print(f"  启动模式批次: {mode.upper()} | 目标模型: {models}")
        print(f"######################################################################\n")
        for m in models:
            train_single_model(m, mode=mode, epochs=args.epochs, batch_size=args.batch, device=args.device)

if __name__ == "__main__":
    main()
