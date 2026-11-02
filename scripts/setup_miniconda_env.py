# -*- coding: utf-8 -*-
"""
Miniconda 自动化安装与反无人机专用深度学习虚拟环境配置脚本
1. 下载 Miniconda Windows x86_64 最新静默安装包
2. 静默安装至 D:\miniconda3 (释放 C 盘空间)
3. 创建 Python 3.11 专用环境 anti_uav_yolo
4. 安装 CUDA 加速版 PyTorch (RTX 5070 适配)
5. 安装 Ultralytics、OpenCV、ONNX 等全套目标检测与跟踪依赖
6. 全流程自动化验收 GPU 识别与张量运算
"""
import os
import sys
import time
import urllib.request
import subprocess

INSTALLER_URL = "https://repo.anaconda.com/miniconda/Miniconda3-latest-Windows-x86_64.exe"
TEMP_INSTALLER = r"D:\miniconda_installer.exe"
TARGET_DIR = r"D:\miniconda3"
ENV_NAME = "anti_uav_yolo"

def download_installer():
    if os.path.exists(TEMP_INSTALLER):
        print(f"[*] 发现已有安装包: {TEMP_INSTALLER}，跳过下载")
        return
        
    print(f"[*] 正在下载 Miniconda 安装程序: {INSTALLER_URL}")
    t0 = time.time()
    
    def report_hook(count, block_size, total_size):
        if total_size > 0:
            percent = int(count * block_size * 100 / total_size)
            downloaded_mb = (count * block_size) / (1024 * 1024)
            total_mb = total_size / (1024 * 1024)
            if count % 200 == 0 or percent >= 100:
                print(f"    下载进度: {min(percent, 100)}% ({downloaded_mb:.1f}MB / {total_mb:.1f}MB)", end="\r")
                
    req = urllib.request.Request(INSTALLER_URL, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req) as resp, open(TEMP_INSTALLER, "wb") as f_out:
        total_size = int(resp.headers.get("Content-Length", 0))
        block_size = 1024 * 256
        count = 0
        while True:
            chunk = resp.read(block_size)
            if not chunk:
                break
            f_out.write(chunk)
            count += 1
            report_hook(count, block_size, total_size)
            
    print(f"\n[+] 下载完成: {TEMP_INSTALLER} ({time.time() - t0:.1f} 秒)")

def install_miniconda():
    conda_exe = os.path.join(TARGET_DIR, "Scripts", "conda.exe")
    if os.path.exists(conda_exe):
        print(f"[*] 检测到已存在 Miniconda: {TARGET_DIR}，跳过安装程序运行")
        return conda_exe
        
    print(f"[*] 开始静默安装 Miniconda 至目标路径: {TARGET_DIR}...")
    # NSIS 规范参数: /S 静默, /InstallationType=JustMe, /RegisterPython=0, /D= 必须放在最后且不加引号
    cmd = f'"{TEMP_INSTALLER}" /InstallationType=JustMe /RegisterPython=0 /S /D={TARGET_DIR}'
    res = subprocess.run(cmd, shell=True)
    if res.returncode != 0:
        raise RuntimeError(f"安装失败，退出码: {res.returncode}")
        
    # 等待安装文件完全落地
    for _ in range(60):
        if os.path.exists(conda_exe):
            break
        time.sleep(2)
        
    if not os.path.exists(conda_exe):
        raise RuntimeError(f"安装超时或未在预期路径找到 conda.exe: {conda_exe}")
        
    print(f"[+] Miniconda 安装成功: {conda_exe}")
    # 清理安装包
    try:
        os.remove(TEMP_INSTALLER)
        print(f"[+] 已清理临时安装包: {TEMP_INSTALLER}")
    except Exception:
        pass
    return conda_exe

def setup_environment(conda_exe):
    env_python = os.path.join(TARGET_DIR, "envs", ENV_NAME, "python.exe")
    if not os.path.exists(env_python):
        print(f"[*] 正在创建 Python 3.11 独立虚拟环境: {ENV_NAME}...")
        cmd = f'"{conda_exe}" create -n {ENV_NAME} python=3.11 -y'
        subprocess.run(cmd, shell=True, check=True)
        print(f"[+] 虚拟环境创建成功: {env_python}")
    else:
        print(f"[*] 虚拟环境已存在: {env_python}")
        
    return env_python

def install_packages(env_python):
    print("=" * 60)
    print("正在安装深度学习基础组件 (CUDA 12.4 PyTorch)...")
    print("=" * 60)
    
    # 升级 pip
    subprocess.run(f'"{env_python}" -m pip install --upgrade pip', shell=True)
    
    # 安装 PyTorch CUDA 版
    print("[*] 正在安装 torch 与 torchvision (CUDA 12.4)...")
    torch_cmd = f'"{env_python}" -m pip install torch torchvision --index-url https://download.pytorch.org/whl/cu124'
    subprocess.run(torch_cmd, shell=True, check=True)
    
    # 安装常用算法库与工具链
    print("[*] 正在安装 ultralytics, opencv, onnx 等上层库...")
    pkgs = [
        "ultralytics",
        "opencv-python",
        "pyyaml",
        "onnx",
        "onnxslim",
        "onnxruntime-directml",
        "tqdm",
        "matplotlib",
        "polars"
    ]
    pkg_cmd = f'"{env_python}" -m pip install {" ".join(pkgs)}'
    subprocess.run(pkg_cmd, shell=True, check=True)
    
    print("[+] 全部依赖组件安装完毕！")

def verify_gpu(env_python):
    print("=" * 60)
    print("正在验收 GPU 硬件加速与环境健全度...")
    print("=" * 60)
    code = """
import torch
print('PyTorch Version:', torch.__version__)
print('CUDA Available:', torch.cuda.is_available())
if torch.cuda.is_available():
    print('Device Count:', torch.cuda.device_count())
    print('Device Name:', torch.cuda.get_device_name(0))
    print('Current Device:', torch.cuda.current_device())
    x = torch.randn(1000, 1000, device='cuda')
    y = torch.matmul(x, x)
    print('GPU Matmul Test PASSED! Tensor sum:', float(y.sum()))
else:
    print('[!] CUDA 不可用')

import ultralytics
print('Ultralytics Version:', ultralytics.__version__)
"""
    subprocess.run([env_python, "-c", code], check=True)

if __name__ == "__main__":
    download_installer()
    conda_exe = install_miniconda()
    env_python = setup_environment(conda_exe)
    install_packages(env_python)
    verify_gpu(env_python)
    print("\n" + "=" * 60)
    print(f"环境配置全部圆满完成！\n环境 Python 绝对路径: {env_python}")
    print("=" * 60)
