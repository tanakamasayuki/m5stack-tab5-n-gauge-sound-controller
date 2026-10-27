#pragma once

// 音の再生（実機側）。合成は SoundSynth が行い、ここでは M5.Speaker で鳴らす。
// - 走行音（チャンネル 0）: SoundSynth::renderRunning() で少しずつ合成し、順に積む
// - 効果音（チャンネル 1〜）: 車両を選んだときに合成して PSRAM に置き、ボタンで鳴らす

#include <M5Unified.h>
#include <SoundSynth.h>
#include <esp_heap_caps.h>

class SoundEngine
{
public:
  static constexpr uint32_t kRate = SoundSynth::kRate;
  static constexpr int kEffectCount = SoundSynth::kEffectCount;
  static constexpr int kPresetCount = SoundSynth::kPresetCount;
  using Preset = SoundSynth::Preset;

  static const Preset &preset(int i) { return SoundSynth::preset(i); }

  void begin(int presetIndex)
  {
    for (auto &b : _chunks)
    {
      b = static_cast<int16_t *>(heap_caps_malloc(kChunk * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    _synth.selectPreset(presetIndex);
    xTaskCreatePinnedToCore(taskEntry, "sound", 4096, this, 10, nullptr, 0);
  }

  // 車両を切り替える。効果音を止めてから合成し直す
  void selectPreset(int i)
  {
    for (int k = 0; k < kEffectCount; ++k)
    {
      M5.Speaker.stop(1 + k);
    }
    for (int wait = 0; wait < 100 && anyEffectPlaying(); ++wait)
    {
      delay(1);
    }
    _synth.selectPreset(i);
  }

  int presetIndex() const { return _synth.presetIndex(); }

  void setDrive(TrainSense::State s, float level, bool sensorOk) { _synth.setDrive(s, level, sensorOk); }
  void setManualRunning(bool on) { _synth.setManualRunning(on); }

  void playEffect(int i)
  {
    size_t len;
    const int16_t *fx = _synth.effect(i, len);
    if (fx)
    {
      M5.Speaker.playRaw(fx, len, kRate, false, 1, 1 + i, true);
    }
  }

  // 全停止: 効果音を止め、走行音は次に発進するまで黙らせる
  void stopAll()
  {
    for (int i = 0; i < kEffectCount; ++i)
    {
      M5.Speaker.stop(1 + i);
    }
    _synth.mute();
  }

private:
  static constexpr size_t kChunk = 480; // 20ms
  static constexpr int kChunkCount = 3;

  static void *psramAlloc(size_t bytes) { return heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM); }

  static void taskEntry(void *p) { static_cast<SoundEngine *>(p)->taskLoop(); }

  bool anyEffectPlaying()
  {
    for (int k = 0; k < kEffectCount; ++k)
    {
      if (M5.Speaker.isPlaying(1 + k))
      {
        return true;
      }
    }
    return false;
  }

  void taskLoop()
  {
    int next = 0;
    for (;;)
    {
      while (M5.Speaker.isPlaying(0) < 2)
      {
        _synth.renderRunning(_chunks[next], kChunk);
        M5.Speaker.playRaw(_chunks[next], kChunk, kRate, false, 1, 0, false);
        next = (next + 1) % kChunkCount;
      }
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }

  SoundSynth _synth{psramAlloc, heap_caps_free};
  int16_t *_chunks[kChunkCount] = {};
};
