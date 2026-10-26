// E1: KATO パワーパック入力電流ロガー
// U200 (Unit INA226-10A) の値を 10ms ごとに集計して Serial に CSV で出す。
// 画面をタップするとマーカー番号が増える（「ここで発進」などの目印用）。
// Serial に 'r' を送ると RAW モード（シャント電圧だけを最速で変換し、1 サンプルずつ出す）、
// 'w' で通常の集計モードに戻る。

#include <M5Unified.h>

// U200 (Unit INA226-10A)
static constexpr uint8_t INA226_ADDR = 0x41;
static constexpr float SHUNT_OHMS = 0.005f;
static constexpr uint32_t I2C_FREQ = 400000;

static constexpr uint8_t REG_CONFIG = 0x00;
static constexpr uint8_t REG_SHUNT = 0x01;
static constexpr uint8_t REG_BUS = 0x02;
static constexpr uint8_t REG_MFG_ID = 0xFE;

// AVG=1, VBUSCT=140us, VSHCT=588us, shunt+bus continuous
static constexpr uint16_t INA226_CONFIG = 0x4000 | (0 << 9) | (0 << 6) | (3 << 3) | 7;
// RAW モード: AVG=1, VSHCT=140us, shunt only continuous
static constexpr uint16_t INA226_CONFIG_RAW = 0x4000 | (0 << 9) | (0 << 6) | (0 << 3) | 5;

static constexpr uint32_t SAMPLE_US = 1000;
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

static bool sensorOk = false;
static bool rawMode = false;
static uint32_t marker = 0;

static uint32_t windowStartMs = 0;
static uint32_t lastSampleUs = 0;
static float sumMa = 0;
static float minMa = 0;
static float maxMa = 0;
static uint32_t count = 0;
static float lastBusV = 0;
static float lastMeanMa = 0;

static void resetWindow()
{
  sumMa = 0;
  minMa = 1e9f;
  maxMa = -1e9f;
  count = 0;
}

static void drawStatus()
{
  M5.Display.startWrite();
  M5.Display.setCursor(20, 20);
  M5.Display.setTextSize(3);
  if (!sensorOk)
  {
    M5.Display.setTextColor(TFT_RED, TFT_BLACK);
    M5.Display.printf("U200 not found (0x%02X)   ", INA226_ADDR);
  }
  else
  {
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.printf("E1 Current Logger\n\n");
    M5.Display.setTextSize(6);
    M5.Display.printf("  %7.1f mA  \n", lastMeanMa);
    M5.Display.setTextSize(3);
    M5.Display.printf("\n  bus %5.2f V   marker %lu   \n", lastBusV, (unsigned long)marker);
    M5.Display.printf("\n  tap screen to add marker");
  }
  M5.Display.endWrite();
}

void setup()
{
  M5.begin();
  Serial.begin(115200);
  M5.Display.fillScreen(TFT_BLACK);

  // Grove (Port A) の 5V 出力を有効にする（U200 の電源）
  M5.Power.setExtOutput(true);
  delay(100);
  M5.Ex_I2C.begin();
  uint16_t mfg = 0;
  sensorOk = readReg16(REG_MFG_ID, mfg) && mfg == 0x5449 && writeReg16(REG_CONFIG, INA226_CONFIG);

  Serial.println("# E1CurrentLogger");
  Serial.println("t_ms,mean_mA,min_mA,max_mA,bus_V,samples,marker");
  resetWindow();
  windowStartMs = millis();
  lastSampleUs = micros();
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
    // 見つからない間は Ex_I2C をスキャンして、見えているアドレスを表示する
    static uint32_t lastScanMs = 0;
    if (millis() - lastScanMs >= 1000)
    {
      lastScanMs = millis();
      bool found[128] = {};
      M5.Ex_I2C.scanID(found);
      char list[200] = "";
      for (int a = 1; a < 127; ++a)
      {
        if (found[a])
        {
          snprintf(list + strlen(list), sizeof(list) - strlen(list), " %02X", a);
        }
      }
      Serial.printf("# scan Ex_I2C SDA=%d SCL=%d:%s\n", (int)M5.Ex_I2C.getSDA(), (int)M5.Ex_I2C.getSCL(), list);
      M5.Display.setCursor(20, 80);
      M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
      M5.Display.printf("Ex_I2C SDA=%d SCL=%d\nfound:%s          ", (int)M5.Ex_I2C.getSDA(), (int)M5.Ex_I2C.getSCL(), list);
      uint16_t mfg = 0;
      sensorOk = readReg16(REG_MFG_ID, mfg) && mfg == 0x5449 && writeReg16(REG_CONFIG, INA226_CONFIG);
      if (sensorOk)
      {
        M5.Display.fillScreen(TFT_BLACK);
        resetWindow();
        windowStartMs = millis();
        lastSampleUs = micros();
      }
    }
    delay(10);
    return;
  }

  while (Serial.available())
  {
    int c = Serial.read();
    if (c == 'r' && !rawMode)
    {
      rawMode = true;
      writeReg16(REG_CONFIG, INA226_CONFIG_RAW);
      Serial.println("# raw mode");
      Serial.println("t_us,mA,marker");
      M5.Display.fillScreen(TFT_BLACK);
      M5.Display.setCursor(20, 20);
      M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
      M5.Display.print("RAW mode (send 'w' to exit)");
    }
    else if (c == 'w' && rawMode)
    {
      rawMode = false;
      writeReg16(REG_CONFIG, INA226_CONFIG);
      Serial.println("# window mode");
      Serial.println("t_ms,mean_mA,min_mA,max_mA,bus_V,samples,marker");
      M5.Display.fillScreen(TFT_BLACK);
      resetWindow();
      windowStartMs = millis();
      lastSampleUs = micros();
    }
  }

  if (rawMode)
  {
    // 描画は止めて、読めるだけ読む（同じ変換結果を重複して読むことがある）
    uint16_t raw;
    if (readReg16(REG_SHUNT, raw))
    {
      Serial.printf("%lu,%.1f,%lu\n", (unsigned long)micros(), shuntRawToMilliAmps(int16_t(raw)), (unsigned long)marker);
    }
    return;
  }

  uint32_t nowUs = micros();
  if (nowUs - lastSampleUs >= SAMPLE_US)
  {
    lastSampleUs += SAMPLE_US;
    uint16_t raw;
    if (readReg16(REG_SHUNT, raw))
    {
      float ma = shuntRawToMilliAmps(int16_t(raw));
      sumMa += ma;
      minMa = min(minMa, ma);
      maxMa = max(maxMa, ma);
      count++;
    }
  }

  uint32_t nowMs = millis();
  if (nowMs - windowStartMs >= WINDOW_MS)
  {
    windowStartMs += WINDOW_MS;
    uint16_t busRaw;
    if (readReg16(REG_BUS, busRaw))
    {
      // bus LSB = 1.25mV
      lastBusV = busRaw * 1.25e-3f;
    }
    if (count > 0)
    {
      lastMeanMa = sumMa / count;
      Serial.printf("%lu,%.1f,%.1f,%.1f,%.3f,%lu,%lu\n", (unsigned long)nowMs, lastMeanMa, minMa, maxMa, lastBusV,
                    (unsigned long)count, (unsigned long)marker);
    }
    resetWindow();
  }

  static uint32_t lastDrawMs = 0;
  if (nowMs - lastDrawMs >= 200)
  {
    lastDrawMs = nowMs;
    drawStatus();
  }
}
