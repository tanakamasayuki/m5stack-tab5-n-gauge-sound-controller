# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial>=3.5"]
# ///
"""Serial 出力をそのままファイルに保存する（E1 の CSV 記録用）。

uv run tools/capture_serial.py --port /dev/ttyACM0 --out logs/e1_run1.csv
uv run tools/capture_serial.py --port /dev/ttyACM0 --out logs/e1_raw1.csv --send r   # RAW モード
"""

import argparse
import sys
from pathlib import Path

import serial


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--send", default="", help="接続直後に送る文字列（例: r で RAW モード）")
    args = parser.parse_args()

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with serial.Serial(args.port, args.baud, timeout=1) as ser, args.out.open("w", encoding="utf-8") as f:
        if args.send:
            ser.write(args.send.encode())
        print(f"recording {args.port} -> {args.out} (Ctrl+C to stop)", file=sys.stderr)
        try:
            while True:
                line = ser.readline().decode("utf-8", errors="replace")
                if not line:
                    continue
                f.write(line)
                f.flush()
                if line.startswith("#"):
                    print(line.rstrip(), file=sys.stderr)
        except KeyboardInterrupt:
            pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
