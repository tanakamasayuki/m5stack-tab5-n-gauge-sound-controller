// 記録した入力電流（10ms ごとの平均値）を TrainSense に流し、
// 100ms ごとの状態を output/trace.csv に書き出す。

#include <TrainSense.h>

#include <stdio.h>
#include <sys/stat.h>

void setup()
{
  Serial.begin(115200);
  Serial.println("TEST start train_state");
  mkdir("output", 0755);

  FILE *in = fopen("input/e1_raw2_10ms.csv", "r");
  FILE *out = fopen("output/trace.csv", "w");
  if (!in || !out)
  {
    Serial.println("TEST error open");
    return;
  }
  fprintf(out, "t_ms,mA,current,baseline,slope,level,state\n");

  TrainSense sense;
  char line[64];
  fgets(line, sizeof(line), in); // header
  unsigned long t;
  float ma;
  unsigned long rows = 0;
  while (fgets(line, sizeof(line), in))
  {
    if (sscanf(line, "%lu,%f", &t, &ma) != 2)
    {
      continue;
    }
    sense.update(t, ma);
    if (t % 100 == 0)
    {
      fprintf(out, "%lu,%.1f,%.1f,%.1f,%.0f,%.3f,%s\n", t, ma, sense.current(), sense.baseline(), sense.slope(),
              sense.level(), TrainSense::stateName(sense.state()));
    }
    rows++;
  }
  fclose(in);
  fclose(out);
  Serial.printf("ROWS %lu\n", rows);
  Serial.println("TEST done");
}

void loop()
{
  delay(1000);
}
