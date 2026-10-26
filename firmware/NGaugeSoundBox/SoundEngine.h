#pragma once

// サウンドの合成と再生。
// - 走行音（チャンネル 0）: モーター音・走行ノイズ・レールの継ぎ目の音を、
//   TrainSense の状態と level に合わせて少しずつ合成し、M5.Speaker に順に積む。
// - 効果音（チャンネル 1〜）: 起動時に合成して PSRAM に置き、ボタンで鳴らす。
// 今は音をすべてプログラムで合成している。音源パック（音声ファイル）への差し替えはあとで行う。

#include <M5Unified.h>
#include <TrainSense.h>
#include <esp_heap_caps.h>
#include <math.h>

class SoundEngine
{
public:
  static constexpr uint32_t kRate = 24000;
  static constexpr int kEffectCount = 6;

  void begin()
  {
    renderEffects();
    for (auto &b : _chunks)
    {
      b = static_cast<int16_t *>(heap_caps_malloc(kChunk * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    xTaskCreatePinnedToCore(taskEntry, "sound", 4096, this, 10, nullptr, 0);
  }

  // 走行状態を渡す（UI 側から呼ぶ）
  void setDrive(TrainSense::State s, float level, bool sensorOk)
  {
    _state = s;
    _level = level;
    _sensorOk = sensorOk;
    if (s == TrainSense::State::Stop)
    {
      _muted = false;
    }
  }

  // 電流検出がないときの手動の発車・停車
  void setManualRunning(bool on) { _manual = on; }

  void playEffect(int i)
  {
    if (i < 0 || i >= kEffectCount || !_fx[i])
    {
      return;
    }
    M5.Speaker.playRaw(_fx[i], _fxLen[i], kRate, false, 1, 1 + i, true);
  }

  // 全停止: 効果音を止め、走行音は次に停止するまで黙らせる
  void stopAll()
  {
    for (int i = 0; i < kEffectCount; ++i)
    {
      M5.Speaker.stop(1 + i);
    }
    _muted = true;
  }

private:
  static constexpr size_t kChunk = 480; // 20ms
  static constexpr int kChunkCount = 3;

  static void taskEntry(void *p) { static_cast<SoundEngine *>(p)->taskLoop(); }

  void taskLoop()
  {
    int next = 0;
    for (;;)
    {
      while (M5.Speaker.isPlaying(0) < 2)
      {
        renderRunning(_chunks[next], kChunk);
        M5.Speaker.playRaw(_chunks[next], kChunk, kRate, false, 1, 0, false);
        next = (next + 1) % kChunkCount;
      }
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }

  // ---- 走行音 ----

  float noise()
  {
    _rng = _rng * 1664525u + 1013904223u;
    return int32_t(_rng) * (1.0f / 2147483648.0f);
  }

  void renderRunning(int16_t *out, size_t n)
  {
    // 目標値（状態ごとの音量）
    bool moving;
    float targetLevel;
    float motorGain;
    if (_sensorOk)
    {
      moving = _state != TrainSense::State::Stop && !_muted;
      targetLevel = _level;
      switch (_state)
      {
      case TrainSense::State::Accel:
      case TrainSense::State::Start:
        motorGain = 0.30f;
        break;
      case TrainSense::State::Brake:
        motorGain = 0.20f;
        break;
      default:
        motorGain = 0.10f;
        break;
      }
    }
    else
    {
      moving = _manual && !_muted;
      targetLevel = 0.5f;
      motorGain = 0.12f;
    }
    const float targetGain = moving ? 1.0f : 0.0f;

    for (size_t i = 0; i < n; ++i)
    {
      // 値の急な変化で音がぶつぶつ切れないよう、少しずつ近づける
      _lv += (targetLevel - _lv) * 0.00005f;
      _gain += (targetGain - _gain) * 0.0002f;
      _motorGain += (motorGain - _motorGain) * 0.0002f;

      // モーター音（level に合わせて高くなる）
      const float f = 180.0f + 1300.0f * _lv;
      _ph1 += f / kRate;
      _ph1 -= floorf(_ph1);
      _ph2 += f * 2.02f / kRate;
      _ph2 -= floorf(_ph2);
      const float motor = (sinf(_ph1 * 2 * PI) * 0.7f + sinf(_ph2 * 2 * PI) * 0.3f) * _motorGain * (0.3f + 0.7f * _lv);

      // 走行ノイズ（低めのざわざわ）
      _lp += (noise() - _lp) * 0.04f;
      const float roll = _lp * (0.6f + 1.4f * _lv);

      // レールの継ぎ目（ガタン・ゴトン）。速いほど間隔が短い
      const float sp = _lv < 0.06f ? 0.06f : _lv;
      const float pairInterval = 0.7f / sp;
      const float gap = pairInterval * 0.18f;
      _jointT += 1.0f / kRate;
      if (_jointT >= pairInterval)
      {
        _jointT = 0;
        _clackT = 0;
        _clack2 = true;
      }
      if (_clack2 && _jointT >= gap)
      {
        _clack2 = false;
        _clackT = 0;
      }
      float clack = 0;
      if (_clackT < 0.08f)
      {
        _clackT += 1.0f / kRate;
        _cph += 85.0f / kRate;
        _cph -= floorf(_cph);
        const float env = expf(-_clackT * 60.0f);
        _clp += (noise() - _clp) * 0.15f;
        clack = (sinf(_cph * 2 * PI) * 0.6f + _clp * 1.2f) * env * (0.35f + 0.4f * _lv);
      }

      float s = (motor + roll * 0.25f + clack) * _gain;
      s = s / (1.0f + fabsf(s)); // ソフトクリップ
      out[i] = int16_t(s * 26000.0f);
    }
  }

  // ---- 効果音（起動時に合成する） ----

  int16_t *alloc(size_t n, int idx)
  {
    _fxLen[idx] = n;
    _fx[idx] = static_cast<int16_t *>(heap_caps_calloc(n, sizeof(int16_t), MALLOC_CAP_SPIRAM));
    return _fx[idx];
  }

  static void addTone(int16_t *buf, size_t len, float startSec, float durSec, float freq, float amp, float decay)
  {
    const size_t s0 = size_t(startSec * kRate);
    const size_t n = size_t(durSec * kRate);
    for (size_t i = 0; i < n && s0 + i < len; ++i)
    {
      const float t = float(i) / kRate;
      const float env = expf(-t * decay) * (t < 0.005f ? t / 0.005f : 1.0f);
      const float v = sinf(2 * PI * freq * t) * amp * env;
      int32_t x = buf[s0 + i] + int32_t(v * 32767);
      buf[s0 + i] = int16_t(x > 32767 ? 32767 : (x < -32768 ? -32768 : x));
    }
  }

  void addHiss(int16_t *buf, size_t len, float startSec, float durSec, float amp)
  {
    const size_t s0 = size_t(startSec * kRate);
    const size_t n = size_t(durSec * kRate);
    float prev = 0;
    for (size_t i = 0; i < n && s0 + i < len; ++i)
    {
      const float t = float(i) / kRate;
      const float env = (t < 0.02f ? t / 0.02f : 1.0f) * expf(-t * 3.0f / durSec);
      const float w = noise();
      const float hp = w - prev; // 高い音だけ残す
      prev = w;
      int32_t x = buf[s0 + i] + int32_t(hp * amp * env * 16000);
      buf[s0 + i] = int16_t(x > 32767 ? 32767 : (x < -32768 ? -32768 : x));
    }
  }

  void renderEffects()
  {
    // 0: 警笛（2 つの音を重ねた電子警笛）
    {
      const float dur = 1.2f;
      const size_t n = size_t(dur * kRate);
      int16_t *b = alloc(n, 0);
      for (size_t i = 0; b && i < n; ++i)
      {
        const float t = float(i) / kRate;
        const float env = (t < 0.04f ? t / 0.04f : 1.0f) * (t > dur - 0.15f ? (dur - t) / 0.15f : 1.0f);
        float v = 0;
        for (int h = 1; h <= 5; ++h)
        {
          v += sinf(2 * PI * 440.0f * h * t) / h + sinf(2 * PI * 554.0f * h * t) / h;
        }
        b[i] = int16_t(v * 0.12f * env * 32767);
      }
    }
    // 1: ドア（ドアチャイム 3 回のあと、空気の音）
    {
      const size_t n = size_t(2.4f * kRate);
      int16_t *b = alloc(n, 1);
      if (b)
      {
        for (int k = 0; k < 3; ++k)
        {
          addTone(b, n, k * 0.5f, 0.45f, 1318.5f, 0.25f, 6.0f);
          addTone(b, n, k * 0.5f + 0.22f, 0.45f, 1046.5f, 0.25f, 6.0f);
        }
        addHiss(b, n, 1.6f, 0.8f, 0.5f);
      }
    }
    // 2: 発車ベル（電鈴）
    {
      const float dur = 3.0f;
      const size_t n = size_t(dur * kRate);
      int16_t *b = alloc(n, 2);
      for (size_t i = 0; b && i < n; ++i)
      {
        const float t = float(i) / kRate;
        const float strike = fmodf(t, 1.0f / 22.0f) * 22.0f; // 1 秒に 22 回たたく
        const float env = (1.0f - strike * 0.6f) * (t > dur - 0.2f ? (dur - t) / 0.2f : 1.0f);
        const float v = sinf(2 * PI * 2350.0f * t) * 0.6f + sinf(2 * PI * 3420.0f * t) * 0.4f;
        b[i] = int16_t(v * 0.35f * env * 32767);
      }
    }
    // 3: 案内チャイム（ピンポンパンポン）
    {
      const size_t n = size_t(2.0f * kRate);
      int16_t *b = alloc(n, 3);
      if (b)
      {
        const float notes[] = {523.3f, 659.3f, 784.0f, 1046.5f};
        for (int k = 0; k < 4; ++k)
        {
          addTone(b, n, k * 0.32f, 1.0f, notes[k], 0.3f, 2.5f);
        }
      }
    }
    // 4: 駅到着（下りのチャイム → ブレーキの空気の音 → ドアチャイム）
    {
      const size_t n = size_t(4.0f * kRate);
      int16_t *b = alloc(n, 4);
      if (b)
      {
        const float notes[] = {1046.5f, 784.0f, 659.3f, 523.3f};
        for (int k = 0; k < 4; ++k)
        {
          addTone(b, n, k * 0.32f, 1.0f, notes[k], 0.3f, 2.5f);
        }
        addHiss(b, n, 1.6f, 0.7f, 0.6f);
        for (int k = 0; k < 2; ++k)
        {
          addTone(b, n, 2.6f + k * 0.5f, 0.45f, 1318.5f, 0.25f, 6.0f);
          addTone(b, n, 2.6f + k * 0.5f + 0.22f, 0.45f, 1046.5f, 0.25f, 6.0f);
        }
      }
    }
    // 5: ブレーキ（空気を抜く音）
    {
      const size_t n = size_t(1.0f * kRate);
      int16_t *b = alloc(n, 5);
      if (b)
      {
        addHiss(b, n, 0.0f, 1.0f, 0.8f);
      }
    }
  }

  int16_t *_chunks[kChunkCount] = {};
  int16_t *_fx[kEffectCount] = {};
  size_t _fxLen[kEffectCount] = {};

  volatile TrainSense::State _state = TrainSense::State::Stop;
  volatile float _level = 0;
  volatile bool _sensorOk = false;
  volatile bool _manual = false;
  volatile bool _muted = false;

  uint32_t _rng = 12345;
  float _lv = 0, _gain = 0, _motorGain = 0;
  float _ph1 = 0, _ph2 = 0, _lp = 0;
  float _jointT = 0, _clackT = 1, _cph = 0, _clp = 0;
  bool _clack2 = false;
};
