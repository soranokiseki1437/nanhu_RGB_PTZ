地空背景弱小飞行器目标检测与跟踪基准数据集 (IRSTD_22) 说明
========================================================================

1. 真实学术出处：
   本目录数据来自知名公开学术基准：
   《A Dataset for Infrared Image Dim-Small Aircraft Target Detection and Tracking Under Ground / Air Background》
   （作者：Hui et al.，发布于 Science Data Bank / ScienceDB 权威科学数据平台）。

2. 物理存储位置（归拢至 D:/PyProjects/数据集 统管）：
   D:/PyProjects/数据集/IRSTD_22/
   - 包含 01 ~ 22 共 22 个连续飞行序列、30 条航迹、16,000+ 帧图像与官方 Ground Truth 标注 (*_gt.txt)。
   - 目标大多在 6x6 ~ 24x24 像素的微小尺度。

3. 目录联接说明：
   本目录 (testdata/sequences) 为系统级 Junction 软链接，直接指向 D:/PyProjects/数据集/IRSTD_22，
   确保现有测试工具 (test_detector.cpp / test_amft.cpp 等) 零修改无缝兼容运行。