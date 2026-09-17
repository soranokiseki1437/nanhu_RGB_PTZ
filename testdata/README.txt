对拍数据放置说明（离线验证用）
=====================================

目录结构（把图像序列放进来即可）：

  testdata/sequence01/
    imgs/               ← 帧图，按文件名排序即可（支持 0000.bmp / 0001.png / frame_001.jpg 等）
    initial_box.txt     ← 首帧目标框，一行四个整数：x y w h（左上角原点，单位像素）

说明：
- 帧率/分辨率不限，几十帧就够；要求连续、目标始终在画面内（中途可短暂遮挡，正好测遮挡逻辑）。
- initial_box.txt 请与 imgs/ 里第一帧对应；若暂时不标，我可以用一个默认框（画面中心 64x64）先跑通流程。
- 该目录下的 imgs/ 已被 .gitignore 忽略（图片不入库，box 文件会入库）。

生成 CSV 对拍结果的两个程序（我来写）：
  tools/offline_runner --ref  ... → reference 原版 DSST 的输出
  tools/offline_runner --port ... → 移植版 DsstCore 的输出
两者 CSV 逐帧比较中心点误差，< 2px 视为移植正确。