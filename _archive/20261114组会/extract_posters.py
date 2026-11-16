import cv2
import os

videos = {
    '跟上自动变焦跟丢.mp4': ('video_zoom_poster.png', 30),
    '框选目标尺度变化.mp4': ('video_scale_poster.png', 40),
    '背景复杂跟丢.mp4': ('video_complex_poster.png', 50),
    'yolov11通用权重跟椅子.mp4': ('video_chair_poster.png', 20)
}

base_dir = r'D:\PyProjects\RGB_PTZ_Integrated\20261114组会'

for vname, (pname, frame_idx) in videos.items():
    vpath = os.path.join(base_dir, vname)
    cap = cv2.VideoCapture(vpath)
    cap.set(cv2.CAP_PROP_POS_FRAMES, frame_idx)
    ret, frame = cap.read()
    if not ret:
        cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
        ret, frame = cap.read()
    if ret:
        out_path = os.path.join(base_dir, pname)
        cv2.imwrite(out_path, frame)
        print(f'Extracted poster for {vname} -> {pname}')
    cap.release()

