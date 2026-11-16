import pandas as pd

df_t = pd.read_csv("results/lighttrack/torch/per_frame.csv")
df_o = pd.read_csv("results/lighttrack/onnx/per_frame.csv")

df_t16 = df_t[df_t["seq"] == "video16"].reset_index(drop=True)
df_o16 = df_o[df_o["seq"] == "video16"].reset_index(drop=True)

for i in range(438, 445):
    print(f"--- Frame {i} ---")
    print(f"  Torch: center=({df_t16.loc[i, 'ax']:.4f}, {df_t16.loc[i, 'ay']:.4f}), IoU={df_t16.loc[i, 'iou']:.4f}, cls_max={df_t16.loc[i, 'cls_max']:.4f}")
    print(f"  ONNX : center=({df_o16.loc[i, 'ax']:.4f}, {df_o16.loc[i, 'ay']:.4f}), IoU={df_o16.loc[i, 'iou']:.4f}, cls_max={df_o16.loc[i, 'cls_max']:.4f}")
    print(f"  Diff pos: dx={df_o16.loc[i, 'ax']-df_t16.loc[i, 'ax']:.4f}, dy={df_o16.loc[i, 'ay']-df_t16.loc[i, 'ay']:.4f}")
