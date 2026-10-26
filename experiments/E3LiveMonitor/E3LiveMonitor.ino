// E3: 走行状態判定のライブ確認
// U200 (Unit INA226-10A) の電流を 10ms ごとに平均して TrainSense に渡し、
// 判定した状態と level を画面に出す。同じ値を Serial に CSV で出すので、
// tools/capture_serial.py で記録すれば、そのまま tests/train_state の入力に使える。
// 画面をタップするとマーカー番号が増える。

#include <M5Unified.h>
#include <TrainSense.h>

// U200 (Unit INA226-10A)
static constexpr uint8_t INA226_ADDR = 0x41;
static constexpr float SHUNT_OHMS = 0.005f;
static constexpr uint32_t I2C_FREQ = 400000;

static constexpr uint8_t REG_CONFIG = 0x00;
static constexpr uint8_t REG_SHUNT = 0x01;
static constexpr uint8_t REG_MFG_ID = 0xFE;

// AVG=1, VSHCT=140us, shunt only continuous（PWM を均すため、できるだけ多く読む）
static constexpr uint16_t INA226_CONFIG = 0x4000 | (0 << 9) | (0 << 6) | (0 << 3) | 5;

static constexpr uint32_t WINDOW_MS = 10;

static bool writeReg16(uint8_t reg, uint16_t value)
{
  uint8_t buf[2] = {uint8_t(value >> 8), uint8_t(value & 0xFF)};
  return M5.Ex_I2C.writeRegister(INA226_ADDR, reg, buf, 2, I2C_FREQ);
}

static bool readReg16(uint8_t reg, uint16_t &value)
{
  uint8_t buf[2];
  if (!M5.Ex_I2C.readRegister(INA226_ADDR, reg, buf, 2, I2C_FREQ))
  {
    return false;
  }
  value = (uint16_t(buf[0]) << 8) | buf[1];
  return true;
}

static float shuntRawToMilliAmps(int16_t raw)
{
  // shunt LSB = 2.5uV
  return raw * 2.5e-6f / SHUNT_OHMS * 1000.0f;
}

static bool probeSensor()
{
  uint16_t mfg = 0;
  return readReg16(REG_MFG_ID, mfg) && mfg == 0x5449 && writeReg16(REG_CONFIG, INA226_CONFIG);
}

static TrainSense sense;
static bool sensorOk = false;
static uint32_t marker = 0;
static uint32_t windowStartMs = 0;
static float sumMa = 0;
static uint32_t count = 0;

static uint32_t stateColor(TrainSense::State s)
{
  switch (s)
  {
  case TrainSense::State::Stop:
    return 0x9a9a9au;
  case TrainSense::State::Start:
    return 0xd4a645u;
  case TrainSense::State::Run:
    return 0x4f8a5bu;
  case TrainSense::State::Brake:
    return 0xb5473fu;
  }
  return 0xffffffu;
}

static const char *stateLabel(TrainSense::State s)
{
  switch (s)
  {
  case TrainSense::State::Stop:
    return "停止";
  case TrainSense::State::Start:
    return "発進";
  case TrainSense::State::Run:
    return "走行中";
  case TrainSense::State::Brake:
    return "減速";
  }
  return "?";
}

static void drawStatus()
{
  auto &d = M5.Display;
  d.startWrite();
  if (!sensorOk)
  {
    d.fillScreen(TFT_BLACK);
    d.setTextColor(TFT_RED);
    d.setTextDatum(middle_center);
    d.drawString("U200 が見つかりません", d.width() / 2, d.height() / 2);
    d.endWrite();
    return;
  }

  const auto s = sense.state();
  d.fillRect(0, 0, d.width(), 260, d.color888(0x2f, 0x3a, 0x45));
  d.fillRoundRect(40, 40, 480, 180, 24, stateColor(s));
  d.setTextColor(TFT_WHITE);
  d.setTextDatum(middle_center);
  d.setTextSize(2);
  d.drawString(stateLabel(s), 280, 130);
  d.setTextSize(1);
  d.setTextDatum(middle_left);
  d.drawString("level", 580, 70);
  d.fillRoundRect(580, 100, 640, 80, 16, d.color888(0x1c, 0x24, 0x2c));
  d.fillRoundRect(580, 100, int(640 * sense.level()), 80, 16, d.color888(0x4f, 0x8a, 0x5b));

  char buf[96];
  d.fillRect(0, 260, d.width(), d.height() - 260, d.color888(0xec, 0xea, 0xe4));
  d.setTextColor(d.color888(0x2a, 0x2a, 0x2a));
  snprintf(buf, sizeof(buf), "電流 %6.1f mA   基準 %5.1f mA", sense.current(), sense.baseline());
  d.drawString(buf, 60, 340);
  snprintf(buf, sizeof(buf), "傾き %+6.0f mA/s   level %.2f", sense.slope(), sense.level());
  d.drawString(buf, 60, 420);
  snprintf(buf, sizeof(buf), "marker %lu（画面タップで +1）", (unsigned long)marker);
  d.drawString(buf, 60, 500);
  d.endWrite();
}

void setup()
{
  M5.begin();
  Serial.begin(115200);
  M5.Display.setFont(&fonts::lgfxJapanGothicP_40);

  // Grove (Port A) の 5V 出力を有効にする（U200 の電源）
  M5.Power.setExtOutput(true);
  delay(100);
  M5.Ex_I2C.begin();
  sensorOk = probeSensor();

  Serial.println("# E3LiveMonitor");
  Serial.println("t_ms,mA,state,level,marker");
  M5.Display.fillScreen(TFT_BLACK);
  windowStartMs = millis();
  drawStatus();
}

void loop()
{
  M5.update();
  if (M5.Touch.getDetail().wasPressed())
  {
    marker++;
    Serial.printf("# marker %lu\n", (unsigned long)marker);
  }

  if (!sensorOk)
  {
    static uint32_t lastProbeMs = 0;
    if (millis() - lastProbeMs >= 1000)
    {
      lastProbeMs = millis();
      sensorOk = probeSensor();
      if (sensorOk)
      {
        M5.Display.fillScreen(TFT_BLACK);
        windowStartMs = millis();
        sumMa = 0;
        count = 0;
      }
      drawStatus();
    }
    delay(10);
    return;
  }

  uint16_t raw;
  if (readReg16(REG_SHUNT, raw))
  {
    sumMa += shuntRawToMilliAmps(int16_t(raw));
    count++;
  }

  const uint32_t nowMs = millis();
  if (nowMs - windowStartMs >= WINDOW_MS)
  {
    // 描画などで遅れたときは、遅れを引きずらずに今の時刻から数え直す
    windowStartMs = nowMs;
    if (count > 0)
    {
      const float mA = sumMa / count;
      sense.update(nowMs, mA);
      Serial.printf("%lu,%.1f,%s,%.3f,%lu\n", (unsigned long)nowMs, mA, TrainSense::stateName(sense.state()),
                    sense.level(), (unsigned long)marker);
    }
    sumMa = 0;
    count = 0;
  }

  static uint32_t lastDrawMs = 0;
  if (nowMs - lastDrawMs >= 200)
  {
    lastDrawMs = nowMs;
    drawStatus();
  }
}
