#pragma once

// U200 (Unit INA226-10A) でパワーパックの入力電流を読み、TrainSense で走行状態を判定する。
// 読み取りは専用タスク（コア 0）で回し、画面の描き換えで止まらないようにする。
// 結果は snapshot() で取り出す。

#include <M5Unified.h>
#include <TrainSense.h>

class CurrentSensor
{
public:
  struct Snapshot
  {
    bool ok = false;
    TrainSense::State state = TrainSense::State::Stop;
    float level = 0, current = 0, baseline = 0;
  };

  void begin()
  {
    // Grove (Port A) の 5V 出力を有効にする（U200 の電源）
    M5.Power.setExtOutput(true);
    delay(100);
    M5.Ex_I2C.begin();
    xTaskCreatePinnedToCore(taskEntry, "sense", 4096, this, 5, nullptr, 0);
  }

  Snapshot snapshot()
  {
    portENTER_CRITICAL(&_mux);
    Snapshot s = _snap;
    portEXIT_CRITICAL(&_mux);
    return s;
  }

  // 10ms ごとの平均電流を Serial に CSV で出す（tools/capture_serial.py で記録できる）
  void setLogging(bool on) { _logging = on; }

private:
  static constexpr uint8_t kAddr = 0x41;
  static constexpr float kShuntOhms = 0.005f;
  static constexpr uint32_t kI2cFreq = 400000;
  // AVG=1, VSHCT=140us, shunt only continuous（PWM を均すため、できるだけ多く読む）
  static constexpr uint16_t kConfig = 0x4000 | (0 << 9) | (0 << 6) | (0 << 3) | 5;
  static constexpr uint32_t kWindowMs = 10;
  // 1 区切りに必要な最小サンプル数。足りなければ次の区切りに持ち越す
  static constexpr uint32_t kMinSamples = 20;

  static void taskEntry(void *p) { static_cast<CurrentSensor *>(p)->taskLoop(); }

  bool writeReg16(uint8_t reg, uint16_t value)
  {
    uint8_t buf[2] = {uint8_t(value >> 8), uint8_t(value & 0xFF)};
    return M5.Ex_I2C.writeRegister(kAddr, reg, buf, 2, kI2cFreq);
  }

  bool readReg16(uint8_t reg, uint16_t &value)
  {
    uint8_t buf[2];
    if (!M5.Ex_I2C.readRegister(kAddr, reg, buf, 2, kI2cFreq))
    {
      return false;
    }
    value = (uint16_t(buf[0]) << 8) | buf[1];
    return true;
  }

  bool probe()
  {
    uint16_t mfg = 0;
    return readReg16(0xFE, mfg) && mfg == 0x5449 && writeReg16(0x00, kConfig);
  }

  void publish(bool ok)
  {
    Snapshot s;
    s.ok = ok;
    s.state = _sense.state();
    s.level = _sense.level();
    s.current = _sense.current();
    s.baseline = _sense.baseline();
    portENTER_CRITICAL(&_mux);
    _snap = s;
    portEXIT_CRITICAL(&_mux);
  }

  void taskLoop()
  {
    bool ok = false;
    uint32_t windowStartMs = millis();
    uint32_t loops = 0;
    float sumMa = 0;
    uint32_t count = 0;
    for (;;)
    {
      if (!ok)
      {
        ok = probe();
        if (!ok)
        {
          publish(false);
          vTaskDelay(pdMS_TO_TICKS(1000));
          continue;
        }
        _sense.reset();
        windowStartMs = millis();
        sumMa = 0;
        count = 0;
      }

      uint16_t raw;
      if (readReg16(0x01, raw))
      {
        // shunt LSB = 2.5uV
        sumMa += int16_t(raw) * 2.5e-6f / kShuntOhms * 1000.0f;
        count++;
      }

      const uint32_t nowMs = millis();
      if (nowMs - windowStartMs >= kWindowMs && count >= kMinSamples)
      {
        windowStartMs = nowMs;
        const float mA = sumMa / count;
        sumMa = 0;
        count = 0;
        _sense.update(nowMs, mA);
        publish(true);
        if (_logging)
        {
          Serial.printf("%lu,%.1f,%s,%.3f\n", (unsigned long)nowMs, mA, TrainSense::stateName(_sense.state()),
                        _sense.level());
        }
      }
      // ウォッチドッグに CPU を返す（I2C の読み取りは 1 回あたり 100us 程度）
      if ((++loops & 63) == 0)
      {
        vTaskDelay(1);
      }
    }
  }

  TrainSense _sense;
  Snapshot _snap;
  portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
  volatile bool _logging = true;
};
