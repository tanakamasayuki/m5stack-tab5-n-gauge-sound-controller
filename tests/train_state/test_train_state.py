"""記録した入力電流で状態判定を確かめる。

- e1_raw2: OFF→25→50→75→100→方向切り替え→逆50→負荷なし50（ツマミを素早く動かした）
- e3_run1: ゆっくり加速して最大近くまで上げ、ゆっくり減速して止めた
- e3_run2: e3_run1 と同様。減速の途中で一度ほぼ一定の速さにしてから止めた（第2版で記録）
- e3_steady: 約 95mA の定速で周回。周回ごとに電流が ±5% ほど揺れる（記録は走行中から始まる）
"""

import csv
from pathlib import Path

OUTPUT = Path(__file__).parent / "output"


def load_trace(name):
    with open(OUTPUT / f"trace_{name}.csv", encoding="utf-8") as f:
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
    check_quick_knob(load_trace("e1_raw2"))
    check_slow_ramp(load_trace("e3_run1"))
    check_slow_ramp_with_plateau(load_trace("e3_run2"))
    check_steady(load_trace("e3_steady"))


def check_quick_knob(trace):
    # 時刻は、100ms 平均が 30mA をまたいだ実際の変化点（動き出し 27.68s、停止 55.01s、
    # 逆50 で動き出し 60.51s、負荷なし 67.03s）。判定はそこから 0.8s 以内に追いつけばよい
    # OFF の間は停止のまま
    assert states_between(trace, 0, 27600) == {"Stop"}
    # 動き出す → 発進 → 加速または走行中
    assert 27680 <= first_time(trace, "Start", 0) <= 28480
    assert min(first_time(trace, "Run", 27680), first_time(trace, "Accel", 27680)) <= 31000
    # ツマミを 50→75→100 と上げた直後は加速になる
    assert "Accel" in states_between(trace, 36500, 40000)
    assert "Accel" in states_between(trace, 45500, 48000)
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


def check_slow_ramp(trace):
    # 電流は 13s ごろからゆっくり上がり始め、36s 付近で最大、40s から下げ始めて 55s ごろに止まる
    assert states_between(trace, 0, 13000) == {"Stop"}
    assert 13000 <= first_time(trace, "Start", 0) <= 15500
    # ゆっくり上げている間に、減速や停止にならず、加速と判定される
    assert states_between(trace, 15500, 36000) <= {"Start", "Run", "Accel"}
    assert "Accel" in states_between(trace, 15500, 36000)
    # ゆっくり下げている間に、減速と判定される
    assert "Brake" in states_between(trace, 40000, 48000)
    assert "Stop" not in states_between(trace, 15500, 54000)
    assert 54000 <= first_time(trace, "Stop", 15500) <= 56800
    assert states_between(trace, 57000, 70000) == {"Stop"}


def check_slow_ramp_with_plateau(trace):
    # 24s ごろから上げ始め、44〜51s が最大、52s から下げて 58〜60s はほぼ一定、そこから止める
    assert states_between(trace, 0, 23500) == {"Stop"}
    assert 23500 <= first_time(trace, "Start", 0) <= 25500
    assert states_between(trace, 25500, 52000) <= {"Start", "Run", "Accel"}
    assert "Accel" in states_between(trace, 25500, 45000)
    # 最大で一定になった区間（46〜51s）は走行中
    assert "Run" in states_between(trace, 46000, 52000)
    assert "Brake" in states_between(trace, 52000, 58000)
    # ほぼ一定になったところで走行中に戻る
    assert "Run" in states_between(trace, 58000, 63000)
    assert "Stop" not in states_between(trace, 25500, 66000)
    assert 66000 <= first_time(trace, "Stop", 25500) <= 69000


def check_steady(trace):
    # 走行中から記録が始まるので、立ち上がり（発進・加速）が落ち着いたあとを見る。
    # 周回ごとの負荷の揺れで、加速・減速・停止にならない
    assert states_between(trace, 6000, 46000) == {"Run"}
