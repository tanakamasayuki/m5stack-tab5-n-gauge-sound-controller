// 全シーンを Tab5 サイズで描画し、output/<Scene>.png に保存する。
// 画面デザインの確認用（ホスト環境で実行）。

#include <LovyanGFX.hpp>
#include <LGFX_AUTODETECT.hpp>
#include <LGFXScreenBuilder.h>
#include "SoundBoxScreen.h"

#include <stdio.h>
#include <sys/stat.h>

using namespace SoundBoxScreen;

static LGFX lcd(1280, 720);
static Screen screen(lcd);

static bool savePng(const char *path)
{
  size_t len = 0;
  void *png = lcd.createPng(&len, 0, 0, lcd.width(), lcd.height());
  if (!png || !len)
  {
    return false;
  }
  FILE *fp = fopen(path, "wb");
  const bool ok = fp && fwrite(png, 1, len, fp) == len;
  if (fp)
  {
    fclose(fp);
  }
  free(png);
  return ok;
}

// メーターの中身（overlay の描画例）
template <class GFX>
void driveOverlay(GFX &g, const Scene::Drive &)
{
  lgfxsb::Rect r;
  if (screen.bounds(Scene::Drive::Part::meterFrame, r))
  {
    g.fillRoundRect(r.x + 6, r.y + 6, (r.w - 12) * 62 / 100, r.h - 12, 8, g.color888(0x4f, 0x8a, 0x5b));
  }
}

void setup()
{
  Serial.begin(115200);
  Serial.println("TEST start screens");
  mkdir("output", 0755);
  lcd.init();
  screen.begin();
  screen.setOverlay(driveOverlay);
  Serial.printf("PANEL %dx%d\n", (int)lcd.width(), (int)lcd.height());

  for (uint16_t i = 0; i < detail::kSceneInfoCount; ++i)
  {
    const auto &sc = detail::kSceneInfo[i];
    if (sc.id == Scene::Drive::id)
    {
      screen.show(Scene::Drive{});
    }
    else
    {
      screen.show(sc.id);
    }
    char path[64];
    snprintf(path, sizeof(path), "output/%s.png", sc.name);
    Serial.printf("SCENE %s saved=%d\n", sc.name, (int)savePng(path));
  }

  // 押下中の見た目
  Scene::Drive pressed;
  pressed.fx1PressedVisible = true;
  screen.show(pressed);
  Serial.printf("SCENE DrivePressed saved=%d\n", (int)savePng("output/DrivePressed.png"));

  Serial.println("TEST done");
}

void loop()
{
  delay(1000);
}
