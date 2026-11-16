import os
import sys
import argparse
import json
import math
import pandas as pd
import numpy as np

def compute_dynamic_tau(r_hist, tau0, alpha=0.5, min_tau=0.35, max_tau=0.75):
    if len(r_hist) >= 3:
        mean = sum(r_hist) / len(r_hist)
        variance = sum((x - mean) ** 2 for x in r_hist) / len(r_hist)
        sigma = math.sqrt(variance)
    else:
        sigma = 0.0
    tau = tau0 - alpha * sigma
    return max(min_tau, min(max_tau, tau))

def analyze_tau0(df, tau0):
    # 按序列进行动态阈值计算与状态统计
    # 失败片段：GT 有效且 IoU < 0.1 连续 >= 4 帧的片段
    
    total_good_frames = 0
    false_triggers = 0
    
    failure_episodes = [] # list of (seq, start_idx, end_idx)
    episode_triggered = []
    episode_delays = []
    
    # 逐序列处理
    grouped = df.groupby('seq', sort=False)
    for seq_name, group in grouped:
        frames = group.to_dict('records')
        n = len(frames)
        
        # 1. 模拟 AMFTEvaluator 滑动窗口
        r_hist = [1.0]
        trigger_flags = [False] * n
        trigger_events = [False] * n
        low_count = 0
        
        for i in range(1, n):
            r_t = float(frames[i]['r_t'])
            r_hist.append(r_t)
            if len(r_hist) > 10:
                r_hist.pop(0)
            
            tau_t = compute_dynamic_tau(r_hist, tau0)
            if r_t < tau_t:
                low_count += 1
            else:
                low_count = 0
            
            if low_count >= 4:
                trigger_flags[i] = True
                if low_count == 4:
                    trigger_events[i] = True
        
        # 2. 统计好帧段误触发
        for i in range(1, n):
            iou = float(frames[i]['iou'])
            if iou >= 0.5:
                total_good_frames += 1
                if trigger_events[i]:
                    false_triggers += 1
        
        # 3. 提取失败片段 (连续 IoU < 0.1 且 >= 4 帧)
        in_failure = False
        start_idx = -1
        for i in range(1, n):
            iou = float(frames[i]['iou'])
            # 若第一帧为初始帧，通常跳过第0帧
            if iou < 0.1:
                if not in_failure:
                    in_failure = True
                    start_idx = i
            else:
                if in_failure:
                    if (i - start_idx) >= 4:
                        failure_episodes.append((seq_name, start_idx, i - 1))
                    in_failure = False
                    start_idx = -1
        if in_failure and (n - start_idx) >= 4:
            failure_episodes.append((seq_name, start_idx, n - 1))
            
        # 4. 对本序列失败片段统计覆盖与延迟
        for ep in failure_episodes:
            if ep[0] != seq_name:
                continue
            s_idx, e_idx = ep[1], ep[2]
            # 检查片段内是否有触发
            trig_in_ep = [idx for idx in range(s_idx, e_idx + 1) if trigger_flags[idx]]
            if trig_in_ep:
                episode_triggered.append(True)
                episode_delays.append(trig_in_ep[0] - s_idx)
            else:
                episode_triggered.append(False)

    false_alarm_rate = (false_triggers / total_good_frames * 1000.0) if total_good_frames > 0 else 0.0
    total_episodes = len(failure_episodes)
    covered_episodes = sum(1 for t in episode_triggered if t)
    coverage = (covered_episodes / total_episodes * 100.0) if total_episodes > 0 else 0.0
    median_delay = float(np.median(episode_delays)) if episode_delays else -1.0
    
    return {
        "tau0": round(tau0, 2),
        "good_frames": total_good_frames,
        "false_triggers": false_triggers,
        "false_alarm_rate_per_1k": round(false_alarm_rate, 4),
        "total_failure_episodes": total_episodes,
        "covered_episodes": covered_episodes,
        "coverage_pct": round(coverage, 2),
        "median_delay_frames": median_delay
    }

def main():
    parser = argparse.ArgumentParser(description="Calibrate AMFT tau0 threshold for LightTrack")
    parser.add_argument("--amft-csv", default="results/lighttrack/cpp_dml/amft.csv", help="Path to amft.csv")
    parser.add_argument("--out", default="results/lighttrack/amft_calibration.json", help="Output JSON path")
    args = parser.parse_args()

    if not os.path.exists(args.amft_csv):
        print(f"[-] Error: AMFT CSV not found: {args.amft_csv}")
        sys.exit(1)

    df = pd.read_csv(args.amft_csv)
    print(f"[*] Loaded {len(df)} rows from {args.amft_csv}")

    candidate_tau0 = [0.40, 0.45, 0.50, 0.55, 0.60, 0.65, 0.70, 0.75]
    results = []

    print("\n" + "=" * 80)
    print(f"{'tau0':>6} | {'FAR (/1k frames)':>16} | {'Fail Ep':>8} | {'Covered':>8} | {'Coverage %':>10} | {'Median Delay':>12}")
    print("-" * 80)

    for tau0 in candidate_tau0:
        res = analyze_tau0(df, tau0)
        results.append(res)
        print(f"{res['tau0']:6.2f} | {res['false_alarm_rate_per_1k']:16.4f} | {res['total_failure_episodes']:8d} | {res['covered_episodes']:8d} | {res['coverage_pct']:9.2f}% | {res['median_delay_frames']:12.1f}")

    print("=" * 80 + "\n")

    # 规则判定：在误触发率 <= 1.0 次 / 1000 帧的 tau0 中选覆盖率最大者
    valid_cands = [r for r in results if r['false_alarm_rate_per_1k'] <= 1.0]
    best_tau0 = 0.65 # 默认 fallback
    if valid_cands:
        best_cand = max(valid_cands, key=lambda x: (x['coverage_pct'], -x['median_delay_frames']))
        best_tau0 = best_cand['tau0']
        print(f"[*] Optimal tau0 selected: {best_tau0} (Coverage: {best_cand['coverage_pct']}%, FAR: {best_cand['false_alarm_rate_per_1k']}/1k)")
    else:
        print("[!] Warning: No candidate satisfied FAR <= 1.0 / 1000 frames.")

    out_data = {
        "optimal_tau0": best_tau0,
        "grid_results": results
    }

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(out_data, f, indent=2)

    print(f"[*] Saved calibration report to {args.out}")

if __name__ == "__main__":
    main()
