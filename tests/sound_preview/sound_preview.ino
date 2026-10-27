// 記録した入力電流を TrainSense に流し、その判定に合わせた走行音を WAV に書き出す（ホスト環境）。
// 実機がなくても、音の雰囲気を PC で聞いて確かめられる。
// - output/<車両>_<記録>.wav: 走行音
// - output/<車両>_fx<番号>.wav: 効果音

#include <SoundSynth.h>
#include <TrainSense.h>

#include <math.h>
#include <stdio.h>
#include <sys/stat.h>

static const char *const kPresetIds[SoundSynth::kPresetCount] = {"electric", "steam", "diesel"};
static const char *const kInputs[] = {"e3_run1", "e1_raw2"};

// 書き出した音の大きさ（無音や音割れの確認用）
struct Stats
{
  uint32_t samples = 0, clipped = 0;
  int32_t peak = 0;
  double sumSq = 0;

  void add(const int16_t *p, size_t n)
  {
    for (size_t i = 0; i < n; ++i)
    {
      const int32_t v = p[i] < 0 ? -p[i] : p[i];
      peak = v > peak ? v : peak;
      clipped += v >= 32000;
      sumSq += double(p[i]) * p[i];
    }
    samples += n;
  }
  double rms() const { return samples ? sqrt(sumSq / samples) : 0; }
};

static void writeLe16(FILE *f, uint16_t v)
{
  fputc(v & 0xFF, f);
  fputc(v >> 8, f);
}

static void writeLe32(FILE *f, uint32_t v)
{
  writeLe16(f, v & 0xFFFF);
  writeLe16(f, v >> 16);
}

// 16bit モノラルの WAV ヘッダ。データの長さはあとで書き直す
static void writeWavHeader(FILE *f, uint32_t samples)
{
  fwrite("RIFF", 1, 4, f);
  writeLe32(f, 36 + samples * 2);
  fwrite("WAVEfmt ", 1, 8, f);
  writeLe32(f, 16);
  writeLe16(f, 1);
  writeLe16(f, 1);
  writeLe32(f, SoundSynth::kRate);
  writeLe32(f, SoundSynth::kRate * 2);
  writeLe16(f, 2);
  writeLe16(f, 16);
  fwrite("data", 1, 4, f);
  writeLe32(f, samples * 2);
}

static void printStats(const char *kind, const char *name, const Stats &s)
{
  Serial.printf("WAV %s %s seconds=%.1f peak=%ld rms=%.0f clipped=%lu\n", kind, name, double(s.samples) / SoundSynth::kRate,
                (long)s.peak, s.rms(), (unsigned long)s.clipped);
}

static bool renderRun(SoundSynth &synth, int preset, const char *input)
{
  char path[96];
  snprintf(path, sizeof(path), "../train_state/input/%s_10ms.csv", input);
  FILE *in = fopen(path, "r");
  char name[64];
  snprintf(name, sizeof(name), "%s_%s", kPresetIds[preset], input);
  snprintf(path, sizeof(path), "output/%s.wav", name);
  FILE *out = fopen(path, "wb");
  if (!in || !out)
  {
    return false;
  }
  writeWavHeader(out, 0);

  TrainSense sense;
  Stats stats;
  int16_t buf[SoundSynth::kRate / 10];
  char line[64];
  fgets(line, sizeof(line), in); // header
  unsigned long t, lastT = 0;
  float ma;
  while (fgets(line, sizeof(line), in))
  {
    if (sscanf(line, "%lu,%f", &t, &ma) != 2)
    {
      continue;
    }
    sense.update(t, ma);
    synth.setDrive(sense.state(), sense.level(), true);
    // 前の行からの経過時間ぶんを合成する（記録は 10ms 刻み）
    size_t n = lastT ? size_t((t - lastT) * SoundSynth::kRate / 1000) : 0;
    lastT = t;
    while (n > 0)
    {
      const size_t m = n < sizeof(buf) / sizeof(buf[0]) ? n : sizeof(buf) / sizeof(buf[0]);
      synth.renderRunning(buf, m);
      fwrite(buf, sizeof(int16_t), m, out);
      stats.add(buf, m);
      n -= m;
    }
  }
  fseek(out, 0, SEEK_SET);
  writeWavHeader(out, stats.samples);
  fclose(out);
  fclose(in);
  printStats("run", name, stats);
  return true;
}

static void renderEffects(SoundSynth &synth, int preset)
{
  for (int i = 0; i < SoundSynth::kEffectCount; ++i)
  {
    size_t len;
    const int16_t *fx = synth.effect(i, len);
    char name[64], path[96];
    snprintf(name, sizeof(name), "%s_fx%d", kPresetIds[preset], i + 1);
    snprintf(path, sizeof(path), "output/%s.wav", name);
    FILE *out = fopen(path, "wb");
    if (!out || !fx)
    {
      Serial.printf("WAV fx %s missing\n", name);
      continue;
    }
    writeWavHeader(out, len);
    fwrite(fx, sizeof(int16_t), len, out);
    fclose(out);
    Stats stats;
    stats.add(fx, len);
    printStats("fx", name, stats);
  }
}

void setup()
{
  Serial.begin(115200);
  Serial.println("TEST start sound_preview");
  mkdir("output", 0755);
  for (int p = 0; p < SoundSynth::kPresetCount; ++p)
  {
    for (const char *input : kInputs)
    {
      SoundSynth synth; // 記録ごとに作り直し、前の状態を引き継がない
      synth.selectPreset(p);
      if (!renderRun(synth, p, input))
      {
        Serial.printf("TEST error %s\n", input);
        return;
      }
    }
    SoundSynth synth;
    synth.selectPreset(p);
    renderEffects(synth, p);
  }
  Serial.println("TEST done");
}

void loop()
{
  delay(1000);
}
