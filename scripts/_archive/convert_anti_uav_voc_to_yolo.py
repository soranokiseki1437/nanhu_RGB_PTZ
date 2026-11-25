# -*- coding: utf-8 -*-
"""
DUT Anti-UAV Detection VOC XML ➔ 标准 YOLO 标注格式转换器
1. 解析 train (5,200) 与 val (2,600) 的 XML 标注文件
2. 坐标归一化: [xmin, ymin, xmax, ymax] ➔ [x_center, y_center, width, height] (归一化到 [0, 1])
3. 边界值安全 clamp 防止轻微越界
4. 单类别标签统一映射: 'UAV' ➔ 0
5. 目录规范化: 建立 images 软链接/目录以适配 Ultralytics 数据流规范
6. 自动生成标准 datasets/anti_uav.yaml 配置文件
"""
import os
import glob
import xml.etree.ElementTree as ET

BASE_DIR = r"d:\PyProjects\RGB_PTZ_Integrated\datasets\Anti-UAV\detection"

def convert_split(split_name):
    split_dir = os.path.join(BASE_DIR, split_name)
    xml_dir = os.path.join(split_dir, "xml")
    img_dir = os.path.join(split_dir, "img")
    images_dir = os.path.join(split_dir, "images")
    labels_dir = os.path.join(split_dir, "labels")
    
    os.makedirs(labels_dir, exist_ok=True)
    
    # 建立 images 目录与 img 对齐（兼容 Ultralytics img2label_paths 规则）
    if not os.path.exists(images_dir):
        try:
            # 尝试创建目录符号链接或 Junction
            import subprocess
            subprocess.run(f'cmd /c mklink /J "{images_dir}" "{img_dir}"', shell=True, check=True)
            print(f"[+] 成功建立 Junction 链接: {images_dir} ➔ {img_dir}")
        except Exception as e:
            print(f"[!] Junction 链接失败 ({e})，将采用复制/引用方式")
            
    xml_files = sorted(glob.glob(os.path.join(xml_dir, "*.xml")))
    print(f"[*] 开始转换 {split_name} 分割集，共 {len(xml_files)} 份 XML 标注...")
    
    total_objects = 0
    empty_images = 0
    
    for xml_path in xml_files:
        base_name = os.path.splitext(os.path.basename(xml_path))[0]
        txt_path = os.path.join(labels_dir, f"{base_name}.txt")
        
        try:
            tree = ET.parse(xml_path)
            root = tree.getroot()
            
            size_elem = root.find("size")
            width = float(size_elem.find("width").text)
            height = float(size_elem.find("height").text)
            
            if width <= 0 or height <= 0:
                print(f"[!] 警告: 尺寸异常 {xml_path}")
                continue
                
            yolo_lines = []
            objects = root.findall("object")
            
            for obj in objects:
                name = obj.find("name").text.strip()
                if name.upper() != "UAV":
                    print(f"[!] 发现非 UAV 类别: {name} in {xml_path}")
                    continue
                    
                bndbox = obj.find("bndbox")
                xmin = float(bndbox.find("xmin").text)
                ymin = float(bndbox.find("ymin").text)
                xmax = float(bndbox.find("xmax").text)
                ymax = float(bndbox.find("ymax").text)
                
                # 安全边界截断
                xmin = max(0.0, min(xmin, width))
                xmax = max(0.0, min(xmax, width))
                ymin = max(0.0, min(ymin, height))
                ymax = max(0.0, min(ymax, height))
                
                box_w = xmax - xmin
                box_h = ymax - ymin
                if box_w <= 0 or box_h <= 0:
                    continue
                    
                x_center = (xmin + box_w / 2.0) / width
                y_center = (ymin + box_h / 2.0) / height
                norm_w = box_w / width
                norm_h = box_h / height
                
                # YOLO 格式: <class_id> <x_center> <y_center> <width> <height>
                yolo_lines.append(f"0 {x_center:.6f} {y_center:.6f} {norm_w:.6f} {norm_h:.6f}")
                total_objects += 1
                
            if len(yolo_lines) == 0:
                empty_images += 1
                
            with open(txt_path, "w", encoding="utf-8") as f_out:
                f_out.write("\n".join(yolo_lines) + ("\n" if yolo_lines else ""))
                
        except Exception as err:
            print(f"[x] 解析出错 {xml_path}: {err}")
            
    print(f"[+] {split_name} 转换完成: {len(xml_files)} 份标注生成，检测目标总计 {total_objects} 个，纯背景空图像 {empty_images} 张。")

def generate_yaml():
    yaml_path = r"d:\PyProjects\RGB_PTZ_Integrated\datasets\anti_uav.yaml"
    content = f"""# Anti-UAV 专属无人机目标检测数据集配置 (DUT Anti-UAV Detection)
# 任务映射: 地空视轴仰拍无人机小目标检测 (Target: UAV)

path: d:/PyProjects/RGB_PTZ_Integrated/datasets/Anti-UAV/detection
train: train/images
val: val/images
test: val/images

# 单类别无人机定义
nc: 1
names: ['UAV']
"""
    with open(yaml_path, "w", encoding="utf-8") as f:
        f.write(content)
    print(f"[+] 成功生成标准 Ultralytics 配置文件: {yaml_path}")

if __name__ == "__main__":
    print("=" * 60)
    print("开始 DUT Anti-UAV Detection VOC ➔ YOLO 格式标准化转换...")
    print("=" * 60)
    convert_split("train")
    convert_split("val")
    generate_yaml()
    print("=" * 60)
    print("转换全部就绪！")
    print("=" * 60)
