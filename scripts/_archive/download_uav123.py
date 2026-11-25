import os
import sys
import urllib.request
import tarfile

UAV123_URL = "https://huggingface.co/datasets/xche32/UAV123/resolve/main/UAV123.tar.gz"

def download_with_resume(url: str, target_file: str):
    headers = {'User-Agent': 'Mozilla/5.0'}
    existing_bytes = 0
    if os.path.exists(target_file):
        existing_bytes = os.path.getsize(target_file)
        
    req = urllib.request.Request(url, headers=headers)
    try:
        with urllib.request.urlopen(req) as resp:
            total_bytes = int(resp.headers.get('content-length', 0))
    except Exception as e:
        print(f"[提示] 获取总长度失败: {e}，直接尝试恢复下载")
        total_bytes = 14033813268
        
    if existing_bytes >= total_bytes and total_bytes > 0:
        print(f"[已完成] 压缩包文件已完整 ({existing_bytes / 1024 / 1024:.1f} MB)，跳过下载。")
        return target_file

    if existing_bytes > 0:
        print(f">> 检测到已有已下载部分 ({existing_bytes / 1024 / 1024:.1f} MB)，启动 HTTP Range 断点续传...")
        headers['Range'] = f"bytes={existing_bytes}-"
        req = urllib.request.Request(url, headers=headers)
        mode = 'ab'
    else:
        print(f">> 开始全新下载 UAV123 (总计约 {total_bytes / 1024 / 1024:.1f} MB)...")
        mode = 'wb'

    with urllib.request.urlopen(req) as resp, open(target_file, mode) as f:
        downloaded = existing_bytes
        block_size = 1024 * 1024 # 1MB 块
        while True:
            chunk = resp.read(block_size)
            if not chunk:
                break
            f.write(chunk)
            downloaded += len(chunk)
            if total_bytes > 0:
                pct = int(downloaded * 100 / total_bytes)
                print(f"\r>> UAV123 断点续传进度: {pct}% ({downloaded / 1024 / 1024:.1f}MB / {total_bytes / 1024 / 1024:.1f}MB)", end="", flush=True)

    print(f"\n[完成] UAV123.tar.gz 全部下载完毕！")
    return target_file

def main():
    base_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    uav_dir = os.path.join(base_dir, "datasets", "UAV123")
    os.makedirs(uav_dir, exist_ok=True)
    
    tar_path = os.path.join(uav_dir, "UAV123.tar.gz")
    download_with_resume(UAV123_URL, tar_path)
    
    print(f">> 开始解压至: {uav_dir} ...")
    with tarfile.open(tar_path, "r:gz") as tar:
        tar.extractall(path=uav_dir)
    print(f"[完成] UAV123 数据集已成功就绪并解压完成！")

if __name__ == "__main__":
    main()
