// LovyanGFX の各描画関数が、クリップ範囲（スプライト）の外を早く捨てるかを測る。
// 1280x7 のスプライト（LGFXVirtualCanvas の既定タイルと同じ大きさ）に、
// 完全に範囲外（y=500）と範囲内（y=0 付近）で描き、1 回あたりの時間（ns）を出す。
// 最後に、LGFXVirtualCanvas で何も描かない描画をタイル数を変えて測り、タイル 1 枚あたりの固定コストを見る。
//
// ホスト環境（lang-ship:host）で動かす。実機は experiments/RenderBench を使う。
// 実行: cd tests && uv run --env-file .env pytest ../experiments/ClipBench -s
#include <LovyanGFX.hpp>
#include <LGFX_AUTODETECT.hpp>
#include <LGFXVirtualCanvas.h>

static LGFX lcd(1280, 720);
static LGFX_Sprite tile(&lcd);

// 7 回測った中の最小値を、1 回あたりの ns で返す
#define TIME_NS(out, EXPR)                         \
  do                                               \
  {                                                \
    uint32_t best = UINT32_MAX;                    \
    for (int k = 0; k < 7; ++k)                    \
    {                                              \
      const uint32_t t0 = micros();                \
      for (int i = 0; i < 2000; ++i)               \
      {                                            \
        EXPR;                                      \
      }                                            \
      const uint32_t dt = micros() - t0;           \
      best = dt < best ? dt : best;                \
    }                                              \
    out = best * 1000 / 2000;                      \
  } while (0)

#define BENCH(name, IN, OUT)                                                                  \
  do                                                                                          \
  {                                                                                           \
    uint32_t a, b;                                                                            \
    TIME_NS(a, IN);                                                                           \
    TIME_NS(b, OUT);                                                                          \
    Serial.printf("CLIP %-34s in=%7lu ns  out=%7lu ns\n", name, (unsigned long)a, (unsigned long)b); \
  } while (0)

void setup()
{
  Serial.begin(115200);
  Serial.println("TEST start ClipBench");
  lcd.init();
  tile.setColorDepth(16);
  tile.createSprite(1280, 7);

  BENCH("fillRect 260x250", tile.fillRect(370, -100, 260, 250, 0x1234), tile.fillRect(370, 400, 260, 250, 0x1234));
  BENCH("fillRoundRect 260x250 r18", tile.fillRoundRect(370, -100, 260, 250, 18, 0x1234),
        tile.fillRoundRect(370, 400, 260, 250, 18, 0x1234));
  BENCH("drawRoundRect 244x234 r12", tile.drawRoundRect(378, -100, 244, 234, 12, 0x1234),
        tile.drawRoundRect(378, 400, 244, 234, 12, 0x1234));
  BENCH("fillCircle r60", tile.fillCircle(640, 3, 60, 0x1234), tile.fillCircle(640, 500, 60, 0x1234));
  BENCH("drawLine", tile.drawLine(0, 0, 1279, 6, 0x1234), tile.drawLine(0, 400, 1279, 500, 0x1234));

  tile.setTextDatum(middle_center);
  tile.setTextColor(0xFFFF);
  tile.setFont(&fonts::lgfxJapanGothicP_40);
  tile.setTextSize(1.0f);
  BENCH("drawString u8g2 JP40 x1.0 (4ch)", tile.drawString("発車ベル", 640, 3), tile.drawString("発車ベル", 640, 500));
  tile.setTextSize(1.25f);
  BENCH("drawString u8g2 JP40 x1.25 (4ch)", tile.drawString("発車ベル", 640, 3), tile.drawString("発車ベル", 640, 500));
  tile.setTextSize(1.0f);
  BENCH("drawString u8g2 ASCII (8ch)", tile.drawString("E231 OK!", 640, 3), tile.drawString("E231 OK!", 640, 500));
  tile.setFont(&fonts::FreeSans24pt7b);
  BENCH("drawString GFXfont 24pt (8ch)", tile.drawString("E231 OK!", 640, 3), tile.drawString("E231 OK!", 640, 500));
  tile.setFont(&fonts::Font4);
  BENCH("drawString Font4 RLE (8ch)", tile.drawString("E231 OK!", 640, 3), tile.drawString("E231 OK!", 640, 500));
  tile.setFont(&fonts::lgfxJapanGothicP_40);
  BENCH("textWidth u8g2 JP40 (4ch)", (void)tile.textWidth("発車ベル"), (void)tile.textWidth("発車ベル"));
  BENCH("setFont u8g2 JP40", tile.setFont(&fonts::lgfxJapanGothicP_40), tile.setFont(&fonts::lgfxJapanGothicP_40));
  BENCH("setFont u8g2 JP40 <-> Font0", (tile.setFont(&fonts::lgfxJapanGothicP_40), tile.setFont(&fonts::Font0)),
        (tile.setFont(&fonts::lgfxJapanGothicP_40), tile.setFont(&fonts::Font0)));
  tile.setFont(&fonts::lgfxJapanGothicP_40);
  BENCH("drawString u8g2 empty", tile.drawString("", 640, 3), tile.drawString("", 640, 500));
  BENCH("setTextSize+Datum+Color", (tile.setTextSize(1.25f), tile.setTextDatum(middle_center), tile.setTextColor(0xFFFF)),
        (tile.setTextSize(1.0f), tile.setTextDatum(top_left), tile.setTextColor(0x0000)));

  // タイル 1 枚あたりの固定コスト: 何も描かない描画を、タイル数を変えて測る
  {
    LGFXVirtualScreen vs(lcd);
    vs.setAutoClear(false);
    const size_t budgets[] = {19200, 102400, 1280 * 180 * 2, 1280 * 720 * 2};
    for (size_t bb : budgets)
    {
      vs.setMemoryLimit(bb);
      vs.begin();
      uint32_t best = UINT32_MAX;
      for (int k = 0; k < 5; ++k)
      {
        const uint32_t t0 = micros();
        vs.render([](LGFXVirtualCanvas &, void *) {}, nullptr);
        const uint32_t dt = micros() - t0;
        best = dt < best ? dt : best;
      }
      Serial.printf("TILES tiles=%d empty_render_us=%lu\n", vs.tileCount(), (unsigned long)best);
    }
  }
  Serial.println("TEST done");
}

void loop() { delay(1000); }
