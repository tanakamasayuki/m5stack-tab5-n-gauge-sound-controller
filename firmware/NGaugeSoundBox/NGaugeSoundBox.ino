// Nゲージ サウンドボックス（M5Stack Tab5）
// パワーパックの入力電流から走行状態を判定して走行音を鳴らし、
// 画面のボタンで効果音を鳴らす。
//
// いまの段階:
// - 画面は運転画面（Drive）、車両選択画面（TrainSelect）、設定画面（Settings）。
//   設定画面は、運転画面の上の帯を 1 秒長押しすると開く
// - 音はすべて SoundEngine で合成している。車両は内蔵の 3 種類で、音源パックはまだ読まない
// - 電流検出がないときは、メーター部分をタップすると手動で発車・停車する

#include <M5Unified.h>
#include <LGFXVirtualCanvas.h> // LGFXScreenBuilder より前に include すると、分割ダブルバッファで描く
#include <LGFXScreenBuilder.h>
#include <Preferences.h>
#include "SoundBoxScreen.h"
#include "CurrentSensor.h"
#include "SoundEngine.h"

using namespace SoundBoxScreen;

// 既定のタイル（約 19KB）だと 1280 幅では約 100 分割になり、描画に 1 秒近くかかる。
// PSRAM に大きなタイルを置いて、分割数を減らす
struct BufferedScreen : Screen
{
  using Screen::Screen;

  void setupTiles(size_t bytes, bool psram = true)
  {
    _vscreen.setUsePsram(psram);
    _vscreen.setMemoryLimit(bytes);
    _vscreen.begin();
    Serial.printf("# tiles=%d height=%dpx double=%d psram=%d\n", _vscreen.tileCount(), _vscreen.tileHeight(),
                  (int)_vscreen.doubleBuffer(), (int)_vscreen.tileIsPsram());
  }
};

static BufferedScreen screen(M5.Display);
static CurrentSensor sensor;
static SoundEngine sound;
static Preferences prefs;

enum class Page : uint8_t
{
  Drive,
  TrainSelect,
  Settings,
  Credits, // 設定画面の上に重ねるダイアログ
};

static Page page = Page::Drive;
static CurrentSensor::Snapshot lastSnap;
static bool manualRunning = false;
static int pressedFx = -1; // 押している効果音ボタン（-1 = なし）
static bool pressedStop = false;
static bool pressedTrain = false;
static bool pressedBack = false;
static int pressedCard = -1;
static int pressedSetting = -1; // 設定画面で押しているボタン（Scene::Settings::Part）
static bool pressedOk = false;

// 音量・明るさは 1〜10 の 10 段階で保存する
static int volumeLevel = 7;
static int brightnessLevel = 8;

static void applyVolume() { M5.Speaker.setVolume(uint8_t(volumeLevel * 25)); }
static void applyBrightness() { M5.Display.setBrightness(uint8_t(brightnessLevel * 25)); }

static const char *stateLabel(const CurrentSensor::Snapshot &s)
{
  if (!s.ok)
  {
    return manualRunning ? "手動: 走行中" : "手動: 停止";
  }
  switch (s.state)
  {
  case TrainSense::State::Stop:
    return "停止";
  case TrainSense::State::Start:
    return "発進";
  case TrainSense::State::Run:
    return "走行中";
  case TrainSense::State::Brake:
    return "減速";
  case TrainSense::State::Accel:
    return "加速";
  }
  return "";
}

// メーターの中身（level のバー）。画面全体を描き直すと 200ms 以上かかるので、
// メーター枠の内側だけをスプライトに描いて直接転送する
static LGFX_Sprite meterSprite(&M5.Display);
static lgfxsb::Rect meterRect;

static void drawMeter()
{
  if (page != Page::Drive)
  {
    return;
  }
  if (!meterSprite.getBuffer())
  {
    if (!screen.bounds(Scene::Drive::Part::meterFrame, meterRect))
    {
      return;
    }
    meterSprite.createSprite(meterRect.w - 2, meterRect.h - 2);
  }
  const float level = lastSnap.ok ? lastSnap.level : (manualRunning ? 0.5f : 0.0f);
  meterSprite.fillScreen(meterSprite.color888(0xfb, 0xfa, 0xf7)); // statusCard の色
  const int w = int((meterSprite.width() - 10) * level);
  if (w > 0)
  {
    meterSprite.fillRoundRect(5, 5, w, meterSprite.height() - 10, 8, meterSprite.color888(0x4f, 0x8a, 0x5b));
  }
  meterSprite.pushSprite(meterRect.x + 1, meterRect.y + 1);
}

static void drawDrive()
{
  const auto &p = SoundEngine::preset(sound.presetIndex());
  Scene::Drive d;
  d.trainName = p.name;
  d.sensorStatus = lastSnap.ok ? "● 電流検出 OK" : "○ 電流検出なし";
  d.stateText = stateLabel(lastSnap);
  const char **labels[] = {&d.fx1Label, &d.fx2Label, &d.fx3Label, &d.fx4Label, &d.fx5Label, &d.fx6Label};
  lgfxsb::OptBool *pressed[] = {&d.fx1PressedVisible, &d.fx2PressedVisible, &d.fx3PressedVisible,
                                &d.fx4PressedVisible, &d.fx5PressedVisible, &d.fx6PressedVisible};
  for (int i = 0; i < SoundEngine::kEffectCount; ++i)
  {
    *labels[i] = p.labels[i];
  }
  if (pressedFx >= 0)
  {
    *pressed[pressedFx] = true;
  }
  if (pressedStop)
  {
    d.stopBtnPressedVisible = true;
  }
  if (pressedTrain)
  {
    d.trainBtnPressedVisible = true;
  }
  screen.show(d);
  drawMeter();
}

static void drawTrainSelect()
{
  Scene::TrainSelect d;
  const char **names[] = {&d.card1Name, &d.card2Name, &d.card3Name, &d.card4Name, &d.card5Name, &d.card6Name};
  lgfxsb::OptBool *selected[] = {&d.card1SelectedVisible, &d.card2SelectedVisible, &d.card3SelectedVisible,
                                 &d.card4SelectedVisible, &d.card5SelectedVisible, &d.card6SelectedVisible};
  for (int i = 0; i < 6; ++i)
  {
    *names[i] = i < SoundEngine::kPresetCount ? SoundEngine::preset(i).name : "";
    *selected[i] = (i == sound.presetIndex() && pressedCard < 0) || i == pressedCard;
  }
  if (pressedBack)
  {
    d.backBtnPressedVisible = true;
  }
  screen.show(d);
}

static void drawSettings()
{
  Scene::Settings d;
  char vol[8], bri[8], mon[96];
  snprintf(vol, sizeof(vol), "%d", volumeLevel);
  snprintf(bri, sizeof(bri), "%d", brightnessLevel);
  if (lastSnap.ok)
  {
    snprintf(mon, sizeof(mon), "%.1f mA  %s  %d%%", lastSnap.current, stateLabel(lastSnap), int(lastSnap.level * 100 + 0.5f));
  }
  else
  {
    snprintf(mon, sizeof(mon), "電流検出なし（U200 を確認）");
  }
  d.volValue = vol;
  d.briValue = bri;
  d.monitor = mon;
  using P = Scene::Settings::Part;
  const P p = static_cast<P>(pressedSetting);
  d.volDownPressedVisible = p == P::volDown;
  d.volUpPressedVisible = p == P::volUp;
  d.briDownPressedVisible = p == P::briDown;
  d.briUpPressedVisible = p == P::briUp;
  d.creditBtnPressedVisible = p == P::creditBtn;
  d.backBtnPressedVisible = p == P::backBtn;
  screen.show(d);
}

static void drawCredits()
{
  Scene::Message d;
  d.msg = "音はすべて本体で合成（自作、CC0）";
  d.okBtnPressedVisible = pressedOk;
  screen.show(d);
}

static void draw()
{
  switch (page)
  {
  case Page::Drive:
    drawDrive();
    break;
  case Page::TrainSelect:
    drawTrainSelect();
    break;
  case Page::Settings:
    drawSettings();
    break;
  case Page::Credits:
    drawCredits();
    break;
  }
}

static int fxIndex(Scene::Drive::Part p)
{
  switch (p)
  {
  case Scene::Drive::Part::fx1:
    return 0;
  case Scene::Drive::Part::fx2:
    return 1;
  case Scene::Drive::Part::fx3:
    return 2;
  case Scene::Drive::Part::fx4:
    return 3;
  case Scene::Drive::Part::fx5:
    return 4;
  case Scene::Drive::Part::fx6:
    return 5;
  default:
    return -1;
  }
}

static int cardIndex(Scene::TrainSelect::Part p)
{
  switch (p)
  {
  case Scene::TrainSelect::Part::card1:
    return 0;
  case Scene::TrainSelect::Part::card2:
    return 1;
  case Scene::TrainSelect::Part::card3:
    return 2;
  case Scene::TrainSelect::Part::card4:
    return 3;
  case Scene::TrainSelect::Part::card5:
    return 4;
  case Scene::TrainSelect::Part::card6:
    return 5;
  default:
    return -1;
  }
}

// 運転画面のタッチ。触れた瞬間に鳴らす
static void touchDrive(int x, int y)
{
  Scene::Drive cur;
  const auto part = screen.hitTest(cur, x, y);
  const int fx = fxIndex(part);
  if (fx >= 0)
  {
    sound.playEffect(fx);
    pressedFx = fx;
  }
  else if (part == Scene::Drive::Part::stopBtn)
  {
    sound.stopAll();
    manualRunning = false;
    sound.setManualRunning(false);
    pressedStop = true;
  }
  else if (part == Scene::Drive::Part::trainBtn)
  {
    pressedTrain = true; // 離したときに車両選択へ
  }
  else if (part == Scene::Drive::Part::meterFrame && !lastSnap.ok)
  {
    manualRunning = !manualRunning;
    sound.setManualRunning(manualRunning);
  }
}

// 車両選択画面のタッチ。押した見た目を出し、離したときに決める
static void touchTrainSelect(int x, int y)
{
  Scene::TrainSelect cur;
  const auto part = screen.hitTest(cur, x, y);
  const int card = cardIndex(part);
  if (card >= 0 && card < SoundEngine::kPresetCount)
  {
    pressedCard = card;
  }
  else if (part == Scene::TrainSelect::Part::backBtn)
  {
    pressedBack = true;
  }
}

// 設定画面のタッチ。音量・明るさは押した瞬間に変え、戻る・クレジットは離したときに動く
static void touchSettings(int x, int y)
{
  Scene::Settings cur;
  using P = Scene::Settings::Part;
  const P part = screen.hitTest(cur, x, y);
  if (part == P::None)
  {
    return;
  }
  pressedSetting = int(part);
  switch (part)
  {
  case P::volDown:
  case P::volUp:
    volumeLevel += part == P::volUp ? 1 : -1;
    volumeLevel = volumeLevel < 1 ? 1 : (volumeLevel > 10 ? 10 : volumeLevel);
    applyVolume();
    prefs.putUChar("volume", uint8_t(volumeLevel));
    sound.playEffect(0); // 音量の確かめ用に、1 番目の効果音を鳴らす
    break;
  case P::briDown:
  case P::briUp:
    brightnessLevel += part == P::briUp ? 1 : -1;
    brightnessLevel = brightnessLevel < 1 ? 1 : (brightnessLevel > 10 ? 10 : brightnessLevel);
    applyBrightness();
    prefs.putUChar("bright", uint8_t(brightnessLevel));
    break;
  default:
    break;
  }
}

static void touchCredits(int x, int y)
{
  Scene::Message cur;
  if (screen.hitTest(cur, x, y) == Scene::Message::Part::okBtn)
  {
    pressedOk = true;
  }
}

static void release()
{
  using P = Scene::Settings::Part;
  if (pressedSetting == int(P::backBtn))
  {
    page = Page::Drive;
  }
  else if (pressedSetting == int(P::creditBtn))
  {
    page = Page::Credits;
  }
  if (pressedOk)
  {
    page = Page::Settings;
  }
  pressedSetting = -1;
  pressedOk = false;
  if (pressedTrain)
  {
    page = Page::TrainSelect;
  }
  if (pressedCard >= 0)
  {
    if (pressedCard != sound.presetIndex())
    {
      sound.selectPreset(pressedCard);
      prefs.putUChar("preset", uint8_t(pressedCard));
    }
    page = Page::Drive;
  }
  if (pressedBack)
  {
    page = Page::Drive;
  }
  pressedFx = -1;
  pressedStop = false;
  pressedTrain = false;
  pressedBack = false;
  pressedCard = -1;
}

void setup()
{
  M5.begin();
  Serial.begin(115200);
  M5.Speaker.begin();
  prefs.begin("soundbox");
  volumeLevel = prefs.getUChar("volume", volumeLevel);
  brightnessLevel = prefs.getUChar("bright", brightnessLevel);
  applyVolume();
  applyBrightness();
  M5.Touch.setHoldThresh(1000); // 上の帯の長押し（設定画面を開く）

  // Tab5 の標準の向きは縦（720x1280）。横向きにしてから begin() で基準の向きとして覚えさせる
  M5.Display.setRotation(1);
  screen.begin();
  screen.setupTiles(1280 * 180 * 2); // PSRAM に 4 分割。描画は約 216ms（内蔵 RAM 100KB の 18 分割とほぼ同じ）

  sensor.begin();
  sound.begin(prefs.getUChar("preset", 0));
  Serial.println("# NGaugeSoundBox");
  Serial.println("t_ms,mA,state,level");
  draw();
}

void loop()
{
  M5.update();
  bool dirty = false;

  const auto t = M5.Touch.getDetail();
  if (t.wasPressed())
  {
    switch (page)
    {
    case Page::Drive:
      touchDrive(t.x, t.y);
      break;
    case Page::TrainSelect:
      touchTrainSelect(t.x, t.y);
      break;
    case Page::Settings:
      touchSettings(t.x, t.y);
      break;
    case Page::Credits:
      touchCredits(t.x, t.y);
      break;
    }
    dirty = true;
  }
  // 運転画面の上の帯を 1 秒長押しすると、設定画面を開く
  if (t.wasHold() && page == Page::Drive)
  {
    Scene::Drive cur;
    if (screen.hitTest(cur, t.base_x, t.base_y) == Scene::Drive::Part::topBar)
    {
      page = Page::Settings;
      dirty = true;
    }
  }
  if (t.wasReleased())
  {
    release();
    dirty = true;
  }

  // 走行状態: 変わったときだけ画面全体を描き直す。メーターは 0.2 秒ごとに単独で描く
  const auto s = sensor.snapshot();
  sound.setDrive(s.state, s.level, s.ok);
  if (page == Page::Drive && (s.ok != lastSnap.ok || s.state != lastSnap.state))
  {
    dirty = true;
  }
  lastSnap = s;
  // 設定画面の電流モニタは 1 秒ごとに描き直す（画面全体の描き直しは約 0.2 秒かかるため控えめに）
  static uint32_t lastMonitorMs = 0;
  if (page == Page::Settings && pressedSetting < 0 && millis() - lastMonitorMs >= 1000)
  {
    lastMonitorMs = millis();
    dirty = true;
  }
  static uint32_t lastMeterMs = 0;
  if (!dirty && millis() - lastMeterMs >= 200)
  {
    lastMeterMs = millis();
    drawMeter();
  }

  if (dirty)
  {
    draw();
  }
  delay(5);
}
