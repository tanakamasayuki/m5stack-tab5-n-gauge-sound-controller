# E1CurrentLogger

KATO パワーパックの入力電流を U200 (Unit INA226-10A) で測り、Serial に CSV で出すスケッチ。実験 E1 用（[docs/experiments.md](../../docs/experiments.md)）。

## 配線

```
ACアダプタ ─ DCジャック(オス/メス)中継 ─ パワーパック
                  │ +側を U200 の電流経路に通す
               U200 ─ Grove ─ Tab5 Port A
```

## ビルドと書き込み

```sh
arduino-cli compile --profile m5stack_tab5 experiments/E1CurrentLogger
arduino-cli upload  --profile m5stack_tab5 -p /dev/ttyACM0 experiments/E1CurrentLogger
```

## 記録

```sh
uv run tools/capture_serial.py --port /dev/ttyACM0 --out logs/e1_run1.csv
```

- 10ms ごとに 1 行出力する：`t_ms,mean_mA,min_mA,max_mA,bus_V,samples,marker`
- 1 行は、約 1ms 間隔で取った約 10 サンプルの平均・最小・最大。
- 画面をタップすると `marker` が 1 増える。「ツマミを上げ始めた」「発進した」などの目印に使い、どのマーカーが何だったかを別にメモしておく。
- 電流の分解能は 0.5mA（シャント 5mΩ、INA226 のシャント LSB 2.5µV）。
