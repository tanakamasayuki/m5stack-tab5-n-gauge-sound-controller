"""E1 第2回の記録（OFF→25→50→75→100→方向切り替え→逆50→負荷なし50）で状態判定を確かめる。"""

import csv
from pathlib import Path

OUTPUT = Path(__file__).parent / "output"


def load_trace():
    with open(OUTPUT / "trace.csv", encoding="utf-8") as f:
        return [{**r, "t": int(r["t_ms"]), "level": float(r["level"])} for r in csv.DictReader(f)]


def states_between(trace, t0, t1):
    return {r["state"] for r in trace if t0 <= r["t"] < t1}


def first_time(trace, state, after):
    return next(r["t"] for r in trace if r["t"] >= after and r["state"] == state)


def mean_level(trace, t0, t1):
    v = [r["level"] for r in trace if t0 <= r["t"] < t1]
    return sum(v) / len(v)


def test_train_state(dut):
    dut.expect("TEST start train_state", timeout=15)
    dut.expect("TEST done", timeout=30)
    trace = load_trace()

    # 時刻は、100ms 平均が 30mA をまたいだ実際の変化点（動き出し 27.68s、停止 55.01s、
    # 逆50 で動き出し 60.51s、負荷なし 67.03s）。判定はそこから 0.8s 以内に追いつけばよい
    # OFF の間は停止のまま
    assert states_between(trace, 0, 27600) == {"Stop"}
    # 動き出す → 発進 → 走行中
    assert 27680 <= first_time(trace, "Start", 0) <= 28480
    assert first_time(trace, "Run", 27680) <= 31000
    # 100 からツマミを下げると減速になり、止めると停止
    assert "Brake" in states_between(trace, 51500, 55500)
    assert 55010 <= first_time(trace, "Stop", 51500) <= 55810
    # 方向切り替え後の逆50 で再び動き出す
    assert 60510 <= first_time(trace, "Start", 55500) <= 61310
    # 負荷なしは停止になり、そのまま
    assert 67030 <= first_time(trace, "Stop", 61000) <= 67830
    assert states_between(trace, 68000, 75000) == {"Stop"}

    # level はツマミの位置の順に大きくなる（25 < 50 < 75 < 100）
    l25, l50, l75, l100 = (mean_level(trace, *seg) for seg in
                           ((29000, 36000), (38000, 40500), (42500, 45500), (47500, 51500)))
    assert l25 < l50 < l75 < l100, (l25, l50, l75, l100)
