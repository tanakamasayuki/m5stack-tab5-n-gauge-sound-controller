## Environment ( 実行環境 )

- MCU or Board name:  other（PC 上のホスト環境。lang-ship:host 1.7.1 の `mode=lgfx`、SDL2 の dummy ドライバ）
- Panel Driver IC:    other（パネルなし。LGFX_Sprite に描画）
- Bus type:           other（なし）
- LovyanGFX version:  v1.2.26
- FrameWork version:  other（lang-ship:host 1.7.1）
- Build Environment:  other（arduino-cli 1.5.1）
- Operating System:   Linux（WSL2）

## Problem Description ( 問題の内容 )

`drawString()` で、描画先（スプライト）のクリップ範囲の完全に外にある文字を描いても、範囲内に描いたときとほぼ同じ時間がかかります。`fillRect()` などの図形は、範囲外ならすぐに戻ります。

小さなスプライトをタイルとして使い、画面を分割して描く（分割ダブルバッファ）と、すべての文字がタイルの数だけ描かれます。そのため、このコストがタイルの数だけ掛け算になります。M5Stack Tab5（1280x720）で、1280x7 のタイル（約 100 分割）で画面を描いているときに気づきました。

### Expected Behavior ( 期待される動作 )

文字の描画範囲が、クリップ範囲と重ならないときは、グリフを展開せずにすぐ戻ること。たとえば、datum・フォントの高さ・文字サイズから行の上下の範囲が分かるので、それがクリップ範囲と重ならなければ描かずに戻る、という動作を期待しています。

### Actual Behavior ( 実際の動作 )

1280x7 の RGB565 スプライトに、範囲内（y=3）と完全に範囲外（y=500）で描いたときの 1 回あたりの時間です（2000 回を 7 セット測った最小値）。

| 呼び出し | 範囲内 | 範囲外 |
|---|---|---|
| drawString lgfxJapanGothicP_40（日本語 4 文字） | 4,101 ns | 3,660 ns |
| drawString lgfxJapanGothicP_40（ASCII 8 文字） | 3,054 ns | 2,754 ns |
| drawString FreeSans24pt7b（8 文字） | 3,911 ns | 3,631 ns |
| drawString Font4（8 文字） | 1,620 ns | 1,240 ns |
| 参考: fillRect 260x250 | 75 ns | 3 ns |

- 範囲外でも、範囲内の 8〜9 割の時間がかかっています。u8g2・GFXfont・RLE のどのフォント形式でも同じ傾向です。
- 同じ条件で測った `textWidth()`（lgfxJapanGothicP_40、4 文字）は約 0.6µs でした。グリフを展開する処理が大部分を占めているようです。

ついでに気づいた点: `drawLine()` は、範囲外のほうが範囲内より遅くなりました（範囲内 550 ns、範囲外 857 ns。(0,400)-(1279,500) を 1280x7 のスプライトに描いた場合）。

### Steps to reproduce ( 再現のための前提条件 )

1. 下のスケッチをビルドして実行します（パネルは使わず、スプライトだけに描きます）。
2. Serial に出力される `in`（範囲内）と `out`（範囲外）の時間を比べます。

### Code to reproduce this issue ( 再現させるためのコード )

```cpp
#include <LovyanGFX.hpp>

static LGFX_Sprite tile;

// 2000 回を 7 セット測り、最小値を 1 回あたりの ns で出す
#define TIME_NS(out, EXPR)                     \
  do {                                         \
    uint32_t best = UINT32_MAX;                \
    for (int k = 0; k < 7; ++k) {              \
      uint32_t t0 = micros();                  \
      for (int i = 0; i < 2000; ++i) { EXPR; } \
      uint32_t dt = micros() - t0;             \
      if (dt < best) best = dt;                \
    }                                          \
    out = best * 1000 / 2000;                  \
  } while (0)

#define BENCH(name, IN, OUT)                                            \
  do {                                                                  \
    uint32_t a, b;                                                      \
    TIME_NS(a, IN);                                                     \
    TIME_NS(b, OUT);                                                    \
    Serial.printf("%-40s in=%6lu ns  out=%6lu ns\n", name,              \
                  (unsigned long)a, (unsigned long)b);                  \
  } while (0)

void setup()
{
  Serial.begin(115200);
  tile.setColorDepth(16);
  tile.createSprite(1280, 7);   // 分割描画のタイルを想定した細長いスプライト
  tile.setTextDatum(middle_center);
  tile.setTextColor(0xFFFF);

  BENCH("fillRect 260x250",
        tile.fillRect(370, -100, 260, 250, 0x1234),
        tile.fillRect(370, 400, 260, 250, 0x1234));
  BENCH("drawLine",
        tile.drawLine(0, 0, 1279, 6, 0x1234),
        tile.drawLine(0, 400, 1279, 500, 0x1234));

  tile.setFont(&fonts::lgfxJapanGothicP_40);
  BENCH("drawString lgfxJapanGothicP_40 (4ch)",
        tile.drawString("発車ベル", 640, 3),
        tile.drawString("発車ベル", 640, 500));
  BENCH("drawString lgfxJapanGothicP_40 ASCII",
        tile.drawString("E231 OK!", 640, 3),
        tile.drawString("E231 OK!", 640, 500));

  tile.setFont(&fonts::FreeSans24pt7b);
  BENCH("drawString FreeSans24pt7b",
        tile.drawString("E231 OK!", 640, 3),
        tile.drawString("E231 OK!", 640, 500));

  tile.setFont(&fonts::Font4);
  BENCH("drawString Font4",
        tile.drawString("E231 OK!", 640, 3),
        tile.drawString("E231 OK!", 640, 500));
}

void loop() { delay(1000); }
```

ESP32 の実機（M5Stack Tab5、ESP32-P4）での同じ計測は、まだできていません。手元に実機が戻ったら、結果を追記します。
