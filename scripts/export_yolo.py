import os
import sys
from ultralytics import YOLO

# 严格遵循方案 03 标准化导出契约：
# 1. 输入格式：严格静态尺寸 1 x 3 x 640 x 640 (RGB 彩色输入)
# 2. 算子规范：OPSET 12/17，通用静态图，不内置 NMS 算子
# 3. 输出目录：./models/ 文件夹

MODELS_TO_EXPORT = ["yolov8n", "yolov8s", "yolo11n", "yolo11s"]

def export_model(model_name: str, output_dir: str):
    os.makedirs(output_dir, exist_ok=True)
    target_onnx = os.path.join(output_dir, f"{model_name}.onnx")
    
    if os.path.exists(target_onnx):
        print(f"[已存在] 跳过导出已有的模型: {target_onnx}")
        return target_onnx

    print(f"\n>> 正在下载/加载预训练权重: {model_name}.pt ...")
    model = YOLO(f"{model_name}.pt")
    
    print(f">> 开始导出为标准静态 ONNX (RGB 640x640, opset=12): {target_onnx} ...")
    exported_path = model.export(
        format="onnx",
        imgsz=640,
        dynamic=False,
        opset=12,
        simplify=True
    )
    
    # 若导出在当前目录，移动至目标目录
    if exported_path and os.path.exists(exported_path) and os.path.abspath(exported_path) != os.path.abspath(target_onnx):
        if os.path.exists(target_onnx):
            os.remove(target_onnx)
        os.rename(exported_path, target_onnx)
        
    print(f"[完成] 成功生成标准可见光模型: {target_onnx}")
    return target_onnx

if __name__ == "__main__":
    base_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    models_dir = os.path.join(base_dir, "models")
    
    targets = sys.argv[1:] if len(sys.argv) > 1 else MODELS_TO_EXPORT
    for m in targets:
        try:
            export_model(m, models_dir)
        except Exception as e:
            print(f"[失败] 导出 {m} 发生异常: {e}")
