"""全シーンを PNG に書き出す（画面デザインの確認用）。"""

import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = Path(__file__).parent / "output"


# ui/SoundBoxScreen.lgfxsb.json から SoundBoxScreen.h を作り直してからビルドする
subprocess.run(["node", str(ROOT / "tools" / "gen_screen.mjs")], check=True)


def test_screens(dut):
    dut.expect("TEST start screens", timeout=15)
    dut.expect("PANEL 1280x720", timeout=5)
    for scene in ("Boot", "Drive", "TrainSelect", "Settings", "Message", "DrivePressed"):
        dut.expect(f"SCENE {scene} saved=1", timeout=10)
    dut.expect("TEST done", timeout=5)
    for scene in ("Boot", "Drive", "TrainSelect", "Settings", "Message", "DrivePressed"):
        png = OUTPUT / f"{scene}.png"
        assert png.exists() and png.stat().st_size > 100, f"missing {png}"
