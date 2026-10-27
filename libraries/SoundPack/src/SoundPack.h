#pragma once

// 音源パックのマニフェスト（pack.json）と WAV ヘッダの読み込み。仕様は docs/sound-pack.md。
// ファイルの読み込みは呼び出し側が行い、ここには中身（バイト列）を渡す。
// ハードウェアに依存しないので、PC 上でテストできる（tests/sound_pack）。

#include <ArduinoJson.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace soundpack
{
  static constexpr int kFormat = 1;
  static constexpr int kMaxButtons = 6;
  static constexpr int kMaxSequence = 4;
  static constexpr size_t kNameLen = 64;
  static constexpr size_t kKindLen = 32;
  static constexpr size_t kFileLen = 64;

  enum class RunningType : uint8_t
  {
    Synth,
    Wav,
    None,
  };

  enum class ButtonMode : uint8_t
  {
    OneShot,
    Hold,
    Toggle,
  };

  struct Running
  {
    RunningType type = RunningType::None;
    char preset[16] = "";   // synth のとき
    char loop[kFileLen] = ""; // wav のとき
    char idle[kFileLen] = "";
    float pitch[2] = {1.0f, 1.0f};
    float volume[2] = {1.0f, 1.0f};
  };

  struct Button
  {
    char label[kNameLen] = "";
    char files[kMaxSequence][kFileLen] = {};
    int fileCount = 0;
    ButtonMode mode = ButtonMode::OneShot;
  };

  struct Manifest
  {
    char name[kNameLen] = "";
    char kind[kKindLen] = "";
    Running running;
    Button buttons[kMaxButtons];
    int buttonCount = 0;
    int creditCount = 0;
  };

  namespace detail
  {
    inline bool copy(char *dst, size_t size, const char *src)
    {
      if (!src || strlen(src) >= size)
      {
        return false;
      }
      strcpy(dst, src);
      return true;
    }

    // パック内のファイル名として使えるか（サブフォルダや親フォルダは不可）
    inline bool validFileName(const char *s)
    {
      return s && *s && !strchr(s, '/') && !strchr(s, '\\') && strcmp(s, "..") != 0;
    }

    inline bool fail(char *err, size_t errLen, const char *msg)
    {
      if (err && errLen)
      {
        snprintf(err, errLen, "%s", msg);
      }
      return false;
    }

    inline bool readRange(JsonVariantConst v, float out[2])
    {
      if (v.isNull())
      {
        return true;
      }
      JsonArrayConst a = v.as<JsonArrayConst>();
      if (a.isNull() || a.size() != 2 || !a[0].is<float>() || !a[1].is<float>())
      {
        return false;
      }
      out[0] = a[0].as<float>();
      out[1] = a[1].as<float>();
      return true;
    }
  } // namespace detail

  // pack.json の中身を読む。失敗したときは err に理由を入れて false を返す
  inline bool parseManifest(const char *json, size_t len, Manifest &out, char *err = nullptr, size_t errLen = 0)
  {
    using namespace detail;
    out = Manifest();
    JsonDocument doc;
    const DeserializationError de = deserializeJson(doc, json, len);
    if (de)
    {
      return fail(err, errLen, de.c_str());
    }
    if (doc["format"] != kFormat)
    {
      return fail(err, errLen, "format must be 1");
    }
    if (!copy(out.name, sizeof(out.name), doc["name"]) || !out.name[0])
    {
      return fail(err, errLen, "name is missing or too long");
    }
    if (!doc["kind"].isNull() && !copy(out.kind, sizeof(out.kind), doc["kind"]))
    {
      return fail(err, errLen, "kind is too long");
    }

    JsonObjectConst run = doc["running"];
    const char *type = run["type"];
    if (!type)
    {
      return fail(err, errLen, "running.type is missing");
    }
    if (strcmp(type, "synth") == 0)
    {
      out.running.type = RunningType::Synth;
      const char *p = run["preset"];
      if (!p || (strcmp(p, "electric") && strcmp(p, "steam") && strcmp(p, "diesel")))
      {
        return fail(err, errLen, "running.preset must be electric, steam or diesel");
      }
      copy(out.running.preset, sizeof(out.running.preset), p);
    }
    else if (strcmp(type, "wav") == 0)
    {
      out.running.type = RunningType::Wav;
      if (!validFileName(run["loop"]) || !copy(out.running.loop, sizeof(out.running.loop), run["loop"]))
      {
        return fail(err, errLen, "running.loop is missing or invalid");
      }
      if (!run["idle"].isNull() &&
          (!validFileName(run["idle"]) || !copy(out.running.idle, sizeof(out.running.idle), run["idle"])))
      {
        return fail(err, errLen, "running.idle is invalid");
      }
      if (!readRange(run["pitch"], out.running.pitch) || out.running.pitch[0] <= 0 || out.running.pitch[1] <= 0)
      {
        return fail(err, errLen, "running.pitch must be [min, max] (> 0)");
      }
      if (!readRange(run["volume"], out.running.volume))
      {
        return fail(err, errLen, "running.volume must be [min, max]");
      }
    }
    else if (strcmp(type, "none") == 0)
    {
      out.running.type = RunningType::None;
    }
    else
    {
      return fail(err, errLen, "running.type must be synth, wav or none");
    }

    JsonArrayConst buttons = doc["buttons"];
    if (buttons.isNull() || buttons.size() < 1 || buttons.size() > kMaxButtons)
    {
      return fail(err, errLen, "buttons must have 1 to 6 entries");
    }
    for (JsonObjectConst b : buttons)
    {
      Button &dst = out.buttons[out.buttonCount];
      if (!copy(dst.label, sizeof(dst.label), b["label"]) || !dst.label[0])
      {
        return fail(err, errLen, "button label is missing or too long");
      }
      const bool hasFile = !b["file"].isNull();
      const bool hasFiles = !b["files"].isNull();
      if (hasFile == hasFiles)
      {
        return fail(err, errLen, "button needs exactly one of file or files");
      }
      if (hasFile)
      {
        if (!validFileName(b["file"]) || !copy(dst.files[0], kFileLen, b["file"]))
        {
          return fail(err, errLen, "button file is invalid");
        }
        dst.fileCount = 1;
      }
      else
      {
        JsonArrayConst files = b["files"];
        if (files.isNull() || files.size() < 2 || files.size() > kMaxSequence)
        {
          return fail(err, errLen, "button files must have 2 to 4 entries");
        }
        for (JsonVariantConst f : files)
        {
          if (!validFileName(f) || !copy(dst.files[dst.fileCount], kFileLen, f))
          {
            return fail(err, errLen, "button files entry is invalid");
          }
          dst.fileCount++;
        }
      }
      const char *mode = b["mode"] | "oneshot";
      if (strcmp(mode, "oneshot") == 0)
      {
        dst.mode = ButtonMode::OneShot;
      }
      else if (strcmp(mode, "hold") == 0)
      {
        dst.mode = ButtonMode::Hold;
      }
      else if (strcmp(mode, "toggle") == 0)
      {
        dst.mode = ButtonMode::Toggle;
      }
      else
      {
        return fail(err, errLen, "button mode must be oneshot, hold or toggle");
      }
      if (dst.fileCount > 1 && dst.mode != ButtonMode::OneShot)
      {
        return fail(err, errLen, "a sequence (files) must be oneshot");
      }
      out.buttonCount++;
    }

    JsonArrayConst credits = doc["credits"];
    for (JsonObjectConst c : credits)
    {
      if (c["files"].isNull() || c["author"].isNull() || c["license"].isNull())
      {
        return fail(err, errLen, "credit needs files, author and license");
      }
      out.creditCount++;
    }
    return true;
  }

  struct WavInfo
  {
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    uint32_t dataOffset = 0; // ファイル先頭から音声データまでのバイト数
    uint32_t dataBytes = 0;
  };

  // WAV の先頭部分（fmt と data チャンクの見出しまで。通常 44 バイト以上）を読む
  inline bool parseWavHeader(const uint8_t *p, size_t n, WavInfo &out, char *err = nullptr, size_t errLen = 0)
  {
    using namespace detail;
    auto le16 = [&](size_t o) { return uint16_t(p[o] | (p[o + 1] << 8)); };
    auto le32 = [&](size_t o) { return uint32_t(le16(o) | (uint32_t(le16(o + 2)) << 16)); };

    out = WavInfo();
    if (n < 12 || memcmp(p, "RIFF", 4) || memcmp(p + 8, "WAVE", 4))
    {
      return fail(err, errLen, "not a RIFF/WAVE file");
    }
    bool haveFmt = false;
    size_t o = 12;
    while (o + 8 <= n)
    {
      const uint32_t size = le32(o + 4);
      if (!memcmp(p + o, "fmt ", 4))
      {
        if (o + 8 + 16 > n)
        {
          return fail(err, errLen, "fmt chunk is truncated");
        }
        const uint16_t format = le16(o + 8);
        out.channels = le16(o + 10);
        out.sampleRate = le32(o + 12);
        out.bitsPerSample = le16(o + 22);
        if (format != 1)
        {
          return fail(err, errLen, "only linear PCM is supported");
        }
        haveFmt = true;
      }
      else if (!memcmp(p + o, "data", 4))
      {
        if (!haveFmt)
        {
          return fail(err, errLen, "data chunk before fmt chunk");
        }
        out.dataOffset = uint32_t(o + 8);
        out.dataBytes = size;
        if (out.bitsPerSample != 16)
        {
          return fail(err, errLen, "only 16-bit samples are supported");
        }
        if (out.channels < 1 || out.channels > 2)
        {
          return fail(err, errLen, "only mono or stereo is supported");
        }
        if (out.sampleRate < 8000 || out.sampleRate > 48000)
        {
          return fail(err, errLen, "sample rate must be 8000 to 48000 Hz");
        }
        return true;
      }
      o += 8 + size + (size & 1); // チャンクは偶数バイトにそろえる
    }
    return fail(err, errLen, haveFmt ? "data chunk not found" : "fmt chunk not found");
  }
} // namespace soundpack
