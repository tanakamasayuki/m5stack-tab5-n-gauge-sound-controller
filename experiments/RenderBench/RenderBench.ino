// Tab5 の実機で、運転画面の描画が重い原因を切り分ける。結果は Serial に出す。
// 1. LovyanGFX の各描画関数が、タイルの外を早く捨てるか（範囲内・範囲外の時間）
// 2. LGFXVirtualCanvas のタイル 1 枚あたりの固定コスト（何も描かない描画）
// 3. 運転画面の描画時間（全部・文字なし）を、タイルの設定ごとに
// PC（ホスト環境）では時間のばらつきが大きく、シーン全体の計測が当てにならないため、実機で測る。
//
// 見立て（docs/rendering-performance.md）: Tab5 のパネルは本来縦向き（720x1280）で、横向きに回すと
// 横長のタイル（行分割）がパネルでは縦長の細い帯になる。転送が飛び飛びになり、M5GFX のキャッシュ書き戻しが
// 毎回ほぼフレームバッファ全体にかかる。列分割（Columns）なら連続した数行になるはず。それを比べる。

#include <M5Unified.h>
#include <LGFXVirtualCanvas.h>
#include <LGFXScreenBuilder.h>
#include <esp_heap_caps.h>
#include "SoundBoxScreen.h"

using namespace SoundBoxScreen;

struct BenchScreen : Screen
{
  using Screen::Screen;
  void tiles(size_t bytes, bool psram, bool columns)
  {
    _vscreen.setSplitAxis(columns ? LGFXVirtualSplitAxis::Columns : LGFXVirtualSplitAxis::Rows);
    _vscreen.setUsePsram(psram);
    _vscreen.setMemoryLimit(bytes);
    _vscreen.begin();
  }
  int tileCount() const { return _vscreen.tileCount(); }
  bool tileIsPsram() const { return _vscreen.tileIsPsram(); }
};

static BenchScreen screen(M5.Display);

// 7 回測った中の最小値を、1 回あたりの ns で返す
#define TIME_NS(out, REPS, EXPR)               \
  do                                           \
  {                                            \
    uint32_t best = UINT32_MAX;                \
    for (int k = 0; k < 7; ++k)                \
    {                                          \
      const uint32_t t0 = micros();            \
      for (int i = 0; i < REPS; ++i)           \
      {                                        \
        EXPR;                                  \
      }                                        \
      const uint32_t dt = micros() - t0;       \
      best = dt < best ? dt : best;            \
    }                                          \
    out = uint32_t(uint64_t(best) * 1000 / REPS); \
  } while (0)

#define CLIP(name, IN, OUT)                                                                            \
  do                                                                                                   \
  {                                                                                                    \
    uint32_t a, b;                                                                                     \
    TIME_NS(a, 200, IN);                                                                               \
    TIME_NS(b, 200, OUT);                                                                              \
    Serial.printf("CLIP %-34s in=%8lu ns  out=%8lu ns\n", name, (unsigned long)a, (unsigned long)b); \
  } while (0)

static void clipBench()
{
  LGFX_Sprite tile(&M5.Display);
  tile.setColorDepth(16);
  tile.createSprite(1280, 7); // LGFXVirtualCanvas の既定タイルと同じ大きさ（内蔵 RAM）

  CLIP("fillRect 260x250", tile.fillRect(370, -100, 260, 250, 0x1234), tile.fillRect(370, 400, 260, 250, 0x1234));
  CLIP("fillRoundRect 260x250 r18", tile.fillRoundRect(370, -100, 260, 250, 18, 0x1234),
       tile.fillRoundRect(370, 400, 260, 250, 18, 0x1234));
  CLIP("drawRoundRect 244x234 r12", tile.drawRoundRect(378, -100, 244, 234, 12, 0x1234),
       tile.drawRoundRect(378, 400, 244, 234, 12, 0x1234));
  CLIP("fillCircle r60", tile.fillCircle(640, 3, 60, 0x1234), tile.fillCircle(640, 500, 60, 0x1234));
  CLIP("drawLine", tile.drawLine(0, 0, 1279, 6, 0x1234), tile.drawLine(0, 400, 1279, 500, 0x1234));

  tile.setTextDatum(middle_center);
  tile.setTextColor(0xFFFF);
  tile.setFont(&fonts::lgfxJapanGothicP_40);
  tile.setTextSize(1.0f);
  CLIP("drawString u8g2 JP40 x1.0 (4ch)", tile.drawString("発車ベル", 640, 3), tile.drawString("発車ベル", 640, 500));
  tile.setTextSize(1.25f);
  CLIP("drawString u8g2 JP40 x1.25 (4ch)", tile.drawString("発車ベル", 640, 3), tile.drawString("発車ベル", 640, 500));
  tile.setTextSize(1.0f);
  CLIP("drawString u8g2 ASCII (8ch)", tile.drawString("E231 OK!", 640, 3), tile.drawString("E231 OK!", 640, 500));
  tile.setFont(&fonts::FreeSans24pt7b);
  CLIP("drawString GFXfont 24pt (8ch)", tile.drawString("E231 OK!", 640, 3), tile.drawString("E231 OK!", 640, 500));
  tile.setFont(&fonts::Font4);
  CLIP("drawString Font4 RLE (8ch)", tile.drawString("E231 OK!", 640, 3), tile.drawString("E231 OK!", 640, 500));
  tile.setFont(&fonts::lgfxJapanGothicP_40);
  CLIP("textWidth u8g2 JP40 (4ch)", (void)tile.textWidth("発車ベル"), (void)tile.textWidth("発車ベル"));
}

struct TileCfg
{
  size_t bytes;
  bool psram;
  bool columns;
};
static const TileCfg kCfgs[] = {
    {19200, false, false}, {51200, false, false}, {102400, false, false}, {1280 * 180 * 2, true, false},
    {19200, false, true},  {51200, false, true},  {102400, false, true},  {1280 * 180 * 2, true, true},
};

static void emptyBench()
{
  LGFXVirtualScreen vs(M5.Display);
  vs.setAutoClear(false);
  for (auto c : kCfgs)
  {
    vs.setSplitAxis(c.columns ? LGFXVirtualSplitAxis::Columns : LGFXVirtualSplitAxis::Rows);
    vs.setUsePsram(c.psram);
    vs.setMemoryLimit(c.bytes);
    vs.begin();
    uint32_t best = UINT32_MAX;
    for (int k = 0; k < 5; ++k)
    {
      const uint32_t t0 = micros();
      vs.render([](LGFXVirtualCanvas &, void *) {}, nullptr);
      const uint32_t dt = micros() - t0;
      best = dt < best ? dt : best;
    }
    Serial.printf("EMPTY split=%s tiles=%d psram=%d render_us=%lu\n", c.columns ? "cols" : "rows", vs.tileCount(),
                  (int)vs.tileIsPsram(), (unsigned long)best);
  }
}

static Scene::Drive scene(bool withText)
{
  Scene::Drive d;
  if (!withText)
  {
    d.trainName = d.sensorStatus = d.stateText = d.trainBtnLabel = d.stopBtnLabel = "";
    d.fx1Label = d.fx2Label = d.fx3Label = d.fx4Label = d.fx5Label = d.fx6Label = "";
  }
  return d;
}

static uint32_t timeShow(const Scene::Drive &d)
{
  uint32_t best = UINT32_MAX;
  for (int i = 0; i < 5; ++i)
  {
    const uint32_t t0 = micros();
    screen.show(d);
    const uint32_t dt = micros() - t0;
    best = dt < best ? dt : best;
  }
  return best;
}

static void sceneBench()
{
  for (auto c : kCfgs)
  {
    screen.tiles(c.bytes, c.psram, c.columns);
    const uint32_t full = timeShow(scene(true));
    const uint32_t noText = timeShow(scene(false));
    Serial.printf("SCENE split=%s tiles=%d psram=%d full_us=%lu notext_us=%lu free_internal=%u\n",
                  c.columns ? "cols" : "rows", screen.tileCount(),
                  (int)screen.tileIsPsram(), (unsigned long)full, (unsigned long)noText,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  }
}

void setup()
{
  M5.begin();
  Serial.begin(115200);
  M5.Display.setRotation(1);
  screen.begin();
  delay(3000); // Serial をつなぐ時間
  Serial.println("TEST start RenderBench");
  clipBench();
  emptyBench();
  sceneBench();
  Serial.println("TEST done");
}

void loop()
{
  delay(1000);
}
