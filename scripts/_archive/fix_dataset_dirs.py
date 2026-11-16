# -*- coding: utf-8 -*-
import os
import subprocess

BASE = r"d:\PyProjects\RGB_PTZ_Integrated\datasets\Anti-UAV\detection"

for s in ['train', 'val']:
    split_dir = os.path.join(BASE, s)
    images_dir = os.path.join(split_dir, "images")
    img_dir = os.path.join(split_dir, "img")
    
    # 1. 如果 images 是 junction，先删除链接
    if os.path.exists(images_dir):
        subprocess.run(f'cmd /c rmdir "{images_dir}"', shell=True, check=True)
        print(f"[+] 移除旧 junction: {images_dir}")
        
    # 2. 将真实的物理目录 img 重命名为 images
    if os.path.exists(img_dir):
        os.rename(img_dir, images_dir)
        print(f"[+] 物理重命名: {img_dir} ➔ {images_dir}")
        
    # 3. 创建向后兼容的 junction: img ➔ images
    subprocess.run(f'cmd /c mklink /J "{img_dir}" "{images_dir}"', shell=True, check=True)
    print(f"[+] 建立兼容 junction: {img_dir} ➔ {images_dir}")

print("[+] 目录架构优化完成！")
