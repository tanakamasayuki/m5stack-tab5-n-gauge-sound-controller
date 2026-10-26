// Nゲージ サウンドボックス（M5Stack Tab5）
// パワーパックの入力電流から走行状態を判定して走行音を鳴らし、
// 画面のボタンで効果音を鳴らす。
//
// いまの段階:
// - 画面は運転画面（Drive）だけ。車両選択・設定は未実装
// - 音はすべて SoundEngine で合成している。音源パックはまだ読まない
// - 電流検出がないときは、メーター部分をタップすると手動で発車・停車する

#include <M5Unified.h>
#include <LGFXVirtualCanvas.h> // LGFXScreenBuilder より前に include すると、分割ダブルバッファで描く
#include <LGFXScreenBuilder.h>
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

static const char *const kFxLabels[SoundEngine::kEffectCount] = {"警笛", "ドア", "発車ベル", "案内チャイム", "駅到着", "ブレーキ"};

static CurrentSensor::Snapshot lastSnap;
static bool manualRunning = false;
static int pressedFx = -1; // 押している効果音ボタン（-1 = なし）
static bool pressedStop = false;
static bool pressedTrain = false;

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

static void draw()
{
  Scene::Drive d;
  d.trainName = "E231系 通勤電車（合成音）";
  d.sensorStatus = lastSnap.ok ? "● 電流検出 OK" : "○ 電流検出なし";
  d.stateText = stateLabel(lastSnap);
  const char *labels[] = {kFxLabels[0], kFxLabels[1], kFxLabels[2], kFxLabels[3], kFxLabels[4], kFxLabels[5]};
  d.fx1Label = labels[0];
  d.fx2Label = labels[1];
  d.fx3Label = labels[2];
  d.fx4Label = labels[3];
  d.fx5Label = labels[4];
  d.fx6Label = labels[5];
  lgfxsb::OptBool *pressed[] = {&d.fx1PressedVisible, &d.fx2PressedVisible, &d.fx3PressedVisible,
                                &d.fx4PressedVisible, &d.fx5PressedVisible, &d.fx6PressedVisible};
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

void setup()
{
  M5.begin();
  Serial.begin(115200);
  M5.Speaker.begin();
  M5.Speaker.setVolume(180);

  // Tab5 の標準の向きは縦（720x1280）。横向きにしてから begin() で基準の向きとして覚えさせる
  M5.Display.setRotation(1);
  screen.begin();

  screen.setupTiles(1280 * 180 * 2); // PSRAM に 4 分割。描画は約 216ms（内蔵 RAM 100KB の 18 分割とほぼ同じ）
  sensor.begin();
  sound.begin();
  Serial.println("# NGaugeSoundBox");
  Serial.println("t_ms,mA,state,level");
  draw();
}

void loop()
{
  M5.update();
  bool dirty = false;

  // タッチ: 触れた瞬間に鳴らす
  const auto t = M5.Touch.getDetail();
  if (t.wasPressed())
  {
    Scene::Drive cur;
    const auto part = screen.hitTest(cur, t.x, t.y);
    const int fx = fxIndex(part);
    if (fx >= 0)
    {
      sound.playEffect(fx);
      pressedFx = fx;
      dirty = true;
    }
    else if (part == Scene::Drive::Part::stopBtn)
    {
      sound.stopAll();
      manualRunning = false;
      sound.setManualRunning(false);
      pressedStop = true;
      dirty = true;
    }
    else if (part == Scene::Drive::Part::trainBtn)
    {
      pressedTrain = true; // 車両選択は未実装
      dirty = true;
    }
    else if (part == Scene::Drive::Part::meterFrame && !lastSnap.ok)
    {
      manualRunning = !manualRunning;
      sound.setManualRunning(manualRunning);
      dirty = true;
    }
  }
  if (t.wasReleased() && (pressedFx >= 0 || pressedStop || pressedTrain))
  {
    pressedFx = -1;
    pressedStop = false;
    pressedTrain = false;
    dirty = true;
  }

  // 走行状態: 変わったときだけ画面全体を描き直す。メーターは 0.2 秒ごとに単独で描く
  const auto s = sensor.snapshot();
  sound.setDrive(s.state, s.level, s.ok);
  if (s.ok != lastSnap.ok || s.state != lastSnap.state)
  {
    dirty = true;
  }
  lastSnap = s;
  static uint32_t lastMeterMs = 0;
  if (!dirty && millis() - lastMeterMs >= 200)
  {
    lastMeterMs = millis();
    drawMeter();
  }

  if (dirty)
  {
    const uint32_t t0 = millis();
    draw();
    static uint32_t lastReportMs = 0;
    if (millis() - lastReportMs >= 2000)
    {
      lastReportMs = millis();
      Serial.printf("# draw %lu ms\n", (unsigned long)(millis() - t0));
    }
  }
  delay(5);
}
