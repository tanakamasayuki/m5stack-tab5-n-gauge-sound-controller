"""計測結果を表示するだけで、合否は判定しない。"""


def test_clip_bench(dut):
    dut.expect("TEST start ClipBench", timeout=15)
    dut.expect("TEST done", timeout=300)
