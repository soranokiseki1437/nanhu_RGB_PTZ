import json

with open("results/lighttrack/torch/summary.json") as f:
    t = json.load(f)
with open("results/lighttrack/onnx/summary.json") as f:
    o = json.load(f)

print(f"{'Seq':<10} | {'Torch AUC':<10} | {'ONNX AUC':<10} | {'Diff AUC':<10} | {'Torch DP':<10} | {'ONNX DP':<10} | {'Diff DP':<10}")
print("-" * 80)
for k in t["per_sequence"]:
    ta = t["per_sequence"][k]["auc"] * 100
    oa = o["per_sequence"][k]["auc"] * 100
    da = oa - ta
    tdp = t["per_sequence"][k]["dp20"] * 100
    odp = o["per_sequence"][k]["dp20"] * 100
    ddp = odp - tdp
    print(f"{k:<10} | {ta:8.2f}%  | {oa:8.2f}%  | {da:+8.2f}%  | {tdp:8.2f}%  | {odp:8.2f}%  | {ddp:+8.2f}%")

print("-" * 80)
print(f"{'OVERALL':<10} | {t['overall']['auc']*100:8.2f}%  | {o['overall']['auc']*100:8.2f}%  | {(o['overall']['auc']-t['overall']['auc'])*100:+8.2f}%  | {t['overall']['dp20']*100:8.2f}%  | {o['overall']['dp20']*100:8.2f}%  | {(o['overall']['dp20']-t['overall']['dp20'])*100:+8.2f}%")

