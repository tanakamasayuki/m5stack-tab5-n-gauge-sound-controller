# 描画が重い原因の調査

**日付：** 2026-10-27
**状態：** PC（ホスト環境）での計測と、ライブラリのコードを読んだ結果による見立て。実機（Tab5）での確認はまだ。

## 1. 症状

運転画面（Drive）を LGFXVirtualCanvas のバッファ描画で描くと、1 回の描画に時間がかかる。実機で測った値は次のとおり。

| タイルの設定 | 分割数 | 描画時間 |
|---|---|---|
| 既定（19KB、内蔵 RAM） | 103 | 833ms |
| 50KB、内蔵 RAM | 36 | 346ms |
| 75KB、内蔵 RAM | 24 | 259ms |
| 100KB、内蔵 RAM | 18 | 210ms |
| 450KB、PSRAM | 4 | 216ms |
| 全画面 1.8MB、PSRAM | 1 | 750ms |

103 分割と 18 分割の差から逆算すると、**タイル 1 枚あたり約 7ms の固定コスト**がある。

## 2. 分かったこと（PC での計測）

### 2.1 LovyanGFX の描画関数は、範囲外を早く捨てるか

`experiments/ClipBench`。1280x7 のスプライトに、完全に範囲外と範囲内で描いたときの 1 回あたりの時間。

| 描画関数 | 範囲内 | 範囲外 |
|---|---|---|
| fillRect 260x250 | 54ns | 3ns |
| fillRoundRect 260x250 r18 | 86ns | 52ns |
| drawRoundRect 244x234 r12 | 67ns | 57ns |
| fillCircle r60 | 155ns | 108ns |
| drawLine | 559ns | **860ns** |
| drawString u8g2 日本語 40px（4 文字） | 4,386ns | **4,058ns** |
| drawString u8g2 日本語 40px ×1.25 | 4,810ns | **4,656ns** |
| drawString u8g2 ASCII（8 文字） | 4,072ns | **3,865ns** |
| drawString GFXfont 24pt（8 文字） | 5,413ns | **4,964ns** |
| drawString Font4（8 文字） | 2,109ns | **1,594ns** |
| 参考: textWidth u8g2 日本語 40px（4 文字） | 595ns | − |

- 図形は、範囲外でも 0.1µs 前後で、問題にならない。
- **文字（drawString）は、どのフォント形式でも範囲外を早く捨てていない。** 範囲外でも、範囲内の約 9 割の時間がかかる。
- drawLine は、範囲外のほうが遅い。

### 2.2 タイル 1 枚あたりの固定コスト（PC）

何も描かない描画を、LGFXVirtualCanvas で測った。103 分割でも約 4ms（1 枚あたり約 0.04ms）。PC では、LGFXVirtualCanvas の仕組みそのものの固定コストは小さい。

### 2.3 シーン全体の時間（PC）

同じ条件でも、回によって数十倍ばらついた（WSL2 上で、計測中にプロセスが止められるためと思われる）。判断の材料には使わない。

## 3. 見立て

### 3.1 主な原因: 画面の回転と、タイルの分け方の組み合わせ（実機のみで起きる）

- Tab5 のパネルは本来縦向き（720x1280）で、フレームバッファも縦向きの 1 行（720 画素）ずつ PSRAM に並ぶ。
- アプリは横向き（`setRotation(1)`）で使っている。LGFXVirtualCanvas の既定は行分割（横長の帯、1280x7）だが、これはパネル本来の向きでは**縦長の細い帯（7 画素 × 1280 行）**になる。
- そのため、タイル 1 枚を転送するたびに、次のことが起きていると考えられる。
  1. 1280 行それぞれに 14 バイトずつ、飛び飛びに書き込む。
  2. M5GFX の `Panel_FrameBufferBase::display()` が、書き換えた範囲の CPU キャッシュを PSRAM に書き戻す。行どうしが 4096 バイト以内なら一つの範囲にまとめる作りなので、**範囲がほぼフレームバッファ全体（約 1.8MB）になる。**
- これがタイルの数だけ繰り返されるので、タイル 1 枚あたり約 7ms の固定コストになる、という見立て。PC では、何も描かない描画のタイル 1 枚あたりのコストが約 0.04ms なので、差の大部分は実機の転送と書き戻しで説明できる。

**対策の候補：** LGFXVirtualCanvas の列分割（`setSplitAxis(LGFXVirtualSplitAxis::Columns)`）。横向きの画面で縦長の帯にすれば、パネル本来の向きでは連続した数行になり、書き込みも書き戻しもその数行分で済むはず。ライブラリの修正は要らない。

### 3.2 副次的な原因: 文字が範囲外を早く捨てない（LovyanGFX）

運転画面には文字が 11 個あり、タイルの数 × 11 回の drawString が実行される。PC では 1 回 4〜5µs。実機での値はまだ測っていないが、仮に 10 倍として 103 分割で約 50ms になる。3.1 ほどではないが、分割数が多いほど効く。

## 4. 次にやること

1. 実機で `experiments/RenderBench` を動かす。行分割と列分割のそれぞれで、次の 3 つを測る。
   - 描画関数ごとの範囲内・範囲外の時間（実機での値）
   - 何も描かない描画の時間
   - 運転画面の描画時間（全部・文字なし）
2. 列分割で大きく速くなれば、アプリの設定を列分割に変える。
3. 文字の件は、LovyanGFX に修正を依頼する（下の下書き）。

## 5. LovyanGFX への修正依頼の下書き

> **タイトル：** drawString does not early-reject text that lies entirely outside the clip rect
>
> When drawing into a small sprite (e.g. a 1280x7 tile used for tiled double buffering), `drawString()` costs about the same whether the text is inside the sprite or completely outside it. Shapes such as `fillRect` / `fillRoundRect` return almost immediately when they are outside the clip rect, but text does not.
>
> Measured on lang-ship host (LovyanGFX 1.2.26), 1280x7 RGB565 sprite, best of 7 x 2000 calls:
>
> | call | inside | outside |
> |---|---|---|
> | drawString u8g2 lgfxJapanGothicP_40, 4 chars | 4,386 ns | 4,058 ns |
> | drawString u8g2 ASCII, 8 chars | 4,072 ns | 3,865 ns |
> | drawString GFXfont FreeSans24pt7b, 8 chars | 5,413 ns | 4,964 ns |
> | drawString Font4, 8 chars | 2,109 ns | 1,594 ns |
> | fillRect 260x250 (reference) | 54 ns | 3 ns |
>
> With tiled rendering, every text is drawn once per tile, so this cost is multiplied by the number of tiles. A vertical bounds check before decoding glyphs (the line box is known from the datum, the font height and the text size) would let text outside the clip rect be skipped almost for free.
>
> Minor: `drawLine` entirely outside the clip rect is slower than inside (860 ns vs 559 ns).
>
> Repro sketch: `experiments/ClipBench/ClipBench.ino`.
