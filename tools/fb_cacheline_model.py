"""Tab5 のフレームバッファに、横向きのタイルを転送するときに触る CPU キャッシュラインの数を数える。

docs/rendering-performance.md の見立てを、時間ではなく処理量で確かめるためのモデル。
- パネル本来の向きは縦（横 720 x 縦 1280）。フレームバッファは RGB565 で、1 行 1440 バイトが縦に並ぶ。
- アプリは横向き（1280x720）で描く。横向きの (X, Y) は、パネルでは列 Y・行 X に当たる（回転 1）。
  向きの細かな違い（上下左右の反転）は、触る範囲の大きさには影響しない。
- LGFXVirtualCanvas はタイルを 1 枚ずつ転送する。タイルが触るキャッシュラインを数え、全タイル分を足す。

python3 tools/fb_cacheline_model.py
"""

PANEL_W, PANEL_H = 720, 1280  # パネル本来の向き
BPP = 2
STRIDE = PANEL_W * BPP
SCREEN_W, SCREEN_H = 1280, 720  # 横向き


def lines_touched(tile_x, tile_y, tile_w, tile_h, line_bytes):
    """横向きの矩形 (tile_x, tile_y, tile_w, tile_h) を書いたときに触るキャッシュラインの数"""
    lines = set()
    # 横向きの X はパネルの行、Y はパネルの列（回転 1）
    for prow in range(tile_x, tile_x + tile_w):
        start = prow * STRIDE + tile_y * BPP
        end = prow * STRIDE + (tile_y + tile_h) * BPP  # 1 行の中では連続している
        for ln in range(start // line_bytes, (end - 1) // line_bytes + 1):
            lines.add(ln)
    return len(lines)


def frame(split, budget, line_bytes):
    """1 回の描画（全タイル）で触るキャッシュラインの延べ数"""
    if split == "rows":
        th = max(1, budget // (SCREEN_W * BPP))
        tiles = [(0, y, SCREEN_W, min(th, SCREEN_H - y)) for y in range(0, SCREEN_H, th)]
    else:
        tw = max(1, budget // (SCREEN_H * BPP))
        tiles = [(x, 0, min(tw, SCREEN_W - x), SCREEN_H) for x in range(0, SCREEN_W, tw)]
    return len(tiles), sum(lines_touched(*t, line_bytes) for t in tiles)


def main():
    total_lines = {64: PANEL_W * PANEL_H * BPP // 64, 128: PANEL_W * PANEL_H * BPP // 128}
    print(f"framebuffer {PANEL_W}x{PANEL_H} RGB565 = {PANEL_W * PANEL_H * BPP} bytes")
    for line_bytes in (64, 128):
        print(f"\ncache line {line_bytes} bytes (framebuffer = {total_lines[line_bytes]} lines)")
        print(f"{'split':6} {'budget':>8} {'tiles':>6} {'line touches':>13} {'x framebuffer':>14}")
        for budget in (19200, 51200, 102400, 1280 * 180 * 2):
            for split in ("rows", "cols"):
                tiles, touches = frame(split, budget, line_bytes)
                print(f"{split:6} {budget:8} {tiles:6} {touches:13} {touches / total_lines[line_bytes]:14.1f}")


if __name__ == "__main__":
    main()
