"""音源パックのマニフェストと WAV ヘッダの読み込みを確かめる（仕様: docs/sound-pack.md）。"""

import re
import struct
from pathlib import Path

INPUT = Path(__file__).parent / "input"


def write_wav(path, *, fmt=1, channels=1, rate=24000, bits=16, extra_chunk=False, data_bytes=4800):
    """WAV の見本を作る。extra_chunk なら fmt と data の間に LIST チャンクを挟む"""
    block = channels * bits // 8
    fmt_chunk = b"fmt " + struct.pack("<IHHIIHH", 16, fmt, channels, rate, rate * block, block, bits)
    extra = b"LIST" + struct.pack("<I", 5) + b"INFOx\0" if extra_chunk else b""  # 奇数長 + パディング
    data = b"data" + struct.pack("<I", data_bytes) + bytes(data_bytes)
    body = b"WAVE" + fmt_chunk + extra + data
    path.write_bytes(b"RIFF" + struct.pack("<I", len(body)) + body)


# テストの見本の WAV（ビルドより前に作る）
write_wav(INPUT / "wav_mono24k.wav")
write_wav(INPUT / "wav_stereo44k.wav", channels=2, rate=44100)
write_wav(INPUT / "wav_list_chunk.wav", extra_chunk=True)
write_wav(INPUT / "wav_8bit.wav", bits=8)
write_wav(INPUT / "wav_float.wav", fmt=3, bits=32)
write_wav(INPUT / "wav_rate96k.wav", rate=96000)
(INPUT / "wav_not_riff.wav").write_bytes(b"OggS" + bytes(40))

EXPECTED_ERRORS = {
    "bad_format.json": "format must be 1",
    "bad_noname.json": "name is missing",
    "bad_preset.json": "running.preset",
    "bad_nobuttons.json": "buttons must have 1 to 6",
    "bad_7buttons.json": "buttons must have 1 to 6",
    "bad_path.json": "button file is invalid",
    "bad_both.json": "exactly one of file or files",
    "bad_seqmode.json": "sequence (files) must be oneshot",
    "bad_noloop.json": "running.loop",
    "bad_credit.json": "credit needs",
    "bad_syntax.json": "IncompleteInput",
    "wav_8bit.wav": "only 16-bit",
    "wav_float.wav": "only linear PCM",
    "wav_rate96k.wav": "sample rate",
    "wav_not_riff.wav": "not a RIFF/WAVE",
}


def test_sound_pack(dut):
    dut.expect("TEST start sound_pack", timeout=15)
    results = {}
    while True:
        m = dut.expect(re.compile(rb"(?:(MANIFEST|WAVFILE) (\S+) ([^\r\n]*))|(TEST done)"), timeout=30)
        if m.group(4):
            break
        results[m.group(2).decode()] = m.group(3).decode()

    good = results["good.json"]
    assert good.startswith("ok name=E231系 通勤電車 buttons=6 running=1 credits=1"), good
    # [ラベル:ファイル数:モード]（モード 0=oneshot 1=hold 2=toggle）
    assert "[警笛:1:1] [ドア:1:0] [発車ベル:1:0] [車内放送:1:0] [駅到着:2:0] [雨:1:2]" in good, good
    assert "pitch=0.70-1.50" in good, good
    assert results["synth.json"].startswith("ok name=蒸気機関車 buttons=1 running=0"), results["synth.json"]

    assert results["wav_mono24k.wav"] == "ok ch=1 rate=24000 offset=44 bytes=4800"
    assert results["wav_stereo44k.wav"] == "ok ch=2 rate=44100 offset=44 bytes=4800"
    # LIST チャンク（5 バイト + パディング 1 バイト）を飛ばして data を見つける
    assert results["wav_list_chunk.wav"] == "ok ch=1 rate=24000 offset=58 bytes=4800"

    for name, msg in EXPECTED_ERRORS.items():
        assert results[name].startswith("error=") and msg in results[name], (name, results[name])
