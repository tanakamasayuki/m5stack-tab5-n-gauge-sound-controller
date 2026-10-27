"""合成音を WAV に書き出し、無音や音割れがないかを確かめる。

書き出した WAV（tests/sound_preview/output/）は、PC で再生して音の雰囲気を確かめるためのもの。
"""

import re
import wave
from pathlib import Path

OUTPUT = Path(__file__).parent / "output"
PRESETS = ("electric", "steam", "diesel")
INPUTS = ("e3_run1", "e1_raw2")


def test_sound_preview(dut):
    dut.expect("TEST start sound_preview", timeout=15)
    stats = {}
    while True:
        m = dut.expect(re.compile(rb"(WAV (\w+) (\S+) seconds=([\d.]+) peak=(\d+) rms=(\d+) clipped=(\d+))|(TEST done)"),
                       timeout=300)
        if m.group(8):
            break
        stats[m.group(3).decode()] = {
            "seconds": float(m.group(4)),
            "peak": int(m.group(5)),
            "rms": int(m.group(6)),
            "clipped": int(m.group(7)),
        }

    for preset in PRESETS:
        for inp in INPUTS:
            name = f"{preset}_{inp}"
            s = stats[name]
            # 記録の長さ（約 70 秒）ぶんの走行音ができている
            assert s["seconds"] > 60, (name, s)
            # 走っている区間があるので、無音ではない
            assert s["rms"] > 300, (name, s)
            # ソフトクリップしているので、音割れはほぼない
            assert s["clipped"] < s["seconds"] * 24, (name, s)
            with wave.open(str(OUTPUT / f"{name}.wav")) as w:
                assert w.getframerate() == 24000 and w.getnchannels() == 1 and w.getsampwidth() == 2
        for i in range(1, 7):
            s = stats[f"{preset}_fx{i}"]
            assert 0.3 <= s["seconds"] <= 5, (preset, i, s)
            assert s["rms"] > 300, (preset, i, s)
