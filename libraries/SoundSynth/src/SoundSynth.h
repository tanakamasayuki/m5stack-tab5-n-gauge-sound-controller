#pragma once

// 車両ごとの音の合成（ハードウェアに依存しない部分）。
// - 走行音: renderRunning() が、TrainSense の状態と level に合わせて少しずつ合成する。
// - 効果音: selectPreset() で車両を選んだときに合成してメモリに置き、effect() で取り出す。
// 実機では firmware/NGaugeSoundBox/SoundEngine.h がスピーカーで鳴らす。
// PC では tests/sound_preview が WAV に書き出す。

#include <TrainSense.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

class SoundSynth
{
public:
  static constexpr uint32_t kRate = 24000;
  static constexpr int kEffectCount = 6;

  enum class Running : uint8_t
  {
    Electric, // モーター音・走行ノイズ・継ぎ目の音
    Steam,    // ドラフト音
    Diesel,   // エンジン音（停車中もアイドリング）
  };

  struct Preset
  {
    const char *name;
    const char *kind;
    Running running;
    const char *labels[kEffectCount];
  };

  static constexpr int kPresetCount = 3;
  static const Preset &preset(int i)
  {
    static const Preset kPresets[kPresetCount] = {
        {"通勤電車", "電車", Running::Electric, {"警笛", "ドア", "発車ベル", "案内チャイム", "駅到着", "ブレーキ"}},
        {"蒸気機関車", "SL", Running::Steam, {"汽笛", "短い汽笛", "発車ベル", "安全弁", "ドレン", "駅到着"}},
        {"気動車", "ディーゼル", Running::Diesel, {"警笛", "短い警笛", "ドア", "案内チャイム", "エンジン始動", "ブレーキ"}},
    };
    return kPresets[i < 0 || i >= kPresetCount ? 0 : i];
  }

  // 効果音のメモリの確保と解放（実機では PSRAM を使う）
  using AllocFn = void *(*)(size_t bytes);
  using FreeFn = void (*)(void *p);

  SoundSynth(AllocFn alloc = nullptr, FreeFn release = nullptr)
      : _allocFn(alloc ? alloc : defaultAlloc), _freeFn(release ? release : free)
  {
  }

  // 車両を切り替え、効果音を合成し直す。
  // 呼ぶ前に、古い効果音の再生を止めておくこと（バッファを解放するため）
  void selectPreset(int i)
  {
    _presetIndex = (i < 0 || i >= kPresetCount) ? 0 : i;
    for (int k = 0; k < kEffectCount; ++k)
    {
      _freeFn(_fx[k]);
      _fx[k] = nullptr;
      _fxLen[k] = 0;
    }
    renderEffects(preset(_presetIndex));
    _running = preset(_presetIndex).running;
  }

  int presetIndex() const { return _presetIndex; }

  // 走行状態を渡す（UI 側から呼ぶ）
  void setDrive(TrainSense::State s, float level, bool sensorOk)
  {
    if (_state == TrainSense::State::Stop && s != TrainSense::State::Stop)
    {
      _muted = false; // 全停止のあと、次に発進したら走行音を戻す
    }
    _state = s;
    _level = level;
    _sensorOk = sensorOk;
  }

  // 電流検出がないときの手動の発車・停車
  void setManualRunning(bool on)
  {
    if (on && !_manual)
    {
      _muted = false;
    }
    _manual = on;
  }

  // 効果音（モノラル、kRate）。合成できなかったときは nullptr
  const int16_t *effect(int i, size_t &len) const
  {
    if (i < 0 || i >= kEffectCount)
    {
      len = 0;
      return nullptr;
    }
    len = _fxLen[i];
    return _fx[i];
  }

  // 走行音を黙らせる（全停止）。次に発進したら戻る
  void mute() { _muted = true; }

  // 走行音を n サンプル合成する（モノラル、kRate）
  void renderRunning(int16_t *out, size_t n) { renderRunningImpl(out, n); }

private:
  static void *defaultAlloc(size_t bytes) { return calloc(1, bytes); }
  static constexpr float kPi = 3.14159265f;

  // ---- 走行音 ----

  float noise()
  {
    _rng = _rng * 1664525u + 1013904223u;
    return int32_t(_rng) * (1.0f / 2147483648.0f);
  }

  static float wrap(float ph) { return ph - floorf(ph); }

  void renderRunningImpl(int16_t *out, size_t n)
  {
    // 目標値
    bool moving;
    float targetLevel;
    float drive; // 加速中は大きく、減速中は小さく
    if (_sensorOk)
    {
      moving = _state != TrainSense::State::Stop;
      targetLevel = _level;
      switch (_state)
      {
      case TrainSense::State::Accel:
      case TrainSense::State::Start:
        drive = 1.0f;
        break;
      case TrainSense::State::Brake:
        drive = 0.15f;
        break;
      default:
        drive = 0.45f;
        break;
      }
    }
    else
    {
      moving = _manual;
      targetLevel = 0.5f;
      drive = 0.45f;
    }
    const Running running = _running;
    // 気動車は停車中もアイドリングする
    const float targetGain = _muted ? 0.0f : (moving ? 1.0f : (running == Running::Diesel ? 0.45f : 0.0f));
    if (!moving)
    {
      targetLevel = 0;
      drive = 0;
    }

    for (size_t i = 0; i < n; ++i)
    {
      // 値の急な変化で音がぶつぶつ切れないよう、少しずつ近づける
      _lv += (targetLevel - _lv) * 0.00005f;
      _gain += (targetGain - _gain) * 0.0002f;
      _drive += (drive - _drive) * 0.0001f;

      float s;
      switch (running)
      {
      case Running::Steam:
        s = steamSample();
        break;
      case Running::Diesel:
        s = dieselSample();
        break;
      default:
        s = electricSample();
        break;
      }
      s *= _gain;
      s = s / (1.0f + fabsf(s)); // ソフトクリップ
      out[i] = int16_t(s * 26000.0f);
    }
  }

  // レールの継ぎ目（ガタン・ゴトン）。速いほど間隔が短い
  float jointSample(float amp)
  {
    const float sp = _lv < 0.06f ? 0.06f : _lv;
    const float pairInterval = 0.7f / sp;
    _jointT += 1.0f / kRate;
    if (_jointT >= pairInterval)
    {
      _jointT = 0;
      _clackT = 0;
      _clack2 = true;
    }
    if (_clack2 && _jointT >= pairInterval * 0.18f)
    {
      _clack2 = false;
      _clackT = 0;
    }
    if (_clackT >= 0.08f || _gain < 0.01f || _lv < 0.02f)
    {
      return 0;
    }
    _clackT += 1.0f / kRate;
    _cph = wrap(_cph + 85.0f / kRate);
    _clp += (noise() - _clp) * 0.15f;
    return (sinf(_cph * 2 * kPi) * 0.6f + _clp * 1.2f) * expf(-_clackT * 60.0f) * amp * (0.35f + 0.4f * _lv);
  }

  float electricSample()
  {
    // モーター音（level に合わせて高くなる）
    const float f = 180.0f + 1300.0f * _lv;
    _ph1 = wrap(_ph1 + f / kRate);
    _ph2 = wrap(_ph2 + f * 2.02f / kRate);
    const float motor = (sinf(_ph1 * 2 * kPi) * 0.7f + sinf(_ph2 * 2 * kPi) * 0.3f) * (0.04f + 0.26f * _drive) *
                        (0.3f + 0.7f * _lv) * (_lv > 0.01f ? 1.0f : _lv * 100.0f);
    // 走行ノイズ（低めのざわざわ）
    _lp += (noise() - _lp) * 0.04f;
    const float roll = _lp * (0.6f + 1.4f * _lv) * 0.25f * (_lv > 0.01f ? 1.0f : _lv * 100.0f);
    return motor + roll + jointSample(1.0f);
  }

  float steamSample()
  {
    // ドラフト音: 1 秒あたり 1.5〜9 回。加速中は強く、減速中は弱い
    const float rate = 1.5f + 7.5f * _lv;
    _chuffT += rate / kRate;
    if (_chuffT >= 1.0f)
    {
      _chuffT -= 1.0f;
      _chuffAmp = (_chuffIdx++ & 3) == 0 ? 1.0f : 0.75f; // 4 回に 1 回強い
    }
    const float env = expf(-_chuffT * (8.0f + 10.0f * _lv)) * _chuffAmp;
    const float w = noise();
    _lp += (w - _lp) * 0.25f;
    _lp2 += (_lp - _lp2) * 0.25f;
    const float band = _lp - _lp2; // 中くらいの高さのノイズ
    const float chuff = band * env * (0.6f + 1.6f * _drive) * (_lv > 0.01f ? 1.0f : _lv * 100.0f) * 2.5f;
    return chuff + jointSample(0.6f);
  }

  float dieselSample()
  {
    // エンジン音: 停車中はアイドリング、加速中は回転が上がる
    const float throttle = _drive > _lv ? _drive : _lv;
    const float f0 = 28.0f + 40.0f * throttle;
    _ph1 = wrap(_ph1 + f0 / kRate);
    float eng = 0;
    for (int h = 1; h <= 6; ++h)
    {
      eng += sinf(_ph1 * 2 * kPi * h) / h;
    }
    // 燃焼のばらつき（ごろごろした感じ）
    _lp += (noise() - _lp) * 0.02f;
    eng *= (0.8f + 0.6f * _lp) * (0.18f + 0.25f * throttle);
    _lp2 += (noise() - _lp2) * 0.04f;
    const float roll = _lp2 * 0.3f * _lv;
    return eng + roll + jointSample(0.8f);
  }

  // ---- 効果音（車両を選んだときに合成する） ----

  int16_t *alloc(int idx, float seconds)
  {
    const size_t n = size_t(seconds * kRate);
    _fx[idx] = static_cast<int16_t *>(_allocFn(n * sizeof(int16_t)));
    _fxLen[idx] = _fx[idx] ? n : 0;
    return _fx[idx];
  }

  static void mix(int16_t *buf, size_t len, size_t i, float v)
  {
    if (i >= len)
    {
      return;
    }
    int32_t x = buf[i] + int32_t(v * 32767);
    buf[i] = int16_t(x > 32767 ? 32767 : (x < -32768 ? -32768 : x));
  }

  // 減衰する純音（チャイム）
  static void addTone(int16_t *buf, size_t len, float startSec, float durSec, float freq, float amp, float decay)
  {
    const size_t s0 = size_t(startSec * kRate);
    const size_t n = size_t(durSec * kRate);
    for (size_t i = 0; i < n; ++i)
    {
      const float t = float(i) / kRate;
      const float env = expf(-t * decay) * (t < 0.005f ? t / 0.005f : 1.0f);
      mix(buf, len, s0 + i, sinf(2 * kPi * freq * t) * amp * env);
    }
  }

  // 倍音を含む持続音（警笛）
  static void addHorn(int16_t *buf, size_t len, float startSec, float durSec, float f1, float f2, float amp)
  {
    const size_t s0 = size_t(startSec * kRate);
    const size_t n = size_t(durSec * kRate);
    for (size_t i = 0; i < n; ++i)
    {
      const float t = float(i) / kRate;
      const float env = (t < 0.04f ? t / 0.04f : 1.0f) * (t > durSec - 0.15f ? (durSec - t) / 0.15f : 1.0f);
      float v = 0;
      for (int h = 1; h <= 5; ++h)
      {
        v += sinf(2 * kPi * f1 * h * t) / h + (f2 > 0 ? sinf(2 * kPi * f2 * h * t) / h : 0);
      }
      mix(buf, len, s0 + i, v * amp * env);
    }
  }

  // 汽笛（息の混じった和音。鳴り始めに音が少し上がる）
  void addWhistle(int16_t *buf, size_t len, float startSec, float durSec, float amp)
  {
    const size_t s0 = size_t(startSec * kRate);
    const size_t n = size_t(durSec * kRate);
    float ph[3] = {};
    const float f[3] = {440.0f, 523.3f, 659.3f};
    float lp = 0;
    for (size_t i = 0; i < n; ++i)
    {
      const float t = float(i) / kRate;
      const float bend = 0.94f + 0.06f * (t < 0.15f ? t / 0.15f : 1.0f);
      const float env = (t < 0.08f ? t / 0.08f : 1.0f) * (t > durSec - 0.25f ? (durSec - t) / 0.25f : 1.0f);
      float v = 0;
      for (int k = 0; k < 3; ++k)
      {
        ph[k] = wrap(ph[k] + f[k] * bend / kRate);
        v += sinf(ph[k] * 2 * kPi);
      }
      lp += (noise() - lp) * 0.3f;
      mix(buf, len, s0 + i, (v * 0.3f + lp * 0.25f) * amp * env);
    }
  }

  // 空気の音（ブレーキ、ドア、安全弁）
  void addHiss(int16_t *buf, size_t len, float startSec, float durSec, float amp)
  {
    const size_t s0 = size_t(startSec * kRate);
    const size_t n = size_t(durSec * kRate);
    float prev = 0;
    for (size_t i = 0; i < n; ++i)
    {
      const float t = float(i) / kRate;
      const float env = (t < 0.02f ? t / 0.02f : 1.0f) * expf(-t * 3.0f / durSec);
      const float w = noise();
      mix(buf, len, s0 + i, (w - prev) * amp * env * 0.5f); // 高い音だけ残す
      prev = w;
    }
  }

  // 電鈴（発車ベル）
  static void addBell(int16_t *buf, size_t len, float startSec, float durSec, float amp)
  {
    const size_t s0 = size_t(startSec * kRate);
    const size_t n = size_t(durSec * kRate);
    for (size_t i = 0; i < n; ++i)
    {
      const float t = float(i) / kRate;
      const float strike = fmodf(t, 1.0f / 22.0f) * 22.0f; // 1 秒に 22 回たたく
      const float env = (1.0f - strike * 0.6f) * (t > durSec - 0.2f ? (durSec - t) / 0.2f : 1.0f);
      mix(buf, len, s0 + i, (sinf(2 * kPi * 2350.0f * t) * 0.6f + sinf(2 * kPi * 3420.0f * t) * 0.4f) * amp * env);
    }
  }

  static void addDoorChime(int16_t *buf, size_t len, float startSec, int times)
  {
    for (int k = 0; k < times; ++k)
    {
      addTone(buf, len, startSec + k * 0.5f, 0.45f, 1318.5f, 0.25f, 6.0f);
      addTone(buf, len, startSec + k * 0.5f + 0.22f, 0.45f, 1046.5f, 0.25f, 6.0f);
    }
  }

  static void addAnnounceChime(int16_t *buf, size_t len, float startSec, bool up)
  {
    const float notes[] = {523.3f, 659.3f, 784.0f, 1046.5f};
    for (int k = 0; k < 4; ++k)
    {
      addTone(buf, len, startSec + k * 0.32f, 1.0f, notes[up ? k : 3 - k], 0.3f, 2.5f);
    }
  }

  // エンジン始動（セルモーターが回って、かかる）
  void addCrank(int16_t *buf, size_t len, float startSec)
  {
    const size_t s0 = size_t(startSec * kRate);
    const size_t n = size_t(2.5f * kRate);
    float ph = 0, lp = 0;
    for (size_t i = 0; i < n; ++i)
    {
      const float t = float(i) / kRate;
      float v;
      if (t < 1.0f)
      {
        // セルモーター: 1 秒に 9 回ほど、うなる
        const float wob = 0.5f + 0.5f * sinf(2 * kPi * 9.0f * t);
        ph = wrap(ph + 180.0f / kRate);
        v = sinf(ph * 2 * kPi) * 0.25f * wob;
      }
      else
      {
        // かかって回転が上がり、アイドリングに落ち着く
        const float u = t - 1.0f;
        const float f0 = 28.0f + 50.0f * expf(-u * 2.0f) * (u < 0.2f ? u / 0.2f : 1.0f);
        ph = wrap(ph + f0 / kRate);
        v = 0;
        for (int h = 1; h <= 6; ++h)
        {
          v += sinf(ph * 2 * kPi * h) / h;
        }
        lp += (noise() - lp) * 0.02f;
        v *= 0.3f * (0.8f + 0.6f * lp) * (t > 2.2f ? (2.5f - t) / 0.3f : 1.0f);
      }
      mix(buf, len, s0 + i, v);
    }
  }

  void renderEffects(const Preset &p)
  {
    int16_t *b;
    switch (p.running)
    {
    case Running::Steam:
      if ((b = alloc(0, 1.8f)))
        addWhistle(b, _fxLen[0], 0, 1.8f, 0.8f);
      if ((b = alloc(1, 0.5f)))
        addWhistle(b, _fxLen[1], 0, 0.5f, 0.8f);
      if ((b = alloc(2, 3.0f)))
        addBell(b, _fxLen[2], 0, 3.0f, 0.35f);
      if ((b = alloc(3, 2.5f)))
        addHiss(b, _fxLen[3], 0, 2.5f, 0.7f);
      if ((b = alloc(4, 1.6f)))
      {
        for (int k = 0; k < 4; ++k)
        {
          addHiss(b, _fxLen[4], k * 0.4f, 0.35f, 0.55f);
        }
      }
      if ((b = alloc(5, 3.0f)))
      {
        addAnnounceChime(b, _fxLen[5], 0, false);
        addHiss(b, _fxLen[5], 1.6f, 1.2f, 0.5f);
      }
      break;

    case Running::Diesel:
      if ((b = alloc(0, 1.2f)))
        addHorn(b, _fxLen[0], 0, 1.2f, 370.0f, 0, 0.22f);
      if ((b = alloc(1, 0.4f)))
        addHorn(b, _fxLen[1], 0, 0.4f, 370.0f, 0, 0.22f);
      if ((b = alloc(2, 2.4f)))
      {
        addDoorChime(b, _fxLen[2], 0, 2);
        addHiss(b, _fxLen[2], 1.2f, 0.8f, 0.5f);
      }
      if ((b = alloc(3, 2.0f)))
        addAnnounceChime(b, _fxLen[3], 0, true);
      if ((b = alloc(4, 2.5f)))
        addCrank(b, _fxLen[4], 0);
      if ((b = alloc(5, 1.0f)))
        addHiss(b, _fxLen[5], 0, 1.0f, 0.8f);
      break;

    default:
      if ((b = alloc(0, 1.2f)))
        addHorn(b, _fxLen[0], 0, 1.2f, 440.0f, 554.0f, 0.12f);
      if ((b = alloc(1, 2.4f)))
      {
        addDoorChime(b, _fxLen[1], 0, 3);
        addHiss(b, _fxLen[1], 1.6f, 0.8f, 0.5f);
      }
      if ((b = alloc(2, 3.0f)))
        addBell(b, _fxLen[2], 0, 3.0f, 0.35f);
      if ((b = alloc(3, 2.0f)))
        addAnnounceChime(b, _fxLen[3], 0, true);
      if ((b = alloc(4, 4.0f)))
      {
        addAnnounceChime(b, _fxLen[4], 0, false);
        addHiss(b, _fxLen[4], 1.6f, 0.7f, 0.6f);
        addDoorChime(b, _fxLen[4], 2.6f, 2);
      }
      if ((b = alloc(5, 1.0f)))
        addHiss(b, _fxLen[5], 0, 1.0f, 0.8f);
      break;
    }
  }

  AllocFn _allocFn;
  FreeFn _freeFn;
  int16_t *_fx[kEffectCount] = {};
  size_t _fxLen[kEffectCount] = {};
  int _presetIndex = 0;

  volatile Running _running = Running::Electric;
  volatile TrainSense::State _state = TrainSense::State::Stop;
  volatile float _level = 0;
  volatile bool _sensorOk = false;
  volatile bool _manual = false;
  volatile bool _muted = false;

  uint32_t _rng = 12345;
  float _lv = 0, _gain = 0, _drive = 0;
  float _ph1 = 0, _ph2 = 0, _lp = 0, _lp2 = 0;
  float _jointT = 0, _clackT = 1, _cph = 0, _clp = 0;
  bool _clack2 = false;
  float _chuffT = 0, _chuffAmp = 1;
  uint32_t _chuffIdx = 0;
};
