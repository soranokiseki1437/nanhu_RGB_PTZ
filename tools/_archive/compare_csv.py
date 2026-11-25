# 对拍 CSV 比较：逐帧比较两个跟踪器输出框的中心点误差
# 用法: python tools/compare_csv.py <runner_ref.csv> <runner_port.csv>
import sys


def load(path):
    rows = []
    with open(path, "r", encoding="utf-8") as f:
        next(f)  # 跳过表头
        for line in f:
            parts = line.strip().split(",")
            if len(parts) != 5:
                continue
            frame, x, y, w, h = (int(float(p)) for p in parts)
            rows.append((frame, x, y, w, h))
    return rows


def main():
    if len(sys.argv) != 3:
        print("用法: python compare_csv.py <ref.csv> <port.csv>")
        return 1

    ref = load(sys.argv[1])
    port = load(sys.argv[2])
    if not ref or not port:
        print("CSV 为空")
        return 1

    port_by_frame = {r[0]: r for r in port}
    diffs = []
    for frame, x, y, w, h in ref:
        other = port_by_frame.get(frame)
        if other is None:
            continue
        cx1, cy1 = x + w / 2.0, y + h / 2.0
        cx2, cy2 = other[1] + other[3] / 2.0, other[2] + other[4] / 2.0
        diffs.append((frame, ((cx1 - cx2) ** 2 + (cy1 - cy2) ** 2) ** 0.5,
                      max(abs(w - other[3]), abs(h - other[4]))))

    if not diffs:
        print("两个 CSV 没有可比较的帧")
        return 1

    errs = [d[1] for d in diffs]
    size_errs = [d[2] for d in diffs]
    worst = max(diffs, key=lambda d: d[1])
    print(f"比较帧数: {len(diffs)}")
    print(f"中心误差: 平均 {sum(errs)/len(errs):.3f}px  最大 {worst[1]:.3f}px (frame {worst[0]})")
    print(f"尺寸误差: 最大 {max(size_errs)}px")
    print("结论:", "通过（<2px）" if worst[1] < 2.0 else "未通过（需排查差异）")
    return 0 if worst[1] < 2.0 else 2


if __name__ == "__main__":
    sys.exit(main())