#pragma once

// パワーパック入力電流から、列車の走行状態をざっくり推定する。
// 10ms ごとの平均電流を update() に渡す。速度は推定しない。代わりに、
// 「待機電流からどれだけ増えているか」を 0〜1 に正規化した level を出す。
// 車両ごとの調整はしない前提なので、基準値と範囲は運転中に自動で学習する。

#include <stdint.h>

class TrainSense
{
public:
  enum class State : uint8_t
  {
    Stop,  // 停止
    Start, // 発進（停止から動き出した直後）
    Run,   // 走行中
    Brake, // 減速
  };

  struct Config
  {
    float smoothMs = 200;       // 電流の平滑化。PWM（約 7ms 周期）と 10ms 区切りのずれを均す
    float trendMs = 1000;       // 増減の傾きを見るための平滑化
    float slopeMs = 500;        // 傾きそのものの平滑化
    float startMarginMa = 12;   // 基準値からこれだけ増えたら動いたとみなす
    float stopMarginMa = 8;     // 基準値からの増分がこれを下回ったら止まったとみなす
    uint32_t confirmMs = 150;   // 動いた・止まったの確定に必要な継続時間
    // 傾きは「level が 1 秒あたりどれだけ変わるか」で判定する（車両による電流の差を吸収するため）
    float brakeSlope = -0.04f;    // これより速く減っていたら減速
    float brakeExitSlope = -0.015f; // 減速中に傾きがこれより緩やかになったら走行中へ戻る
    float runSlope = 0.04f;       // 発進中に傾きがこれより緩やかになったら走行中へ
    uint32_t maxStartMs = 3000;   // 発進のまま居続ける上限
    uint32_t brakeHoldMs = 300;   // 減速を表示し続ける最短時間
    float initialRangeMa = 150;   // level = 1 とみなす増分の初期値
    float rangeDecayMs = 60000;   // 学習した範囲が初期値へ戻っていく速さ
    float baselineUpMs = 20000;   // 停止中に基準値を上へ追従させる速さ（下へはすぐ追従する）
    float baselineBandMa = 3;     // 基準値との差がこれ未満のときだけ、上へ追従させる
  };

  TrainSense() {}
  explicit TrainSense(const Config &cfg) : _cfg(cfg) {}

  void reset()
  {
    _init = false;
    _state = State::Stop;
  }

  void update(uint32_t nowMs, float mA)
  {
    if (!_init)
    {
      _init = true;
      _lastMs = nowMs;
      _fast = _slow = _prevSlow = _baseline = mA;
      _slope = 0;
      _range = _cfg.initialRangeMa;
      _state = State::Stop;
      _stateMs = _pendingMs = nowMs;
      return;
    }

    const float dt = float(nowMs - _lastMs);
    _lastMs = nowMs;
    if (dt <= 0)
    {
      return;
    }

    _fast += (mA - _fast) * alpha(dt, _cfg.smoothMs);
    _prevSlow = _slow;
    _slow += (mA - _slow) * alpha(dt, _cfg.trendMs);
    const float d = (_slow - _prevSlow) * 1000.0f / dt;
    _slope += (d - _slope) * alpha(dt, _cfg.slopeMs);

    // 基準値（待機電流）。下回ったらすぐ下げ、上へは停止中にゆっくりだけ追従する。
    // ゆっくりツマミを上げたときに、基準値まで一緒に上がってしまわないようにするため
    if (_fast < _baseline)
    {
      _baseline = _fast;
    }
    else if (_state == State::Stop && _fast - _baseline < _cfg.baselineBandMa)
    {
      _baseline += (_fast - _baseline) * alpha(dt, _cfg.baselineUpMs);
    }

    const float excess = _fast - _baseline;

    // 範囲（level = 1 とみなす増分）。最大値を覚え、ゆっくり初期値へ戻す
    if (excess > _range)
    {
      _range = excess;
    }
    else if (_range > _cfg.initialRangeMa)
    {
      _range += (_cfg.initialRangeMa - _range) * alpha(dt, _cfg.rangeDecayMs);
    }
    _level = excess <= 0 ? 0 : (excess >= _range ? 1 : excess / _range);
    const float levelSlope = _slope / _range;

    // 動いている・止まっている（ヒステリシスと継続時間で確定）
    const bool wantActive = _active ? (excess > _cfg.stopMarginMa) : (excess > _cfg.startMarginMa);
    if (wantActive == _active)
    {
      _pendingMs = nowMs;
    }
    else if (nowMs - _pendingMs >= _cfg.confirmMs)
    {
      _active = wantActive;
      _pendingMs = nowMs;
    }

    switch (_state)
    {
    case State::Stop:
      if (_active)
      {
        setState(State::Start, nowMs);
      }
      break;
    case State::Start:
      if (!_active)
      {
        setState(State::Stop, nowMs);
      }
      else if ((levelSlope < _cfg.runSlope && nowMs - _stateMs >= _cfg.confirmMs) || nowMs - _stateMs >= _cfg.maxStartMs)
      {
        setState(State::Run, nowMs);
      }
      break;
    case State::Run:
      if (!_active)
      {
        setState(State::Stop, nowMs);
      }
      else if (levelSlope < _cfg.brakeSlope)
      {
        setState(State::Brake, nowMs);
      }
      break;
    case State::Brake:
      if (!_active)
      {
        setState(State::Stop, nowMs);
      }
      else if (levelSlope > _cfg.brakeExitSlope && nowMs - _stateMs >= _cfg.brakeHoldMs)
      {
        setState(State::Run, nowMs);
      }
      break;
    }
  }

  State state() const { return _state; }
  float level() const { return _level; }     // 0〜1
  float current() const { return _fast; }    // 平滑化した電流（mA）
  float baseline() const { return _baseline; } // 学習した待機電流（mA）
  float slope() const { return _slope; }     // 電流の増減（mA/s）

  static const char *stateName(State s)
  {
    switch (s)
    {
    case State::Stop:
      return "Stop";
    case State::Start:
      return "Start";
    case State::Run:
      return "Run";
    case State::Brake:
      return "Brake";
    }
    return "?";
  }

private:
  static float alpha(float dt, float tauMs) { return dt >= tauMs ? 1.0f : dt / tauMs; }

  void setState(State s, uint32_t nowMs)
  {
    _state = s;
    _stateMs = nowMs;
  }

  Config _cfg;
  bool _init = false;
  bool _active = false;
  State _state = State::Stop;
  uint32_t _lastMs = 0, _stateMs = 0, _pendingMs = 0;
  float _fast = 0, _slow = 0, _prevSlow = 0, _slope = 0;
  float _baseline = 0, _range = 0, _level = 0;
};
