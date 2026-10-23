import argparse
import shutil
import sys
from pathlib import Path

def extract_cfg(model_type: str, output_path: str = None):
    try:
        import ultralytics
    except ImportError:
        print("[错误] 未检测到 ultralytics 库，请先执行: pip install ultralytics")
        sys.exit(1)

    ultralytics_root = Path(ultralytics.__file__).parent
    model_lower = model_type.lower()

    if "11" in model_lower:
        src_yaml = ultralytics_root / "cfg" / "models" / "11" / "yolo11.yaml"
        default_out = "yolo11_custom.yaml"
    elif "8" in model_lower:
        src_yaml = ultralytics_root / "cfg" / "models" / "v8" / "yolov8.yaml"
        default_out = "yolov8_custom.yaml"
    else:
        print(f"[错误] 不支持的模型类型: {model_type}，支持: yolo11, yolov8")
        sys.exit(1)

    if not src_yaml.exists():
        print(f"[错误] 官方模板文件未找到: {src_yaml}")
        sys.exit(1)

    dest_file = Path(output_path if output_path else default_out)
    dest_file.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy(src_yaml, dest_file)

    print("=================================================================")
    print(f" [成功] 官方 YOLO 结构蓝图已提取至: {dest_file.resolve()}")
    print("=================================================================")
    print("【后续微创手术与极速训练工作流】:")
    print(f" 1. 编辑结构文件: 在 {dest_file.name} 中微调 Neck / Head (如增加 P2 头或注意力)")
    print(" 2. 状态字典继承: model = YOLO('{dest_file.name}').load('yolo11s.pt')")
    print(" 3. 冻结骨干微调: model.train(data='uav.yaml', epochs=50, freeze=10, device=0)")
    print(" 4. 标准静态导出: model.export(format='onnx', imgsz=640, opset=12, dynamic=False, simplify=True)")
    print(" 5. 上位机热测试: .\\build\\release\\test_detector.exe models/your_model.onnx")
    print("=================================================================")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="提取 Ultralytics 官方 YOLOv8 / YOLO11 架构 YAML 模板")
    parser.add_argument("--model", type=str, default="yolo11", help="模型系列: yolo11 或 yolov8 (默认: yolo11)")
    parser.add_argument("--out", type=str, default=None, help="目标输出文件路径 (默认: yolo11_custom.yaml)")
    args = parser.parse_args()
    extract_cfg(args.model, args.out)
