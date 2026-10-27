"""音源パックを確認する（仕様: docs/sound-pack.md）。

python3 tools/check_pack.py /path/to/packs/e231 [/path/to/packs/d51 ...]

マニフェスト（pack.json）の項目、ファイルの有無、WAV の形式、クレジットの抜けを確認する。
問題があれば一覧で出し、終了コード 1 を返す。標準ライブラリだけで動く。
"""

import json
import struct
import sys
from pathlib import Path

PRESETS = ("electric", "steam", "diesel")
MODES = ("oneshot", "hold", "toggle")


def wav_problem(path: Path) -> str | None:
    """WAV が本体で使える形式か。使えなければ理由を返す"""
    data = path.read_bytes()[:4096]
    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        return "WAV（RIFF/WAVE）ではない"
    pos, fmt = 12, None
    while pos + 8 <= len(data):
        cid, size = data[pos : pos + 4], struct.unpack_from("<I", data, pos + 4)[0]
        if cid == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", data, pos + 8)
        elif cid == b"data":
            if fmt is None:
                return "fmt チャンクより前に data チャンクがある"
            tag, channels, rate, _, _, bits = fmt
            if tag != 1:
                return "リニア PCM ではない"
            if bits != 16:
                return f"{bits}bit（16bit だけ使える）"
            if channels not in (1, 2):
                return f"{channels} チャンネル（モノラルかステレオだけ使える）"
            if not 8000 <= rate <= 48000:
                return f"{rate}Hz（8000〜48000Hz だけ使える）"
            return None
        pos += 8 + size + (size & 1)
    return "data チャンクが見つからない"


def valid_name(name) -> bool:
    return isinstance(name, str) and name != "" and "/" not in name and "\\" not in name and name != ".."


def check(pack: Path) -> list[str]:
    errors: list[str] = []
    manifest = pack / "pack.json"
    if not manifest.exists():
        return ["pack.json がない"]
    try:
        doc = json.loads(manifest.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError) as e:
        return [f"pack.json を読めない: {e}"]

    if doc.get("format") != 1:
        errors.append("format は 1 にする")
    name = doc.get("name")
    if not isinstance(name, str) or not name or len(name.encode()) >= 64:
        errors.append("name がない、または 63 バイトを超えている")
    kind = doc.get("kind")
    if kind is not None and (not isinstance(kind, str) or len(kind.encode()) >= 32):
        errors.append("kind が 31 バイトを超えている")

    used: list[str] = []  # パックで使う音声ファイル
    running = doc.get("running") or {}
    rtype = running.get("type")
    if rtype == "synth":
        if running.get("preset") not in PRESETS:
            errors.append("running.preset は electric・steam・diesel のどれかにする")
    elif rtype == "wav":
        if not valid_name(running.get("loop")):
            errors.append("running.loop がない、または不正")
        else:
            used.append(running["loop"])
        if "idle" in running:
            if valid_name(running["idle"]):
                used.append(running["idle"])
            else:
                errors.append("running.idle が不正")
        for key in ("pitch", "volume"):
            v = running.get(key)
            if v is not None and not (isinstance(v, list) and len(v) == 2 and all(isinstance(x, (int, float)) for x in v)):
                errors.append(f"running.{key} は [最小, 最大] にする")
        pitch = running.get("pitch")
        if isinstance(pitch, list) and any(isinstance(x, (int, float)) and x <= 0 for x in pitch):
            errors.append("running.pitch は 0 より大きくする")
    elif rtype != "none":
        errors.append("running.type は synth・wav・none のどれかにする")

    buttons = doc.get("buttons")
    if not isinstance(buttons, list) or not 1 <= len(buttons) <= 6:
        errors.append("buttons は 1〜6 個にする")
        buttons = buttons if isinstance(buttons, list) else []
    for i, b in enumerate(buttons, 1):
        where = f"buttons[{i}]"
        label = b.get("label")
        if not isinstance(label, str) or not label or len(label.encode()) >= 64:
            errors.append(f"{where}: label がない、または 63 バイトを超えている")
        has_file, has_files = "file" in b, "files" in b
        if has_file == has_files:
            errors.append(f"{where}: file と files のどちらか一方だけを書く")
        elif has_file:
            if valid_name(b["file"]):
                used.append(b["file"])
            else:
                errors.append(f"{where}: file が不正")
        else:
            files = b["files"]
            if not isinstance(files, list) or not 2 <= len(files) <= 4 or not all(valid_name(f) for f in files):
                errors.append(f"{where}: files は 2〜4 個のファイル名にする")
            else:
                used.extend(files)
        mode = b.get("mode", "oneshot")
        if mode not in MODES:
            errors.append(f"{where}: mode は oneshot・hold・toggle のどれかにする")
        elif has_files and mode != "oneshot":
            errors.append(f"{where}: files（連続再生）は oneshot だけ使える")

    credited: set[str] = set()
    for i, c in enumerate(doc.get("credits") or [], 1):
        if not all(k in c for k in ("files", "author", "license")):
            errors.append(f"credits[{i}]: files・author・license を書く")
            continue
        credited.update(c["files"])

    for f in dict.fromkeys(used):
        path = pack / f
        if not path.exists():
            errors.append(f"{f}: ファイルがない")
            continue
        problem = wav_problem(path)
        if problem:
            errors.append(f"{f}: {problem}")
        if f not in credited:
            errors.append(f"{f}: credits に書かれていない")
    return errors


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__.strip())
        return 2
    failed = False
    for arg in sys.argv[1:]:
        pack = Path(arg)
        errors = check(pack)
        if errors:
            failed = True
            print(f"NG {pack}")
            for e in errors:
                print(f"  - {e}")
        else:
            print(f"OK {pack}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
