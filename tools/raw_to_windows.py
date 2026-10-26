"""RAW モードの記録（t_us,mA,marker）を、10ms ごとの平均値（t_ms,mA）に変換する。

実機では 10ms ごとの平均値を TrainSense に渡すので、PC 上のテストも同じ形の入力を使う。

python3 tools/raw_to_windows.py logs/e1_raw2.csv tests/train_state/input/e1_raw2_10ms.csv
"""

import csv
import sys

WINDOW_US = 10_000


def main() -> int:
    src, dst = sys.argv[1], sys.argv[2]
    rows = [(int(r[0]), float(r[1])) for r in csv.reader(open(src, encoding="utf-8")) if r and r[0].isdigit()]
    t0 = rows[0][0]
    out = []
    start, acc = 0, []
    for t, v in rows:
        rel = t - t0
        while rel >= start + WINDOW_US:
            if acc:
                out.append((start // 1000, sum(acc) / len(acc)))
            start += WINDOW_US
            acc = []
        acc.append(v)
    with open(dst, "w", encoding="utf-8", newline="") as f:
        f.write("t_ms,mA\n")
        for t, v in out:
            f.write(f"{t},{v:.1f}\n")
    print(f"{len(out)} windows -> {dst}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
