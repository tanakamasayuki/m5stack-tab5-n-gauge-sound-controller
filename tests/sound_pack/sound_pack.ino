// input/ のマニフェスト（*.json）と WAV（*.wav）を SoundPack で読み、結果を Serial に出す（ホスト環境）。

#include <SoundPack.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool endsWith(const char *s, const char *suffix)
{
  const size_t n = strlen(s), m = strlen(suffix);
  return n >= m && strcmp(s + n - m, suffix) == 0;
}

static size_t readFile(const char *name, char *buf, size_t size)
{
  char path[160];
  snprintf(path, sizeof(path), "input/%s", name);
  FILE *f = fopen(path, "rb");
  if (!f)
  {
    return 0;
  }
  const size_t n = fread(buf, 1, size, f);
  fclose(f);
  return n;
}

static void checkManifest(const char *name)
{
  static char buf[8192];
  const size_t n = readFile(name, buf, sizeof(buf));
  soundpack::Manifest m;
  char err[96];
  if (soundpack::parseManifest(buf, n, m, err, sizeof(err)))
  {
    Serial.printf("MANIFEST %s ok name=%s buttons=%d running=%d credits=%d", name, m.name, m.buttonCount,
                  (int)m.running.type, m.creditCount);
    for (int i = 0; i < m.buttonCount; ++i)
    {
      Serial.printf(" [%s:%d:%d]", m.buttons[i].label, m.buttons[i].fileCount, (int)m.buttons[i].mode);
    }
    Serial.printf(" pitch=%.2f-%.2f\n", m.running.pitch[0], m.running.pitch[1]);
  }
  else
  {
    Serial.printf("MANIFEST %s error=%s\n", name, err);
  }
}

static void checkWav(const char *name)
{
  static uint8_t buf[512];
  const size_t n = readFile(name, reinterpret_cast<char *>(buf), sizeof(buf));
  soundpack::WavInfo w;
  char err[96];
  if (soundpack::parseWavHeader(buf, n, w, err, sizeof(err)))
  {
    Serial.printf("WAVFILE %s ok ch=%u rate=%lu offset=%lu bytes=%lu\n", name, (unsigned)w.channels,
                  (unsigned long)w.sampleRate, (unsigned long)w.dataOffset, (unsigned long)w.dataBytes);
  }
  else
  {
    Serial.printf("WAVFILE %s error=%s\n", name, err);
  }
}

void setup()
{
  Serial.begin(115200);
  Serial.println("TEST start sound_pack");
  DIR *d = opendir("input");
  if (!d)
  {
    Serial.println("TEST error opendir");
    return;
  }
  while (dirent *e = readdir(d))
  {
    if (endsWith(e->d_name, ".json"))
    {
      checkManifest(e->d_name);
    }
    else if (endsWith(e->d_name, ".wav"))
    {
      checkWav(e->d_name);
    }
  }
  closedir(d);
  Serial.println("TEST done");
}

void loop()
{
  delay(1000);
}
