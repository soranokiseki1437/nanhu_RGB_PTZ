# -*- coding: utf-8 -*-
"""
Anti-UAV 数据集（目标为无人机）自动下载与组织解压脚本
数据源：DUT Anti-UAV / CASIA Anti-UAV 官方镜像 (aegean-ai/dut-anti-uav)
目标：专供光电云台对空跟踪系统（目标为无人机本身）的检测 (DET) 与跟踪 (SOT)
"""
import os
import zipfile
from huggingface_hub import hf_hub_download

BASE_DIR = r"d:\PyProjects\RGB_PTZ_Integrated\datasets\Anti-UAV"
REPO_ID = "aegean-ai/dut-anti-uav"

def download_and_extract(filename, subfolder):
    target_dir = os.path.join(BASE_DIR, subfolder)
    os.makedirs(target_dir, exist_ok=True)
    print(f"[*] 开始下载: {filename} -> {target_dir}")
    f_path = hf_hub_download(
        repo_id=REPO_ID,
        filename=filename,
        repo_type="dataset",
        local_dir=BASE_DIR
    )
    print(f"[+] 下载完成: {f_path} ({os.path.getsize(f_path)/(1024*1024):.2f} MB)")
    print(f"[*] 开始解压: {f_path}")
    with zipfile.ZipFile(f_path, "r") as z:
        z.extractall(target_dir)
    print(f"[+] 解压完成至: {target_dir}")

def main():
    print("=" * 60)
    print("正在拉取针对无人机目标（Target: UAV）的专属 Anti-UAV 权威数据集...")
    print("=" * 60)

    # 1. 验证集已就绪或下载
    val_zip = os.path.join(BASE_DIR, "detection", "val.zip")
    if os.path.exists(val_zip):
        print(f"[*] 检测验证集已有压缩包: {val_zip}，正在解压...")
        with zipfile.ZipFile(val_zip, "r") as z:
            z.extractall(os.path.join(BASE_DIR, "detection"))
        print(f"[+] 验证集解压完毕")
    else:
        download_and_extract("detection/val.zip", "detection")

    # 2. 跟踪 Ground Truth 已就绪或下载
    gt_zip = os.path.join(BASE_DIR, "tracking", "Anti-UAV-Tracking-V0GT.zip")
    if os.path.exists(gt_zip):
        print(f"[*] 跟踪真值标注已有: {gt_zip}，正在解压...")
        with zipfile.ZipFile(gt_zip, "r") as z:
            z.extractall(os.path.join(BASE_DIR, "tracking"))
        print(f"[+] 跟踪真值标注解压完毕")

    # 3. 训练集 (744MB)
    train_zip = os.path.join(BASE_DIR, "detection", "train.zip")
    if not os.path.exists(train_zip):
        download_and_extract("detection/train.zip", "detection")
    else:
        print("[*] 训练集已存在，跳过下载")

    # 4. 跟踪序列 (3.63GB)
    track_zip = os.path.join(BASE_DIR, "tracking", "Anti-UAV-Tracking-V0.zip")
    if not os.path.exists(track_zip):
        download_and_extract("tracking/Anti-UAV-Tracking-V0.zip", "tracking")
    else:
        print("[*] 跟踪序列已存在，跳过下载")

    print("=" * 60)
    print("Anti-UAV 专属数据集全部就绪！")
    print("=" * 60)

if __name__ == "__main__":
    main()
