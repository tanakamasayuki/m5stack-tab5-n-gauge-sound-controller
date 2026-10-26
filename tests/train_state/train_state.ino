// 記録した入力電流（10ms ごとの平均値）を TrainSense に流し、
// 100ms ごとの状態を output/trace_<name>.csv に書き出す。

#include <TrainSense.h>

#include <stdio.h>
#include <sys/stat.h>

static const char *const kInputs[] = {"e1_raw2", "e3_run1"};

static bool runOne(const char *name)
{
  char path[96];
  snprintf(path, sizeof(path), "input/%s_10ms.csv", name);
  FILE *in = fopen(path, "r");
  snprintf(path, sizeof(path), "output/trace_%s.csv", name);
  FILE *out = fopen(path, "w");
  if (!in || !out)
  {
    if (in)
    {
      fclose(in);
    }
    if (out)
    {
      fclose(out);
    }
    return false;
  }
  fprintf(out, "t_ms,mA,current,baseline,slope,level,state\n");

  TrainSense sense;
  char line[64];
  fgets(line, sizeof(line), in); // header
  unsigned long t;
  float ma;
  unsigned long rows = 0;
  unsigned long nextOut = 0;
  while (fgets(line, sizeof(line), in))
  {
    if (sscanf(line, "%lu,%f", &t, &ma) != 2)
    {
      continue;
    }
    sense.update(t, ma);
    // 記録によって 10ms の刻みがずれるので、100ms を過ぎるごとに 1 行出す
    if (t >= nextOut)
    {
      nextOut = (t / 100 + 1) * 100;
      fprintf(out, "%lu,%.1f,%.1f,%.1f,%.0f,%.3f,%s\n", t / 100 * 100, ma, sense.current(), sense.baseline(),
              sense.slope(), sense.level(), TrainSense::stateName(sense.state()));
    }
    rows++;
  }
  fclose(in);
  fclose(out);
  Serial.printf("ROWS %s %lu\n", name, rows);
  return true;
}

void setup()
{
  Serial.begin(115200);
  Serial.println("TEST start train_state");
  mkdir("output", 0755);
  for (const char *name : kInputs)
  {
    if (!runOne(name))
    {
      Serial.printf("TEST error open %s\n", name);
      return;
    }
  }
  Serial.println("TEST done");
}

void loop()
{
  delay(1000);
}
